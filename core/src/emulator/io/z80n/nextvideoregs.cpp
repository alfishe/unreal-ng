#include "stdafx.h"

#include "nextvideoregs.h"

namespace
{
/// The ULA's default colours (RRRGGGBB): normal and bright, ink and paper alike
const uint8_t kUlaColours[16] = {0x00, 0x02, 0xA0, 0xA2, 0x14, 0x16, 0xB4, 0xB6, 0x00, 0x03, 0xE0, 0xE7, 0x1C, 0x1F, 0xFC, 0xFF};

uint16_t Nine(uint8_t rrrgggbb)
{
    return static_cast<uint16_t>((rrrgggbb << 1) | ((rrrgggbb & 3) ? 1 : 0));
}
}  // namespace

void NextVideoRegs::Reset()
{
    for (unsigned w = 0; w < kClipWindows; w++)
    {
        _clip[w][0] = 0;
        _clip[w][1] = w == 3 ? 159 : 255;
        _clip[w][2] = 0;
        _clip[w][3] = w == 3 ? 255 : 191;
        _clipIndex[w] = 0;
    }
    _index = 0;
    _control = 0;
    _ulaNext = 0x07;
    _portFf = 0;
    _port123b = 0;
    _shadowAlias = false;
    _haveFirst = false;
    // power-on palettes: the ULA's own colours (ink and paper), the others colour = index
    for (unsigned p = 0; p < kPalettes; p++)
        for (unsigned i = 0; i < 256; i++)
            _palette[p][i] = ((p & 3) == 0 && i < 32) ? Nine(kUlaColours[i & 15]) : Nine(static_cast<uint8_t>(i));
}

void NextVideoRegs::WriteClip(unsigned window, uint8_t value)
{
    _clip[window][_clipIndex[window]] = value;
    _clipIndex[window] = static_cast<uint8_t>((_clipIndex[window] + 1) & 3);
}

void NextVideoRegs::WriteClipControl(uint8_t value)
{
    // bit 3 tilemap, 2 ULA, 1 sprites, 0 Layer 2 -> windows 3, 2, 1, 0
    for (unsigned w = 0; w < kClipWindows; w++)
        if (value & (1u << w))
            _clipIndex[w] = 0;
}

uint8_t NextVideoRegs::ReadClipControl() const
{
    return static_cast<uint8_t>((_clipIndex[3] << 6) | (_clipIndex[2] << 4) | (_clipIndex[1] << 2) | _clipIndex[0]);
}

void NextVideoRegs::Advance()
{
    if (!(_control & 0x80))
        _index++;
}

void NextVideoRegs::WritePaletteIndex(uint8_t value)
{
    _index = value;
    _haveFirst = false;
}

void NextVideoRegs::WritePaletteControl(uint8_t value)
{
    _control = value;
    _haveFirst = false;
}

void NextVideoRegs::WritePaletteValue8(uint8_t value)
{
    _palette[Selected()][_index] = Nine(value);
    _haveFirst = false;
    Advance();
}

void NextVideoRegs::WritePaletteValue9(uint8_t value)
{
    if (!_haveFirst)
    {
        _first = value;
        _haveFirst = true;
        return;
    }
    const bool layer2 = (Selected() & 3) == 1;
    uint16_t entry = static_cast<uint16_t>((_first << 1) | (value & 1));
    if (layer2 && (value & 0x80))
        entry |= 0x200;
    _palette[Selected()][_index] = entry;
    _haveFirst = false;
    Advance();
}

uint8_t NextVideoRegs::ReadPaletteValue8() const
{
    return static_cast<uint8_t>(_palette[Selected()][_index] >> 1);
}

uint8_t NextVideoRegs::ReadPaletteValue9() const
{
    const uint16_t entry = _palette[Selected()][_index];
    return static_cast<uint8_t>((entry & 1) | ((entry & 0x200) ? 0x80 : 0));
}
