#pragma once

#include "stdafx.h"

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/io/fdc/diskimage.h"

class EmulatorContext;

/// DSK / EDSK (Amstrad CPC, ZX Spectrum +3) loader / writer.
///
/// Both the standard "MV - CPC" and the "EXTENDED CPC DSK" layouts are read. DSK stores a sector list per track
/// (C H R N + uPD765 status bytes) plus the sector data, so every track is rebuilt with Track::formatTrack() from a
/// TrackFormatSpec and the data / status bits are applied on top. Writing always produces an Extended DSK.
///
/// File layout:
///   Disk information block (256 bytes)
///   0x00  34  "MV - CPCEMU Disk-File\r\nDisk-Info\r\n" (standard) / "EXTENDED CPC DSK File\r\nDisk-Info\r\n"
///   0x22  14  creator
///   0x30  1   number of tracks (cylinders)
///   0x31  1   number of sides
///   0x32  2   standard only: size of every track including its 256-byte track info block
///   0x34  ..  extended only: one byte per track = track size / 256 (0 = unformatted), cylinder-major, side-minor
///
///   Track information block (256 bytes at the start of every formatted track)
///   0x00  12  "Track-Info\r\n"
///   0x10  1   cylinder
///   0x11  1   side
///   0x12  1   extended: data rate (0 unknown, 1 SD/DD, 2 HD, 3 ED)
///   0x13  1   extended: recording mode (0 unknown, 1 FM, 2 MFM)
///   0x14  1   sector size code N
///   0x15  1   sector count S
///   0x16  1   GAP#3 length
///   0x17  1   filler byte
///   0x18  8*S sector info: C, H, R, N, ST1, ST2, [extended: actual data length, little-endian]
///   0x100     sector data in list order; standard: 128 << N per sector, extended: actual data length per sector
///
/// uPD765 status bits that carry model information (as the uPD765 sets them - a data-field CRC error sets ST1.DE
/// together with ST2.DD, an ID-field CRC error sets ST1.DE alone):
///   ST1 bit 5 (0x20) alone   -> ID CRC invalid
///   ST2 bit 5 (0x20)         -> data CRC invalid
///   ST2 bit 6 (0x40)         -> deleted data address mark (0xF8)
///   ST1 bit 2 (0x04), ST1 bit 0 (0x01), ST2 bit 0 (0x01), or extended length 0 -> ID-only sector (no data field)
/// Extended "actual data length" = k * (128 << N), k > 1 -> k copies of a weak sector: the first copy is stored,
/// bytes that differ between copies are marked in the track weak-bit bitmap.
///
/// See docs/inprogress/2026-09-02-universal-track-model/loader-dsk.md
class LoaderDSK
{
    /// region <Constants>
public:
    static constexpr const char* SIGNATURE_STANDARD = "MV - CPC";              // First 8 bytes of a standard DSK
    static constexpr const char* SIGNATURE_EXTENDED = "EXTENDED";              // First 8 bytes of an extended DSK
    static constexpr const char* SIGNATURE_STANDARD_FULL = "MV - CPCEMU Disk-File\r\nDisk-Info\r\n";
    static constexpr const char* SIGNATURE_EXTENDED_FULL = "EXTENDED CPC DSK File\r\nDisk-Info\r\n";
    static constexpr const char* TRACK_SIGNATURE = "Track-Info";
    static constexpr const char* CREATOR = "unreal-ng     ";                   // 14 bytes, space padded
    static constexpr const size_t SIGNATURE_LEN = 8;
    static constexpr const size_t SIGNATURE_FULL_LEN = 34;
    static constexpr const size_t CREATOR_LEN = 14;
    static constexpr const size_t DISK_INFO_SIZE = 256;
    static constexpr const size_t TRACK_INFO_SIZE = 256;
    static constexpr const size_t SECTOR_INFO_SIZE = 8;
    static constexpr const size_t MAX_SECTORS_PER_TRACK = (TRACK_INFO_SIZE - 0x18) / SECTOR_INFO_SIZE;  // 29
    static constexpr const size_t MAX_TRACKS = DISK_INFO_SIZE - 0x34;                                    // 204 size bytes

    // Disk information block offsets
    static constexpr const size_t OFF_CREATOR = 0x22;
    static constexpr const size_t OFF_TRACKS = 0x30;
    static constexpr const size_t OFF_SIDES = 0x31;
    static constexpr const size_t OFF_TRACK_SIZE = 0x32;
    static constexpr const size_t OFF_TRACK_SIZE_TABLE = 0x34;

    // Track information block offsets
    static constexpr const size_t OFF_TI_CYLINDER = 0x10;
    static constexpr const size_t OFF_TI_SIDE = 0x11;
    static constexpr const size_t OFF_TI_RATE = 0x12;
    static constexpr const size_t OFF_TI_MODE = 0x13;
    static constexpr const size_t OFF_TI_SIZE_CODE = 0x14;
    static constexpr const size_t OFF_TI_SECTOR_COUNT = 0x15;
    static constexpr const size_t OFF_TI_GAP3 = 0x16;
    static constexpr const size_t OFF_TI_FILLER = 0x17;
    static constexpr const size_t OFF_TI_SECTOR_INFO = 0x18;

