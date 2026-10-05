#include "stdafx.h"

#include "fatsynthvolume.h"

#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/subrangedevice.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "emulator/io/storage/compose/sourcepool.h"
#include "emulator/io/storage/hostfolder/fatnamemapper.h"

namespace
{
    constexpr uint32_t kSector = 512;

    // FAT type limits as strict readers check them (ChaN FatFs: <= 4085 is
    // FAT12, <= 65525 FAT16), with a margin so no reader sits on an edge
    constexpr uint32_t kFat16MinClusters = 4086 + 64;
    constexpr uint32_t kFat16MaxClusters = 65525 - 64;
    constexpr uint32_t kFat32MinClusters = 65526 + 64;
    constexpr uint32_t kFat32MaxClusters = 0x0FFFFFF5 - 64;

    void Put16(uint8_t* p, uint16_t v)
    {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
    }
    void Put32(uint8_t* p, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            p[i] = static_cast<uint8_t>(v >> (8 * i));
    }

    /// DOS date / time of a UTC second count (days-from-civil, no time zone)
    void DosDateTime(int64_t unixSeconds, uint16_t& date, uint16_t& time)
    {
        constexpr int64_t kDosEpoch = 315532800;  // 1980-01-01 00:00:00 UTC
        constexpr int64_t kDosLast = 4354819198;  // 2107-12-31 23:59:58 UTC
        unixSeconds = std::clamp(unixSeconds, kDosEpoch, kDosLast);

        const int64_t days = unixSeconds / 86400;
        const int64_t secondsOfDay = unixSeconds % 86400;
        // Howard Hinnant's civil_from_days
        const int64_t z = days + 719468;
        const int64_t era = z / 146097;
        const int64_t doe = z - era * 146097;
        const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const int64_t mp = (5 * doy + 2) / 153;
        const int64_t day = doy - (153 * mp + 2) / 5 + 1;
        const int64_t month = mp < 10 ? mp + 3 : mp - 9;
        const int64_t year = yoe + era * 400 + (month <= 2 ? 1 : 0);

        date = static_cast<uint16_t>(((year - 1980) << 9) | (month << 5) | day);
        time = static_cast<uint16_t>(((secondsOfDay / 3600) << 11) | (((secondsOfDay / 60) % 60) << 5) | ((secondsOfDay % 60) / 2));
    }

    /// One directory while building
    struct DirPlan
    {
        uint32_t node = 0;
        size_t parent = 0;               ///< index of the parent DirPlan
        std::vector<FatMappedName> names;
        std::vector<uint32_t> children;  ///< only those whose names map
        uint32_t entryCount = 0;         ///< 32-byte entries incl. ".", "..", label, LFN
        uint32_t firstCluster = 0;       ///< 0 for the FAT16 root
        uint32_t clusters = 0;
    };

    uint32_t CeilDiv(uint64_t a, uint64_t b)
    {
        return static_cast<uint32_t>((a + b - 1) / b);
    }
}  // namespace

std::unique_ptr<FatSynthVolume> FatSynthVolume::Build(std::shared_ptr<const FileTree> tree, std::shared_ptr<SourcePool> pool,
                                                      const FatVolumeOptions& options, uint64_t sourceIdentity,
                                                      std::string description, std::string* error,
                                                      std::vector<std::string>* report)
{
    std::unique_ptr<FatSynthVolume> volume(new FatSynthVolume());
    if (!volume->Init(std::move(tree), std::move(pool), options, sourceIdentity, std::move(description), error, report))
        return nullptr;
    return volume;
}

