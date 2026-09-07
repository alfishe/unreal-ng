#pragma once

#include "stdafx.h"

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/io/fdc/diskimage.h"

class EmulatorContext;

/// Sydex Teledisk TD0 loader / writer.
///
/// TD0 stores each track as a list of sectors (C/H/R/N, flags, CRC) followed by optionally packed data blocks, so
/// like FDI every track is rebuilt with DiskImage::TrackFormatSpec on load. "TD" files are plain; "td" files are
/// LZSS + adaptive-Huffman ("LZHUF") compressed after the 12-byte header and are decompressed into memory first.
/// Writing produces the uncompressed "TD" variant only (data encodings 0 and 1).
///
/// File layout (little-endian), see docs/inprogress/2026-09-02-universal-track-model/loader-td0.md:
///   Header (12 bytes): "TD" | "td", sequence, check sequence, version, data rate (bits 0-1: 0 = 250k, 1 = 300k,
///   2 = 500k; bit 7 = FM), drive type, stepping (bit 7 = comment block present), DOS allocation flag, sides,
///   CRC-16 (polynomial 0xA097, init 0) of the first 10 bytes.
///   Comment block (optional): CRC-16 (2), length (2), year-1900, month-1, day, hour, minute, second, text
///   (NUL-separated lines; exposed as '\n'-separated description).
///   Track header (4): sector count (0xFF = end of image), cylinder, head (bit 7 = FM track), CRC-8 (low byte
///   of the CRC-16 over the first 3 bytes).
///   Sector header (6): C, H, R, N, flags, CRC-8 of the decoded sector data. Flags: 0x01 duplicate, 0x02 data CRC
///   error, 0x04 deleted DAM, 0x10 DOS-unallocated (no data block), 0x20 no data field (no data block),
///   0x40 no ID field (skipped with a warning).
///   Data block (unless flags & 0x30): length (2, includes the encoding byte), encoding: 0 raw, 1 count(2) +
///   2-byte pattern, 2 RLE (blocks [0, count, count raw bytes] | [l, count, 2*l-byte pattern x count]).
class LoaderTD0
{
    /// region <Constants>
public:
    static constexpr const size_t HEADER_SIZE = 12;
    static constexpr const size_t COMMENT_HEADER_SIZE = 10;
    static constexpr const size_t TRACK_HEADER_SIZE = 4;
    static constexpr const size_t SECTOR_HEADER_SIZE = 6;
    static constexpr const uint8_t END_OF_IMAGE = 0xFF;

    static constexpr const uint8_t FLAG_DUPLICATE = 0x01;
    static constexpr const uint8_t FLAG_DATA_CRC_ERROR = 0x02;
    static constexpr const uint8_t FLAG_DELETED = 0x04;
    static constexpr const uint8_t FLAG_DOS_UNALLOCATED = 0x10;
    static constexpr const uint8_t FLAG_NO_DATA = 0x20;
    static constexpr const uint8_t FLAG_NO_ID = 0x40;
    static constexpr const uint8_t FLAGS_NO_DATA_BLOCK = FLAG_DOS_UNALLOCATED | FLAG_NO_DATA;

    static constexpr const uint8_t RATE_MASK = 0x03;
    static constexpr const uint8_t RATE_FM = 0x80;
    static constexpr const uint8_t HEAD_FM = 0x80;
    static constexpr const uint8_t STEPPING_COMMENT = 0x80;

    static constexpr const uint8_t ENCODING_RAW = 0;
    static constexpr const uint8_t ENCODING_PATTERN = 1;
    static constexpr const uint8_t ENCODING_RLE = 2;

    static constexpr const uint8_t DEFAULT_VERSION = 0x15;
    static constexpr const size_t MAX_DECOMPRESSED_SIZE = 64u * 1024u * 1024u;
    /// endregion </Constants>

    /// region <Types>
public:
    struct CommentDate
    {
        uint16_t year = 0;   // Full year (file stores year - 1900)
        uint8_t month = 0;   // 1..12 (file stores month - 1)
        uint8_t day = 0;
        uint8_t hour = 0;
        uint8_t minute = 0;
        uint8_t second = 0;
    };

protected:
    /// One decoded sector before it is mapped onto the track model
    struct SectorEntry
    {
        uint8_t cylinder = 0;
        uint8_t head = 0;
        uint8_t number = 0;
        uint8_t sizeCode = 0;
        uint8_t flags = 0;
        std::vector<uint8_t> data;   // Empty when flags & 0x30
    };

    struct TrackEntry
    {
        uint8_t cylinder = 0;
        uint8_t head = 0;
        bool fm = false;
        std::vector<SectorEntry> sectors;
    };
    /// endregion </Types>

    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;
    std::string _filepath;

