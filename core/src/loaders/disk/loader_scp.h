#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/io/fdc/diskimage.h"

class EmulatorContext;

/// Loader for SuperCard Pro flux images (SCP).
/// SCP stores raw flux transition timings, which are converted to bit cells via PLL.
class LoaderSCP
{
public:
    /// region <Constants>

    static constexpr uint8_t SIGNATURE[] = {'S', 'C', 'P'};
    static constexpr size_t SIGNATURE_LEN = 3;
    static constexpr size_t HEADER_SIZE = 16;
    static constexpr size_t TRACK_TABLE_OFFSET = 0x10;
    static constexpr size_t TRACK_TABLE_ENTRIES = 168;
    static constexpr size_t TRACK_HEADER_SIZE = 4;
    static constexpr size_t REV_HEADER_SIZE = 12;

    static constexpr uint8_t FLAG_INDEX = 0x01;
    static constexpr uint8_t FLAG_96TPI = 0x02;
    static constexpr uint8_t FLAG_360RPM = 0x04;
    static constexpr uint8_t FLAG_NORMALISED = 0x08;
    static constexpr uint8_t FLAG_READWRITE = 0x10;
    static constexpr uint8_t FLAG_FOOTER = 0x20;
    static constexpr uint8_t FLAG_EXTENDED = 0x40;
    static constexpr uint8_t FLAG_NONFLUX = 0x80;

    static constexpr uint8_t HEADS_BOTH = 0;
    static constexpr uint8_t HEADS_SIDE0 = 1;
    static constexpr uint8_t HEADS_SIDE1 = 2;

    static constexpr uint32_t RESOLUTION_25NS = 0;
    static constexpr uint32_t NS_PER_TICK = 25;

    static constexpr uint8_t MAX_CYLINDERS = 86;
    static constexpr uint8_t MAX_REVOLUTIONS = 5;

    /// endregion </Constants>

    /// region <Constructors>

    LoaderSCP() = default;
    explicit LoaderSCP(const std::string& filepath) : _filepath(filepath) {}
    LoaderSCP(EmulatorContext* context, const std::string& filepath)
        : _context(context), _filepath(filepath) {}

    /// endregion </Constructors>

    /// region <Static helpers>

    /// Detect SCP format from first bytes.
    static bool detect(const uint8_t* data, size_t len);

    /// endregion </Static helpers>

    /// region <Parsing>

    /// Parse SCP data into a DiskImage. Returns nullptr on failure.
    DiskImage* parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings);

    /// endregion </Parsing>

    /// region <Serialisation>

    /// Serialize a DiskImage to SCP format (single revolution).
    bool serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const;

    /// endregion </Serialisation>

    /// region <Basic methods>

    bool loadImage();
    bool writeImage();
    bool writeImage(const std::string& path);

    /// endregion </Basic methods>

    /// region <Accessors>

    DiskImage* getDiskImage() const { return _diskImage; }
    void setDiskImage(DiskImage* image) { _diskImage = image; }
    const std::string& getFilePath() const { return _filepath; }
    void setFilePath(const std::string& path) { _filepath = path; }
    const std::vector<std::string>& getWarnings() const { return _warnings; }
    void setContext(EmulatorContext* context) { _context = context; }

    /// endregion </Accessors>

private:
    struct RevolutionData
    {
        uint32_t indexTimeNs;
        std::vector<uint32_t> fluxIntervalsNs;
    };

    struct TrackData
    {
        uint8_t trackNumber;
        std::vector<RevolutionData> revolutions;
    };

    /// Parse flux intervals for one revolution from SCP data.
    static bool parseFluxData(const uint8_t* data, size_t offset, uint32_t count,
                               std::vector<uint32_t>& intervalsNs);

    /// Decode flux intervals to raw track bytes using PLL and MFM/FM decoder.
    static void decodeTrack(const std::vector<uint32_t>& intervalsNs, bool fm,
                            std::vector<uint8_t>& bytes, std::vector<uint8_t>& clockBitmap);

    /// Merge multiple revolutions by majority voting, marking disagreements as weak.
    static void mergeRevolutions(const std::vector<std::vector<uint8_t>>& revBytes,
                                  std::vector<uint8_t>& merged,
                                  std::vector<bool>& weakBits);

    /// Encode raw track bytes to flux intervals for saving.
    static void encodeTrack(const uint8_t* bytes, size_t count, const uint8_t* clockBitmap,
                            bool fm, std::vector<uint16_t>& fluxTicks);

    EmulatorContext* _context = nullptr;
    std::string _filepath;
    DiskImage* _diskImage = nullptr;
    std::vector<std::string> _warnings;

    // Metadata
    uint8_t _version = 0x20;
    uint8_t _diskType = 0;
    uint8_t _flags = FLAG_INDEX | FLAG_NORMALISED;
    uint8_t _bitCellWidth = 0;
    uint8_t _heads = HEADS_BOTH;
    uint8_t _resolution = RESOLUTION_25NS;
};