bool FatSynthVolume::Init(std::shared_ptr<const FileTree> tree, std::shared_ptr<SourcePool> pool, const FatVolumeOptions& options,
                          uint64_t sourceIdentity, std::string description, std::string* error, std::vector<std::string>* report)
{
    _options = options;
    _description = std::move(description);
    _tree = std::move(tree);
    _pool = std::move(pool);
    _reader = std::make_unique<ExtentReader>(*_pool, _tree->Extents());
    const FileTree& files = *_tree;
    const bool fat32 = options.fs == FatType::Fat32;

    // --- Directories, breadth-first; names mapped per directory ---
    std::vector<DirPlan> dirs;
    dirs.push_back(DirPlan{FileTree::kRoot, 0, {}, {}, 0, 0, 0});
    for (size_t d = 0; d < dirs.size(); d++)
    {
        const TreeNode& dirNode = files.Node(dirs[d].node);
        std::vector<std::string> names;
        for (uint32_t child : dirNode.children)
            names.push_back(files.Node(child).name);
        const std::vector<FatMappedName> mapped = FatNameMapper::MapFolder(names, options.codePage);

        DirPlan& plan = dirs[d];
        plan.entryCount = d == 0 ? 1 : 2;  // the volume label, or "." and ".."
        for (size_t i = 0; i < mapped.size(); i++)
        {
            const uint32_t child = dirNode.children[i];
            if (!mapped[i].ok)
            {
                if (report)
                    report->push_back(files.Node(child).name + ": " + mapped[i].reason);
                continue;
            }
            plan.names.push_back(mapped[i]);
            plan.children.push_back(child);
            plan.entryCount += 1 + (mapped[i].hasLongName ? CeilDiv(mapped[i].longName.size(), 13) : 0);
        }
        for (uint32_t child : std::vector<uint32_t>(dirs[d].children))
        {
            if (files.Node(child).isDirectory)
                dirs.push_back(DirPlan{child, d, {}, {}, 0, 0, 0});
        }
    }

    // --- Cluster size and volume size ---
    const uint32_t entryBytes = fat32 ? 4 : 2;
    uint64_t fileBytesTotal = 0;
    for (const DirPlan& plan : dirs)
        for (uint32_t child : plan.children)
            if (!files.Node(child).isDirectory)
                fileBytesTotal += files.Node(child).data.bytes;

    const uint32_t firstSpc = fat32 ? 8 : 1;  // FAT32: 4 KiB clusters, as SD cards ship
    bool fitted = false;
    for (uint32_t spc = firstSpc; spc <= 64 && !fitted; spc *= 2)
    {
        const uint64_t clusterBytes = static_cast<uint64_t>(spc) * kSector;
        uint64_t used = 0;
        for (size_t d = 0; d < dirs.size(); d++)
        {
            if (d == 0 && !fat32)
                continue;  // the FAT16 root has its own region
            used += std::max<uint32_t>(1, CeilDiv(static_cast<uint64_t>(dirs[d].entryCount) * 32, clusterBytes));
        }
        for (const DirPlan& plan : dirs)
            for (uint32_t child : plan.children)
                if (!files.Node(child).isDirectory)
                    used += CeilDiv(files.Node(child).data.bytes, clusterBytes);

        uint64_t total = used + options.freeBytes / clusterBytes;
        total = std::max<uint64_t>(total, fat32 ? kFat32MinClusters : kFat16MinClusters);
        if (total <= (fat32 ? kFat32MaxClusters : kFat16MaxClusters))
        {
            _sectorsPerCluster = spc;
            _clusterCount = static_cast<uint32_t>(total);
            _usedClusters = static_cast<uint32_t>(used);
            fitted = true;
        }
    }
    if (!fitted)
    {
        if (error)
        {
            *error = fat32 ? "the folder is too large for a FAT32 volume"
                           : "the folder (" + std::to_string(fileBytesTotal) +
                                 " bytes) does not fit a FAT16 volume (2 GiB with 32 KiB clusters): use fs=fat32";
        }
        return false;
    }

    _reservedSectors = fat32 ? 32 : 1;
    if (!fat32)
    {
        _rootEntries = std::max<uint32_t>(512, (dirs[0].entryCount + 15) / 16 * 16);
        _rootDirSectors = _rootEntries * 32 / kSector;
    }
    _fatSectors = CeilDiv(static_cast<uint64_t>(_clusterCount + 2) * entryBytes, kSector);
    _volumeSectors = _reservedSectors + 2ull * _fatSectors + _rootDirSectors +
                     static_cast<uint64_t>(_clusterCount) * _sectorsPerCluster;
    _volumeStart = options.mbr ? options.partitionStart : 0;
    _totalSectors = _volumeStart + _volumeSectors;

    // --- Boot structures (D-6): sizes and places checked before anything is laid out from them ---
    if (options.boot && !options.boot->Empty())
    {
        auto boot = std::make_shared<FatBootPlan>(*options.boot);
        // Too big or in the wrong place: an explicit boot section fails, an implicit carry drops it and says so
        auto refuse = [error, report, &boot](const std::string& text) {
            if (boot->bestEffort)
            {
                if (report)
                    report->push_back(text + ": not carried");
                return true;
            }
            if (error)
                *error = text;
            return false;
        };
        if (boot->mbrCode.size() > 446)
        {
            if (!refuse("boot: the MBR code is " + std::to_string(boot->mbrCode.size()) + " bytes, the MBR holds 446"))
                return false;
            boot->mbrCode.clear();
        }
        for (auto it = boot->gap.begin(); it != boot->gap.end();)
        {
            if (it->first == 0 || it->first >= _volumeStart)
            {
                if (!refuse("boot: sector " + std::to_string(it->first) + " is not between the MBR and the partition"))
                    return false;
                it = boot->gap.erase(it);
            }
            else
                ++it;
        }
        if (!boot->mbrCode.empty() && _volumeStart == 0)
        {
            if (report && !boot->bestEffort)
                report->push_back("boot: the MBR code is not used, the volume has no MBR (partition: none)");
            boot->mbrCode.clear();
        }
        const size_t codeStart = fat32 ? 90 : 62;
        const size_t codeEnd = _volumeStart == 0 ? 446 : 510;  // a superfloppy's sector 0 carries a partition entry
        if (boot->volumeCode.size() > codeEnd - codeStart)
        {
            if (!refuse("boot: the volume boot code is " + std::to_string(boot->volumeCode.size()) +
                        " bytes, the boot sector holds " + std::to_string(codeEnd - codeStart)))
                return false;
            boot->volumeCode.clear();
        }
        for (auto it = boot->reserved.begin(); it != boot->reserved.end();)
        {
            const uint32_t lba = it->first;
            if (fat32 && (lba == 1 || lba == 2 || lba == 6 || lba == 7))
            {
                if (!refuse("boot: reserved sector " + std::to_string(lba) + " is FAT32's FSInfo or boot record backup"))
                    return false;
                it = boot->reserved.erase(it);
            }
            else if (lba >= 0xFFFF)
            {
                if (!refuse("boot: reserved sector " + std::to_string(lba) + " is past what a BPB can reserve"))
                    return false;
                it = boot->reserved.erase(it);
            }
            else
                ++it;
        }
        if (!boot->reserved.empty())
        {
            const uint32_t needed = boot->reserved.rbegin()->first + 1;
            if (needed > _reservedSectors)
            {
                // More reserved sectors: the layout moves by the difference
                _volumeSectors += needed - _reservedSectors;
                _reservedSectors = needed;
                _totalSectors = _volumeStart + _volumeSectors;
            }
        }
        _options.boot = boot;
    }

    // --- Clusters: directories first (breadth-first), then files ---
    const uint64_t clusterBytes = static_cast<uint64_t>(_sectorsPerCluster) * kSector;
    uint32_t next = 2;
    for (size_t d = 0; d < dirs.size(); d++)
    {
        if (d == 0 && !fat32)
            continue;
        dirs[d].clusters = std::max<uint32_t>(1, CeilDiv(static_cast<uint64_t>(dirs[d].entryCount) * 32, clusterBytes));
        dirs[d].firstCluster = next;
        next += dirs[d].clusters;
    }
    std::unordered_map<uint32_t, uint32_t> fileCluster;  // tree node -> first cluster (0: empty file)
    for (size_t d = 0; d < dirs.size(); d++)
    {
        for (uint32_t child : dirs[d].children)
        {
            const TreeNode& node = files.Node(child);
            if (node.isDirectory)
                continue;
            const uint32_t clusters = CeilDiv(node.data.bytes, clusterBytes);
            fileCluster[child] = clusters ? next : 0;
            if (clusters)
            {
                _runs.push_back({next, clusters, false, child});
                next += clusters;
            }
        }
    }

    // --- Directory contents ---
    const FatShortName label = FatNameMapper::ShortNameFromText(options.label, /*isLabel*/ true, options.codePage);
    std::unordered_map<uint32_t, uint32_t> dirCluster;  // tree node -> first cluster
    for (const DirPlan& plan : dirs)
        dirCluster[plan.node] = plan.firstCluster;

    _directories.resize(dirs.size());
    for (size_t d = 0; d < dirs.size(); d++)
    {
        const DirPlan& plan = dirs[d];
        std::vector<uint8_t>& bytes = _directories[d];
        bytes.assign(d == 0 && !fat32 ? static_cast<size_t>(_rootDirSectors) * kSector
                                      : static_cast<size_t>(plan.clusters) * clusterBytes,
                     0);
        size_t at = 0;
        auto entry = [&](const FatShortName& name, uint8_t attr, uint32_t cluster, uint32_t size, int64_t mtime) {
            uint8_t* e = bytes.data() + at;
            std::copy(name.begin(), name.end(), e);
            e[11] = attr;
            uint16_t date = 0;
            uint16_t time = 0;
            DosDateTime(options.fixedTimeUtc.value_or(mtime), date, time);
            Put16(e + 14, time);  // creation
            Put16(e + 16, date);
            Put16(e + 18, date);  // last access
            Put16(e + 20, static_cast<uint16_t>(cluster >> 16));
            Put16(e + 22, time);  // last write
            Put16(e + 24, date);
            Put16(e + 26, static_cast<uint16_t>(cluster));
            Put32(e + 28, size);
            at += 32;
        };

        const TreeNode& dirNode = files.Node(plan.node);
        if (d == 0)
        {
            entry(label, 0x08, 0, 0, dirNode.mtimeUtc);
        }
        else
        {
            FatShortName dot;
            dot.fill(' ');
            dot[0] = '.';
            entry(dot, 0x10, plan.firstCluster, 0, dirNode.mtimeUtc);
            dot[1] = '.';
            // ".." of a first-level directory points at cluster 0, the root, on FAT16 and FAT32 alike
            entry(dot, 0x10, plan.parent == 0 ? 0 : dirs[plan.parent].firstCluster, 0, dirNode.mtimeUtc);
        }

        for (size_t i = 0; i < plan.children.size(); i++)
        {
            const uint32_t child = plan.children[i];
            const TreeNode& node = files.Node(child);
            const FatMappedName& name = plan.names[i];
            if (name.hasLongName)
            {
                for (const auto& lfn : FatNameMapper::LongNameEntries(name.longName, FatNameMapper::Checksum(name.shortName)))
                {
                    std::copy(lfn.begin(), lfn.end(), bytes.data() + at);
                    at += 32;
                }
            }
            const uint8_t attr = static_cast<uint8_t>((node.isDirectory ? 0x10 : 0x20) | node.attributes);
            const uint32_t cluster = node.isDirectory ? dirCluster[child] : fileCluster[child];
            entry(name.shortName, attr, cluster, node.isDirectory ? 0 : static_cast<uint32_t>(node.data.bytes), node.mtimeUtc);
        }
        if (d != 0 || fat32)
            _runs.push_back({plan.firstCluster, plan.clusters, true, static_cast<uint32_t>(d)});
    }
    std::sort(_runs.begin(), _runs.end(), [](const Run& a, const Run& b) { return a.firstCluster < b.firstCluster; });

    // Identity: the sources and every option that changes the bytes
    uint64_t id = sourceIdentity;
    auto mix = [&id](uint64_t v) {
        for (int i = 0; i < 8; i++)
        {
            id ^= static_cast<uint8_t>(v >> (8 * i));
            id *= 0x100000001b3ULL;
        }
    };
    mix(static_cast<uint64_t>(options.fs));
    mix(static_cast<uint64_t>(options.codePage));
    mix(options.mbr ? options.partitionStart : 0xFFFFFFFFull);
    mix(options.freeBytes);
    mix(options.serial);
    for (char c : options.label)
        mix(static_cast<uint8_t>(c));
    if (_options.boot && !_options.boot->Empty())
        mix(_options.boot->Identity());
    _contentId = id;
    return true;
}

