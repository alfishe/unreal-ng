#pragma once

#include "stdafx.h"

/// TR-DOS uses 256 Bytes per sector (BPS) and 16 Sectors per track (SPT) for disks
// 40 track 1 sided image length => 163840 bytes (1×40×16×256)
// 40 track 2 sided image length => 327680 bytes
// 80 track 1 sided image length => 327680 bytes
// 80 track 2 sided image length => 655360 bytes
// .trd file can be smaller than actual floppy disk, if last logical tracks are empty (contain no file data) they can be omitted
// @see: https://sinclair.wiki.zxnet.co.uk/wiki/TR-DOS_filesystem
// @see: https://formats.kaitai.io/tr_dos_image/

/// region <Types>

/// Disk type. Bit3 - Number of sides. 0 -> 1 side, 1 -> 2 sides. Bit0 - Number of tracks. 0 -> 40 tracks, 1 -> 80 tracks (0x16..0x19)
enum TRDDiskType
{
    DS_80 = 0x16,
    DS_40 = 0x17,
    SS_80 = 0x18,
    SS_40 = 0x19
};

enum
{
    TRD_SIG = 0x10
};

#pragma pack(push, 1)

/// Sector 9 contains disk descriptor
struct TRDVolumeInfo
{
    uint8_t zeroMarker;
    uint8_t reserved[224];      // Unused (Usually filled with zeroes, but can be used for creation program, author, etc.)
    uint8_t firstFreeSector;
    uint8_t firstFreeTrack;
    uint8_t diskType;           // Disk type. Bit3 - Number of sides. 0 -> 1 side, 1 -> 2 sides. Bit0 - Number of tracks. 0 -> 40 tracks, 1 -> 80 tracks (0x16..0x19).
    uint8_t numFiles;
    uint16_t freeSectorCount;
    uint8_t trDOSSignature;
    uint8_t reserved1[2];
    uint8_t reserved2[9];       // Password?
    uint8_t reserved3;
    uint8_t deletedFileCount;
    uint8_t label[8];
    uint8_t reserved4[3];
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

class LoaderTRD
{
    /// region <Basic methods>
public:
    bool readImage();
    bool writeImage();
    bool createNewImage();
    bool format();
    /// endregion </Basic methods>

};