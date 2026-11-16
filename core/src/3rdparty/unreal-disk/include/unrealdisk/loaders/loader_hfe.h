#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "unrealdisk/diskimage.h"
#include "unrealdisk/options.h"

namespace unrealdisk
{


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
    LoaderHFE(const LoaderOptions& options, const std::string& filepath)
        : _options(options), _filepath(filepath) {}

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
    void setOptions(const LoaderOptions& options) { _options = options; }

    /// endregion </Accessors>

protected:
    /// The cell rate this loader writes: 250 kbit/s (2 us cells, double density MFM); FM tracks go at double rate,
    /// each FM cell as two ("0c"), the HFE convention (HxC, Greaseweazle)
    static constexpr uint16_t WRITE_BIT_RATE = 250;
    /// SETBITRATE argument for it: 36 MHz / cells per second (72 for 500 000 cells/s)
    static constexpr uint8_t WRITE_RATE_OPCODE_VALUE = 72;

    /// One track's sides from its LUT entry: trackLength / 2 bytes a side, in 512-byte blocks of 256 bytes of side 0
    /// then 256 of side 1 (the last block's unused part is padding). `available` bounds the reads to the file
    static void deinterleave(const uint8_t* src, size_t available, size_t trackLength, std::vector<uint8_t>& side0,
                             std::vector<uint8_t>& side1);

    /// The blocks of one track; the LUT length is 2 x the longer side (padding bytes `pad` are not counted)
    static void interleave(const std::vector<uint8_t>& side0, const std::vector<uint8_t>& side1, uint8_t pad,
                           std::vector<uint8_t>& out);

    /// Bit-reverse a byte (v3 stores bits in reverse order compared to v1)
    static uint8_t bitReverse(uint8_t b);

    /// Convert packed bit cells to a bit vector
    static void unpackBits(const uint8_t* data, size_t byteCount, std::vector<uint8_t>& bits, bool lsbFirst = true);

    /// Pack bit vector back to bytes
    static void packBits(const std::vector<uint8_t>& bits, std::vector<uint8_t>& out, bool lsbFirst = true);

    /// The v3 opcode stream (bytes in transmission order, first cell in bit 7) to cells and a weak flag per cell:
    /// NOP, SETINDEX: nothing; SETBITRATE n: one argument byte; SKIPBITS n, x: the low 8 - n bits of x (x = RAND:
    /// 8 - n weak cells); RAND: 8 weak cells
    static void processV3Opcodes(const std::vector<uint8_t>& raw, std::vector<uint8_t>& bits,
                                 std::vector<uint8_t>& weakCells);

    /// The cells of one track at the write rate (FM doubled) and a weak flag per cell (16 per weak byte, 32 in FM)
    static void encodeSide(DiskImage::Track& track, std::vector<uint8_t>& cells, std::vector<uint8_t>& weakCells);

    /// v3 opcode stream of one side: SETBITRATE, then the cells 8 at a time (RAND for weak ones, SKIPBITS for a
    /// short last group or a group that would read as an opcode); transmission order, not yet bit-reversed
    static void emitV3(const std::vector<uint8_t>& cells, const std::vector<uint8_t>& weakCells,
                       std::vector<uint8_t>& out);

    /// Decode one side into the track: MFM, or FM at double or single rate, whichever finds the most sectors (a tie
    /// goes to the header's encoding). An empty side keeps a blank track
    static void decodeSide(const std::vector<uint8_t>& cells, const std::vector<uint8_t>& weakCells, bool headerFm,
                           bool singleRateFm, DiskImage::Track& track);

    bool serializeVersion(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings,
                          int version) const;

    LoaderOptions _options;
    std::string _filepath;
    DiskImage* _diskImage = nullptr;
    std::vector<std::string> _warnings;

    // Metadata from file
    uint8_t _formatRevision = 0;
    uint16_t _bitRate = 250;
    uint16_t _rpm = 300;
    uint8_t _interfaceMode = 7;
    bool _writeAllowed = true;
    bool _singleStep = true;  // header 0xFF: single step (0 is double step)
};
#ifdef _CODE_UNDER_TEST

class LoaderHFECUT : public LoaderHFE
{
public:
    using LoaderHFE::processV3Opcodes;
    using LoaderHFE::emitV3;
};
#endif  // _CODE_UNDER_TEST
}  // namespace unrealdisk