std::unique_ptr<FatSynthVolume> FatSynthVolume::BuildToSize(std::shared_ptr<const FileTree> tree, std::shared_ptr<SourcePool> pool,
                                                            FatVolumeOptions options, uint64_t bytes, uint64_t sourceIdentity,
                                                            const std::string& description, std::string* error,
                                                            std::vector<std::string>* report)
{
    // Lay out with no room first, then give the rest of the size to free space
    FatVolumeOptions bare = options;
    bare.freeBytes = 0;
    auto probe = Build(tree, pool, bare, sourceIdentity, description, error, nullptr);
    if (!probe)
        return nullptr;
    const uint64_t minimum = probe->SectorCount() * kSector;
    if (minimum > bytes)
    {
        if (error)
            *error = "the content needs " + std::to_string(minimum) + " bytes as " +
                     (options.fs == FatType::Fat32 ? "fat32" : "fat16") + ", more than " + std::to_string(bytes);
        return nullptr;
    }
    // Free space turns into clusters, FAT and cluster size grow with the volume: correct the
    // estimate a few times and keep the largest volume not over the size
    std::unique_ptr<FatSynthVolume> best;
    std::vector<std::string> bestReport;
    options.freeBytes = bytes - minimum;
    for (int pass = 0; pass < 6; pass++)
    {
        std::vector<std::string> passReport;
        auto candidate = Build(tree, pool, options, sourceIdentity, description, error, &passReport);
        if (!candidate)
            break;
        const uint64_t size = candidate->SectorCount() * kSector;
        const uint64_t cluster = uint64_t(candidate->SectorsPerCluster()) * kSector;
        if (size > bytes)
        {
            options.freeBytes -= std::min(options.freeBytes, size - bytes + cluster);
            continue;
        }
        const bool closeEnough = bytes - size < cluster;
        if (!best || size > best->SectorCount() * kSector)
        {
            best = std::move(candidate);
            bestReport = std::move(passReport);
        }
        if (closeEnough)
            break;
        options.freeBytes += bytes - size;
    }
    if (!best)
    {
        best = std::move(probe);  // nothing fits under the size: the smallest layout
        bestReport.clear();
    }
    if (report)
        report->insert(report->end(), bestReport.begin(), bestReport.end());
    return best;
}

