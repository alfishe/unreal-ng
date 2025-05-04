#pragma once

#include <stdafx.h>

/// TR-DOS uses 256 Bytes per sector (BPS) and 16 Sectors per track (SPT) for disks
// 40 track 1 sided image length => 163840 bytes (1×40×16×256)
// 40 track 2 sided image length => 327680 bytes
// 80 track 1 sided image length => 327680 bytes
// 80 track 2 sided image length => 655360 bytes
// .trd file can be smaller than actual floppy disk, if last logical tracks are empty (contain no file data) they can be omitted
// @see: https://sinclair.wiki.zxnet.co.uk/wiki/TR-DOS_filesystem
// @see: https://formats.kaitai.io/tr_dos_image/

/// region <Constants>

static constexpr const uint8_t TRD_SIGNATURE = 0x10;          // Unique signature for TR-DOS volume. Must be placed at offset
static constexpr const uint8_t TRD_80_TRACKS = 80;         // TR-DOS uses 80 tracks for Double-Side (DS) disks
static constexpr const uint8_t TRD_40_TRACKS = 40;         // TR-DOS uses 40 tracks for Double-Side (DS) disks
static constexpr const uint8_t TRD_SECTORS_PER_TRACK = 16;    // TR-DOS uses 16 sectors per track
static constexpr const uint16_t TRD_SECTORS_SIZE_BYTES = 256; // TR-DOS uses 256 bytes sectors
static constexpr const uint16_t TRD_FREE_SECTORS_ON_DS_80_EMPTY_DISK = (TRD_80_TRACKS * MAX_SIDES - 1) * TRD_SECTORS_PER_TRACK; // Whole first track is loaded with TR-DOS system information, so only 2544 sectors available

static constexpr const uint8_t TRD_MAX_FILES = 128;           // TR-DOS catalog can handle only up to 128 files
static constexpr const uint8_t TRD_VOLUME_SECTOR = 9 - 1;     // Sector 9 on track 0 stores volume information

/// endregion </Constants>

/// region <Types>

/// Disk type. Acceptable values 0x16..0x19
/// Bit3 - Number of sides. 0 -> 1 side, 1 -> 2 sides.
/// Bit0 - Number of tracks. 0 -> 40 tracks, 1 -> 80 tracks
enum TRDDiskType : uint8_t
{
    DS_80 = 0x16,
    DS_40 = 0x17,
    SS_80 = 0x18,
    SS_40 = 0x19
};

static inline std::string getTRDDiskTypeName(uint8_t type)
{
    std::string result = "<Unknown>";

    switch (type)
    {
        case DS_80: result = "DS_80"; break;
        case DS_40: result = "DS_40"; break;
        case SS_80: result = "SS_80"; break;
        case SS_40: result = "SS_40"; break;
        default: break;
    }

    return result;
}

#pragma pack(push, 1)

/// Sector 9 contains disk descriptor
struct TRDVolumeInfo
{
    uint8_t zeroMarker = 0;
    uint8_t reserved[224] = {};         // Unused (Usually filled with zeroes, but can be used for creation program, author, etc.)
    uint8_t firstFreeSector = 1;
    uint8_t firstFreeTrack = 1;
    uint8_t diskType = DS_80;           // Disk type. Bit3 - Number of sides. 0 -> 1 side, 1 -> 2 sides. Bit0 - Number of tracks. 0 -> 40 tracks, 1 -> 80 tracks (0x16..0x19).
    uint8_t fileCount = 0;
    uint16_t freeSectorCount = TRD_FREE_SECTORS_ON_DS_80_EMPTY_DISK;
    uint8_t trDOSSignature = TRD_SIGNATURE;   // TR-DOS system signature
    uint8_t reserved1[2] = { 0x00, 0x00 };
    uint8_t reserved2[9] = { 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20 };    // Password?
    uint8_t reserved3;
    uint8_t deletedFileCount = 0;       // Number of deleted files
    uint8_t label[8];                   // Disk label
    uint8_t reserved4[3] = { 0, 0, 0 }; // Must always be filled with zeroes
};

