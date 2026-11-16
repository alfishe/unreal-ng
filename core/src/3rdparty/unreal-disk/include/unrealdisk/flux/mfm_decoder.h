#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace unrealdisk
{

namespace fdc {
namespace flux {

class MfmDecoder
{
public:
    MfmDecoder();
    void reset();
    void decode(const uint8_t* bitCells, size_t count);
    void decode(const std::vector<uint8_t>& bitCells);
    /// As decode, with a weak flag per cell (may be null): weakBytes() then holds one flag per byte, set when a weak
    /// cell came in since the byte before it was emitted
    void decode(const uint8_t* bitCells, size_t count, const uint8_t* weakCells);

    const std::vector<uint8_t>& bytes() const { return _bytes; }
    const std::vector<uint8_t>& clockBitmap() const { return _clockBitmap; }
    const std::vector<uint8_t>& weakBytes() const { return _weakBytes; }
    size_t byteCount() const { return _bytes.size(); }

private:
    void processBit(uint8_t bit);
    void emitByte(uint8_t byte, bool isSyncMark);

    std::vector<uint8_t> _bytes;
    std::vector<uint8_t> _clockBitmap;
    std::vector<uint8_t> _weakBytes;
    bool _pendingWeak = false;
    bool _trackWeak = false;
    uint16_t _shiftReg;
    int _bitCount;
    uint8_t _currentByte;
    bool _syncPhase;
};

class FmDecoder
{
public:
    FmDecoder();
    void reset();
    void decode(const uint8_t* bitCells, size_t count);
    void decode(const std::vector<uint8_t>& bitCells);
    /// As decode, with a weak flag per cell (may be null): weakBytes() then holds one flag per byte, set when a weak
    /// cell came in since the byte before it was emitted
    void decode(const uint8_t* bitCells, size_t count, const uint8_t* weakCells);

    const std::vector<uint8_t>& bytes() const { return _bytes; }
    const std::vector<uint8_t>& clockBitmap() const { return _clockBitmap; }
    const std::vector<uint8_t>& weakBytes() const { return _weakBytes; }
    size_t byteCount() const { return _bytes.size(); }

private:
    void processBitPair(uint8_t clockBit, uint8_t dataBit);
    void emitByte(uint8_t byte, uint8_t clockPattern);

    std::vector<uint8_t> _bytes;
    std::vector<uint8_t> _clockBitmap;
    std::vector<uint8_t> _weakBytes;
    bool _pendingWeak = false;
    bool _trackWeak = false;
    int _bitCount;
    uint8_t _currentByte;
    uint8_t _currentClockPattern;
};

} // namespace flux
} // namespace fdc
}  // namespace unrealdisk
