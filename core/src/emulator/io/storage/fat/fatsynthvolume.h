#pragma once

/// @file fatsynthvolume.h
/// @brief A FAT16 / FAT32 volume synthesized from a FileTree, sector by sector.
///
/// Nothing is copied: every file is laid out as one contiguous run of
/// clusters, the FAT is computed for each sector read, directories are
/// generated once at build time, and file data is read from wherever the
/// tree says it lives (a host file, an extent of a source device) when the
/// guest reads it, straight into the caller's buffer (ExtentReader). The
/// volume itself is read-only; guest writes go to the change layer on top.
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
/// FAT32 >= 65 526. HostFolderFat is this volume over one host folder.
/// Design: docs/inprogress/2026-09-28-storage-manager/technical-design.md §6,
/// docs/inprogress/2026-10-05-media-multisource/tdd.md §5.

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/unicodehelper.h"
#include "emulator/io/storage/compose/extentreader.h"
#include "emulator/io/storage/compose/filetree.h"
#include "emulator/io/storage/iblockdevice.h"
#include "emulator/media/mediatypes.h"

class SourcePool;

/// Boot structures a rebuilt volume carries (D-6): taken from the bottom FAT
/// image or from the descriptor's boot section. The BPB, the partition table and
/// FAT32's FSInfo / backup sectors stay the builder's
struct FatBootPlan
{
    std::vector<uint8_t> mbrCode;      ///< up to 446 bytes into LBA 0 before the partition table (volumes with an MBR)
    std::vector<uint8_t> volumeCode;   ///< the boot sector's code area after the BPB (FAT32: the backup's too)
    std::map<uint32_t, std::array<uint8_t, 512>> reserved;  ///< whole reserved sectors, volume-relative (1..)
    /// Carried implicitly from the bottom image: what does not fit the target is
    /// left out with a report line instead of failing the build
    bool bestEffort = false;

    bool Empty() const { return mbrCode.empty() && volumeCode.empty() && reserved.empty(); }
    uint64_t Identity() const;
};

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
    std::shared_ptr<const FatBootPlan> boot;   ///< D-6 boot structures; null: the builder's own (no code)
};

class FatSynthVolume : public IBlockDevice
{
public:
    /// Build the volume over `tree`, whose file data `pool` serves.
    /// `sourceIdentity` is the identity of everything the tree was built from
    /// (the content id mixes it with the options); `description` names the
    /// source in Describe(). Nullptr with `error` when the tree does not fit
    /// the FAT type; `report` lists names that could not be stored
    static std::unique_ptr<FatSynthVolume> Build(std::shared_ptr<const FileTree> tree, std::shared_ptr<SourcePool> pool,
                                                 const FatVolumeOptions& options, uint64_t sourceIdentity,
                                                 std::string description, std::string* error,
                                                 std::vector<std::string>* report);

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
    /// The first LBA after the last used cluster: everything from here to the
    /// end is free space and reads as zeros (parity tests hash up to here)
    uint64_t UsedSectorEnd() const
    {
        return _volumeStart + _reservedSectors + 2ull * _fatSectors + _rootDirSectors +
               static_cast<uint64_t>(_usedClusters) * _sectorsPerCluster;
    }

    const FileTree& Tree() const { return *_tree; }
    /// Problems met while serving reads (a host file that shrank or vanished)
    const std::vector<std::string>& Warnings() const;

protected:
    FatSynthVolume() = default;
    bool Init(std::shared_ptr<const FileTree> tree, std::shared_ptr<SourcePool> pool, const FatVolumeOptions& options,
              uint64_t sourceIdentity, std::string description, std::string* error, std::vector<std::string>* report);

private:
    struct Run
    {
        uint32_t firstCluster = 0;
        uint32_t clusters = 0;
        bool isDirectory = false;
        uint32_t index = 0;  ///< into _directories, or the tree node of a file
    };

    void BuildBootSector(uint8_t* sector, bool backup) const;
    void BuildFsInfo(uint8_t* sector) const;
    void BuildMbr(uint8_t* sector) const;
    void BuildFatSector(uint64_t fatSector, uint8_t* sector) const;
    void ReadData(uint64_t cluster, uint32_t sectorInCluster, uint8_t* dst);
    const Run* FindRun(uint64_t cluster) const;

    FatVolumeOptions _options;
    std::string _description;
    std::shared_ptr<const FileTree> _tree;
    std::shared_ptr<SourcePool> _pool;
    std::unique_ptr<ExtentReader> _reader;

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
    std::vector<Run> _runs;                          ///< sorted by firstCluster
};
