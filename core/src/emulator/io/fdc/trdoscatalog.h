#pragma once

#include <cstdint>
#include <string>
#include <vector>

class DiskImage;

/// One 16-byte TR-DOS catalog entry
struct TrdosFile
{
    std::string name;         // 8 characters as stored (space padded)
    char type = ' ';          // 'B' BASIC, 'C' code, 'D' data, '#' stream, ...
    uint16_t start = 0;       // Bytes 9-10 (BASIC: program + variables length)
    uint16_t length = 0;      // Bytes 11-12 (BASIC: program length)
    uint8_t sectors = 0;      // Length in sectors
    uint8_t firstSector = 0;  // 0..15
    uint8_t firstTrack = 0;   // Logical track (cylinder * sides + side)
    bool deleted = false;
    int slot = 0;             // Index in the catalog (0..127)

    /// Name without the trailing padding
    std::string TrimmedName() const;
};

/// Read-only view of the TR-DOS catalog of a disk image (track 0: sectors 1-8 file entries, sector 9 disk info)
class TrdosCatalog
{
public:
    static constexpr size_t MAX_FILES = 128;
    static constexpr size_t SECTOR_SIZE = 256;
    static constexpr size_t SECTORS_PER_TRACK = 16;

    // Disk info sector (track 0, sector 9) field offsets
    static constexpr size_t INFO_FIRST_FREE_SECTOR = 0xE1;
    static constexpr size_t INFO_FIRST_FREE_TRACK = 0xE2;
    static constexpr size_t INFO_DISK_TYPE = 0xE3;
    static constexpr size_t INFO_FILE_COUNT = 0xE4;
    static constexpr size_t INFO_FREE_SECTORS = 0xE5;  // 16 bit little endian
    static constexpr size_t INFO_TRDOS_ID = 0xE7;

public:
    /// True when the image carries a TR-DOS file system (id byte, disk type, geometry)
    static bool IsTrdos(DiskImage& image);

    /// Parse the catalog. Returns false when the image is not TR-DOS formatted
    bool Parse(DiskImage& image);

    const std::vector<TrdosFile>& Files() const { return _files; }

    /// Live BASIC files (not deleted, type 'B')
    std::vector<const TrdosFile*> BasicFiles() const;

    /// The file TR-DOS runs for a bare RUN: exact name "boot    " type 'B', not deleted
    const TrdosFile* FindBoot() const;

    uint16_t FreeSectors() const { return _freeSectors; }
    uint8_t FileCount() const { return _fileCount; }

    /// Number of catalog slots in use (entries up to the end-of-catalog marker, deleted ones included)
    size_t UsedSlots() const { return _files.size(); }

private:
    std::vector<TrdosFile> _files;
    uint16_t _freeSectors = 0;
    uint8_t _fileCount = 0;
};
