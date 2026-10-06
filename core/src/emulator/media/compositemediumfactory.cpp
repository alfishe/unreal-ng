#include "stdafx.h"

#include "compositemediumfactory.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>

#include "common/filehelper.h"
#include "emulator/io/storage/cd/cdimage.h"
#include "emulator/io/storage/cd/cdimageformats.h"
#include "emulator/io/storage/cd/iso9660reader.h"
#include "emulator/io/storage/cd/isosynthvolume.h"
#include "emulator/io/storage/compose/extentreader.h"
#include "emulator/io/storage/compose/fatimagesource.h"
#include "emulator/io/storage/compose/isoimagesource.h"
#include "emulator/io/storage/compose/graftvolume.h"
#include "emulator/io/storage/compose/hostfoldersource.h"
#include "emulator/io/storage/compose/sourcepool.h"
#include "emulator/io/storage/compose/unionbuilder.h"
#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/commitjournal.h"
#include "emulator/io/storage/partitioneddisk.h"
#include "emulator/io/storage/subrangedevice.h"
#include "emulator/io/storage/hostfolder/foldermanifest.h"
#include "emulator/io/storage/hddimageformats.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/media/mediaformatregistry.h"
#include "emulator/media/medium.h"
#include "emulator/media/sessiondelta.h"
#include "emulator/media/writeback.h"
#include "emulator/io/storage/sessionwritemap.h"

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

    /// The files and bytes under `dir`; an unexpanded base directory (C4b) goes to `unexpanded` (its cluster)
    void Count(const FileTree& tree, uint32_t dir, uint64_t& files, uint64_t& bytes, std::vector<uint32_t>* unexpanded = nullptr)
    {
        for (uint32_t child : tree.Node(dir).children)
        {
            const TreeNode& node = tree.Node(child);
            if (node.isDirectory && node.unexpanded && unexpanded)
                unexpanded->push_back(node.baseCluster);
            else if (node.isDirectory)
                Count(tree, child, files, bytes, unexpanded);
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

    /// Every byte of a file of the union or a hidden image
    bool ReadAll(SourcePool& pool, const FileTree& tree, const FileData& data, std::vector<uint8_t>& out)
    {
        out.clear();
        ExtentReader reader(pool, tree.Extents());
        uint8_t sector[512];
        for (uint64_t s = 0; s * 512 < data.bytes; s++)
        {
            if (!reader.ReadFileSector(data, s, sector))
                return false;
            out.insert(out.end(), sector, sector + std::min<uint64_t>(512, data.bytes - s * 512));
        }
        return true;
    }

    /// A boot file of the descriptor: a file of the union (its node) or a host file (registered in the pool)
    bool BootFileData(const ComposeBootFile& file, const FileTree& tree, SourcePool& pool, FileData& data, uint32_t& node,
                      std::string& error)
    {
        node = FileTree::kNone;
        if (!file.unionPath.empty())
        {
            node = tree.Find(file.unionPath);
            if (node == FileTree::kNone || tree.Node(node).isDirectory)
            {
                error = "boot: " + file.unionPath + " is not a file of the union";
                return false;
            }
            data = tree.Node(node).data;
            return true;
        }
        std::error_code ec;
        const uint64_t size = std::filesystem::file_size(file.host, ec);
        if (ec)
        {
            error = "boot: no host file " + PathText(file.host);
            return false;
        }
        data = FileData();
        data.bytes = size;
        if (size > 0)
        {
            data.storage = FileData::Storage::HostFile;
            data.hostFile = pool.AddHostFile(file.host, size);
        }
        return true;
    }

    /// The FAT part of the descriptor's boot section
    bool PlanFromDescriptor(const ComposeBoot& boot, const FileTree& tree, SourcePool& pool, FatBootPlan& plan, std::string& error)
    {
        auto bytes = [&](const ComposeBootFile& file, std::vector<uint8_t>& out) {
            FileData data;
            uint32_t node = 0;
            if (!BootFileData(file, tree, pool, data, node, error))
                return false;
            if (!ReadAll(pool, tree, data, out))
            {
                error = "boot: cannot read " + (file.unionPath.empty() ? PathText(file.host) : file.unionPath);
                return false;
            }
            return true;
        };
        if (boot.mbrCode.Set() && !bytes(boot.mbrCode, plan.mbrCode))
            return false;
        if (boot.volumeCode.Set() && !bytes(boot.volumeCode, plan.volumeCode))
            return false;
        for (const auto& [lba, file] : boot.reserved)
        {
            std::vector<uint8_t> data;
            if (!bytes(file, data))
                return false;
            // A file of several sectors fills consecutive reserved sectors
            for (size_t at = 0, s = 0; at < data.size() || (data.empty() && s == 0); at += 512, s++)
            {
                std::array<uint8_t, 512> sector{};
                std::copy(data.begin() + static_cast<std::ptrdiff_t>(std::min(at, data.size())),
                          data.begin() + static_cast<std::ptrdiff_t>(std::min(at + 512, data.size())), sector.begin());
                plan.reserved[lba + static_cast<uint32_t>(s)] = sector;
                if (data.empty())
                    break;
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

MediaResult CompositeMediumFactory::BuildPartitioned(const ComposeDescriptor& d, const CompositeBuildOptions& options,
                                                     std::unique_ptr<IBlockDevice>& volume, CompositeInfo& info, MediaResult result)
{
    if (options.slotKind == MediaKind::Optical || d.target.kind == MediaKind::Optical)
        return MediaResult::Fail(MediaError::BadRequest, "partitions are for disks, not CDs");
    info.descriptor = d.file.empty() ? "(inline)" : PathText(d.file);
    info.normalized = d.Normalized();
    info.delta = d.writes.delta;
    info.writesSave = d.writes.save;
    info.build = "partitions";
    info.fsName = "mbr";

    // D-6: the MBR code of the first source disk that has one (a Z80 loader on a Profi disk), unless the
    // descriptor's boot section names one
    std::vector<uint8_t> mbrCode;
    std::string mbrFrom;
    auto carryMbr = [&](const std::filesystem::path& path, IBlockDevice& image) {
        uint8_t s[512];
        if (!mbrCode.empty() || !image.ReadSector(0, s) || s[510] != 0x55 || s[511] != 0xAA ||
            std::all_of(s, s + 440, [](uint8_t b) { return b == 0; }))
            return;
        mbrCode.assign(s, s + 446);
        mbrFrom = PathText(path);
    };
    if (d.boot.mbrCode.Set())
    {
        if (d.boot.mbrCode.host.empty())
            result.report.push_back("boot.mbrCode: a partitioned disk takes a host file ({host: path}); ignored");
        else
        {
            std::ifstream in(d.boot.mbrCode.host, std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (!in.good() && !in.eof())
                bytes.clear();
            if (bytes.empty() || bytes.size() > 446)
                return MediaResult::Fail(MediaError::BadRequest, "boot.mbrCode: " + PathText(d.boot.mbrCode.host) +
                                                                     " must hold 1 to 446 bytes");
            mbrCode = bytes;
            mbrCode.resize(446, 0);
            mbrFrom = PathText(d.boot.mbrCode.host);
        }
    }

    std::vector<PartitionedDisk::Part> parts;
    for (const ComposePartition& p : d.partitions)
    {
        const std::string where = "partition '" + p.name + "'";
        PartitionedDisk::Part part;
        part.name = p.name;
        CompositePartitionInfo pi;
        pi.name = p.name;
        pi.firstLayer = info.layers.size();
        if (p.source)
        {
            // Passthrough: the image's sectors as they are (a window of it for one of its partitions)
            const std::string path = PathText(p.source->path);
            std::string recovered;
            if (CommitJournal::Recover(p.source->path, &recovered) != CommitJournal::Recovery::None)
                result.report.push_back(where + ": " + recovered);
            std::string error;
            const std::string format = FileHelper::FileExists(path) ? HddImageFormats::Probe(path, &error) : std::string();
            std::unique_ptr<IBlockDevice> opened =
                format.empty() ? nullptr : HddImageFormats::OpenBlock(path, format, RawImage::Access::ReadOnly, &error);
            if (!opened)
                return MediaResult::Fail(MediaError::UnreadableSource, where + ": " + path + ": " + (error.empty() ? "no such image" : error));
            std::shared_ptr<IBlockDevice> image(std::move(opened));
            if (p.source->partition)
                carryMbr(p.source->path, *image);
            if (p.source->partition)
            {
                uint8_t mbr[512];
                const uint32_t n = *p.source->partition;
                if (n < 1 || n > 4 || !image->ReadSector(0, mbr) || mbr[510] != 0x55 || mbr[511] != 0xAA)
                    return MediaResult::Fail(MediaError::BadRequest, where + ": " + path + " has no MBR partition " + std::to_string(n));
                const uint8_t* e = mbr + 446 + 16 * (n - 1);
                const uint64_t first = e[8] | (e[9] << 8) | (e[10] << 16) | (static_cast<uint64_t>(e[11]) << 24);
                const uint64_t count = e[12] | (e[13] << 8) | (e[14] << 16) | (static_cast<uint64_t>(e[15]) << 24);
                if (e[4] == 0 || count == 0 || first >= image->SectorCount())
                    return MediaResult::Fail(MediaError::BadRequest, where + ": " + path + " has no MBR partition " + std::to_string(n));
                part.type = e[4];
                part.sectors = count;
                const uint64_t held = std::min(count, image->SectorCount() - first);
                if (held < count)
                    result.report.push_back(where + ": the image holds " + std::to_string(held) + " of its " + std::to_string(count) +
                                            " sectors; the rest reads as zeros");
                part.device = std::make_shared<SubRangeDevice>(image, first, count);
            }
            else
            {
                part.device = image;
                part.sectors = image->SectorCount();
            }
            FatVolumeReader reader;
            if (reader.Open(*part.device))
            {
                pi.fs = reader.Type() == FatReaderType::Fat32 ? "fat32" : reader.Type() == FatReaderType::Fat12 ? "fat12" : "fat16";
                part.fatBits = reader.Type() == FatReaderType::Fat32 ? 32 : reader.Type() == FatReaderType::Fat12 ? 12 : 16;
            }
            else if (!part.type && !p.type)
                return MediaResult::Fail(MediaError::BadRequest, where + ": " + path + " holds no FAT volume: name its type");
            pi.kind = "image";
            CompositeLayerInfo layer;
            layer.name = p.name;
            layer.kind = "image";
            layer.path = path;
            layer.identity = part.device->ContentId();
            info.layers.push_back(layer);
        }
        else
        {
            // A composition of its own, without an MBR (the disk has one)
            ComposeDescriptor child = *p.compose;
            if (!child.layers.empty() && child.layers[0].source.kind == ComposeSource::Kind::Image && child.layers[0].source.partition)
            {
                std::string ignored;
                const std::string path = PathText(child.layers[0].source.path);
                const std::string format = FileHelper::FileExists(path) ? HddImageFormats::Probe(path, &ignored) : std::string();
                if (auto bottom = format.empty() ? nullptr : HddImageFormats::OpenBlock(path, format, RawImage::Access::ReadOnly, &ignored))
                    carryMbr(child.layers[0].source.path, *bottom);
            }
            if (!child.target.fixedTimeUtc)
                child.target.fixedTimeUtc = d.target.fixedTimeUtc;
            if (!child.target.codePage)
                child.target.codePage = d.target.codePage;
            child.file = d.file;
            CompositeBuildOptions childOptions = options;
            childOptions.mbr = false;
            childOptions.slotKind = MediaKind::Block;
            std::unique_ptr<IBlockDevice> built;
            CompositeInfo childInfo;
            const MediaResult r = Build(child, childOptions, built, childInfo);
            for (const std::string& line : r.report)
                result.report.push_back(where + ": " + line);
            if (!r.Ok())
                return MediaResult::Fail(r.error, where + ": " + r.message);
            std::shared_ptr<IBlockDevice> device(std::move(built));
            part.layout = dynamic_cast<const IComposedLayout*>(device.get());
            if (const auto* graft = dynamic_cast<const GraftVolume*>(device.get()); graft && graft->VolumeStart() > 0)
            {
                // A graft over an image partition is in the image's coordinates: cut the volume out
                part.sectors = graft->VolumeSectors();
                part.layoutOffset = graft->VolumeStart();
                part.device = std::make_shared<SubRangeDevice>(device, graft->VolumeStart(), part.sectors);
            }
            else
            {
                part.sectors = device->SectorCount();
                part.device = device;
            }
            part.fatBits = childInfo.fsName == "fat32" ? 32 : childInfo.fsName == "fat12" ? 12 : 16;
            pi.kind = "compose";
            pi.fs = childInfo.fsName;
            pi.build = childInfo.build;
            for (CompositeLayerInfo layer : childInfo.layers)
            {
                layer.name = p.name + "/" + layer.name;
                info.layers.push_back(std::move(layer));
            }
            info.files += childInfo.files;
            info.bytes += childInfo.bytes;
            info.sourceDevices += childInfo.sourceDevices;
            if (childInfo.countLater)
            {
                // C4b: a graft partition counts its unread base directories when the disk's counts are asked for
                auto chained = std::move(info.countLater);
                info.countLater = [chained, child = std::make_shared<CompositeInfo>(std::move(childInfo)),
                                   first = pi.firstLayer](CompositeInfo& counted) {
                    if (chained)
                        chained(counted);
                    const uint64_t files = child->files, bytes = child->bytes;
                    const uint64_t baseFiles = child->layers.front().files, baseBytes = child->layers.front().bytes;
                    child->CompleteCounts();
                    counted.files += child->files - files;
                    counted.bytes += child->bytes - bytes;
                    counted.layers[first].files += child->layers.front().files - baseFiles;
                    counted.layers[first].bytes += child->layers.front().bytes - baseBytes;
                };
            }
        }
        if (p.type)
            part.type = *p.type;
        pi.layerCount = info.layers.size() - pi.firstLayer;
        info.partitions.push_back(pi);
        parts.push_back(std::move(part));
    }

    std::optional<uint64_t> total;
    if (d.target.size)
        total = (*d.target.size + 511) / 512;
    std::string error;
    auto disk = PartitionedDisk::Build(std::move(parts), total, info.descriptor, &error);
    if (!disk)
        return MediaResult::Fail(MediaError::DoesNotFit, error);
    if (!mbrCode.empty())
    {
        disk->SetMbrCode(mbrCode);
        result.report.push_back("boot: MBR code carried from " + mbrFrom);
    }
    for (size_t i = 0; i < disk->Parts().size(); i++)
    {
        info.partitions[i].type = disk->Parts()[i].type;
        info.partitions[i].start = disk->Parts()[i].start;
        info.partitions[i].sectors = disk->Parts()[i].sectors;
    }
    info.sectors = disk->SectorCount();
    uint64_t id = disk->ContentId();
    for (char c : info.normalized)
    {
        id ^= static_cast<uint8_t>(c);
        id *= 0x100000001b3ULL;
    }
    info.contentId = id;
    disk->SetContentId(id);
    volume = std::move(disk);
    return result;
}

MediaResult CompositeMediumFactory::Build(const ComposeDescriptor& d, const CompositeBuildOptions& options,
                                          std::unique_ptr<IBlockDevice>& volume, CompositeInfo& info)
{
    volume.reset();
    if (!d.Ok())
        return MediaResult::Fail(MediaError::BadRequest, d.error);

    MediaResult result = MediaResult::Success();
    result.report = d.report;

    // --- Partitions (c7-partitions.md): each one a passthrough image or a composition of its own ---
    if (d.hasPartitions)
        return BuildPartitioned(d, options, volume, info, std::move(result));

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
    if (d.target.onBadName == "replace")
        result.report.push_back("target.onBadName: replace is not implemented yet; names a FAT volume cannot hold are skipped");

    // --- Layers: scan, filter, enumerate ---
    auto pool = std::make_shared<SourcePool>();
    std::vector<FileTree> trees(d.layers.size());
    std::vector<UnionLayer> layers;
    int baseDevice = -1;  ///< the bottom layer's image (the whole file), a graft candidate
    int isoBottom = -1;   ///< the bottom layer's CD image, an El Torito source
    std::vector<std::pair<size_t, int>> isoLayers;  ///< (layer, device) of every ISO layer
    info = CompositeInfo{};
    info.descriptor = d.file.empty() ? "(inline)" : PathText(d.file);
    info.normalized = d.Normalized();
    info.delta = d.writes.delta;
    info.writesSave = d.writes.save;
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

    // C4b: a graft candidate's base is read lazily, only the directories the upper layers reach. Filters on
    // the base keep the full read: a graft removes what they leave out, in every directory
    const ComposeLayer* bottomLayer = d.layers.empty() ? nullptr : &d.layers.front();
    const bool lazyBase = options.lazyBase && !optical && d.target.build != ComposeTarget::Build::Rebuild && bottomLayer &&
                          bottomLayer->source.kind == ComposeSource::Kind::Image && (bottomLayer->mount.empty() || bottomLayer->mount == "/") &&
                          (bottomLayer->from.empty() || bottomLayer->from == "/") && bottomLayer->include.empty() &&
                          bottomLayer->exclude.empty();
    std::unique_ptr<FatImageExpander> expander;
    std::vector<uint32_t> uncountedBase, uncountedUnion;  ///< C4b: unexpanded directories, counted by CompleteCounts

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
            if (i == 0)
                isoBottom = device;
            isoLayers.push_back({i, device});
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
                // An interrupted S3 commit into this image is undone first (DT-14)
                std::string recovered;
                if (CommitJournal::Recover(layer.source.path, &recovered) != CommitJournal::Recovery::None)
                    result.report.push_back(where + ": " + recovered);
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
            source.lazy = i == 0 && lazyBase;
            std::vector<std::string> sourceReport;
            std::string error;
            uint16_t volume = 0;
            if (!FatImageSource::Enumerate(static_cast<uint16_t>(device), source, *pool, trees[i], &sourceReport, &error,
                                           &sourceIdentity, &volume))
                return MediaResult::Fail(MediaError::UnreadableSource, where + ": " + path + ": " + error);
            if (source.lazy)
                expander = std::make_unique<FatImageExpander>(*pool, volume, source);
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
        layerInfo.writable = layer.writable && layer.source.kind == ComposeSource::Kind::Folder;
        Count(trees[i], FileTree::kRoot, layerInfo.files, layerInfo.bytes, i == 0 && expander ? &uncountedBase : nullptr);
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

    // --- C4b: the base directories the build reaches, read before the merge ---
    if (expander)
    {
        std::vector<std::string> paths;
        for (size_t i = 1; i < d.layers.size(); i++)
        {
            // Every directory of an upper layer merges with the base's directory of the same path
            const std::string mount = d.layers[i].mount.empty() ? std::string("/") : d.layers[i].mount;
            for (uint32_t n = 0; n < trees[i].NodeCount(); n++)
                if (trees[i].Node(n).isDirectory)
                    paths.push_back(mount + "/" + trees[i].PathOf(n));
            paths.insert(paths.end(), d.layers[i].whiteout.begin(), d.layers[i].whiteout.end());
            paths.insert(paths.end(), d.layers[i].opaque.begin(), d.layers[i].opaque.end());
        }
        paths.insert(paths.end(), d.deleted.begin(), d.deleted.end());
        for (const auto& [path, bits] : d.attributes)
            paths.push_back(path);
        for (const ComposeBootFile* file : {&d.boot.mbrCode, &d.boot.volumeCode})
            paths.push_back(file->unionPath);
        for (const auto& [lba, file] : d.boot.reserved)
            paths.push_back(file.unionPath);
        std::vector<std::string> expandReport;
        std::string error;
        for (const std::string& path : paths)
        {
            if (!expander->ExpandPath(trees[0], path, &expandReport, &error))
                return MediaResult::Fail(MediaError::UnreadableSource, "layer '" + d.layers[0].name + "': " + error);
        }
        for (const std::string& line : expandReport)
            result.report.push_back("layer '" + d.layers[0].name + "': " + line);
    }

    // --- Union ---
    std::shared_ptr<FileTree> tree;
    std::string error;
    // A guest path in the union, by the FAT key of each component
    auto findInUnion = [&tree](const std::string& path) {
        uint32_t at = FileTree::kRoot;
        size_t pos = 1;
        while (at != FileTree::kNone && pos < path.size())
        {
            const size_t slash = path.find('/', pos);
            const std::string part = path.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
            pos = slash == std::string::npos ? path.size() : slash + 1;
            uint32_t found = FileTree::kNone;
            for (uint32_t child : tree->Node(at).children)
                if (UnionBuilder::FatKey(tree->Node(child).name) == UnionBuilder::FatKey(part))
                    found = child;
            at = found;
        }
        return at;
    };
    // The merge, what the guest deleted and the attributes it set, and the counts: again when a lazily read base
    // is read in full for a rebuild
    auto unite = [&](std::vector<std::string>& report) {
        tree = std::make_shared<FileTree>();
        if (!UnionBuilder::Merge(layers, optical ? UnionBuilder::ExactKey : UnionBuilder::FatKey, *tree, &report, &error))
            return false;
        // S4: what the guest deleted from layers that are not changed by a delete (<descriptor>.whiteout)
        for (const std::string& path : d.deleted)
        {
            const uint32_t at = findInUnion(path);
            if (at != FileTree::kNone && at != FileTree::kRoot)
            {
                tree->Detach(at);
                report.push_back(path + ": deleted by the guest (" + FileHelper::FromFsPath(d.file.filename()) + ".whiteout)");
            }
        }
        // C8d: the attribute bits the guest gave files (<descriptor>.attributes)
        for (const auto& [path, bits] : d.attributes)
        {
            const uint32_t at = findInUnion(path);
            if (at != FileTree::kNone && at != FileTree::kRoot)
                tree->Node(at).attributes = bits;
            else
                report.push_back(path + ": in " + FileHelper::FromFsPath(d.file.filename()) + ".attributes but not on the disk any more");
        }
        info.files = 0;
        info.bytes = 0;
        uncountedUnion.clear();
        Count(*tree, FileTree::kRoot, info.files, info.bytes, expander ? &uncountedUnion : nullptr);
        return true;
    };
    if (!unite(result.report))
        return MediaResult::Fail(MediaError::BadRequest, error);

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

        // El Torito (D-6, DT-5): the boot section's list, else the bottom ISO layer's catalog
        if (!d.boot.eltorito.empty())
        {
            for (const ComposeElTorito& e : d.boot.eltorito)
            {
                IsoBootImage image;
                image.entry.platform = e.platform;
                image.entry.emulation = e.emulation;
                image.entry.loadSegment = e.loadSegment;
                image.entry.sectorCount = e.sectors;
                FileData data;
                uint32_t node = FileTree::kNone;
                if (!BootFileData(e.image, *tree, *pool, data, node, error))
                    return MediaResult::Fail(MediaError::BadRequest, error);
                image.unionNode = node;
                image.data = data;
                iso.boot.push_back(image);
            }
            result.report.push_back("boot: El Torito, " + std::to_string(iso.boot.size()) + " entries from the boot section");
        }
        else if (isoBottom >= 0)
        {
            Iso9660Reader bottom;
            std::vector<IsoBootEntry> entries;
            std::string why;
            if (bottom.Open(pool->Device(static_cast<uint16_t>(isoBottom))) && bottom.BootCatalogBlock() != 0)
            {
                if (!bottom.ReadBootCatalog(entries, &why))
                    result.report.push_back("boot: the bottom layer's El Torito catalog is not carried: " + why);
                for (const IsoBootEntry& entry : entries)
                {
                    IsoBootImage image;
                    image.entry = entry;
                    // A boot image that is also a visible file shares its extent
                    for (uint32_t n = 0; n < tree->NodeCount() && image.unionNode == FileTree::kNone; n++)
                    {
                        const TreeNode& node = tree->Node(n);
                        if (node.isDirectory || node.data.storage != FileData::Storage::DeviceExtents ||
                            node.data.source != static_cast<uint16_t>(isoBottom) || node.data.extentCount == 0 ||
                            tree->Extents()[node.data.firstExtent].sourceLba != static_cast<uint64_t>(entry.loadBlock) * 4)
                            continue;
                        if (tree->Find(tree->PathOf(n)) == n)
                            image.unionNode = n;
                    }
                    if (image.unionNode == FileTree::kNone)
                    {
                        image.data.storage = FileData::Storage::DeviceExtents;
                        image.data.source = static_cast<uint16_t>(isoBottom);
                        image.data.firstExtent = static_cast<uint32_t>(tree->Extents().size());
                        image.data.extentCount = 1;
                        image.data.bytes = entry.imageBytes;
                        tree->Extents().push_back(Extent{static_cast<uint64_t>(entry.loadBlock) * 4,
                                                         static_cast<uint32_t>((entry.imageBytes + 511) / 512), 0});
                    }
                    iso.boot.push_back(image);
                }
                if (!iso.boot.empty())
                    result.report.push_back("boot: El Torito carried from layer '" + d.layers.front().name + "', " +
                                            std::to_string(iso.boot.size()) + " entries");
            }
        }
        for (const auto& [layerIndex, device] : isoLayers)
        {
            Iso9660Reader upper;
            if (layerIndex > 0 && upper.Open(pool->Device(static_cast<uint16_t>(device))) && upper.BootCatalogBlock() != 0)
                result.report.push_back("boot data in layer '" + d.layers[layerIndex].name +
                                        "' ignored: only the bottom layer or a boot section provides it");
        }
        if (d.boot.mbrCode.Set() || d.boot.volumeCode.Set() || !d.boot.reserved.empty())
            result.report.push_back("boot.mbrCode, volumeCode, reserved: ignored, a CD has none");

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

    // --- The boot section's FAT structures (D-6): for a graft and a rebuild alike ---
    std::shared_ptr<FatBootPlan> descriptorBoot;
    if (d.boot.mbrCode.Set() || d.boot.volumeCode.Set() || !d.boot.reserved.empty())
    {
        descriptorBoot = std::make_shared<FatBootPlan>();
        if (!PlanFromDescriptor(d.boot, *tree, *pool, *descriptorBoot, error))
            return MediaResult::Fail(MediaError::BadRequest, error);
    }
    if (!d.boot.eltorito.empty())
        result.report.push_back("boot.eltorito: ignored, a FAT volume has no El Torito catalog");

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
                graft.boot = descriptorBoot;
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
                    if (expander && (!uncountedBase.empty() || !uncountedUnion.empty()))
                    {
                        // C4b: the directories the graft did not read are counted when somebody asks
                        std::shared_ptr<FatImageExpander> lister(std::move(expander));
                        info.countLater = [lister, pool, base = std::move(uncountedBase),
                                           merged = std::move(uncountedUnion)](CompositeInfo& counted) {
                            for (uint32_t cluster : base)
                            {
                                const auto [files, bytes] = lister->Count(cluster);
                                counted.layers.front().files += files;
                                counted.layers.front().bytes += bytes;
                            }
                            for (uint32_t cluster : merged)
                            {
                                const auto [files, bytes] = lister->Count(cluster);
                                counted.files += files;
                                counted.bytes += bytes;
                            }
                        };
                    }
                    return result;
                }
                failWith = failure == GraftFailure::DoesNotFit ? MediaError::DoesNotFit : MediaError::UnreadableSource;
            }
            if (graftOnly)
                return MediaResult::Fail(failWith, "build: graft: " + why);
            result.report.push_back("rebuild instead of a graft: " + why);
        }
    }
    if (expander)
    {
        // C4b: a rebuild lays out the whole base: read the rest of it and merge again (the report has the merge already)
        std::vector<std::string> again;
        if (!expander->ExpandAll(trees[0], &again, &error))
            return MediaResult::Fail(MediaError::UnreadableSource, "layer '" + d.layers[0].name + "': " + error);
        expander.reset();
        if (!unite(again))
            return MediaResult::Fail(MediaError::BadRequest, error);
        info.layers.front().files = 0;
        info.layers.front().bytes = 0;
        Count(trees[0], FileTree::kRoot, info.layers.front().files, info.layers.front().bytes);
        if (descriptorBoot)
        {
            descriptorBoot = std::make_shared<FatBootPlan>();
            if (!PlanFromDescriptor(d.boot, *tree, *pool, *descriptorBoot, error))
                return MediaResult::Fail(MediaError::BadRequest, error);
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
    // Boot structures (D-6, DT-5): the boot section's, else the bottom FAT image's
    if (descriptorBoot)
        fat.boot = descriptorBoot;
    else if (baseDevice >= 0)
    {
        std::vector<std::string> what;
        auto carried = std::make_shared<FatBootPlan>(
            FatBootPlan::FromVolume(pool->DevicePtr(static_cast<uint16_t>(baseDevice)), d.layers.front().source.partition, &what));
        if (!carried->Empty())
        {
            std::string list;
            for (const std::string& w : what)
                list += (list.empty() ? "" : ", ") + w;
            result.report.push_back("boot: carried from layer '" + d.layers.front().name + "': " + list);
            fat.boot = carried;
        }
    }

    std::unique_ptr<FatSynthVolume> rebuilt;
    for (size_t c = 0; c < candidates.size() && !rebuilt; c++)
    {
        fat.fs = candidates[c];
        std::vector<std::string> buildReport;
        if (d.target.size)
            rebuilt = FatSynthVolume::BuildToSize(tree, pool, fat, *d.target.size, identity, info.descriptor, &error, &buildReport);
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

void CompositeInfo::CompleteCounts() const
{
    if (!countLater)
        return;
    // The info is made mutable by the build and only shared as const: the counts are its own to finish
    auto& self = const_cast<CompositeInfo&>(*this);
    const std::function<void(CompositeInfo&)> count = std::move(self.countLater);
    self.countLater = nullptr;
    count(self);
}

DeltaIdentity CompositeMediumFactory::DeltaIdentityOf(const CompositeInfo& info)
{
    DeltaIdentity identity;
    identity.contentId = info.contentId;
    identity.sectorCount = info.sectors;
    for (const CompositeLayerInfo& layer : info.layers)
        identity.layers.push_back({layer.name, layer.identity});
    return identity;
}

MediaResult CompositeMediumFactory::Open(const OpenRequest& request, std::unique_ptr<Medium>& medium)
{
    medium.reset();
    const MediaSource& source = request.source;
    if (request.kind != MediaKind::Block && request.kind != MediaKind::Optical)
        return MediaResult::Fail(MediaError::NotSupported, "a composite goes into a disk or CD slot, not a floppy or tape slot");
    if (request.kind == MediaKind::Block && request.access == AccessMode::WriteThrough)
        return MediaResult::Fail(MediaError::KindMismatch, "a composite is never written in place: use session or readonly access");

    // An S4 write-back cut short is finished before the descriptor (and its .whiteout list) is read
    const std::string writeBackRecovered = source.inlineBody.empty() ? WriteBack::Recover(FileHelper::ToFsPath(source.path)) : std::string();
    ComposeDescriptor descriptor = source.inlineBody.empty()
                                       ? ComposeDescriptor::Load(FileHelper::ToFsPath(source.path))
                                       : ComposeDescriptor::Parse(source.inlineBody, std::filesystem::current_path(), ComposeDescriptor::kInlineName);
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
    if (!writeBackRecovered.empty())
        result.report.insert(result.report.begin(), writeBackRecovered);
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

    // DT-13: a session delta next to the descriptor goes back into the change layer when it was written
    // over this very composite
    if (SessionWriteMap* session = medium->Session(); session && !info->delta.empty())
    {
        std::string detail;
        const std::string name = FileHelper::FromFsPath(info->delta.filename());
        switch (SessionDelta::Load(info->delta, *session, DeltaIdentityOf(*info), detail))
        {
            case DeltaLoad::Missing:
                break;
            case DeltaLoad::Restored:
                medium->MarkPersisted();
                medium->Report().push_back("session restored from " + name + ": " + detail);
                result.report.push_back(medium->Report().back());
                break;
            case DeltaLoad::Damaged:
            {
                std::filesystem::path bad = info->delta;
                bad += ".bad";
                std::error_code ec;
                std::filesystem::rename(info->delta, bad, ec);
                medium->Report().push_back(name + " is damaged (" + detail + "): not applied, kept as " +
                                           FileHelper::FromFsPath(bad.filename()));
                result.report.push_back(medium->Report().back());
                break;
            }
            case DeltaLoad::Mismatch:
                medium->SetDeltaConflict(name + " was written over other sources (" + detail + ")");
                medium->Report().push_back(name + " not applied, it was written over other sources: " + detail +
                                           "; the medium starts clean");
                result.report.push_back(medium->Report().back());
                break;
        }
    }
    medium->SetComposite(std::move(info));
    return result;
}
