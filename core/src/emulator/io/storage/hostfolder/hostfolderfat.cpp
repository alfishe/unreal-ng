#include "stdafx.h"

#include "hostfolderfat.h"

#include <algorithm>
#include <cstring>
#include <map>

#include "fatnamemapper.h"

namespace
{
    constexpr uint32_t kSector = 512;
    constexpr size_t kMaxOpenFiles = 8;

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

    /// One folder while building
    struct DirPlan
    {
        const FolderEntry* entry = nullptr;
        size_t parent = 0;              ///< index of the parent DirPlan
        std::vector<FatMappedName> names;
        std::vector<const FolderEntry*> children;  ///< only those whose names map
        uint32_t entryCount = 0;        ///< 32-byte entries incl. ".", "..", label, LFN
        uint32_t firstCluster = 0;      ///< 0 for the FAT16 root
        uint32_t clusters = 0;
    };

    uint32_t CeilDiv(uint64_t a, uint64_t b)
    {
        return static_cast<uint32_t>((a + b - 1) / b);
    }
}  // namespace

std::unique_ptr<HostFolderFat> HostFolderFat::Build(const FolderSnapshot& snapshot, const FatVolumeOptions& options,
                                                    std::string* error, std::vector<std::string>* report)
{
    auto fail = [error](const std::string& text) -> std::unique_ptr<HostFolderFat> {
        if (error)
            *error = text;
        return nullptr;
    };

    std::unique_ptr<HostFolderFat> volume(new HostFolderFat());
    volume->_options = options;
    const auto folderText = snapshot.Root().hostPath.u8string();
    volume->_folder = std::string(folderText.begin(), folderText.end());
    const bool fat32 = options.fs == FatType::Fat32;

    // --- Folders, breadth-first; names mapped per folder ---
    std::vector<DirPlan> dirs;
    dirs.push_back(DirPlan{&snapshot.Root(), 0, {}, {}, 0, 0, 0});
    for (size_t d = 0; d < dirs.size(); d++)
    {
        std::vector<std::string> names;
        for (const FolderEntry& child : dirs[d].entry->children)
            names.push_back(child.name);
        const std::vector<FatMappedName> mapped = FatNameMapper::MapFolder(names, options.codePage);

        DirPlan& plan = dirs[d];
        plan.entryCount = d == 0 ? 1 : 2;  // the volume label, or "." and ".."
        for (size_t i = 0; i < mapped.size(); i++)
        {
            const FolderEntry& child = dirs[d].entry->children[i];
            if (!mapped[i].ok)
            {
                if (report)
                    report->push_back(child.name + ": " + mapped[i].reason);
                continue;
            }
            plan.names.push_back(mapped[i]);
            plan.children.push_back(&child);
            plan.entryCount += 1 + (mapped[i].hasLongName ? CeilDiv(mapped[i].longName.size(), 13) : 0);
        }
        for (const FolderEntry* child : std::vector<const FolderEntry*>(dirs[d].children))
        {
            if (child->isDirectory)
                dirs.push_back(DirPlan{child, d, {}, {}, 0, 0, 0});
        }
    }

    // --- Cluster size and volume size ---
    const uint32_t entryBytes = fat32 ? 4 : 2;
    uint64_t fileBytesTotal = 0;
    for (const DirPlan& plan : dirs)
        for (const FolderEntry* child : plan.children)
            if (!child->isDirectory)
                fileBytesTotal += child->size;

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
            for (const FolderEntry* child : plan.children)
                if (!child->isDirectory)
                    used += CeilDiv(child->size, clusterBytes);

        uint64_t total = used + options.freeBytes / clusterBytes;
        total = std::max<uint64_t>(total, fat32 ? kFat32MinClusters : kFat16MinClusters);
        if (total <= (fat32 ? kFat32MaxClusters : kFat16MaxClusters))
        {
            volume->_sectorsPerCluster = spc;
            volume->_clusterCount = static_cast<uint32_t>(total);
            volume->_usedClusters = static_cast<uint32_t>(used);
            fitted = true;
        }
    }
    if (!fitted)
    {
        return fail(fat32 ? "the folder is too large for a FAT32 volume"
                          : "the folder (" + std::to_string(fileBytesTotal) +
                                " bytes) does not fit a FAT16 volume (2 GiB with 32 KiB clusters): use fs=fat32");
    }

    volume->_reservedSectors = fat32 ? 32 : 1;
    if (!fat32)
    {
        volume->_rootEntries = std::max<uint32_t>(512, (dirs[0].entryCount + 15) / 16 * 16);
        volume->_rootDirSectors = volume->_rootEntries * 32 / kSector;
    }
    volume->_fatSectors = CeilDiv(static_cast<uint64_t>(volume->_clusterCount + 2) * entryBytes, kSector);
    volume->_volumeSectors = volume->_reservedSectors + 2ull * volume->_fatSectors + volume->_rootDirSectors +
                             static_cast<uint64_t>(volume->_clusterCount) * volume->_sectorsPerCluster;
    volume->_volumeStart = options.mbr ? options.partitionStart : 0;
    volume->_totalSectors = volume->_volumeStart + volume->_volumeSectors;

    // --- Clusters: directories first (breadth-first), then files ---
    const uint64_t clusterBytes = static_cast<uint64_t>(volume->_sectorsPerCluster) * kSector;
    uint32_t next = 2;
    for (size_t d = 0; d < dirs.size(); d++)
    {
        if (d == 0 && !fat32)
            continue;
        dirs[d].clusters = std::max<uint32_t>(1, CeilDiv(static_cast<uint64_t>(dirs[d].entryCount) * 32, clusterBytes));
        dirs[d].firstCluster = next;
        next += dirs[d].clusters;
    }
    struct FilePlan
    {
        const FolderEntry* entry;
        uint32_t firstCluster;
    };
    std::vector<std::vector<FilePlan>> filesOf(dirs.size());
    for (size_t d = 0; d < dirs.size(); d++)
    {
        for (const FolderEntry* child : dirs[d].children)
        {
            if (child->isDirectory)
                continue;
            const uint32_t clusters = CeilDiv(child->size, clusterBytes);
            filesOf[d].push_back({child, clusters ? next : 0});
            if (clusters)
            {
                volume->_runs.push_back({next, clusters, false, volume->_files.size()});
                const auto display = child->hostPath.u8string();
                volume->_files.push_back({child->hostPath, std::string(display.begin(), display.end()), child->size, false});
                next += clusters;
            }
        }
    }

    // --- Directory contents ---
    const FatShortName label = FatNameMapper::ShortNameFromText(options.label, /*isLabel*/ true, options.codePage);
    std::map<const FolderEntry*, uint32_t> dirCluster;
    for (const DirPlan& plan : dirs)
        dirCluster[plan.entry] = plan.firstCluster;

    volume->_directories.resize(dirs.size());
    for (size_t d = 0; d < dirs.size(); d++)
    {
        const DirPlan& plan = dirs[d];
        std::vector<uint8_t>& bytes = volume->_directories[d];
        bytes.assign(d == 0 && !fat32 ? static_cast<size_t>(volume->_rootDirSectors) * kSector
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

        if (d == 0)
        {
            entry(label, 0x08, 0, 0, plan.entry->mtimeUtc);
        }
        else
        {
            FatShortName dot;
            dot.fill(' ');
            dot[0] = '.';
            entry(dot, 0x10, plan.firstCluster, 0, plan.entry->mtimeUtc);
            dot[1] = '.';
            // ".." of a first-level folder points at cluster 0, the root, on FAT16 and FAT32 alike
            entry(dot, 0x10, plan.parent == 0 ? 0 : dirs[plan.parent].firstCluster, 0, plan.entry->mtimeUtc);
        }

        size_t fileIndex = 0;
        for (size_t i = 0; i < plan.children.size(); i++)
        {
            const FolderEntry* child = plan.children[i];
            const FatMappedName& name = plan.names[i];
            if (name.hasLongName)
            {
                for (const auto& lfn : FatNameMapper::LongNameEntries(name.longName, FatNameMapper::Checksum(name.shortName)))
                {
                    std::copy(lfn.begin(), lfn.end(), bytes.data() + at);
                    at += 32;
                }
            }
            uint8_t attr = child->isDirectory ? 0x10 : 0x20;
            if (!child->name.empty() && child->name[0] == '.')
                attr |= 0x02;  // hidden, as on the host
            const uint32_t cluster = child->isDirectory ? dirCluster[child] : filesOf[d][fileIndex++].firstCluster;
            entry(name.shortName, attr, cluster, child->isDirectory ? 0 : static_cast<uint32_t>(child->size), child->mtimeUtc);
        }
        if (d != 0 || fat32)
            volume->_runs.push_back({plan.firstCluster, plan.clusters, true, d});
    }
    std::sort(volume->_runs.begin(), volume->_runs.end(), [](const Run& a, const Run& b) { return a.firstCluster < b.firstCluster; });

    // Identity: the folder snapshot and every option that changes the bytes
    uint64_t id = snapshot.Identity();
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
    volume->_contentId = id;
    return volume;
}

