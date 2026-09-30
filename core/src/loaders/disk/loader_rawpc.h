#pragma once

#include "stdafx.h"

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/io/fdc/diskimage.h"

class EmulatorContext;

/// Raw PC floppy loader / writer: sector dumps of 3.5" PC disks without any low-level information.
///
/// Recognized by size only (the dumps carry no signature):
///   737 280 bytes   = 80 cylinders x 2 sides x  9 sectors x 512 bytes, IDs 1..9,  N = 2 - DD (720 KB)
///   1 474 560 bytes = 80 cylinders x 2 sides x 18 sectors x 512 bytes, IDs 1..18, N = 2 - HD (1.44 MB)
/// The tracks follow each other cylinder-major with the sides interleaved: c0/h0, c0/h1, c1/h0, ...
///
/// Every track is formatted as a standard PC MFM track (IBM System 34: GAP4a 80, SYNC 12, IAM, GAP1 50, then
/// per sector SYNC 12, IDAM, GAP2 22, SYNC 12, DAM, data, GAP3; GAP3 84 on DD, 108 on HD). The raw track length
/// is what tells the WD1793 the recording density (DiskImage::RawTrack::RecordedDataRate): DD tracks are 6 250
/// bytes (250 kbit/s), HD tracks 12 500 bytes (500 kbit/s). A DD layout takes 6 068 bytes, an HD layout 12 422.
///
/// Saving writes the sectors back while the layout is still regular (80 x 2, the same 9 or 18 sectors 1..N of
/// 512 bytes on every track); otherwise the save is refused with the reason, and the image can be exported as UDI.
///
/// Used by the Sprinter (720 KB / 1.44 MB), Profi CP/M (720 KB) and PC-formatted +3 disks.
/// See docs/inprogress/2026-09-28-sprinter/tdd-storage.md §2.4
class LoaderRawPcFloppy
{
    /// region <Constants>
public:
    static constexpr const size_t SECTOR_SIZE = 512;
    static constexpr const uint8_t CYLINDERS = 80;
    static constexpr const uint8_t SIDES = 2;
    static constexpr const uint8_t SECTORS_DD = 9;
    static constexpr const uint8_t SECTORS_HD = 18;
    static constexpr const size_t IMAGE_SIZE_DD = SECTOR_SIZE * SECTORS_DD * SIDES * CYLINDERS;  // 737 280
    static constexpr const size_t IMAGE_SIZE_HD = SECTOR_SIZE * SECTORS_HD * SIDES * CYLINDERS;  // 1 474 560
    static constexpr const uint8_t GAP3_DD = 84;
    static constexpr const uint8_t GAP3_HD = 108;
    /// endregion </Constants>

    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;
    std::string _filepath;

    DiskImage* _diskImage = nullptr;
    std::vector<std::string> _warnings;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    LoaderRawPcFloppy(EmulatorContext* context, const std::string& filepath) : _context(context), _filepath(filepath) {}
    virtual ~LoaderRawPcFloppy() = default;
    /// endregion </Constructors / destructors>

    /// region <Properties>
public:
    DiskImage* getImage() { return _diskImage; }
    void setImage(DiskImage* diskImage) { _diskImage = diskImage; }  // Does not take ownership

    /// Diagnostics collected by the last loadImage / writeImage call
    const std::vector<std::string>& lastWarnings() const { return _warnings; }
    /// endregion </Properties>

    /// region <Basic methods>
public:
    /// Load the file into a new DiskImage (ownership passes to the caller via getImage())
    bool loadImage();

    /// Save the current image to the loader's path
    bool writeImage();

    /// Save the current image to the given path (Save As)
    bool writeImage(const std::string& path);

    /// True when the size is a 720 KB or 1.44 MB dump
    static bool detect(size_t fileSize);

    /// Sectors per track for a dump of the given size (9, 18) or 0 when the size is no raw PC floppy
    static uint8_t sectorsForSize(size_t fileSize);

    /// The standard PC MFM track layout for 9 (DD, 6 250-byte track) or 18 (HD, 12 500-byte track) sectors
    static DiskImage::TrackFormatSpec trackSpec(uint8_t sectorsPerTrack);

    /// True when the image is 80 x 2 and every track carries the same 9 or 18 sectors 1..N with 512-byte data
    /// fields
    /// @param sectorsPerTrack Receives 9 or 18 on success (may be nullptr)
    /// @param reason Filled with a human readable explanation when the image does not qualify (may be nullptr)
    static bool isRegularGeometry(DiskImage* diskImage, uint8_t* sectorsPerTrack = nullptr, std::string* reason = nullptr);

    /// Parse an in-memory dump into a new DiskImage. Used by loadImage() and by tests.
    /// @return New DiskImage or nullptr
    DiskImage* parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings);

    /// Serialize the image to an in-memory dump. Strict: refuses an irregular layout.
    bool serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const;
    /// endregion </Basic methods>

    /// region <Helper methods>
protected:
    /// Byte offset of track (cylinder, side) inside a dump
    static size_t trackOffset(uint8_t sectorsPerTrack, uint8_t cylinder, uint8_t side);
    /// endregion </Helper methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing
//
#ifdef _CODE_UNDER_TEST

class LoaderRawPcFloppyCUT : public LoaderRawPcFloppy
{
public:
    LoaderRawPcFloppyCUT(EmulatorContext* context, const std::string& filepath) : LoaderRawPcFloppy(context, filepath) {}

public:
    using LoaderRawPcFloppy::_diskImage;
    using LoaderRawPcFloppy::trackOffset;
};
#endif  // _CODE_UNDER_TEST
