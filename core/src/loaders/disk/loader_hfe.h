#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/io/fdc/diskimage.h"

class EmulatorContext;

/// Loader for HxC Floppy Emulator disk images (HFE v1 and v3).
/// HFE stores bit cells directly, encoding MFM/FM transitions.
class LoaderHFE
{
public:
    /// region <Constants>

    static constexpr uint8_t SIGNATURE_V1[] = {'H', 'X', 'C', 'P', 'I', 'C', 'F', 'E'};
    static constexpr uint8_t SIGNATURE_V3[] = {'H', 'X', 'C', 'H', 'F', 'E', 'V', '3'};
    static constexpr size_t SIGNATURE_LEN = 8;
    static constexpr size_t HEADER_SIZE = 512;
    static constexpr size_t BLOCK_SIZE = 512;
    static constexpr size_t INTERLEAVE_SIZE = 256;

    static constexpr uint8_t ENCODING_ISOIBM_MFM = 0;
    static constexpr uint8_t ENCODING_AMIGA_MFM = 1;
    static constexpr uint8_t ENCODING_ISOIBM_FM = 2;
    static constexpr uint8_t ENCODING_EMU_FM = 3;
    static constexpr uint8_t ENCODING_UNKNOWN = 0xFF;

    static constexpr uint8_t MAX_CYLINDERS = 86;

    // v3 opcodes (special bytes in bit stream)
    static constexpr uint8_t OP_NOP = 0xF0;
    static constexpr uint8_t OP_SETINDEX = 0xF1;
    static constexpr uint8_t OP_SETBITRATE = 0xF2;
    static constexpr uint8_t OP_SKIPBITS = 0xF3;
    static constexpr uint8_t OP_RAND = 0xF4;

    /// endregion </Constants>

    /// region <Constructors>

    LoaderHFE() = default;
    explicit LoaderHFE(const std::string& filepath) : _filepath(filepath) {}
    LoaderHFE(EmulatorContext* context, const std::string& filepath)
        : _context(context), _filepath(filepath) {}

    /// endregion </Constructors>

    /// region <Static helpers>

    /// Detect HFE format from first bytes. Returns 1 for v1, 3 for v3, 0 if not HFE.
    static int detect(const uint8_t* data, size_t len);

    /// endregion </Static helpers>

    /// region <Parsing>

    /// Parse HFE data into a DiskImage. Returns nullptr on failure.
    DiskImage* parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings);

    /// endregion </Parsing>

    /// region <Serialisation>

    /// Serialize a DiskImage to HFE v3 format.
    bool serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const;

    /// Serialize to HFE v1 format.
    bool serializeV1(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const;

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
    /// De-interleave track data (256-byte blocks alternating between sides)
    static void deinterleave(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& side0, std::vector<uint8_t>& side1);

    /// Interleave track data for saving
    static void interleave(const std::vector<uint8_t>& side0, const std::vector<uint8_t>& side1, std::vector<uint8_t>& out);

    /// Bit-reverse a byte (v3 stores bits in reverse order compared to v1)
    static uint8_t bitReverse(uint8_t b);

    /// Convert packed bit cells to a bit vector
    static void unpackBits(const uint8_t* data, size_t byteCount, std::vector<uint8_t>& bits, bool lsbFirst = true);

    /// Pack bit vector back to bytes
    static void packBits(const std::vector<uint8_t>& bits, std::vector<uint8_t>& out, bool lsbFirst = true);

    /// Process v3 opcodes and extract actual bit cells + weak regions
    static void processV3Opcodes(const std::vector<uint8_t>& raw, std::vector<uint8_t>& bits,
                                  std::vector<std::pair<size_t, size_t>>& weakRanges);

    EmulatorContext* _context = nullptr;
    std::string _filepath;
    DiskImage* _diskImage = nullptr;
    std::vector<std::string> _warnings;

    // Metadata from file
    uint8_t _formatRevision = 0;
    uint16_t _bitRate = 250;
    uint16_t _rpm = 300;
    uint8_t _interfaceMode = 7;
    bool _writeAllowed = true;
    bool _singleStep = false;
};
