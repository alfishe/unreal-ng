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

/// endregion </Constants>

static constexpr const uint8_t TRD_SIGNATURE = 0x10;          // Unique signature for TR-DOS volume. Must be placed at offset
static constexpr const uint8_t SECTORS_PER_TRACK = 16;  // TR-DOS uses 16 sectors per track
/// region <Types>

/// Disk type. Bit3 - Number of sides. 0 -> 1 side, 1 -> 2 sides. Bit0 - Number of tracks. 0 -> 40 tracks, 1 -> 80 tracks (0x16..0x19)
enum TRDDiskType : uint8_t
{
    DS_80 = 0x16,
    DS_40 = 0x17,
    SS_80 = 0x18,
    SS_40 = 0x19
};

#pragma pack(push, 1)

/// Sector 9 contains disk descriptor
struct TRDVolumeInfo
{
    uint8_t zeroMarker = 0;
    uint8_t reserved[224];              // Unused (Usually filled with zeroes, but can be used for creation program, author, etc.)
    uint8_t firstFreeSector = 1;
    uint8_t firstFreeTrack = 1;
    uint8_t diskType = DS_80;           // Disk type. Bit3 - Number of sides. 0 -> 1 side, 1 -> 2 sides. Bit0 - Number of tracks. 0 -> 40 tracks, 1 -> 80 tracks (0x16..0x19).
    uint8_t numFiles = 0;
    uint16_t freeSectorCount = 79 * SECTORS_PER_TRACK;
    uint8_t trDOSSignature = TRD_SIGNATURE;   // TR-DOS system signature
    uint8_t reserved1[2];
    uint8_t reserved2[9];               // Password?
    uint8_t reserved3;
    uint8_t deletedFileCount = 0;       // Number of deleted files
    uint8_t label[8];                   // Disk label
    uint8_t reserved4[3] = { 0, 0, 0 }; // Must always be zero
};

struct TRDFile
{
    uint8_t name;
    uint8_t type;
    uint16_t start;
    uint16_t lengthInBytes;
    uint8_t sizeInSectors;
};

#pragma pack(pop)

/// endregion </Types>