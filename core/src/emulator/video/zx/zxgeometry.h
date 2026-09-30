#pragma once

/// @file zxgeometry.h
/// @brief The Sinclair screen layout and palette, shared by the ZX renderer
/// (ScreenZX), the AlCo renderer (ZX addressing) and the video mappers
/// (PLAN #42, video-debug-translation design §4.2: one set of constants for
/// the renderer and the debug description of it).

#include <cstdint>

namespace ZxGeometry
{
constexpr uint16_t kWidth = 256;
constexpr uint16_t kHeight = 192;
constexpr uint16_t kColumns = 32;        // bytes per pixel line
constexpr uint16_t kPixelBytes = 0x1800;  // 6144: the bitmap
constexpr uint16_t kAttrBase = 0x1800;    // attributes follow the bitmap
constexpr uint16_t kAttrBytes = 0x300;    // 768

/// Byte of pixel line y (0..191), column col (0..31): the interleaved layout
/// y = TT RRR PPP (third, char row, pixel row) -> offset 0 TT PPP RRR CCCCC
constexpr uint16_t PixelOffset(uint8_t y, uint8_t col)
{
    return static_cast<uint16_t>(((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2) | (col & 0x1F));
}

/// Attribute of the 8x8 cell holding pixel line y, column col
constexpr uint16_t AttrOffset(uint8_t y, uint8_t col)
{
    return static_cast<uint16_t>(kAttrBase + (y >> 3) * kColumns + (col & 0x1F));
}

/// Inverse of PixelOffset for offsets below kPixelBytes
constexpr void DecodePixelOffset(uint16_t offset, uint8_t& y, uint8_t& col)
{
    y = static_cast<uint8_t>(((offset >> 5) & 0xC0) | ((offset >> 8) & 0x07) | ((offset >> 2) & 0x38));
    col = static_cast<uint8_t>(offset & 0x1F);
}

/// Pentagon 384x304 overscan (M_P384): the framebuffer starts right after
/// vsync (16 lines of vblank more on top) and extends 32 pixels to the right;
/// the paper is 16 lines lower in the framebuffer than in the other ZX modes
constexpr int kP384ExtraTopLines = 16;
constexpr int kP384ExtraPixels = 32;

/// ABGR colours (little-endian RGBA8888), [bright][GRB colour]
inline constexpr uint32_t kPalette[2][8] = {
    {0xFF000000, 0xFFC72200, 0xFF1628D6, 0xFFC733D4, 0xFF25C500, 0xFFC9C700, 0xFF2AC8CC, 0xFFCACACA},
    {0xFF000000, 0xFFFB2B00, 0xFF1C33FF, 0xFFFC40FF, 0xFF2FF900, 0xFFFEFB00, 0xFF36FCFF, 0xFFFFFFFF},
};

/// 4-bit colour index {bright, G, R, B}
constexpr uint32_t Colour(uint8_t index)
{
    return kPalette[(index >> 3) & 1][index & 7];
}

/// Colour index of an attribute byte's ink (pixel set) or paper (clear).
/// The ZX renderer does not blink FLASH (it keeps the bit, see ScreenZX)
constexpr uint8_t AttrColourIndex(uint8_t attr, bool ink)
{
    return static_cast<uint8_t>((ink ? (attr & 0x07) : ((attr >> 3) & 0x07)) | ((attr & 0x40) >> 3));
}
} // namespace ZxGeometry
