#pragma once

/// @file atmgeometry.h
/// @brief ATM Turbo 2+ / ATM3 / ZX-Evo extended video modes: frame, window and
/// memory layout, shared by ScreenAtm and AtmVideoMapper (PLAN #42 design
/// §4.2). Sources: reference renderer dxr_atm0/2/6.cpp, ZXMAK2 Atm*Renderer,
/// ZX-Evo BaseConf video_sync_h.v (see ScreenAtm).

#include <cstdint>

namespace AtmGeometry
{
constexpr uint32_t kTStatesPerLine = 224;
constexpr uint32_t kVSyncVBlankLines = 24;  // 16 vsync + 8 vblank before the visible area
constexpr uint32_t kVisibleLines = 288;

/// ATM Turbo 2 v4.50: a 308-line raster - 4 vertical-blank lines fewer than the 312 above,
/// visible area unchanged. Inferred from the system ROM's frame-timing protection, which
/// corrupts typed keys when its frame measure misses the window calibrated on the board:
/// docs/inprogress/2026-10-01-atm450/frame-timing-protection.md
constexpr uint32_t kAtm450DroppedBlankLines = 4;
constexpr uint32_t kAtm450Frame = kTStatesPerLine * (kVSyncVBlankLines + kVisibleLines - kAtm450DroppedBlankLines);
/// INT moves with the dropped lines, so INT-to-first-paper stays 14395 T as on 7.10
constexpr uint32_t kAtm450IntStart = 1756 - kTStatesPerLine * kAtm450DroppedBlankLines;
static_assert(kAtm450Frame == 68992 && kAtm450IntStart == 860, "ATM450 raster");
constexpr uint32_t kScreenLines = 200;
constexpr uint32_t kBytesPerLine = 40;      // linear planes, 40 bytes per line
constexpr uint32_t kPlaneHigh = 0x2000;     // second plane of a page

/// Screen window in T of the line: the 320-dot window starts 16 T before the
/// ZX paper and ends 16 T after it
constexpr uint32_t kScreenStartT = 8;
constexpr uint32_t kScreenEndT = kScreenStartT + 160;

/// Text (ATMTX): row r at kTextBase + 64 r in both planes
constexpr uint32_t kTextBase = 0x1C0;
constexpr uint32_t kTextRowStride = 64;
constexpr uint32_t kTextColumns = 80;
constexpr uint32_t kTextRows = 25;

/// Text linear (ATMTL, ZX-Evo): dedicated page, even / odd columns apart
constexpr uint32_t kTlCodeEven = 0x01C0;
constexpr uint32_t kTlCodeOdd = 0x11C0;
constexpr uint32_t kTlAttrEven = 0x31C0;
constexpr uint32_t kTlAttrOdd = 0x21C0;

constexpr uint8_t VideoPage(uint8_t p7FFD) { return (p7FFD & 0x08) ? 7 : 5; }
constexpr uint8_t AltPage(uint8_t videoPage) { return static_cast<uint8_t>(videoPage - 4); }
constexpr uint8_t TextLinearPage(uint8_t videoPage) { return videoPage == 5 ? 8 : 10; }

/// Colour index of an ATM attribute byte: bit 6 = ink bright, bit 7 = paper bright, no flash
constexpr uint8_t AttrColourIndex(uint8_t attr, bool ink)
{
    return ink ? static_cast<uint8_t>((attr & 0x07) | ((attr & 0x40) >> 3))
               : static_cast<uint8_t>(((attr & 0x38) >> 3) | ((attr & 0x80) >> 4));
}

/// 16-colour pixel pair of a plane byte: left = {b6,b2,b1,b0}, right = {b7,b5,b4,b3}
constexpr uint8_t PairColourIndex(uint8_t bt, bool right)
{
    return right ? static_cast<uint8_t>(((bt >> 3) & 0x07) | ((bt & 0x80) >> 4))
                 : static_cast<uint8_t>((bt & 0x07) | ((bt & 0x40) >> 3));
}
} // namespace AtmGeometry