    DiskImage* _diskImage = nullptr;
    std::vector<std::string> _warnings;

    std::string _description;     // Comment block text ('\n' separated lines)
    CommentDate _commentDate;
    bool _hasComment = false;

    // Header fields preserved from the loaded file (used again when saving)
    uint8_t _version = DEFAULT_VERSION;
    uint8_t _dataRate = 0;
    uint8_t _driveType = 3;
    uint8_t _stepping = 0;
    uint8_t _dosAllocation = 0;
    bool _advanced = false;       // Loaded file used "td" compression
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    LoaderTD0(EmulatorContext* context, const std::string& filepath) : _context(context), _filepath(filepath) {}
    virtual ~LoaderTD0() = default;
    /// endregion </Constructors / destructors>

    /// region <Properties>
public:
    DiskImage* getImage() { return _diskImage; }
    void setImage(DiskImage* diskImage) { _diskImage = diskImage; }  // Does not take ownership

    const std::vector<std::string>& lastWarnings() const { return _warnings; }

    const std::string& getDescription() const { return _description; }
    void setDescription(const std::string& text) { _description = text; }
    bool hasComment() const { return _hasComment; }
    const CommentDate& getCommentDate() const { return _commentDate; }
    void setCommentDate(const CommentDate& date) { _commentDate = date; }

    uint8_t getVersion() const { return _version; }
    uint8_t getDataRate() const { return _dataRate; }
    uint8_t getDriveType() const { return _driveType; }
    void setDriveType(uint8_t type) { _driveType = type; }
    uint8_t getStepping() const { return _stepping; }
    uint8_t getDosAllocation() const { return _dosAllocation; }
    bool isAdvancedCompression() const { return _advanced; }
    /// endregion </Properties>

    /// region <Basic methods>
public:
    bool loadImage();
    bool writeImage();
    bool writeImage(const std::string& path);

    static bool detect(const uint8_t* data, size_t len);

    /// Parse an in-memory TD0 file ("TD" or "td") into a new DiskImage (nullptr on failure, reasons in warnings)
    DiskImage* parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings);

    /// Serialise the image to an in-memory uncompressed "TD" file
    bool serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const;

    /// Teledisk CRC-16 (polynomial 0xA097, MSB first, initial value 0)
    static uint16_t crc16(const uint8_t* data, size_t len);

    /// Decode one data block payload (bytes after the encoding byte) into exactly sectorSize bytes.
    /// Short output is zero padded, extra output is dropped.
    /// @return false when the encoding is unknown or the payload is malformed (out still holds what was decoded)
    static bool decodeDataBlock(uint8_t encoding, const uint8_t* payload, size_t payloadLen, size_t sectorSize,
                                std::vector<uint8_t>& out);

    /// Decompress an LZHUF ("td") stream. Stops at the end of the input or at maxOut bytes.
    static bool decompressLzhuf(const uint8_t* in, size_t len, std::vector<uint8_t>& out, size_t maxOut = MAX_DECOMPRESSED_SIZE);

    /// Build the format spec for one TD0 track (sector list) so that it fits the track length.
    /// Shrinks the post-data gap down to the datasheet minimum, then grows the track up to MAX_TRACK_SIZE.
    static bool buildTrackSpec(const std::vector<uint8_t>& numbers, const std::vector<uint8_t>& sizeCodes,
                               const std::vector<uint8_t>& cylinders, const std::vector<uint8_t>& heads,
                               const std::vector<uint8_t>& idOnly, bool fm, size_t trackLength,
                               DiskImage::TrackFormatSpec& spec, std::string* note = nullptr);
    /// endregion </Basic methods>

    /// region <Helper methods>
protected:
    /// Parse the plain (already decompressed) stream that follows the 12-byte header
    bool parseBody(const uint8_t* data, size_t len, bool headerFm, std::vector<TrackEntry>& tracks, std::vector<std::string>& warnings);

    /// Map decoded tracks onto a new DiskImage
    DiskImage* buildImage(const std::vector<TrackEntry>& tracks, uint8_t sides, size_t trackLengthMfm, size_t trackLengthFm,
                          std::vector<std::string>& warnings);
    /// endregion </Helper methods>
};

#ifdef _CODE_UNDER_TEST

class LoaderTD0CUT : public LoaderTD0
{
public:
    LoaderTD0CUT(EmulatorContext* context, const std::string& filepath) : LoaderTD0(context, filepath) {}

public:
    using LoaderTD0::_diskImage;
    using LoaderTD0::_description;
    using LoaderTD0::_commentDate;
    using LoaderTD0::_advanced;
};
#endif  // _CODE_UNDER_TEST
