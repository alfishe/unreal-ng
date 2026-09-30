#pragma once

/// @file profigeometry.h
/// @brief Profi 512x240 hi-res: frame, window, memory layout and palette,
/// shared by ScreenProfi and ProfiVideoMapper (PLAN #42 design §4.2).
/// Sources: UnrealSpeccy draw_profi, ZXMAK2 ProfiRenderer, Xpeccy (see ScreenProfi).

#include <cstdint>

namespace ProfiGeometry
{
constexpr uint32_t kTStatesPerLine = 224;
constexpr uint32_t kVSyncVBlankLines = 24;
constexpr uint32_t kVisibleLines = 288;
constexpr uint32_t kPaperStartT = 24;       // 48 px left border at 2 px/T
constexpr uint32_t kPaperTStates = 128;     // 512 px at 4 px/T
constexpr uint32_t kPaperEndT = kPaperStartT + kPaperTStates;
constexpr uint32_t kVisibleEndT = kPaperEndT + 24;
constexpr uint32_t kScreenLines = 240;
constexpr uint32_t kWidth = 512;

constexpr uint8_t PixelPage(uint8_t p7FFD) { return (p7FFD & 0x08) ? 6 : 4; }
constexpr uint16_t AttrPage(uint8_t p7FFD, uint16_t ramMask) { return static_cast<uint16_t>(((p7FFD & 0x08) ? 0x3A : 0x38) & ramMask); }

/// Byte of line v (0..239), byte index b (0..63, two per 16-px cell): the ZX
/// line layout plus the column, and the FIRST byte of each cell at +0x2000
constexpr uint16_t ByteOffset(uint32_t v, uint32_t byteIndex)
{
    const uint32_t cell = byteIndex / 2;
    const uint32_t offset = ((v & 0x07) << 8) | ((v & 0x38) << 2) | ((v & 0xC0) << 5) | cell;
    return static_cast<uint16_t>(offset | ((byteIndex & 1) ? 0x0000 : 0x2000));
}

/// Inverse of ByteOffset; false when the offset holds no visible line
constexpr bool DecodeByteOffset(uint16_t offset, uint32_t& v, uint32_t& byteIndex)
{
    const uint32_t base = offset & 0x1FFF;
    v = ((base >> 8) & 0x07) | ((base >> 2) & 0x38) | ((base >> 5) & 0xC0);
    byteIndex = (base & 0x1F) * 2 + ((offset & 0x2000) ? 0 : 1);
    return v < kScreenLines;
}

/// Colour index of an attribute byte: b7 paper bright, b6 ink bright, b5:3 paper, b2:0 ink; no flash
constexpr uint8_t AttrColourIndex(uint8_t attr, bool ink)
{
    return ink ? static_cast<uint8_t>((attr & 0x07) | ((attr & 0x40) >> 3))
               : static_cast<uint8_t>(((attr >> 3) & 0x07) | ((attr & 0x80) >> 4));
}

/// The monochrome option (Profi 3.xx): ink = border colour, paper = its inverse
constexpr uint8_t MonochromeAttr(uint8_t pFE)
{
    return static_cast<uint8_t>((pFE & 0x07) | (((pFE & 0x07) ^ 0x07) << 3));
}

/// Palette entry (9-bit GGGRRRBBB) -> ABGR
constexpr uint32_t PaletteColour(uint16_t raw)
{
    const uint32_t g = ((raw >> 6) & 0x07) * 255 / 7;
    const uint32_t r = ((raw >> 3) & 0x07) * 255 / 7;
    const uint32_t b = (raw & 0x07) * 255 / 7;
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}
} // namespace ProfiGeometry
