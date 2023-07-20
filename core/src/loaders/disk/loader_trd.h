#pragma once

#include "stdafx.h"

#include <string>
#include <emulator/emulatorcontext.h>
#include "emulator/io/fdc/diskimage.h"

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
    /// region <Constants>
public:
    static constexpr const size_t TRD_SECTOR_SIZE = 256;        // Sector data size (without service fields) in bytes
    static constexpr const size_t TRD_SECTORS_PER_TRACK = 16;   // Sectors per track
    static constexpr const size_t TRD_SIDES = 2;                // Sides on disk
    static constexpr const size_t TRD_TRACK_SIZE = TRD_SECTOR_SIZE * TRD_SECTORS_PER_TRACK;                     // Single side track size
    static constexpr const size_t TRD_FULL_TRACK_SIZE = TRD_SECTOR_SIZE * TRD_SECTORS_PER_TRACK * TRD_SIDES;    // Full track size (both sides) in bytes

    /// endregion </Constants>

    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;
    std::string _filepath;

    DiskImage* _diskImage = nullptr;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    LoaderTRD(EmulatorContext* context, const std::string& filepath) : _context(context), _filepath(filepath) {};
    virtual ~LoaderTRD() = default;
    /// endregion </Constructors / destructors>

    /// region <Basic methods>
public:
    bool loadImage();
    bool writeImage();
    DiskImage* getImage();

    bool format(DiskImage* diskImage);

    /// endregion </Basic methods>

    /// region <Helper methods>
protected:
    uint8_t getTrackNoFromImageSize(size_t filesize);
    bool transferSectorData(DiskImage* diskImage, uint8_t* buffer, size_t fileSize);
    /// endregion </Helper methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class LoaderTRDCUT : public LoaderTRD
{
public:
    LoaderTRDCUT(EmulatorContext* context, const std::string& filepath) : LoaderTRD(context, filepath) {};

public:
    using LoaderTRD::_diskImage;
};
#endif // _CODE_UNDER_TEST