/// Each catalog entry contains:
/// - 8 bytes: File name (ASCII)
/// - 1 byte:  File type
/// - 2 bytes: Start address (track and sector)
/// - 2 bytes: File length in bytes
/// - 1 byte:  Size in sectors
/// @see: https://sinclair.wiki.zxnet.co.uk/wiki/TR-DOS_filesystem
struct TRDFile
{
    uint8_t name[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t type = 0x00;
    uint16_t params = 0;
    uint16_t lengthInBytes = 0;
    uint8_t sizeInSectors = 0;
    uint8_t startSector = 0;

    /// Logical track numbering starting from 0 is used. [0..159] for 80 tracks, double-sided, [0..79] for 40 tracks, double-sided
    /// 0 - h0t0, 1 - h1t0, 2 - h0t1 ... 79 - h1t39, 80 - h0t40 ... 159 - h1t79
    uint8_t startTrack = 0;

    /// #region <Methods>
  public:
    std::string Dump() const
    {
        std::stringstream ss;
        
        // Convert name to string
        std::string nameStr;
        for (int i = 0; i < 8; i++)
        {
            if (name[i] == 0)
                break;
            nameStr += name[i];
        }

        // Convert type to string
        std::string typeStr;
        switch (type)
        {
            case 0x42: typeStr = "BASIC"; break;
            case 0x43: typeStr = "CODE"; break;
            case 0x44: typeStr = "DATA"; break;
            default: typeStr = "UNKNOWN"; break;
        }

        // Get bytes from params
        uint8_t highByte = (params >> 8) & 0xFF;
        uint8_t lowByte = params & 0xFF;

        // Format the output
        ss << "TRDFile:\n{\n";
        ss << "    Name: '" << nameStr << "'\n";
        ss << "    Type: " << type << " (" << typeStr << " (0x" << std::hex << std::setw(2) << std::setfill('0') << (int)type << std::dec << ")\n";
        
        // Format params in multiple ways
        ss << "    Params: " << std::dec << params << " (0x" << std::hex << std::setw(4) << std::setfill('0') << params << std::dec << ")\n";
        ss << "           High: " << (int)highByte << " (0x" << std::hex << std::setw(2) << std::setfill('0') << (int)highByte << std::dec << ")\n";
        ss << "           Low:  " << (int)lowByte << " (0x" << std::hex << std::setw(2) << std::setfill('0') << (int)lowByte << std::dec << ")\n";
        
        ss << "    Length: " << lengthInBytes << " bytes\n";
        ss << "    Sectors: " << (int)sizeInSectors << "\n";
        ss << "    Start Track: " << (int)startTrack << "\n";
        ss << "    Start Sector: " << (int)startSector << "\n";
        ss << "}";

        return ss.str();
    }
    /// #endregion </Methods>
};

/// TR-DOS catalog structure
/// The catalog occupies the first 8 sectors of track 0 (sectors 0-7).
/// Each catalog entry is 16 bytes in size, allowing for up to 128 entries (2048 bytes total).
/// 
/// The catalog layout is as follows:
/// - Sectors 0-7: Catalog entries (128 entries × 16 bytes = 2048 bytes)
/// - Sector 8: Volume information (TRDVolumeInfo structure)
/// - Sector 9: Reserved (usually empty)
/// Note: The catalog is stored in track 0, sectors 0-7, with each sector containing 16 entries.
/// The catalog is always stored in sequential sectors, even if files are fragmented on the disk.
/// @see: https://sinclair.wiki.zxnet.co.uk/wiki/TR-DOS_filesystem
struct TRDCatalog
{
    TRDFile files[TRD_MAX_FILES];
};

#pragma pack(pop)

enum TRDValidationErrorType : uint8_t
{
    DISK_IMAGE_NULL,
    TRACK_DATA_NULL,
    SECTOR_DATA_NULL,
    INVALID_DISK_TYPE,
    INVALID_FILE_COUNT,
    INVALID_FREE_SECTORS_COUNT,
    INVALID_FIRST_FREE_TRACK,
    INVALID_FIRST_FREE_SECTOR,
    INVALID_TRDOS_SIGNATURE,
    INVALID_DELETED_FILE_COUNT,
    INVALID_FILE_NAME,
    INVALID_START_TRACK,
    INVALID_START_SECTOR,
};

struct TRDValidationRecord
{
    std::string message;
    TRDValidationErrorType type;
    uint8_t track = 0;
    uint8_t sector = 0;
    uint8_t fileIndex = 0;
};

struct TRDValidationReport
{
    bool isValid = false;

    std::list<TRDValidationRecord> errors;
};



/// endregion </Types>