FatBootPlan FatBootPlan::FromVolume(std::shared_ptr<IBlockDevice> image, std::optional<uint32_t> partition,
                                    std::vector<std::string>* carried)
{
    FatBootPlan plan;
    plan.bestEffort = true;
    std::shared_ptr<IBlockDevice> window;
    uint64_t offset = 0;
    if (partition)
    {
        FatPartition part;
        if (!FatVolumeReader::FindPartition(*image, *partition, part))
            return plan;
        offset = part.first;
        window = std::make_shared<SubRangeDevice>(image, part.first, part.count);
    }
    FatVolumeReader reader;
    if (!reader.Open(window ? *window : *image))
        return plan;
    const uint64_t start = offset + reader.VolumeStart();
    uint8_t s[kSector];
    auto nonZero = [](const uint8_t* p, size_t n) { return std::any_of(p, p + n, [](uint8_t b) { return b != 0; }); };
    auto note = [carried](const std::string& what) {
        if (carried)
            carried->push_back(what);
    };
    if (start > 0 && image->ReadSector(0, s) && nonZero(s, 446))
    {
        plan.mbrCode.assign(s, s + 446);
        note("MBR code");
    }
    // The gap after the MBR: whole non-zero sectors (a loader there). A partition at 1 MiB or less is scanned
    uint32_t gapSectors = 0;
    for (uint64_t lba = 1; lba < start && lba < 2048; lba++)
    {
        if (!image->ReadSector(lba, s) || !nonZero(s, kSector))
            continue;
        std::array<uint8_t, 512> sector{};
        std::copy(s, s + kSector, sector.begin());
        plan.gap[static_cast<uint32_t>(lba)] = sector;
        gapSectors++;
    }
    if (gapSectors)
        note(std::to_string(gapSectors) + " sector(s) after the MBR");
    const bool fat32 = reader.Type() == FatReaderType::Fat32;
    uint32_t backup = 0;
    if (image->ReadSector(start, s))
    {
        const size_t codeStart = fat32 ? 90 : 62;
        // A superfloppy's sector 0 may carry a partition entry over itself at 446 (mformat, our own
        // builder): a FAT type, starting at LBA 0. Then the code ends there; otherwise it runs to 510
        const uint8_t* entry = s + 446;
        const uint8_t type = entry[4];
        const bool fatType = type == 0x01 || type == 0x04 || type == 0x06 || type == 0x0B || type == 0x0C || type == 0x0E;
        const uint32_t entryStart = entry[8] | (entry[9] << 8) | (entry[10] << 16) | (static_cast<uint32_t>(entry[11]) << 24);
        const uint32_t entrySize = entry[12] | (entry[13] << 8) | (entry[14] << 16) | (static_cast<uint32_t>(entry[15]) << 24);
        const bool partitionEntry = start == 0 && fatType && entryStart == 0 && entrySize > 0 && entrySize <= image->SectorCount();
        size_t codeEnd = partitionEntry ? 446 : 510;
        while (codeEnd > codeStart && s[codeEnd - 1] == 0)
            codeEnd--;
        if (codeEnd > codeStart)
        {
            plan.volumeCode.assign(s + codeStart, s + codeEnd);
            note("volume boot code");
        }
        backup = fat32 ? static_cast<uint32_t>(s[50] | (s[51] << 8)) : 0;
    }
    uint32_t sectors = 0;
    for (uint32_t lba = 1; lba < reader.ReservedSectors(); lba++)
    {
        if (fat32 && (lba == reader.FsInfoSector() || lba == 2 || (backup && lba >= backup && lba <= backup + 2)))
            continue;
        if (!image->ReadSector(start + lba, s) || !nonZero(s, kSector))
            continue;
        std::array<uint8_t, 512> sector{};
        std::copy(s, s + kSector, sector.begin());
        plan.reserved[lba] = sector;
        sectors++;
    }
    if (sectors)
        note(std::to_string(sectors) + " reserved sector(s)");
    return plan;
}

