#include "stdafx.h"

#include "compositemediumfactory.h"

#include <algorithm>

#include "common/filehelper.h"
#include "emulator/io/storage/compose/hostfoldersource.h"
#include "emulator/io/storage/compose/sourcepool.h"
#include "emulator/io/storage/compose/unionbuilder.h"
#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/hostfolder/foldermanifest.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/media/mediaformatregistry.h"
#include "emulator/media/medium.h"

namespace
{
    constexpr uint64_t kDefaultFree = 256ull * 1024 * 1024;
    /// FAT: a directory holds at most 65 536 32-byte slots (2 MiB); 8.3 names
    /// alone already use one slot each, long names more
    constexpr uint64_t kFatMaxEntriesPerDirectory = 65534;

    std::string PathText(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    const char* KindName(ComposeSource::Kind kind)
    {
        switch (kind)
        {
            case ComposeSource::Kind::Image: return "image";
            case ComposeSource::Kind::Iso: return "iso";
            default: return "folder";
        }
    }

    const char* FsName(FatType fs)
    {
        return fs == FatType::Fat32 ? "fat32" : "fat16";
    }

    void Count(const FileTree& tree, uint32_t dir, uint64_t& files, uint64_t& bytes)
    {
        for (uint32_t child : tree.Node(dir).children)
        {
            const TreeNode& node = tree.Node(child);
            if (node.isDirectory)
                Count(tree, child, files, bytes);
            else
            {
                files++;
                bytes += node.data.bytes;
            }
        }
    }

    /// FR-20 limits a rebuilt FAT volume cannot hold; the names are FatSynthVolume's
    bool Validate(const FileTree& tree, uint32_t dir, std::string& error)
    {
        const TreeNode& node = tree.Node(dir);
        if (node.children.size() > kFatMaxEntriesPerDirectory)
        {
            error = tree.PathOf(dir) + ": " + std::to_string(node.children.size()) +
                    " entries, a FAT directory holds at most " + std::to_string(kFatMaxEntriesPerDirectory);
            return false;
        }
        for (uint32_t child : node.children)
        {
            const TreeNode& c = tree.Node(child);
            if (c.isDirectory)
            {
                if (!Validate(tree, child, error))
                    return false;
            }
            else if (c.data.bytes > 0xFFFFFFFFull)
            {
                error = tree.PathOf(child) + ": " + std::to_string(c.data.bytes) + " bytes, FAT files are below 4 GiB";
                return false;
            }
        }
        return true;
    }
}  // namespace

std::vector<FatType> CompositeMediumFactory::FsCandidates(std::optional<FatType> want, const std::vector<FatType>& allowed,
                                                          FatType defaultFs, std::string* error)
{
    auto isAllowed = [&allowed](FatType fs) {
        return allowed.empty() || std::find(allowed.begin(), allowed.end(), fs) != allowed.end();
    };
    if (want)
    {
        if (!isAllowed(*want))
        {
            if (error)
            {
                std::string names;
                for (FatType fs : allowed)
                    names += (names.empty() ? "" : ", ") + std::string(FsName(fs));
                *error = std::string("the slot reads ") + names + ", not " + FsName(*want);
            }
            return {};
        }
        return {*want};
    }
    std::vector<FatType> candidates;
    if (!allowed.empty())
    {
        // the slot's default first when it is allowed, then the rest in FAT16, FAT32 order
        if (isAllowed(defaultFs))
            candidates.push_back(defaultFs);
        for (FatType fs : {FatType::Fat16, FatType::Fat32})
            if (isAllowed(fs) && std::find(candidates.begin(), candidates.end(), fs) == candidates.end())
                candidates.push_back(fs);
        return candidates;
    }
    candidates.push_back(defaultFs);
    candidates.push_back(defaultFs == FatType::Fat16 ? FatType::Fat32 : FatType::Fat16);
    if (defaultFs == FatType::Fat32)
        candidates.pop_back();  // FAT32 by default never falls back to FAT16 (it would only shrink the room)
    return candidates;
}

MediaResult CompositeMediumFactory::Build(const ComposeDescriptor& d, const CompositeBuildOptions& options,
                                          std::unique_ptr<FatSynthVolume>& volume, CompositeInfo& info)
{
    volume.reset();
    if (!d.Ok())
        return MediaResult::Fail(MediaError::BadRequest, d.error);

    MediaResult result = MediaResult::Success();
    result.report = d.report;

    // --- What this phase builds ---
    if (d.hasPartitions)
        return MediaResult::Fail(MediaError::NotSupported, "partitions: a later phase (C7) of the multi-source work");
    if (d.target.kind == MediaKind::Optical || d.target.fs == ComposeTarget::Fs::Iso9660)
        return MediaResult::Fail(MediaError::NotSupported, "ISO 9660 targets: a later phase (C5) of the multi-source work");
    if (d.target.build == ComposeTarget::Build::Graft)
        return MediaResult::Fail(MediaError::NotSupported, "build: graft: a later phase (C4) of the multi-source work");
    if (d.hasBoot)
        result.report.push_back("boot: not applied yet (a later phase of the multi-source work)");
    if (d.target.onBadName == "replace")
        result.report.push_back("target.onBadName: replace is not implemented yet; names a FAT volume cannot hold are skipped");
    for (const ComposeLayer& layer : d.layers)
    {
        if (layer.source.kind == ComposeSource::Kind::Image)
            return MediaResult::Fail(MediaError::NotSupported,
                                     "layer '" + layer.name + "': image sources are a later phase (C3) of the multi-source work");
        if (layer.source.kind == ComposeSource::Kind::Iso)
            return MediaResult::Fail(MediaError::NotSupported,
                                     "layer '" + layer.name + "': ISO sources are a later phase (C5) of the multi-source work");
    }

    // --- Layers: scan, filter, enumerate ---
    auto pool = std::make_shared<SourcePool>();
    std::vector<FileTree> trees(d.layers.size());
    std::vector<UnionLayer> layers;
    info = CompositeInfo{};
    info.descriptor = d.file.empty() ? "(inline)" : PathText(d.file);
    info.normalized = d.Normalized();
    uint64_t identity = 0xcbf29ce484222325ULL;
    auto mix = [&identity](uint64_t v) {
        for (int i = 0; i < 8; i++)
        {
            identity ^= static_cast<uint8_t>(v >> (8 * i));
            identity *= 0x100000001b3ULL;
        }
    };
    for (char c : info.normalized)
        mix(static_cast<uint8_t>(c));

    for (size_t i = 0; i < d.layers.size(); i++)
    {
        const ComposeLayer& layer = d.layers[i];
        const std::string where = "layer '" + layer.name + "'";
        if (!FileHelper::IsFolder(PathText(layer.source.path)))
            return MediaResult::Fail(MediaError::UnreadableSource, where + ": no folder '" + PathText(layer.source.path) + "'");

        const FolderManifest manifest = FolderManifest::Load(layer.source.path);
        for (const std::string& line : manifest.report)
            result.report.push_back(where + ": " + line);
        FolderScanOptions scan;
        scan.excludePatterns = manifest.exclude;
        scan.excludePatterns.insert(scan.excludePatterns.end(), layer.exclude.begin(), layer.exclude.end());
        scan.cancelRequested = options.cancelRequested;
        scan.onProgress = options.onProgress;
        FolderSnapshot snapshot;
        std::string error;
        if (!FolderSnapshot::Scan(layer.source.path, scan, snapshot, &error))
        {
            return MediaResult::Fail(error == FolderSnapshot::kCancelledError ? MediaError::Cancelled : MediaError::UnreadableSource,
                                     where + ": " + error);
        }
        for (const SkippedEntry& skipped : snapshot.Skipped())
            result.report.push_back(where + ": " + skipped.path + ": skipped, " + skipped.reason);

        HostFolderSourceOptions source;
        source.from = layer.from;
        source.include = layer.include;
        std::vector<std::string> sourceReport;
        if (!HostFolderSource::Enumerate(snapshot, source, *pool, trees[i], &sourceReport, &error))
            return MediaResult::Fail(MediaError::UnreadableSource, where + ": " + error);
        for (const std::string& line : sourceReport)
            result.report.push_back(where + ": " + line);

        CompositeLayerInfo layerInfo;
        layerInfo.name = layer.name;
        layerInfo.kind = KindName(layer.source.kind);
        layerInfo.path = PathText(layer.source.path);
        layerInfo.mount = layer.mount;
        layerInfo.from = layer.from;
        layerInfo.identity = snapshot.Identity();
        Count(trees[i], FileTree::kRoot, layerInfo.files, layerInfo.bytes);
        info.layers.push_back(layerInfo);
        mix(snapshot.Identity());

        UnionLayer u;
        u.tree = &trees[i];
        u.name = layer.name;
        u.mount = layer.mount;
        u.conflict = layer.conflict;
        u.opaque = layer.opaque;
        u.whiteout = layer.whiteout;
        layers.push_back(std::move(u));
    }

    // --- Union ---
    auto tree = std::make_shared<FileTree>();
    std::string error;
    if (!UnionBuilder::Merge(layers, UnionBuilder::FatKey, *tree, &result.report, &error))
        return MediaResult::Fail(MediaError::BadRequest, error);
    if (!Validate(*tree, FileTree::kRoot, error))
        return MediaResult::Fail(MediaError::DoesNotFit, error);
    Count(*tree, FileTree::kRoot, info.files, info.bytes);

    // --- Target file system (DT-6) and layout ---
    std::optional<FatType> want = options.fs;
    if (!want && d.target.fs == ComposeTarget::Fs::Fat16)
        want = FatType::Fat16;
    if (!want && d.target.fs == ComposeTarget::Fs::Fat32)
        want = FatType::Fat32;
    const std::vector<FatType> candidates = FsCandidates(want, options.allowedFs, options.defaultFs, &error);
    if (candidates.empty())
        return MediaResult::Fail(MediaError::BadRequest, error);

    FatVolumeOptions fat;
    fat.codePage = d.target.codePage.value_or(options.codePage.value_or(CodePage::Cp866));
    fat.mbr = d.target.mbr.value_or(options.mbr);
    fat.fixedTimeUtc = d.target.fixedTimeUtc;
    if (d.target.label)
        fat.label = *d.target.label;
    fat.freeBytes = d.target.free.value_or(options.freeBytes.value_or(kDefaultFree));

    for (size_t c = 0; c < candidates.size() && !volume; c++)
    {
        fat.fs = candidates[c];
        std::vector<std::string> buildReport;
        if (d.target.size)
        {
            // A fixed size: lay out with no room first, then give the rest of the size to free space
            FatVolumeOptions bare = fat;
            bare.freeBytes = 0;
            auto probe = FatSynthVolume::Build(tree, pool, bare, identity, info.descriptor, &error, nullptr);
            if (!probe)
                continue;
            const uint64_t minimum = probe->SectorCount() * 512;
            if (minimum > *d.target.size)
            {
                error = "the content needs " + std::to_string(minimum) + " bytes as " + FsName(fat.fs) +
                        ", more than target.size " + std::to_string(*d.target.size);
                continue;
            }
            // Free space turns into clusters, FAT and cluster size grow with the volume: correct the
            // estimate a few times and keep the largest volume not over the size
            fat.freeBytes = *d.target.size - minimum;
            for (int pass = 0; pass < 6; pass++)
            {
                std::vector<std::string> passReport;
                auto candidate = FatSynthVolume::Build(tree, pool, fat, identity, info.descriptor, &error, &passReport);
                if (!candidate)
                    break;
                const uint64_t bytes = candidate->SectorCount() * 512;
                const uint64_t cluster = uint64_t(candidate->SectorsPerCluster()) * 512;
                if (bytes > *d.target.size)
                {
                    fat.freeBytes -= std::min(fat.freeBytes, bytes - *d.target.size + cluster);
                    continue;
                }
                const bool closeEnough = *d.target.size - bytes < cluster;
                if (!volume || bytes > volume->SectorCount() * 512)
                {
                    volume = std::move(candidate);
                    buildReport = std::move(passReport);
                }
                if (closeEnough)
                    break;
                fat.freeBytes += *d.target.size - bytes;
            }
            if (!volume)
                volume = std::move(probe);  // nothing fits under the size: the smallest layout
        }
        else
            volume = FatSynthVolume::Build(tree, pool, fat, identity, info.descriptor, &error, &buildReport);
        if (volume)
        {
            for (const std::string& line : buildReport)
                result.report.push_back(line + ": skipped");
            if (c > 0)
                result.report.push_back(std::string("the content does not fit ") + FsName(candidates[0]) + ": built as " + FsName(fat.fs));
        }
    }
    if (!volume)
        return MediaResult::Fail(MediaError::DoesNotFit, error);

    info.fs = volume->Type();
    info.sectors = volume->SectorCount();
    info.clusterCount = volume->ClusterCount();
    info.sectorsPerCluster = volume->SectorsPerCluster();
    info.contentId = volume->ContentId();
    return result;
}

MediaResult CompositeMediumFactory::Open(const OpenRequest& request, std::unique_ptr<Medium>& medium)
{
    medium.reset();
    const MediaSource& source = request.source;
    if (request.kind != MediaKind::Block)
        return MediaResult::Fail(MediaError::NotSupported, "composite media for this slot kind are a later phase of the multi-source work");
    if (request.access == AccessMode::WriteThrough)
        return MediaResult::Fail(MediaError::KindMismatch, "a composite is never written in place: use session or readonly access");

    ComposeDescriptor descriptor = source.inlineBody.empty()
                                       ? ComposeDescriptor::Load(FileHelper::ToFsPath(source.path))
                                       : ComposeDescriptor::Parse(source.inlineBody, std::filesystem::current_path(), "(inline)");
    if (!descriptor.Ok())
        return MediaResult::Fail(descriptor.error.find("cannot be opened") != std::string::npos ? MediaError::UnreadableSource
                                                                                                   : MediaError::BadRequest,
                                 descriptor.error);

    CompositeBuildOptions options;
    options.allowedFs = request.allowedFs;
    options.defaultFs = request.fs;
    options.fs = request.explicitFs ? std::optional<FatType>(request.fs) : std::nullopt;
    options.mbr = request.mbr;
    options.codePage = request.codePage;
    options.freeBytes = request.freeBytes;
    options.cancelRequested = request.cancelRequested;
    options.onProgress = request.onProgress;

    std::unique_ptr<FatSynthVolume> volume;
    auto info = std::make_shared<CompositeInfo>();
    MediaResult result = Build(descriptor, options, volume, *info);
    if (!result.Ok())
        return result;

    MediaSource resolved = source;
    resolved.type = MediaSourceType::Composite;
    const AccessMode access = descriptor.writes.access == AccessMode::ReadOnly ? AccessMode::ReadOnly : request.access;
    const FatType fs = volume->Type();
    medium = MediaFormatRegistry::WrapBlock(resolved, access, fs == FatType::Fat32 ? "compose-fat32" : "compose-fat16",
                                            std::move(volume));
    medium->Report() = result.report;
    medium->SetOptions({fs, request.codePage, request.freeBytes});
    medium->SetComposite(std::move(info));
    return result;
}
