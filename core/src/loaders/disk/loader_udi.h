#pragma once

#include "stdafx.h"

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/io/fdc/diskimage.h"

class EmulatorContext;

/// UDI (Ultra Disk Image v1.0, Alex Makeev) loader / writer.
///
/// UDI stores every track as the raw byte stream read from the disk plus one clock bit per byte, which is
/// exactly the universal track model (DiskImage::Track). Loading and saving are therefore lossless: sector
/// layout, gaps, CRC errors, duplicate / missing sectors, deleted marks, non-standard track lengths and FM tracks
/// all survive a round trip. This makes UDI the designated "save anything" target when TRD / SCL refuse an image.
///
/// File layout (little-endian):
///   0x00  4   "UDI!"   ("udi!" = compressed variant, not supported)
///   0x04  4   size of the file without the trailing CRC
///   0x08  1   version (0)
///   0x09  1   max cylinder (cylinders - 1)
///   0x0A  1   max head (sides - 1)
///   0x0B  1   unused (preserved verbatim; TRX2X writes 1)
///   0x0C  4   extended header length X
///   0x10  X   extended header (preserved verbatim)
///   then, for each cylinder, for each head:
///         1   track type: 0 = MFM, 1 = FM, 2 = MFM with per-byte FM/MFM select bitmap, bit 7 = multi-revolution
///         2   TLEN
///         TLEN         raw track bytes (offset 0 = index pulse)
///         (TLEN+7)/8   clock bitmap, bit i (LSB first) of byte i/8 => byte i carries a missing-clock mark
///         [(TLEN+7)/8  FM/MFM select bitmap, type 2 only]
///   optional trailer: any bytes between the last track and the CRC (e.g. an ASCIIZ comment written by TRX2X);
///         preserved verbatim
///   last  4   CRC-32 over everything before it (CRCHelper::crcUDI, signed arithmetic-shift variant)
///
/// See docs/inprogress/2026-09-02-universal-track-model/loader-udi.md
class LoaderUDI
{
    /// region <Constants>
public:
    static constexpr const char* SIGNATURE = "UDI!";
    static constexpr const char* SIGNATURE_COMPRESSED = "udi!";
    static constexpr const size_t HEADER_SIZE = 16;
    static constexpr const uint8_t VERSION = 0;

    static constexpr const uint8_t TRACK_TYPE_MFM = 0;
    static constexpr const uint8_t TRACK_TYPE_FM = 1;
    static constexpr const uint8_t TRACK_TYPE_MIXED = 2;
    static constexpr const uint8_t TRACK_TYPE_MULTIREV_FLAG = 0x80;
    /// endregion </Constants>

    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;
    std::string _filepath;

    DiskImage* _diskImage = nullptr;
    std::vector<std::string> _warnings;

    std::vector<uint8_t> _extendedHeader;   // Preserved from the loaded file, written back on save
    std::vector<uint8_t> _trailer;          // Bytes between the last track and the CRC (comment), preserved
    uint8_t _reserved = 0;                  // Header byte 0x0B ("unused"; some writers store 1), preserved
    bool _ignoreCrc = false;                // Load images with a wrong CRC (warning instead of failure)
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    LoaderUDI(EmulatorContext* context, const std::string& filepath) : _context(context), _filepath(filepath) {}
    virtual ~LoaderUDI() = default;
    /// endregion </Constructors / destructors>

    /// region <Properties>
public:
    DiskImage* getImage() { return _diskImage; }
    void setImage(DiskImage* diskImage) { _diskImage = diskImage; }  // Does not take ownership

    /// Diagnostics collected by the last loadImage / writeImage call
    const std::vector<std::string>& lastWarnings() const { return _warnings; }

    void setIgnoreCrc(bool ignore) { _ignoreCrc = ignore; }
    bool getIgnoreCrc() const { return _ignoreCrc; }

    const std::vector<uint8_t>& getExtendedHeader() const { return _extendedHeader; }
    void setExtendedHeader(const std::vector<uint8_t>& header) { _extendedHeader = header; }
    const std::vector<uint8_t>& getTrailer() const { return _trailer; }
    void setTrailer(const std::vector<uint8_t>& trailer) { _trailer = trailer; }
    uint8_t getReservedByte() const { return _reserved; }
    void setReservedByte(uint8_t value) { _reserved = value; }
    /// endregion </Properties>

    /// region <Basic methods>
public:
    /// Load the file into a new DiskImage (ownership passes to the caller via getImage())
    bool loadImage();

    /// Save the current image to the loader's path
    bool writeImage();

    /// Save the current image to the given path (Save As)
    bool writeImage(const std::string& path);

    /// True when the buffer starts with the UDI signature (compressed "udi!" is reported separately)
    static bool detect(const uint8_t* data, size_t len, bool* compressed = nullptr);

    /// Parse an in-memory UDI file into a new DiskImage. Used by loadImage() and by tests.
    /// @param warnings Diagnostics (why parsing failed, non-fatal issues)
    /// @return New DiskImage or nullptr
    DiskImage* parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings);

    /// Serialise the image to an in-memory UDI file
    bool serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const;

    /// CRC as stored at the end of a UDI file
    static uint32_t computeCrc(const uint8_t* data, size_t len);
    /// endregion </Basic methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing
//
#ifdef _CODE_UNDER_TEST

class LoaderUDICUT : public LoaderUDI
{
public:
    LoaderUDICUT(EmulatorContext* context, const std::string& filepath) : LoaderUDI(context, filepath) {}

public:
    using LoaderUDI::_diskImage;
    using LoaderUDI::_extendedHeader;
    using LoaderUDI::_trailer;
    using LoaderUDI::_reserved;
};
#endif  // _CODE_UNDER_TEST