bool HostFolderFat::ReadSector(uint64_t lba, uint8_t* dst)
{
    if (lba >= _totalSectors)
        return false;
    std::memset(dst, 0, kSector);

    if (lba < _volumeStart)
    {
        if (lba == 0)
            BuildMbr(dst);
        return true;
    }

    const uint64_t rel = lba - _volumeStart;
    const bool fat32 = _options.fs == FatType::Fat32;
    if (rel < _reservedSectors)
    {
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

const HostFolderFat::Run* HostFolderFat::FindRun(uint64_t cluster) const
{
    auto it = std::upper_bound(_runs.begin(), _runs.end(), cluster,
                               [](uint64_t c, const Run& run) { return c < run.firstCluster; });
    if (it == _runs.begin())
        return nullptr;
    --it;
    return cluster < static_cast<uint64_t>(it->firstCluster) + it->clusters ? &*it : nullptr;
}

void HostFolderFat::ReadData(uint64_t cluster, uint32_t sectorInCluster, uint8_t* dst)
{
    const Run* run = FindRun(cluster);
    if (!run)
        return;  // free space: zeros
    const uint64_t offset = ((cluster - run->firstCluster) * _sectorsPerCluster + sectorInCluster) * kSector;
    if (run->isDirectory)
    {
        const std::vector<uint8_t>& bytes = _directories[run->index];
        if (offset < bytes.size())
            std::memcpy(dst, bytes.data() + offset, kSector);
        return;
    }
    ReadFile(run->index, offset, dst);
}

void HostFolderFat::ReadFile(size_t index, uint64_t offset, uint8_t* dst)
{
    FileSource& file = _files[index];
    if (offset >= file.size)
        return;  // the cluster slack past the end of the file
    const uint64_t wanted = std::min<uint64_t>(kSector, file.size - offset);

    // A small LRU of open host files
    auto it = std::find_if(_openFiles.begin(), _openFiles.end(), [index](const OpenFile& f) { return f.index == index; });
    if (it == _openFiles.end())
    {
        _openFiles.push_front(OpenFile{index, std::ifstream(file.hostPath, std::ios::binary)});
        if (_openFiles.size() > kMaxOpenFiles)
            _openFiles.pop_back();
        it = _openFiles.begin();
    }
    else if (it != _openFiles.begin())
    {
        _openFiles.splice(_openFiles.begin(), _openFiles, it);
        it = _openFiles.begin();
    }

    std::ifstream& in = it->stream;
    size_t got = 0;
    if (in.is_open())
    {
        in.clear();
        in.seekg(static_cast<std::streamoff>(offset));
        in.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(wanted));
        got = static_cast<size_t>(in.gcount());
        in.clear();
    }
    if (got < wanted)
    {
        std::memset(dst + got, 0, static_cast<size_t>(wanted - got));
        if (!file.warned)
        {
            file.warned = true;
            _warnings.push_back(file.displayPath + ": shorter than when the folder was scanned, or gone; reads as zeros");
        }
    }
}

void HostFolderFat::BuildFatSector(uint64_t fatSector, uint8_t* sector) const
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

void HostFolderFat::BuildBootSector(uint8_t* s, bool) const
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
    s[510] = 0x55;
    s[511] = 0xAA;
}

void HostFolderFat::BuildFsInfo(uint8_t* s) const
{
    Put32(s, 0x41615252);
    Put32(s + 484, 0x61417272);
    Put32(s + 488, _clusterCount - _usedClusters);  // free clusters
    Put32(s + 492, 2 + _usedClusters);              // the first free cluster
    Put32(s + 508, 0xAA550000);
}

void HostFolderFat::BuildMbr(uint8_t* s) const
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

std::optional<BlockGeometry> HostFolderFat::NativeGeometry() const
{
    BlockGeometry geometry;
    geometry.heads = 16;
    geometry.sectors = 63;
    geometry.cylinders = static_cast<uint32_t>(std::min<uint64_t>(_totalSectors / (16 * 63), 65535));
    return geometry;
}

std::string HostFolderFat::Describe() const
{
    return _folder + (_options.fs == FatType::Fat32 ? " (FAT32 folder volume)" : " (FAT16 folder volume)");
}