uint64_t FatBootPlan::Identity() const
{
    uint64_t h = 0xcbf29ce484222325ULL;
    auto mix = [&h](uint8_t b) {
        h ^= b;
        h *= 0x100000001b3ULL;
    };
    for (uint8_t b : mbrCode)
        mix(b);
    mix(0xFF);
    for (uint8_t b : volumeCode)
        mix(b);
    for (const auto& [lba, sector] : reserved)
    {
        for (int i = 0; i < 4; i++)
            mix(static_cast<uint8_t>(lba >> (8 * i)));
        for (uint8_t b : sector)
            mix(b);
    }
    mix(0xFE);
    for (const auto& [lba, sector] : gap)
    {
        for (int i = 0; i < 4; i++)
            mix(static_cast<uint8_t>(lba >> (8 * i)));
        for (uint8_t b : sector)
            mix(b);
    }
    return h;
}

const std::vector<std::string>& FatSynthVolume::Warnings() const
{
    return _pool->Warnings();
}

bool FatSynthVolume::ReadSector(uint64_t lba, uint8_t* dst)
{
    if (lba >= _totalSectors)
        return false;

    const FatBootPlan* boot = _options.boot && !_options.boot->Empty() ? _options.boot.get() : nullptr;
    if (lba < _volumeStart)
    {
        std::memset(dst, 0, kSector);
        if (lba == 0)
        {
            BuildMbr(dst);
            if (boot)
                std::copy(boot->mbrCode.begin(), boot->mbrCode.end(), dst);
        }
        else if (boot)
        {
            const auto it = boot->gap.find(static_cast<uint32_t>(lba));
            if (it != boot->gap.end())
                std::memcpy(dst, it->second.data(), kSector);
        }
        return true;
    }

    const uint64_t rel = lba - _volumeStart;
    const bool fat32 = _options.fs == FatType::Fat32;
    if (rel < _reservedSectors)
    {
        std::memset(dst, 0, kSector);
        if (boot)
        {
            const auto it = boot->reserved.find(static_cast<uint32_t>(rel));
            if (it != boot->reserved.end())
            {
                std::memcpy(dst, it->second.data(), kSector);
                return true;
            }
        }
        if (rel == 0)
            BuildBootSector(dst, false);
        else if (fat32 && rel == 1)
            BuildFsInfo(dst);
        else if (fat32 && rel == 6)
            BuildBootSector(dst, true);
        else if (fat32 && rel == 7)
            BuildFsInfo(dst);
        else if (fat32 && rel == 2)
        {
            dst[510] = 0x55;  // the third sector of a FAT32 boot record
            dst[511] = 0xAA;
        }
        return true;
    }

    const uint64_t fatStart = _reservedSectors;
    const uint64_t fatEnd = fatStart + 2ull * _fatSectors;
    if (rel < fatEnd)
    {
        BuildFatSector((rel - fatStart) % _fatSectors, dst);
        return true;
    }

    if (rel < fatEnd + _rootDirSectors)
    {
        const size_t offset = static_cast<size_t>(rel - fatEnd) * kSector;
        std::memcpy(dst, _directories[0].data() + offset, kSector);
        return true;
    }

    const uint64_t dataRel = rel - fatEnd - _rootDirSectors;
    ReadData(dataRel / _sectorsPerCluster + 2, static_cast<uint32_t>(dataRel % _sectorsPerCluster), dst);
    return true;
}

