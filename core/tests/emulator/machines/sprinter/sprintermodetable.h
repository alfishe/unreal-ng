#pragma once

/// @file sprintermodetable.h
/// @brief Mode tables as the Sprinter's software writes them, for the classifier tests (core and unreal-qt).

#include <cstdint>

#include "emulator/video/sprinter/sprintervideoram.h"

namespace SprinterModeTable
{
/// Every square of the 40 x 32 picture of `page` gets Mode0..Mode2 = m0, m1, m2
inline void Fill(SprinterVideoRam& vram, uint8_t page, uint8_t m0, uint8_t m1 = 0, uint8_t m2 = 0)
{
    for (uint8_t b = 0; b < 32; b++)
    {
        for (uint8_t a = 0; a < 40; a++)
        {
            const uint32_t at = SprinterVideoRam::ModeAddress(a, b, page);
            vram.Write(at, m0);
            vram.Write(at + 1, m1);
            vram.Write(at + 2, m2);
        }
    }
}

/// The table the launcher (C:\ZX\SPECTRUM.EXE) writes for the Spectrum mode, as dumped from the 128 menu on
/// DSS 1.71 / BIOS 3.06: border squares (#F8) around 32 x 24 ZX-40 squares from square (4, 4); the cell at
/// character row r, column c has m0 = #30 | (r / 8) << 6 (font block 0, 40 columns, the third), m1 = m2 =
/// (r % 8) << 5 | c (the low byte of its bitmap and attribute addresses)
inline void WriteSpectrumScreen(SprinterVideoRam& vram, uint8_t page)
{
    Fill(vram, page, 0xF8);
    for (uint8_t r = 0; r < 24; r++)
    {
        for (uint8_t c = 0; c < 32; c++)
        {
            const uint32_t at = SprinterVideoRam::ModeAddress(static_cast<uint8_t>(4 + c), static_cast<uint8_t>(4 + r), page);
            const uint8_t cell = static_cast<uint8_t>(((r & 7) << 5) | c);
            vram.Write(at, static_cast<uint8_t>(0x30 | ((r >> 3) << 6)));
            vram.Write(at + 1, cell);
            vram.Write(at + 2, cell);
        }
    }
}
}  // namespace SprinterModeTable
