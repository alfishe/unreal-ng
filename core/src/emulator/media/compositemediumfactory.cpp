#include "stdafx.h"

#include "compositemediumfactory.h"

#include <algorithm>

#include "common/filehelper.h"
#include "emulator/io/storage/cd/cdimage.h"
#include "emulator/io/storage/cd/cdimageformats.h"
#include "emulator/io/storage/cd/isosynthvolume.h"
#include "emulator/io/storage/compose/fatimagesource.h"
#include "emulator/io/storage/compose/isoimagesource.h"
#include "emulator/io/storage/compose/graftvolume.h"
#include "emulator/io/storage/compose/hostfoldersource.h"
#include "emulator/io/storage/compose/sourcepool.h"
#include "emulator/io/storage/compose/unionbuilder.h"
#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/subrangedevice.h"
#include "emulator/io/storage/hostfolder/foldermanifest.h"
#include "emulator/io/storage/hddimageformats.h"
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
                                          std::unique_ptr<IBlockDevice>& volume, CompositeInfo& info)
{
    volume.reset();
    if (!d.Ok())
        return MediaResult::Fail(MediaError::BadRequest, d.error);

    MediaResult result = MediaResult::Success();
    result.report = d.report;

    // --- What this phase builds ---
    if (d.hasPartitions)
        return MediaResult::Fail(MediaError::NotSupported, "partitions: a later phase (C7) of the multi-source work");

    // --- The target's kind: an ISO 9660 CD or a FAT disk (c5-iso.md §5) ---
    const bool wantsIso = d.target.kind == MediaKind::Optical || d.target.fs == ComposeTarget::Fs::Iso9660;
    const bool wantsFat = d.target.kind == MediaKind::Block || d.target.fs == ComposeTarget::Fs::Fat16 ||
                          d.target.fs == ComposeTarget::Fs::Fat32;
    if (wantsIso && wantsFat)
        return MediaResult::Fail(MediaError::BadRequest, "target: an optical (ISO 9660) target cannot have a FAT kind or fs");
    if (options.slotKind == MediaKind::Block && wantsIso)
        return MediaResult::Fail(MediaError::BadRequest, "target: an ISO 9660 volume needs a CD slot");
    if (options.slotKind == MediaKind::Optical && wantsFat)
        return MediaResult::Fail(MediaError::BadRequest, "target: a CD slot takes an ISO 9660 volume, not FAT");
    const bool optical = wantsIso || options.slotKind == MediaKind::Optical;
    if (optical && d.target.build == ComposeTarget::Build::Graft)
        return MediaResult::Fail(MediaError::BadRequest, "build: graft is for FAT images; an ISO 9660 target is always rebuilt");
    if (d.hasBoot)
        result.report.push_back("boot: not applied yet (a later phase of the multi-source work)");
    if (d.target.onBadName == "replace")
        result.report.push_back("target.onBadName: replace is not implemented yet; names a FAT volume cannot hold are skipped");

    // --- Layers: scan, filter, enumerate ---
    auto pool = std::make_shared<SourcePool>();
    std::vector<FileTree> trees(d.layers.size());
    std::vector<UnionLayer> layers;
    int baseDevice = -1;  ///< the bottom layer's image (the whole file), a graft candidate
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
        uint64_t sourceIdentity = 0;
        if (layer.source.kind == ComposeSource::Kind::Iso)
        {
            // A CD image (ISO, CUE / BIN, CD CHD): the ISO 9660 volume of its first data track
            const std::string path = PathText(layer.source.path);
            if (!FileHelper::FileExists(path) || FileHelper::IsFolder(path))
                return MediaResult::Fail(MediaError::UnreadableSource, where + ": no CD image file '" + path + "'");
            std::error_code ec;
            const std::filesystem::path canonical = std::filesystem::weakly_canonical(layer.source.path, ec);
            const std::string key = "cd:" + PathText(ec ? layer.source.path : canonical);
            int device = pool->FindDevice(key);
            if (device < 0)
            {
                std::string error;
                std::unique_ptr<CdImage> disc = CdImageFormats::Open(path, &error);
                if (!disc)
                    return MediaResult::Fail(MediaError::UnreadableSource, where + ": " + path + ": " + error);
                device = pool->AddDevice(std::shared_ptr<IBlockDevice>(std::move(disc)), key);
            }
            IsoImageSourceOptions source;
            source.from = layer.from;
            source.include = layer.include;
            source.exclude = layer.exclude;
            std::vector<std::string> sourceReport;
            std::string error;
            if (!IsoImageSource::Enumerate(static_cast<uint16_t>(device), source, *pool, trees[i], &sourceReport, &error,
                                           &sourceIdentity))
                return MediaResult::Fail(MediaError::UnreadableSource, where + ": " + path + ": " + error);
            for (const std::string& line : sourceReport)
                result.report.push_back(where + ": " + line);
        }
        else if (layer.source.kind == ComposeSource::Kind::Image)
        {
            // A FAT disk image, opened once per path and only read
            const std::string path = PathText(layer.source.path);
            if (!FileHelper::FileExists(path) || FileHelper::IsFolder(path))
                return MediaResult::Fail(MediaError::UnreadableSource, where + ": no image file '" + path + "'");
            std::error_code ec;
            const std::filesystem::path canonical = std::filesystem::weakly_canonical(layer.source.path, ec);
            const std::string key = PathText(ec ? layer.source.path : canonical);
            int device = pool->FindDevice(key);
            if (device < 0)
            {
                std::string error;
                const std::string format = HddImageFormats::Probe(path, &error);
                std::unique_ptr<IBlockDevice> opened =
                    format.empty() ? nullptr : HddImageFormats::OpenBlock(path, format, RawImage::Access::ReadOnly, &error);
                if (!opened)
                    return MediaResult::Fail(MediaError::UnreadableSource, where + ": " + path + ": " + error);
                device = pool->AddDevice(std::shared_ptr<IBlockDevice>(std::move(opened)), key);
            }
            if (i == 0)
                baseDevice = device;

            FatImageSourceOptions source;
            source.from = layer.from;
            source.include = layer.include;
            source.exclude = layer.exclude;
            source.partition = layer.source.partition;
            source.codePage = layer.source.codePage.value_or(CodePage::Cp866);
            std::vector<std::string> sourceReport;
            std::string error;
            if (!FatImageSource::Enumerate(static_cast<uint16_t>(device), source, *pool, trees[i], &sourceReport, &error,
                                           &sourceIdentity))
                return MediaResult::Fail(MediaError::UnreadableSource, where + ": " + path + ": " + error);
            for (const std::string& line : sourceReport)
                result.report.push_back(where + ": " + line);
        }
        else
        {
            if (!FileHelper::IsFolder(PathText(layer.source.path)))
                return MediaResult::Fail(MediaError::UnreadableSource, where + ": no folder '" + PathText(layer.source.path) + "'");

            const FolderManifest manifest = FolderManifest::Load(layer.source.path);
            for (const std::string& line : manifest.report)
                result.report.push_back(where + ": " + line);
            FolderScanOptions scan;
            scan.excludePatterns = manifest.exclude;
            scan.excludePatterns.insert(scan.excludePatterns.end(), layer.exclude.begin(), layer.exclude.end());
            if (optical)
                scan.maxFileSize = UINT64_MAX;  // ISO 9660 stores big files as several extents
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
            sourceIdentity = snapshot.Identity();
        }

        CompositeLayerInfo layerInfo;
        layerInfo.name = layer.name;
        layerInfo.kind = KindName(layer.source.kind);
        layerInfo.path = PathText(layer.source.path);
        layerInfo.mount = layer.mount;
        layerInfo.from = layer.from;
        layerInfo.identity = sourceIdentity;
        Count(trees[i], FileTree::kRoot, layerInfo.files, layerInfo.bytes);
        info.layers.push_back(layerInfo);
        mix(sourceIdentity);

        UnionLayer u;
        u.tree = &trees[i];
        u.name = layer.name;
        u.mount = layer.mount;
        u.conflict = layer.conflict;
        u.opaque = layer.opaque;
        u.whiteout = layer.whiteout;
        layers.push_back(std::move(u));
    }

    info.sourceDevices = static_cast<uint32_t>(pool->DeviceCount());

    // --- Union ---
    auto tree = std::make_shared<FileTree>();
    std::string error;
    if (!UnionBuilder::Merge(layers, optical ? UnionBuilder::ExactKey : UnionBuilder::FatKey, *tree, &result.report, &error))
        return MediaResult::Fail(MediaError::BadRequest, error);
    Count(*tree, FileTree::kRoot, info.files, info.bytes);

    // --- An ISO 9660 CD ---
    if (optical)
    {
        IsoTargetOptions iso;
        iso.level = d.target.isoLevel;
        iso.joliet = d.target.joliet;
        iso.relaxDepth = d.target.relaxDepth;
        iso.fixedTimeUtc = d.target.fixedTimeUtc;
        if (d.target.label)
            iso.volumeId = *d.target.label;
        std::vector<std::string> isoReport;
        auto iso9660 = IsoSynthVolume::Build(tree, pool, iso, &error, &isoReport);
        if (!iso9660)
            return MediaResult::Fail(MediaError::DoesNotFit, error);
        for (const std::string& line : isoReport)
            result.report.push_back(line);
        if (d.target.free || d.target.size || d.target.mbr || d.target.codePage)
            result.report.push_back("target.free, size, partition, codepage: ignored, a CD is as large as its content");
        uint64_t id = identity;
        for (uint64_t v : {static_cast<uint64_t>(iso.level), static_cast<uint64_t>(iso.joliet), static_cast<uint64_t>(0x49534f)})
        {
            for (int k = 0; k < 8; k++)
            {
                id ^= static_cast<uint8_t>(v >> (8 * k));
                id *= 0x100000001b3ULL;
            }
        }
        const uint32_t blocks = iso9660->Blocks();
        info.build = "rebuild";
        info.fsName = "iso9660";
        info.sectors = static_cast<uint64_t>(blocks) * 4;
        info.clusterCount = blocks;
        info.sectorsPerCluster = 4;
        info.contentId = id;
        volume = IsoSynthVolume::MakeDisc(std::move(iso9660), info.descriptor, id);
        return result;
    }

    if (!Validate(*tree, FileTree::kRoot, error))
        return MediaResult::Fail(MediaError::DoesNotFit, error);

    // --- Target file system (DT-6) and layout ---
    std::optional<FatType> want = options.fs;
    if (!want && d.target.fs == ComposeTarget::Fs::Fat16)
        want = FatType::Fat16;
    if (!want && d.target.fs == ComposeTarget::Fs::Fat32)
        want = FatType::Fat32;

    // --- DT-4: graft onto the bottom image, or rebuild ---
    if (d.target.build != ComposeTarget::Build::Rebuild)
    {
        const bool graftOnly = d.target.build == ComposeTarget::Build::Graft;
        std::string why;
        MediaError failWith = MediaError::BadRequest;
        const ComposeLayer& bottom = d.layers.front();
        const bool atRoot = (bottom.mount.empty() || bottom.mount == "/") && (bottom.from.empty() || bottom.from == "/");
        if (baseDevice < 0 || !atRoot)
        {
            if (graftOnly)
                return MediaResult::Fail(MediaError::BadRequest,
                                         "build: graft needs a FAT image as the bottom layer, mounted at '/' and taken from '/'");
        }
        else
        {
            // The base's FAT type against the slot and the target (FAT12 counts as the FAT16 family)
            const CodePage basePage = bottom.source.codePage.value_or(CodePage::Cp866);
            FatVolumeReader probe;
            std::shared_ptr<IBlockDevice> window;
            FatPartition partition;
            bool opened = true;
            if (bottom.source.partition)
            {
                opened = FatVolumeReader::FindPartition(pool->Device(static_cast<uint16_t>(baseDevice)), *bottom.source.partition,
                                                        partition, &why);
                if (opened)
                    window = std::make_shared<SubRangeDevice>(pool->DevicePtr(static_cast<uint16_t>(baseDevice)), partition.first,
                                                              partition.count);
            }
            opened = opened && probe.Open(window ? *window : pool->Device(static_cast<uint16_t>(baseDevice)), basePage, &why);
            const FatType family = probe.Type() == FatReaderType::Fat32 ? FatType::Fat32 : FatType::Fat16;
            const char* typeName = probe.Type() == FatReaderType::Fat32 ? "FAT32" : probe.Type() == FatReaderType::Fat16 ? "FAT16" : "FAT12";
            const uint64_t baseBytes = pool->Device(static_cast<uint16_t>(baseDevice)).SectorCount() * 512;
            const bool slotTakes = options.allowedFs.empty() ||
                                   std::find(options.allowedFs.begin(), options.allowedFs.end(), family) != options.allowedFs.end();
            if (!opened)
                why = "the base has no FAT volume: " + why;
            else if (!slotTakes)
                why = std::string("the base is ") + typeName + " and the slot does not read it";
            else if (want && *want != family)
                why = std::string("the base is ") + typeName + ", the target asks for " + FsName(*want);
            else if (d.target.size && *d.target.size != baseBytes)
                why = "target.size differs from the base's size (" + std::to_string(baseBytes) + " bytes): a graft keeps it";
            else
            {
                GraftOptions graft;
                graft.codePage = basePage;
                graft.partition = bottom.source.partition;
                graft.fixedTimeUtc = d.target.fixedTimeUtc;
                std::vector<std::string> graftReport;
                GraftFailure failure = GraftFailure::None;
                auto grafted = GraftVolume::Build(tree, pool, static_cast<uint16_t>(baseDevice), graft, identity, info.descriptor,
                                                  &why, &graftReport, &failure);
                if (grafted)
                {
                    for (const std::string& line : graftReport)
                        result.report.push_back(line);
                    std::vector<std::string> ignored;
                    if (d.target.free)
                        ignored.push_back("free");
                    if (d.target.label)
                        ignored.push_back("label");
                    if (d.target.mbr)
                        ignored.push_back("mbr");
                    if (!ignored.empty())
                    {
                        std::string list;
                        for (const std::string& name : ignored)
                            list += (list.empty() ? "" : ", ") + name;
                        result.report.push_back("target." + list + ": ignored, a graft keeps the base's layout");
                    }
                    if (d.target.codePage && *d.target.codePage != basePage)
                        result.report.push_back("target.codepage: ignored, new names use the base's code page");
                    result.report.push_back(std::string("graft onto ") + PathText(bottom.source.path) + " (" + typeName + "): " +
                                            std::to_string(grafted->FilesGrafted()) + " files grafted, " +
                                            std::to_string(grafted->DirectoriesEncoded()) + " directories patched, " +
                                            std::to_string(grafted->FreeClusters()) + " clusters left free");
                    info.build = "graft";
                    info.fs = family;
                    info.fsName = probe.Type() == FatReaderType::Fat32 ? "fat32" : probe.Type() == FatReaderType::Fat16 ? "fat16" : "fat12";
                    info.sectors = grafted->SectorCount();
                    info.clusterCount = probe.ClusterCount();
                    info.sectorsPerCluster = probe.SectorsPerCluster();
                    info.contentId = grafted->ContentId();
                    volume = std::move(grafted);
                    return result;
                }
                failWith = failure == GraftFailure::DoesNotFit ? MediaError::DoesNotFit : MediaError::UnreadableSource;
            }
            if (graftOnly)
                return MediaResult::Fail(failWith, "build: graft: " + why);
            result.report.push_back("rebuild instead of a graft: " + why);
        }
    }
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

    std::unique_ptr<FatSynthVolume> rebuilt;
    for (size_t c = 0; c < candidates.size() && !rebuilt; c++)
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
                if (!rebuilt || bytes > rebuilt->SectorCount() * 512)
                {
                    rebuilt = std::move(candidate);
                    buildReport = std::move(passReport);
                }
                if (closeEnough)
                    break;
                fat.freeBytes += *d.target.size - bytes;
            }
            if (!rebuilt)
                rebuilt = std::move(probe);  // nothing fits under the size: the smallest layout
        }
        else
            rebuilt = FatSynthVolume::Build(tree, pool, fat, identity, info.descriptor, &error, &buildReport);
        if (rebuilt)
        {
            for (const std::string& line : buildReport)
                result.report.push_back(line + ": skipped");
            if (c > 0)
                result.report.push_back(std::string("the content does not fit ") + FsName(candidates[0]) + ": built as " + FsName(fat.fs));
        }
    }
    if (!rebuilt)
        return MediaResult::Fail(MediaError::DoesNotFit, error);

    info.build = "rebuild";
    info.fs = rebuilt->Type();
    info.fsName = info.fs == FatType::Fat32 ? "fat32" : "fat16";
    info.sectors = rebuilt->SectorCount();
    info.clusterCount = rebuilt->ClusterCount();
    info.sectorsPerCluster = rebuilt->SectorsPerCluster();
    info.contentId = rebuilt->ContentId();
    volume = std::move(rebuilt);
    return result;
}

