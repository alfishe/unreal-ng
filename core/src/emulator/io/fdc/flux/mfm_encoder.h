#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace fdc {
namespace flux {

class MfmEncoder
{
public:
    MfmEncoder();
    void reset();
    void encode(const uint8_t* bytes, size_t count, const uint8_t* clockBitmap);
    void encode(const std::vector<uint8_t>& bytes, const std::vector<uint8_t>& clockBitmap);

    const std::vector<uint8_t>& bitCells() const { return _bitCells; }

private:
    void encodeByte(uint8_t byte, bool isSyncMark);

    std::vector<uint8_t> _bitCells;
    bool _prevDataBit;
};

class FmEncoder
{
public:
    FmEncoder();
    void reset();
    void encode(const uint8_t* bytes, size_t count, const uint8_t* clockBitmap);
    void encode(const std::vector<uint8_t>& bytes, const std::vector<uint8_t>& clockBitmap);

    const std::vector<uint8_t>& bitCells() const { return _bitCells; }

private:
    void encodeByte(uint8_t byte, bool isSyncMark);

    std::vector<uint8_t> _bitCells;
};

} // namespace flux
} // namespace fdc
