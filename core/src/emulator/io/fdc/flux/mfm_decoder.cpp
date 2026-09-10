#include "mfm_decoder.h"

namespace fdc {
namespace flux {

static constexpr uint16_t MFM_SYNC_A1 = 0x4489;
static constexpr uint8_t FM_CLOCK_IDAM = 0xC7;
static constexpr uint8_t FM_CLOCK_DAM = 0xD7;

MfmDecoder::MfmDecoder()
    : _shiftReg(0)
    , _bitCount(0)
    , _currentByte(0)
    , _syncPhase(false)
{
}

void MfmDecoder::reset()
{
    _bytes.clear();
    _clockBitmap.clear();
    _shiftReg = 0;
    _bitCount = 0;
    _currentByte = 0;
    _syncPhase = false;
}

void MfmDecoder::decode(const uint8_t* bitCells, size_t count)
{
    _bytes.reserve(_bytes.size() + count / 16);
    for (size_t i = 0; i < count; ++i)
    {
        processBit(bitCells[i]);
    }
}

void MfmDecoder::decode(const std::vector<uint8_t>& bitCells)
{
    decode(bitCells.data(), bitCells.size());
}

void MfmDecoder::processBit(uint8_t bit)
{
    _shiftReg = static_cast<uint16_t>((_shiftReg << 1) | (bit & 1));
    _bitCount++;

    if (_shiftReg == MFM_SYNC_A1)
    {
        emitByte(0xA1, true);
        _bitCount = 0;
        _syncPhase = true;
        return;
    }

    if (_bitCount >= 16)
    {
        uint8_t byte = 0;
        for (int i = 0; i < 8; ++i)
        {
            if (_shiftReg & (1 << (14 - i * 2)))
            {
                byte |= (1 << (7 - i));
            }
        }
        emitByte(byte, false);
        _bitCount = 0;
    }
}

void MfmDecoder::emitByte(uint8_t byte, bool isSyncMark)
{
    _bytes.push_back(byte);

    size_t byteIndex = _bytes.size() - 1;
    size_t bitmapByte = byteIndex / 8;
    size_t bitmapBit = byteIndex % 8;

    if (bitmapByte >= _clockBitmap.size())
    {
        _clockBitmap.resize(bitmapByte + 1, 0);
    }

    if (isSyncMark)
    {
        _clockBitmap[bitmapByte] |= (1 << bitmapBit);
    }
}

FmDecoder::FmDecoder()
    : _bitCount(0)
    , _currentByte(0)
    , _currentClockPattern(0)
{
}

void FmDecoder::reset()
{
    _bytes.clear();
    _clockBitmap.clear();
    _bitCount = 0;
    _currentByte = 0;
    _currentClockPattern = 0;
}

void FmDecoder::decode(const uint8_t* bitCells, size_t count)
{
    _bytes.reserve(_bytes.size() + count / 16);
    for (size_t i = 0; i + 1 < count; i += 2)
    {
        processBitPair(bitCells[i], bitCells[i + 1]);
    }
}

void FmDecoder::decode(const std::vector<uint8_t>& bitCells)
{
    decode(bitCells.data(), bitCells.size());
}

void FmDecoder::processBitPair(uint8_t clockBit, uint8_t dataBit)
{
    _currentClockPattern = static_cast<uint8_t>((_currentClockPattern << 1) | (clockBit & 1));
    _currentByte = static_cast<uint8_t>((_currentByte << 1) | (dataBit & 1));
    _bitCount++;

    if (_bitCount >= 8)
    {
        emitByte(_currentByte, _currentClockPattern);
        _bitCount = 0;
        _currentByte = 0;
        _currentClockPattern = 0;
    }
}

void FmDecoder::emitByte(uint8_t byte, uint8_t clockPattern)
{
    _bytes.push_back(byte);

    bool isSyncMark = false;

    if (byte == 0xFE && clockPattern == FM_CLOCK_IDAM)
    {
        isSyncMark = true;
    }
    else if (byte >= 0xF8 && byte <= 0xFB && clockPattern == FM_CLOCK_DAM)
    {
        isSyncMark = true;
    }

    size_t byteIndex = _bytes.size() - 1;
    size_t bitmapByte = byteIndex / 8;
    size_t bitmapBit = byteIndex % 8;

    if (bitmapByte >= _clockBitmap.size())
    {
        _clockBitmap.resize(bitmapByte + 1, 0);
    }

    if (isSyncMark)
    {
        _clockBitmap[bitmapByte] |= (1 << bitmapBit);
    }
}

} // namespace flux
} // namespace fdc
