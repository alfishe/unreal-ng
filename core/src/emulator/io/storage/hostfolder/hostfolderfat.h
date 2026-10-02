#pragma once

/// @file hostfolderfat.h
/// @brief A host folder presented as a FAT16 / FAT32 disk, sector by sector.
///
/// Built from a FolderSnapshot. Nothing is copied: every file is laid out as
/// one contiguous run of clusters, the FAT is computed for each sector read,
/// directories are generated once at build time, and file data is read from
/// the host file when the guest reads it. The volume itself is read-only;
/// guest writes go to the change layer on top (SessionWriteMap).
///
/// Layout, in LBA order:
///   0            MBR, one partition (type #06 / #04 FAT16, #0C FAT32) at 2048
///   2048         boot sector (+ FSInfo at +1, backup at +6 / +7 for FAT32)
///   +reserved    FAT 1, FAT 2
///   (FAT16)      root directory region
///   data         cluster 2...: directories (breadth-first), then files
/// With `mbr = false` the volume starts at LBA 0 ("superfloppy"); its boot
/// sector still carries one partition entry, over the volume from LBA 0 (as
/// mtools' mformat writes it), for loaders that only follow a partition table.
///
/// Cluster counts stay clear of the FAT type limits that strict readers
/// (ChaN FatFs, used by the ZX Next firmware) check: FAT16 4 086-65 525,
/// FAT32 >= 65 526. Design: docs/inprogress/2026-09-28-storage-manager/
/// technical-design.md §6.

#include <cstdint>
#include <fstream>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/unicodehelper.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/io/storage/iblockdevice.h"
#include "emulator/media/mediatypes.h"

struct FatVolumeOptions
{
    FatType fs = FatType::Fat16;
    CodePage codePage = CodePage::Cp866;       ///< short names
    bool mbr = true;                           ///< false: the volume starts at LBA 0
    uint32_t partitionStart = 2048;            ///< 1 MiB alignment, as SD cards ship
    uint64_t freeBytes = 256ull * 1024 * 1024; ///< room for guest writes (costs nothing: never stored)
    std::string label = "UNREAL NG";
    uint32_t serial = 0x554E4721;              ///< "UNG!", fixed: the same folder gives the same bytes
    std::optional<int64_t> fixedTimeUtc;       ///< tests: every timestamp this value
};

class HostFolderFat : public IBlockDevice
{
public:
    /// Build the volume. False with `error` when it cannot be built (the
    /// folder does not fit the FAT type); `report` lists names that could not
    /// be stored
    static std::unique_ptr<HostFolderFat> Build(const FolderSnapshot& snapshot, const FatVolumeOptions& options,
                                                std::string* error, std::vector<std::string>* report);

    uint64_t SectorCount() const override { return _totalSectors; }
    bool ReadSector(uint64_t lba, uint8_t* dst) override;
    bool WriteSector(uint64_t, const uint8_t*) override { return false; }
    bool IsWritable() const override { return false; }
    std::optional<BlockGeometry> NativeGeometry() const override;
    std::string Describe() const override;
    uint64_t ContentId() const override { return _contentId; }

    /// Layout facts (tests, `media info`)
    FatType Type() const { return _options.fs; }
    uint32_t ClusterCount() const { return _clusterCount; }
    uint32_t SectorsPerCluster() const { return _sectorsPerCluster; }
    uint64_t VolumeStart() const { return _volumeStart; }

    /// Problems met while serving reads (a host file that shrank or vanished)
    const std::vector<std::string>& Warnings() const { return _warnings; }

private:
    struct Run
    {
        uint32_t firstCluster = 0;
        uint32_t clusters = 0;
        bool isDirectory = false;
        size_t index = 0;  ///< into _directories or _files
    };
    struct FileSource
    {
        std::filesystem::path hostPath;
        std::string displayPath;
        uint64_t size = 0;
        bool warned = false;
    };

    HostFolderFat() = default;

    void BuildBootSector(uint8_t* sector, bool backup) const;
    void BuildFsInfo(uint8_t* sector) const;
    void BuildMbr(uint8_t* sector) const;
    void BuildFatSector(uint64_t fatSector, uint8_t* sector) const;
    void ReadData(uint64_t cluster, uint32_t sectorInCluster, uint8_t* dst);
    void ReadFile(size_t index, uint64_t offset, uint8_t* dst);
    const Run* FindRun(uint64_t cluster) const;

    FatVolumeOptions _options;
    std::string _folder;
    uint64_t _volumeStart = 0;
    uint64_t _volumeSectors = 0;
    uint64_t _totalSectors = 0;
    uint32_t _reservedSectors = 0;
    uint32_t _fatSectors = 0;
    uint32_t _rootDirSectors = 0;   ///< FAT16 only
    uint32_t _rootEntries = 0;      ///< FAT16 only
    uint32_t _sectorsPerCluster = 0;
    uint32_t _clusterCount = 0;
    uint32_t _usedClusters = 0;
    uint64_t _contentId = 0;

    std::vector<std::vector<uint8_t>> _directories;  ///< [0] = root
    std::vector<FileSource> _files;
    std::vector<Run> _runs;                          ///< sorted by firstCluster

    struct OpenFile
    {
        size_t index;
        std::ifstream stream;
    };
    std::list<OpenFile> _openFiles;  ///< most recently used first, at most kMaxOpenFiles
    std::vector<std::string> _warnings;
};
