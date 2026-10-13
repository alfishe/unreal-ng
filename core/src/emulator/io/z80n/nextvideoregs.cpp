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

void NextVideoRegs::Reset(bool palettes)
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
    _ulapMode = 0;
    _ulapIndex = 0;
    _ulaNext = 0x07;
    _portFf = 0;
    _port123b = 0;
    _l2Offset = 0;
    _shadowAlias = false;
    _haveFirst = false;
    _first = 0;  // nr_stored_palette_value
    if (!palettes)
        return;
    // The RTL's palette RAMs power on at zero (dpram2 has no init); the state the program sees is what the boot ROM + TBBLUE.FW leave,
    // measured by running the real firmware from zeroed palettes (2026-10-13): the ULA palettes (0, 4) hold the 16 ULA colours repeated
    // through all 256 entries, the Layer 2 and sprite palettes (1, 2, 5, 6) colour = index, and the tilemap palettes (3, 7) are never
    // written by it: still zero
    for (unsigned p = 0; p < kPalettes; p++)
        for (unsigned i = 0; i < 256; i++)
        {
            if ((p & 3) == 0)
                _palette[p][i] = Nine(kUlaColours[i & 15]);
            else if ((p & 3) == 3)
                _palette[p][i] = 0;
            else
                _palette[p][i] = Nine(static_cast<uint8_t>(i));
        }
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
    // zxnext.vhd 4920: the priority bits (7:6) of the second byte are stored with every palette entry; only Layer 2 uses bit 7
    if (value & 0x80)
        entry |= layer2 ? 0xA00 : 0x800;
    if (value & 0x40)
        entry |= 0x400;
    _palette[Selected()][_index] = entry;
    _haveFirst = false;
    Advance();
}

void NextVideoRegs::WriteUlaPlusPalette(uint8_t value)
{
    // cpu_requester_dat of zxnext.vhd: GGGRRRBB -> the palette's RRRGGGBB
    const uint8_t rrrgggbb = static_cast<uint8_t>(((value >> 2) & 7) << 5 | ((value >> 5) & 7) << 2 | (value & 3));
    _palette[(_control & 0x40) ? 4 : 0][192 + _ulapIndex] = Nine(rrrgggbb);
}

uint8_t NextVideoRegs::ReadUlaPlusPalette() const
{
    // nr_ulatm_palette_dat(5:3) & (8:6) & (2:1): G, R, the top two blue bits
    const uint16_t e = _palette[(_control & 0x40) ? 4 : 0][192 + _ulapIndex];
    return static_cast<uint8_t>(((e >> 3) & 7) << 5 | ((e >> 6) & 7) << 2 | ((e >> 1) & 3));
}

uint8_t NextVideoRegs::ReadPaletteValue8() const
{
    return static_cast<uint8_t>(_palette[Selected()][_index] >> 1);
}

uint8_t NextVideoRegs::ReadPaletteValue9() const
{
    // zxnext.vhd 6048: nr_palette_dat(10:9) & "00000" & nr_palette_dat(0): the stored priority bits 7:6 and the low blue bit
    const uint16_t entry = _palette[Selected()][_index];
    return static_cast<uint8_t>((entry & 1) | ((entry & 0x800) ? 0x80 : 0) | ((entry & 0x400) ? 0x40 : 0));
}
