#pragma once

/// @file p2666geometry.h
/// @brief Pentagon 2.666 LE (MM_P2666) raster: the board's visible window and paper positions in the emulator's
/// 224 T x 320 line frame. Sources: zx_main.vhd at e808402 (docs/inprogress/2026-11-11-pentagon-2666-le,
/// hardware-reference.md §1.3, §6).
///
/// The board draws a 896 x 640 VGA raster, one ZX pixel = 2 x 2 VGA pixels, so one ZX line (224 T) is two VGA lines.
/// The ZX and Pentagon 16 colour modes fetch a ZX line into a line buffer during one VGA line pair and show it during
/// the next (multic_buf*); the border is sampled into a buffer (border_buf*) the same way. The emulator draws at the
/// fetch time: the line delay only moves the picture one ZX line down on the monitor.
///
/// Fetch time (zx_main.vhd:420-427, 474-483, 252-257): ZX line Y's column c is read on the VGA line pair
/// 2 (Y + 64) at pixel_c = 368 + 32 c (c <= 16 on the first VGA line of the pair, c >= 17 on the second), i.e.
/// 1472 + 128 c mainclk edges into the pair = 46 + 4 c T. The decoder's frame origin (PortDecoderP2666::
/// kFrameOriginShift) puts that on line 80 + Y, T 30 + 4 c of the emulator frame, where the renderer latches the cell.
/// Checked against zx_main itself (tools/poc/051-p2666-hdl-timing-oracle/video.sh): the bytes zx_main keeps are read
/// at T 30.375 + 4 c (pixel) and 30.25 + 4 c (attribute) of line 80 + Y, the INT strobe starts at T 71640.
///
/// Visible window (zx_main.vhd:447-454, 238-247): VGA pixels 74..847 and lines 17..607 are shown; the ZX paper is
/// shown at VGA pixels 193..704 and lines 130..513, the ATM paper at 129..768 and 128..527 (:325-331). In ZX pixels
/// and fetch lines (a ZX line is shown one line after its fetch): border left 59.5, right 71.5, top 56.5, bottom
/// 47.5 around the ZX paper, 387 x 295.5. The emulator draws whole T and whole lines: left 60, right 72 (the half
/// pixels shown whole), top 56, bottom 48 (the half line at the top shown at the bottom, which keeps the 320-line
/// frame with the paper on line 80) - 388 x 296.
///
/// The ATM modes have no line buffer (each VGA line fetches again): their paper starts at the ZX paper's line and
/// 16 T (32 ZX pixels) left of it, as the VHDL's 64-VGA-pixel shift (:329).

#include <cstdint>

namespace P2666Geometry
{
constexpr uint16_t kTStatesPerLine = 224;
constexpr uint16_t kFrameLines = 320;

constexpr uint16_t kBorderLeft = 60;     ///< ZX pixels left of the ZX paper (59.5 on the board)
constexpr uint16_t kBorderRight = 72;    ///< ZX pixels right of the ZX paper (71.5)
constexpr uint16_t kBorderTop = 56;      ///< lines above the ZX paper (56.5)
constexpr uint16_t kBorderBottom = 48;   ///< lines below the ZX paper (47.5)
constexpr uint16_t kVisibleWidth = kBorderLeft + 256 + kBorderRight;    // 388
constexpr uint16_t kVisibleHeight = kBorderTop + 192 + kBorderBottom;   // 296
constexpr uint16_t kVSyncLines = 16;
constexpr uint16_t kVBlankLines = kFrameLines - kVisibleHeight - kVSyncLines;  // 8

/// The ZX paper's first line and T in the emulator frame (the cell fetch, see above)
constexpr uint16_t kPaperLine = kVSyncLines + kVBlankLines + kBorderTop;  // 80
constexpr uint16_t kPaperStartT = kBorderLeft / 2;                        // 30

/// ATM modes: the 320 / 640 dot window 16 T left of the ZX paper, the same lines as the ZX paper
constexpr uint16_t kAtmShiftT = 16;
constexpr uint16_t kAtmWindowStartT = kPaperStartT - kAtmShiftT;  // 14
constexpr uint16_t kAtmWindowT = 160;
constexpr uint16_t kAtmBorderRightT = kBorderLeft / 2 + 128 + kBorderRight / 2 - kAtmWindowStartT - kAtmWindowT;  // 20
constexpr uint16_t kAtmLines = 200;

/// ZX and Pentagon 16 colour modes: a border write (FE latched on edge E, the same T as the emulator's port write)
/// shows 3.25 T later than the paper mapping would put it: the border_buf read-out at pixel_c / 2 - 10 (zx_main.vhd
/// :517, :528), measured in ghdl (POC 051 video.sh, FE writes in the top, left and right border). The renderer draws
/// the write T and kBorderHoldT more with the old colour: the new one from T + 3 (2 T + 6 pixels, the board 2 T + 6.5).
/// The plain Pentagon holds 1 T.
constexpr uint32_t kBorderHoldT = 2;

static_assert(kVisibleWidth == 388 && kVisibleHeight == 296 && kVBlankLines == 8 && kPaperLine == 80, "P2666 window");
static_assert(kAtmWindowStartT + kAtmWindowT + kAtmBorderRightT == kVisibleWidth / 2, "ATM window inside the visible line");
}  // namespace P2666Geometry
