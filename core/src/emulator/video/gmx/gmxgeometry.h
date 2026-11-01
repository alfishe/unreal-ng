#pragma once

/// @file gmxgeometry.h
/// @brief Scorpion GMX 640x200 extended mode (#7EFD bit 3): frame, window, memory layout and colours, shared by
/// ScreenGmx and GmxVideoMapper (GMX design section 2.5, Q4, Q11, Q12, Q22).
/// Sources: the 1998 manual, MAME scorpiongmx_state, the Unreal Speccy (zx-evo) GMX drawer, pico-speccy.

#include <cstdint>

namespace GmxGeometry
{
constexpr uint32_t kTStatesPerLine = 224;
constexpr uint32_t kVSyncVBlankLines = 24;  // the Scorpion frame: 312 lines, 288 visible
constexpr uint32_t kVisibleLines = 288;
constexpr uint32_t kPaperStartT = 24;       // 48 px left border at 2 px/T
constexpr uint32_t kPaperTStates = 128;     // 640 px at 5 px/T: the dot clock of the 256-pixel line, doubled and more
constexpr uint32_t kPaperEndT = kPaperStartT + kPaperTStates;
constexpr uint32_t kVisibleEndT = kPaperEndT + 24;
constexpr uint32_t kPixelsPerT = 5;
constexpr uint32_t kScreenLines = 200;
constexpr uint32_t kWidth = 640;
constexpr uint32_t kBytesPerLine = 80;
constexpr uint32_t kScreenBytes = kBytesPerLine * kScreenLines;  // 16000: the scroll wraps here

/// Pixel page #39 (#3B with #7FFD bit 3), attribute page 64 above it (#79 / #7B)
constexpr uint16_t PixelPage(uint8_t p7FFD) { return (p7FFD & 0x08) ? 0x3B : 0x39; }
constexpr uint16_t AttrPage(uint8_t p7FFD) { return static_cast<uint16_t>(PixelPage(p7FFD) + 0x40); }

/// The scroller's start address in bytes: #7CFD bits 5-0 high, #7AFD bits 7-4 (the low four are 0 in hardware)
constexpr uint32_t ScrollStart(uint8_t p7AFD, uint8_t p7CFD)
{
    return (static_cast<uint32_t>(p7CFD & 0x3F) << 8) | (p7AFD & 0xF0);
}

/// Byte of line y (0..199), column x (0..79) after the scroll: pixels and attributes at the same offset
constexpr uint16_t ByteOffset(uint32_t y, uint32_t column, uint32_t start)
{
    return static_cast<uint16_t>((y * kBytesPerLine + column + start) % kScreenBytes);
}

/// Inverse of ByteOffset for a given start; false when the offset holds no visible byte
constexpr bool DecodeByteOffset(uint16_t offset, uint32_t start, uint32_t& y, uint32_t& column)
{
    if (offset >= kScreenBytes)
        return false;
    const uint32_t linear = (offset + kScreenBytes - (start % kScreenBytes)) % kScreenBytes;
    y = linear / kBytesPerLine;
    column = linear % kBytesPerLine;
    return true;
}

/// Colour index {bright, G, R, B} of a standard attribute byte: ink 0-2, paper 3-5, bright 6 (both), flash 7
constexpr uint8_t AttrColourIndex(uint8_t attr, bool ink)
{
    const uint8_t colour = ink ? static_cast<uint8_t>(attr & 0x07) : static_cast<uint8_t>((attr >> 3) & 0x07);
    return static_cast<uint8_t>(colour | ((attr & 0x40) >> 3));
}

/// A pixel after the flash: bit 7 inverts it in the flash phase
constexpr bool Ink(uint8_t pixels, uint32_t bit, uint8_t attr, bool flashPhase)
{
    const bool set = ((pixels >> (7 - bit)) & 1) != 0;
    return ((attr & 0x80) && flashPhase) ? !set : set;
}
}  // namespace GmxGeometry