    // Recording mode (extended track info byte 0x13)
    static constexpr const uint8_t MODE_UNKNOWN = 0;
    static constexpr const uint8_t MODE_FM = 1;
    static constexpr const uint8_t MODE_MFM = 2;
    static constexpr const uint8_t RATE_SD_DD = 1;

    // uPD765 status bits used by the mapping
    static constexpr const uint8_t ST1_MISSING_ADDRESS_MARK = 0x01;
    static constexpr const uint8_t ST1_NO_DATA = 0x04;
    static constexpr const uint8_t ST1_DATA_ERROR = 0x20;
    static constexpr const uint8_t ST2_MISSING_DATA_MARK = 0x01;
    static constexpr const uint8_t ST2_DATA_ERROR_IN_DATA = 0x20;
    static constexpr const uint8_t ST2_CONTROL_MARK = 0x40;

    static constexpr const uint8_t PLUS3_CYLINDERS = 40;
    static constexpr const uint8_t PLUS3_SIDES = 1;
    /// endregion </Constants>

    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;
    std::string _filepath;

    DiskImage* _diskImage = nullptr;
    std::vector<std::string> _warnings;

    bool _extended = false;     // Layout of the last parsed file
    std::string _creator;       // Creator string of the last parsed file (informational)
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    LoaderDSK(EmulatorContext* context, const std::string& filepath) : _context(context), _filepath(filepath) {}
    virtual ~LoaderDSK() = default;
    /// endregion </Constructors / destructors>

    /// region <Properties>
public:
    DiskImage* getImage() { return _diskImage; }
    void setImage(DiskImage* diskImage) { _diskImage = diskImage; }  // Does not take ownership

    /// Diagnostics collected by the last loadImage / writeImage call
    const std::vector<std::string>& lastWarnings() const { return _warnings; }

    /// True when the last parsed file used the extended layout
    bool isExtended() const { return _extended; }

    /// Creator string of the last parsed file
    const std::string& getCreator() const { return _creator; }
    /// endregion </Properties>

    /// region <Basic methods>
public:
    /// Load the file into a new DiskImage (ownership passes to the caller via getImage())
    bool loadImage();

    /// Save the current image to the loader's path (always Extended DSK)
    bool writeImage();

    /// Save the current image to the given path (Save As, always Extended DSK)
    bool writeImage(const std::string& path);

    /// True when the buffer starts with a DSK signature
    /// @param extended Receives whether the extended layout was detected
    static bool detect(const uint8_t* data, size_t len, bool* extended = nullptr);

    /// Parse an in-memory DSK / EDSK file into a new DiskImage. Used by loadImage() and by tests.
    /// @param warnings Diagnostics (why parsing failed, non-fatal issues)
    /// @return New DiskImage or nullptr
    DiskImage* parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings);

    /// Serialise the image to an in-memory Extended DSK file
    bool serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const;

    /// Blank +3DOS disk: 40 cylinders, 1 side, 9 x 512 (sectors 1..9), GAP#3 42, filler 0xE5.
    /// An all-0xE5 disk is a valid empty +3DOS / CP/M disk (empty directory, default disk specification).
    static DiskImage* createBlankPlus3Image();
    /// endregion </Basic methods>

    /// region <Helper methods>
protected:
    /// Build one track of the image from a track information block and its sector data
    /// @param image Target image
    /// @param cylinder Physical cylinder (track position in the image)
    /// @param side Physical side
    /// @param info Pointer to the 256-byte track information block
    /// @param dataEnd End of the region the sector data may occupy (file end / next track)
    /// @param extended Extended layout (per-sector actual data length present)
    /// @return false when the track cannot be built (truncated file, unsupported layout)
    bool parseTrack(DiskImage* image, uint8_t cylinder, uint8_t side, const uint8_t* info, const uint8_t* dataEnd,
                    bool extended, std::vector<std::string>& warnings);

    /// GAP#3 as measured in the stream: gap bytes after the first sector until the next sync
    static uint8_t measureGap3(const DiskImage::Track* track);

    /// Most frequent byte over all sector data of the track, 0xE5 when the track has no data
    static uint8_t mostFrequentDataByte(const DiskImage::Track* track);
    /// endregion </Helper methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing
//
#ifdef _CODE_UNDER_TEST

class LoaderDSKCUT : public LoaderDSK
{
public:
    LoaderDSKCUT(EmulatorContext* context, const std::string& filepath) : LoaderDSK(context, filepath) {}

public:
    using LoaderDSK::_diskImage;
    using LoaderDSK::_extended;
    using LoaderDSK::_creator;
    using LoaderDSK::measureGap3;
    using LoaderDSK::mostFrequentDataByte;
};
#endif  // _CODE_UNDER_TEST
