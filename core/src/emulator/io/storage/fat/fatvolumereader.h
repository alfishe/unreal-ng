#pragma once

/// @file fatvolumereader.h
/// @brief Reads a FAT12 / FAT16 / FAT32 volume through any IBlockDevice: the
/// partition (MBR or superfloppy), the directory tree with long names, and
/// file contents. Written from the FAT specification alone, independently of
/// the volume builder (HostFolderFat), so tests can check one with the other;
/// the media history's file view and folder export use it too.
///
/// The FAT type follows the cluster count exactly as strict readers do (ChaN
/// FatFs, the ZX Next firmware): <= 4 085 FAT12, <= 65 525 FAT16, else FAT32.
/// Names come back as UTF-8: long names from UTF-16, short names from the
/// given code page (CP866 or CP1251).

#include <cstdint>
#include <string>
#include <vector>

#include "common/unicodehelper.h"
#include "emulator/io/storage/iblockdevice.h"

enum class FatReaderType : uint8_t
{
    Fat12,
    Fat16,
    Fat32,
};

/// A run of consecutive sectors of the device the reader was opened on
struct FatChainExtent
{
    uint64_t lba = 0;
    uint32_t sectors = 0;
};

/// An MBR partition entry
struct FatPartition
{
    uint64_t first = 0;   ///< LBA of the partition's first sector
    uint64_t count = 0;   ///< its size in sectors
    uint8_t type = 0;     ///< the partition type byte
};

/// One directory entry as stored: its long-name slots and the short entry
struct FatRawEntry
{
    std::vector<uint8_t> slots;  ///< 32 bytes per slot, the short entry last
    bool isLabel = false;        ///< the volume label entry (no file)
};

struct FatDirEntryInfo
{
    std::string name;        ///< the long name when there is one, else the short name (UTF-8)
    std::string shortName;   ///< "NAME.EXT" (UTF-8)
    bool isDirectory = false;
    uint8_t attributes = 0;
    uint32_t firstCluster = 0;
    uint32_t size = 0;
    uint16_t date = 0;       ///< DOS date of the last write
    uint16_t time = 0;       ///< DOS time of the last write
};

class FatVolumeReader
{
public:
    /// Find the volume on `device` and read its boot sector. Open again after
    /// the device changed under the reader: the FAT window is cached
    bool Open(IBlockDevice& device, CodePage page = CodePage::Cp866, std::string* error = nullptr);

    /// MBR entry `number` (1-4) of `device` when it is a FAT partition
    /// (types #01 #04 #06 #0B #0C #0E); false with the reason otherwise
    static bool FindPartition(IBlockDevice& device, uint32_t number, FatPartition& partition, std::string* error = nullptr);

    /// The UTC second count of a DOS date and time (read as UTC, the convention
    /// FatSynthVolume writes with)
    static int64_t DosToUnix(uint16_t date, uint16_t time);
    /// The reverse: DOS date and time of a UTC second count, clamped to 1980-2107
    static void UnixToDos(int64_t unixSeconds, uint16_t& date, uint16_t& time);

    FatReaderType Type() const { return _type; }
    uint32_t ClusterCount() const { return _clusterCount; }
    uint32_t SectorsPerCluster() const { return _sectorsPerCluster; }
    uint64_t VolumeStart() const { return _volumeStart; }
    /// The volume's size in sectors as its BPB says (the device may be shorter: a cut-down image)
    uint32_t VolumeSectors() const { return _volumeSectors; }
    uint32_t ReservedSectors() const { return _reservedSectors; }
    uint32_t FatCount() const { return _fats; }
    uint32_t FatSectors() const { return _fatSectors; }
    uint32_t RootEntries() const { return _rootEntries; }
    uint32_t RootDirSectors() const { return _rootDirSectors; }
    uint32_t RootCluster() const { return _rootCluster; }
    /// The first data sector, relative to the volume
    uint64_t DataStart() const;
    /// FAT32: the FSInfo sector relative to the volume (0: none)
    uint32_t FsInfoSector() const { return _fsInfoSector; }
    const std::string& Label() const { return _label; }  ///< from the root directory's label entry

