#include "mfm_encoder.h"

namespace fdc {
namespace flux {

static constexpr uint16_t MFM_SYNC_A1 = 0x4489;
static constexpr uint8_t FM_CLOCK_IDAM = 0xC7;
static constexpr uint8_t FM_CLOCK_DAM = 0xD7;

MfmEncoder::MfmEncoder()
    : _prevDataBit(false)
{
}

void MfmEncoder::reset()
{
    _bitCells.clear();
    _prevDataBit = false;
}

void MfmEncoder::encode(const uint8_t* bytes, size_t count, const uint8_t* clockBitmap)
{
    _bitCells.reserve(_bitCells.size() + count * 16);

    for (size_t i = 0; i < count; ++i)
    {
        bool isSyncMark = false;
        if (clockBitmap)
        {
            size_t bitmapByte = i / 8;
            size_t bitmapBit = i % 8;
            isSyncMark = (clockBitmap[bitmapByte] & (1 << bitmapBit)) != 0;
        }

        encodeByte(bytes[i], isSyncMark);
    }
}

void MfmEncoder::encode(const std::vector<uint8_t>& bytes, const std::vector<uint8_t>& clockBitmap)
{
    encode(bytes.data(), bytes.size(), clockBitmap.empty() ? nullptr : clockBitmap.data());
}

void MfmEncoder::encodeByte(uint8_t byte, bool isSyncMark)
{
    if (isSyncMark && byte == 0xA1)
    {
        for (int i = 15; i >= 0; --i)
        {
            _bitCells.push_back((MFM_SYNC_A1 >> i) & 1);
        }
        _prevDataBit = true;
        return;
    }

    for (int i = 7; i >= 0; --i)
    {
        bool dataBit = (byte >> i) & 1;
        bool clockBit = (!_prevDataBit && !dataBit);

        _bitCells.push_back(clockBit ? 1 : 0);
        _bitCells.push_back(dataBit ? 1 : 0);

        _prevDataBit = dataBit;
    }
}

FmEncoder::FmEncoder()
{
}

void FmEncoder::reset()
{
    _bitCells.clear();
}

void FmEncoder::encode(const uint8_t* bytes, size_t count, const uint8_t* clockBitmap)
{
    _bitCells.reserve(_bitCells.size() + count * 16);

    for (size_t i = 0; i < count; ++i)
    {
        bool isSyncMark = false;
        if (clockBitmap)
        {
            size_t bitmapByte = i / 8;
            size_t bitmapBit = i % 8;
            isSyncMark = (clockBitmap[bitmapByte] & (1 << bitmapBit)) != 0;
        }

        encodeByte(bytes[i], isSyncMark);
    }
}

void FmEncoder::encode(const std::vector<uint8_t>& bytes, const std::vector<uint8_t>& clockBitmap)
{
    encode(bytes.data(), bytes.size(), clockBitmap.empty() ? nullptr : clockBitmap.data());
}

void FmEncoder::encodeByte(uint8_t byte, bool isSyncMark)
{
    uint8_t clockPattern = 0xFF;

    if (isSyncMark)
    {
        if (byte == 0xFE)
        {
            clockPattern = FM_CLOCK_IDAM;
        }
        else if (byte >= 0xF8 && byte <= 0xFB)
        {
            clockPattern = FM_CLOCK_DAM;
        }
    }

    for (int i = 7; i >= 0; --i)
    {
        bool clockBit = (clockPattern >> i) & 1;
        bool dataBit = (byte >> i) & 1;

        _bitCells.push_back(clockBit ? 1 : 0);
        _bitCells.push_back(dataBit ? 1 : 0);
    }
}

} // namespace flux
} // namespace fdc
