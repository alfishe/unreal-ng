#pragma once

#include "stdafx.h"

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/io/fdc/diskimage.h"

class EmulatorContext;

/// FDI (Full Disk Image, UKV Spectrum Debugger) loader / writer.
///
/// FDI describes each track as a list of sectors (C/H/R/N, flags, data offset) without gaps, CRC values or clock
/// information, so every track is rebuilt with DiskImage::TrackFormatSpec on load and the sector list is taken
/// from the track index on save. Sector sizes 128..1024, any sector count, arbitrary numbering, deleted data
/// marks, missing data fields and CRC errors are represented; gaps and exact CRC values are not (use UDI for that).
///
/// File layout (little-endian), verified against testdata/loaders/fdi/VORON*.FDI:
///   0x00  3   "FDI"
///   0x03  1   write-protect flag (0 = writable)
///   0x04  2   cylinders
///   0x06  2   heads
///   0x08  2   offset of the description text (ASCIIZ), 0 = none
///   0x0A  2   offset of the sector data area
///   0x0C  2   extra header length E
///   0x0E  E   extra header (preserved)
///   then one track header per (cylinder, head), cylinder-major:
///         4   track data offset, relative to the data area
///         2   reserved
///         1   sector count S
///         7*S sector headers: C, H, R, N, flags, data offset (2) relative to the track data offset
///             flags bit n (n = 0..5): data field present and its CRC is correct for sector size 128 << n
///                                     (i.e. bit N set => data present + CRC ok; no bit set => CRC error or no data)
///                   bit 6 (0x40): deleted data mark (F8)
///                   bit 7 (0x80): no data field (ID only)
///
/// See docs/inprogress/2026-09-02-universal-track-model/loader-fdi.md
class LoaderFDI
{
    /// region <Constants>
public:
    static constexpr const char* SIGNATURE = "FDI";
    static constexpr const size_t HEADER_SIZE = 14;
    static constexpr const size_t SECTOR_HEADER_SIZE = 7;
    static constexpr const size_t TRACK_HEADER_SIZE = 7;

    static constexpr const uint8_t FLAG_DELETED = 0x40;
    static constexpr const uint8_t FLAG_NO_DATA = 0x80;
    static constexpr const uint8_t FLAG_CRC_OK_MASK = 0x3F;
    /// endregion </Constants>

    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;
    std::string _filepath;

    DiskImage* _diskImage = nullptr;
    std::vector<std::string> _warnings;

    std::vector<uint8_t> _extraHeader;   // Preserved from the loaded file
    std::string _description;            // Text at the description offset (preserved / editable)
    bool _writeProtected = false;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    LoaderFDI(EmulatorContext* context, const std::string& filepath) : _context(context), _filepath(filepath) {}
    virtual ~LoaderFDI() = default;
    /// endregion </Constructors / destructors>

    /// region <Properties>
public:
    DiskImage* getImage() { return _diskImage; }
    void setImage(DiskImage* diskImage) { _diskImage = diskImage; }  // Does not take ownership

    const std::vector<std::string>& lastWarnings() const { return _warnings; }

    const std::string& getDescription() const { return _description; }
    void setDescription(const std::string& text) { _description = text; }
    bool isWriteProtected() const { return _writeProtected; }
    void setWriteProtected(bool protect) { _writeProtected = protect; }
    const std::vector<uint8_t>& getExtraHeader() const { return _extraHeader; }
    /// endregion </Properties>

    /// region <Basic methods>
public:
    bool loadImage();
    bool writeImage();
    bool writeImage(const std::string& path);

    static bool detect(const uint8_t* data, size_t len);

    /// Parse an in-memory FDI file into a new DiskImage (nullptr on failure, reasons in warnings)
    DiskImage* parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings);

    /// Serialise the image to an in-memory FDI file. Gaps / clock marks / exact CRC values are dropped;
    /// tracks whose content FDI cannot express are reported in warnings.
    bool serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const;

    /// Build the format spec for one FDI track (sector list) so that it fits the track length.
    /// Shrinks the post-data gap down to the datasheet minimum, then grows the track length up to the maximum.
    /// @return false when the sectors cannot fit even at MAX_TRACK_SIZE
    static bool buildTrackSpec(const std::vector<uint8_t>& numbers, const std::vector<uint8_t>& sizeCodes,
                               const std::vector<uint8_t>& cylinders, const std::vector<uint8_t>& heads,
                               const std::vector<uint8_t>& idOnly, DiskImage::TrackFormatSpec& spec,
                               std::string* note = nullptr);
    /// endregion </Basic methods>
};

#ifdef _CODE_UNDER_TEST

class LoaderFDICUT : public LoaderFDI
{
public:
    LoaderFDICUT(EmulatorContext* context, const std::string& filepath) : LoaderFDI(context, filepath) {}

public:
    using LoaderFDI::_diskImage;
    using LoaderFDI::_extraHeader;
    using LoaderFDI::_description;
};
#endif  // _CODE_UNDER_TEST