MediaResult CompositeMediumFactory::Open(const OpenRequest& request, std::unique_ptr<Medium>& medium)
{
    medium.reset();
    const MediaSource& source = request.source;
    if (request.kind != MediaKind::Block && request.kind != MediaKind::Optical)
        return MediaResult::Fail(MediaError::NotSupported, "a composite goes into a disk or CD slot, not a floppy or tape slot");
    if (request.kind == MediaKind::Block && request.access == AccessMode::WriteThrough)
        return MediaResult::Fail(MediaError::KindMismatch, "a composite is never written in place: use session or readonly access");

    ComposeDescriptor descriptor = source.inlineBody.empty()
                                       ? ComposeDescriptor::Load(FileHelper::ToFsPath(source.path))
                                       : ComposeDescriptor::Parse(source.inlineBody, std::filesystem::current_path(), "(inline)");
    if (!descriptor.Ok())
        return MediaResult::Fail(descriptor.error.find("cannot be opened") != std::string::npos ? MediaError::UnreadableSource
                                                                                                   : MediaError::BadRequest,
                                 descriptor.error);

    CompositeBuildOptions options;
    options.slotKind = request.kind;
    options.allowedFs = request.allowedFs;
    options.defaultFs = request.fs;
    options.fs = request.explicitFs ? std::optional<FatType>(request.fs) : std::nullopt;
    options.mbr = request.mbr;
    options.codePage = request.codePage;
    options.freeBytes = request.freeBytes;
    options.cancelRequested = request.cancelRequested;
    options.onProgress = request.onProgress;

    std::unique_ptr<IBlockDevice> volume;
    auto info = std::make_shared<CompositeInfo>();
    MediaResult result = Build(descriptor, options, volume, *info);
    if (!result.Ok())
        return result;

    MediaSource resolved = source;
    resolved.type = MediaSourceType::Composite;
    const FatType fs = info->fs;
    if (request.kind == MediaKind::Optical)
    {
        // A CD: read-only, and the drive gets the disc itself (tracks, TOC)
        CdImage* cd = dynamic_cast<CdImage*>(volume.get());
        medium = MediaFormatRegistry::WrapBlock(resolved, AccessMode::ReadOnly, "compose-iso", std::move(volume), MediaKind::Optical);
        medium->SetCd(cd);
        medium->Report() = result.report;
        medium->SetComposite(std::move(info));
        return result;
    }
    const AccessMode access = descriptor.writes.access == AccessMode::ReadOnly ? AccessMode::ReadOnly : request.access;
    medium = MediaFormatRegistry::WrapBlock(resolved, access, (info->build == "graft" ? "graft-" : "compose-") + info->fsName,
                                            std::move(volume));
    medium->Report() = result.report;
    medium->SetOptions({fs, request.codePage, request.freeBytes});
    medium->SetComposite(std::move(info));
    return result;
}
