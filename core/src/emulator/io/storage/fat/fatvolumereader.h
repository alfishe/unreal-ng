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
    /// Find the volume on `device` and read its boot sector
    bool Open(IBlockDevice& device, CodePage page = CodePage::Cp866, std::string* error = nullptr);

    FatReaderType Type() const { return _type; }
    uint32_t ClusterCount() const { return _clusterCount; }
    uint32_t SectorsPerCluster() const { return _sectorsPerCluster; }
    uint64_t VolumeStart() const { return _volumeStart; }
    const std::string& Label() const { return _label; }  ///< from the root directory's label entry

    /// "/" or "/GAMES/SUB": the entries of a directory, "." and ".." left out.
    /// Path parts match long or short names, ASCII case-insensitively
    bool List(const std::string& path, std::vector<FatDirEntryInfo>& entries, std::string* error = nullptr);
    bool ReadFile(const std::string& path, std::vector<uint8_t>& data, std::string* error = nullptr);

private:
    bool ReadDirectory(uint32_t firstCluster, bool fixedRoot, std::vector<FatDirEntryInfo>& entries, std::string* error);
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
    uint32_t _rootCluster = 0;
    std::string _label;
};
