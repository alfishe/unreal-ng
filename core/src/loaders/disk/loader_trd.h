#pragma once

#include "stdafx.h"

#include <string>
#include <emulator/emulatorcontext.h>
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/trdos.h"

// TR-DOS uses 256 Bytes per sector (BPS) and 16 Sectors per track (SPT) for disks
// 40 track 1 sided image length => 163840 bytes (1×40×16×256)
// 40 track 2 sided image length => 327680 bytes
// 80 track 1 sided image length => 327680 bytes
// 80 track 2 sided image length => 655360 bytes
// .trd file can be smaller than actual floppy disk, if last logical tracks are empty (contain no file data) they can be omitted
// @see: https://sinclair.wiki.zxnet.co.uk/wiki/TR-DOS_filesystem
// @see: https://formats.kaitai.io/tr_dos_image/

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

    /// region <Properties>
public:
    DiskImage* getImage();
    void setImage(DiskImage* diskImage);
    /// endregion </Properties>

    /// region <Constructors / destructors>
public:
    LoaderTRD(EmulatorContext* context, const std::string& filepath) : _context(context), _filepath(filepath) {};
    virtual ~LoaderTRD() = default;
    /// endregion </Constructors / destructors>

    /// region <Basic methods>
public:
    bool loadImage();
    bool writeImage();

    bool format(DiskImage* diskImage);

    /// endregion </Basic methods>

    /// region <Helper methods>
protected:
    uint8_t getTrackNoFromImageSize(size_t filesize);
    bool transferSectorData(DiskImage* diskImage, uint8_t* buffer, size_t fileSize);
    void populateEmptyVolumeInfo(DiskImage* diskImage);
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