    /// "/" or "/GAMES/SUB": the entries of a directory, "." and ".." left out.
    /// Path parts match long or short names, ASCII case-insensitively
    bool List(const std::string& path, std::vector<FatDirEntryInfo>& entries, std::string* error = nullptr);
    bool ReadFile(const std::string& path, std::vector<uint8_t>& data, std::string* error = nullptr);
    /// The entry at `path`; "/" is the root (a directory with first cluster 0)
    bool Stat(const std::string& path, FatDirEntryInfo& entry, std::string* error = nullptr);
    /// The entries of the directory starting at `firstCluster` (0: the root)
    bool ListDirectory(uint32_t firstCluster, std::vector<FatDirEntryInfo>& entries, std::string* error = nullptr);

    /// Where the first `bytes` of the chain from `firstCluster` are: device
    /// LBAs, adjacent clusters coalesced. Fails on a chain that leaves the
    /// volume, loops or ends before `bytes`
    bool ChainExtents(uint32_t firstCluster, uint64_t bytes, std::vector<FatChainExtent>& extents, std::string* error = nullptr);

    /// The clusters of the chain from `firstCluster`, in order (checked as ChainExtents)
    bool ChainClusters(uint32_t firstCluster, std::vector<uint32_t>& clusters, std::string* error = nullptr);
    /// The FAT entry of `cluster` (0: free)
    uint32_t FatEntry(uint32_t cluster) { return NextCluster(cluster); }
    /// free[c - 2] for every cluster c of the volume: true when its FAT entry is 0
    bool ScanFree(std::vector<bool>& free, std::string* error = nullptr);
    /// The entries of a directory as stored, in order: long-name slots grouped with
    /// their short entry, the volume label included; deleted entries, orphan
    /// long-name slots, "." and ".." left out. `entries` lines up with what
    /// ListDirectory returns for the same directory, label aside
    bool ReadRawDirectory(uint32_t firstCluster, std::vector<FatRawEntry>& raw, std::vector<FatDirEntryInfo>& entries,
                          std::string* error = nullptr);

    /// FAT sectors read so far (tests: the FAT window cache)
    uint64_t FatSectorReads() const { return _fatSectorReads; }

private:
    bool ReadDirectory(uint32_t firstCluster, bool fixedRoot, std::vector<FatDirEntryInfo>& entries, std::string* error,
                       std::vector<FatRawEntry>* raw = nullptr);
    bool Find(const std::string& path, FatDirEntryInfo& found, std::string* error);
    bool ReadClusterChain(uint32_t firstCluster, uint64_t maxBytes, std::vector<uint8_t>& data, std::string* error);
    uint32_t NextCluster(uint32_t cluster);
    bool IsEndOfChain(uint32_t value) const;
    bool Sector(uint64_t volumeLba, uint8_t* dst);

    IBlockDevice* _device = nullptr;
    CodePage _page = CodePage::Cp866;
    FatReaderType _type = FatReaderType::Fat16;
    uint64_t _volumeStart = 0;
    uint32_t _reservedSectors = 0;
    uint32_t _fats = 0;
    uint32_t _fatSectors = 0;
    uint32_t _rootEntries = 0;
    uint32_t _rootDirSectors = 0;
    uint32_t _sectorsPerCluster = 0;
    uint32_t _clusterCount = 0;
    uint32_t _volumeSectors = 0;
    uint32_t _rootCluster = 0;
    uint32_t _fsInfoSector = 0;
    std::string _label;

    /// Two consecutive FAT sectors (a FAT12 entry may straddle them)
    uint8_t _fatWindow[1024] = {};
    uint64_t _fatWindowSector = UINT64_MAX;
    uint64_t _fatSectorReads = 0;
};