const FatSynthVolume::Run* FatSynthVolume::FindRun(uint64_t cluster) const
{
    auto it = std::upper_bound(_runs.begin(), _runs.end(), cluster,
                               [](uint64_t c, const Run& run) { return c < run.firstCluster; });
    if (it == _runs.begin())
        return nullptr;
    --it;
    return cluster < static_cast<uint64_t>(it->firstCluster) + it->clusters ? &*it : nullptr;
}

void FatSynthVolume::ReadData(uint64_t cluster, uint32_t sectorInCluster, uint8_t* dst)
{
    const Run* run = FindRun(cluster);
    if (!run)
    {
        std::memset(dst, 0, kSector);  // free space: zeros
        return;
    }
    const uint64_t sectorInRun = (cluster - run->firstCluster) * _sectorsPerCluster + sectorInCluster;
    if (run->isDirectory)
    {
        const std::vector<uint8_t>& bytes = _directories[run->index];
        const uint64_t offset = sectorInRun * kSector;
        if (offset < bytes.size())
            std::memcpy(dst, bytes.data() + offset, kSector);
        else
            std::memset(dst, 0, kSector);
        return;
    }
    _reader->ReadFileSector(_tree->Node(run->index).data, sectorInRun, dst);
}

void FatSynthVolume::BuildFatSector(uint64_t fatSector, uint8_t* sector) const
{
    const bool fat32 = _options.fs == FatType::Fat32;
    const uint32_t perSector = fat32 ? kSector / 4 : kSector / 2;
    const uint32_t endOfChain = fat32 ? 0x0FFFFFFF : 0xFFFF;
    const uint64_t first = fatSector * perSector;

    for (uint32_t i = 0; i < perSector; i++)
    {
        const uint64_t cluster = first + i;
        uint32_t value = 0;
        if (cluster == 0)
            value = fat32 ? 0x0FFFFFF8 : 0xFFF8;  // media descriptor #F8
        else if (cluster == 1)
            value = endOfChain;                   // clean, no errors
        else if (cluster < static_cast<uint64_t>(_clusterCount) + 2)
        {
            if (const Run* run = FindRun(cluster))
                value = cluster + 1 < static_cast<uint64_t>(run->firstCluster) + run->clusters ? static_cast<uint32_t>(cluster + 1)
                                                                                                : endOfChain;
        }
        if (fat32)
            Put32(sector + i * 4, value);
        else
            Put16(sector + i * 2, static_cast<uint16_t>(value));
    }
}

void FatSynthVolume::BuildBootSector(uint8_t* s, bool) const
{
    const bool fat32 = _options.fs == FatType::Fat32;
    s[0] = 0xEB;
    s[1] = fat32 ? 0x58 : 0x3C;
    s[2] = 0x90;
    std::memcpy(s + 3, "UNREALNG", 8);
    Put16(s + 11, kSector);
    s[13] = static_cast<uint8_t>(_sectorsPerCluster);
    Put16(s + 14, static_cast<uint16_t>(_reservedSectors));
    s[16] = 2;  // FATs
    Put16(s + 17, static_cast<uint16_t>(_rootEntries));
    if (!fat32 && _volumeSectors < 0x10000)
        Put16(s + 19, static_cast<uint16_t>(_volumeSectors));
    else
        Put32(s + 32, static_cast<uint32_t>(_volumeSectors));
    s[21] = 0xF8;  // fixed disk
    if (!fat32)
        Put16(s + 22, static_cast<uint16_t>(_fatSectors));
    Put16(s + 24, 63);   // sectors per track
    Put16(s + 26, 255);  // heads
    Put32(s + 28, static_cast<uint32_t>(_volumeStart));  // hidden sectors

    const FatShortName label = FatNameMapper::ShortNameFromText(_options.label, true, _options.codePage);
    uint8_t* ext = fat32 ? s + 64 : s + 36;
    if (fat32)
    {
        Put32(s + 36, _fatSectors);
        Put16(s + 40, 0);  // both FATs mirrored
        Put16(s + 42, 0);  // version 0.0
        Put32(s + 44, 2);  // root directory cluster
        Put16(s + 48, 1);  // FSInfo sector
        Put16(s + 50, 6);  // backup boot sector
    }
    ext[0] = 0x80;  // drive number
    ext[2] = 0x29;  // extended boot signature
    Put32(ext + 3, _options.serial);
    std::copy(label.begin(), label.end(), ext + 7);
    std::memcpy(ext + 18, fat32 ? "FAT32   " : "FAT16   ", 8);
    if (_volumeStart == 0)
    {
        // Superfloppy: a partition entry over the whole volume, starting at
        // LBA 0 (this very sector), as mtools' mformat writes one. Loaders that
        // only follow a partition table (TS-BIOS: tsfat.asm HDD) find the
        // volume through it; loaders that check for a BPB first see one
        BuildMbr(s);
    }
    if (_options.boot && !_options.boot->volumeCode.empty())
        std::copy(_options.boot->volumeCode.begin(), _options.boot->volumeCode.end(), s + (fat32 ? 90 : 62));
    s[510] = 0x55;
    s[511] = 0xAA;
}

void FatSynthVolume::BuildFsInfo(uint8_t* s) const
{
    Put32(s, 0x41615252);
    Put32(s + 484, 0x61417272);
    Put32(s + 488, _clusterCount - _usedClusters);  // free clusters
    Put32(s + 492, 2 + _usedClusters);              // the first free cluster
    Put32(s + 508, 0xAA550000);
}

void FatSynthVolume::BuildMbr(uint8_t* s) const
{
    uint8_t* p = s + 446;  // partition entry 0
    p[0] = 0x00;           // not marked active (loaders check the type, not the flag)
    // CHS fields: the LBA-only marker values
    p[1] = 0xFE;
    p[2] = 0xFF;
    p[3] = 0xFF;
    if (_options.fs == FatType::Fat32)
        p[4] = 0x0C;  // FAT32 LBA
    else
        p[4] = _volumeSectors < 65536 ? 0x04 : 0x06;  // FAT16 < 32 MB / FAT16
    p[5] = 0xFE;
    p[6] = 0xFF;
    p[7] = 0xFF;
    Put32(p + 8, static_cast<uint32_t>(_volumeStart));
    Put32(p + 12, static_cast<uint32_t>(_volumeSectors));
    s[510] = 0x55;
    s[511] = 0xAA;
}

std::optional<BlockGeometry> FatSynthVolume::NativeGeometry() const
{
    BlockGeometry geometry;
    geometry.heads = 16;
    geometry.sectors = 63;
    geometry.cylinders = static_cast<uint32_t>(std::min<uint64_t>(_totalSectors / (16 * 63), 65535));
    return geometry;
}

std::string FatSynthVolume::Describe() const
{
    return _description + (_options.fs == FatType::Fat32 ? " (FAT32 folder volume)" : " (FAT16 folder volume)");
}
