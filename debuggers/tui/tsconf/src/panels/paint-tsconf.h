// paint-tsconf.h - fork band painters: PC history (§5.1) + TSConf board (§5.2).
//
// The board is a faithful C++ port of the widget engine in the normative
// renderer (unreal_dbg_render.py Canvas/Control, themselves ports of
// dbg_canvas/dbg_control in the pentevo fork). Layout table, tab stops,
// attribute constants and the original's display quirks (LCK128 shows S_EN,
// inverted W0_RAM/W0_MAP, "4:" labels in CacheConfig) are preserved exactly.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "backend/debugger-backend.h"
#include "screen/textscreen.h"

namespace dbg {

// TSConf widget colours (TDD-DBG-02 §5.2 / dbgwidgets).
constexpr uint8_t kWRegval = 0x51;      // "=XX" header register value
constexpr uint8_t kWBits = 0x04;        // bit-number column
constexpr uint8_t kWEq = 0x04;          // "=" column
constexpr uint8_t kWBitsActive = 0x14;  // ditto, inside the selected control
constexpr uint8_t kWEqActive = 0x14;
constexpr uint8_t kWLedon = 0x50;       // lit LED

// One placed board control, absolute screen coordinates (for mouse
// hit-testing; the selection is visual-only per §5.3).
struct TsControlRect {
    std::string name;
    int x = 0;
    int y = 0;  // title row; content occupies y+1 .. y+h
    int h = 0;
};

// PC history panel at (80,0): up to 28 "%02X:%04X" rows in W_OTHER, newest
// first, title "PC hist" at (80,0), frame around (80,1,7,28).
void PaintPcHistory(TextScreen& s, IDebuggerBackend& be);

// TSConf board at base x=88: 16 framed controls in 3 columns (base x 88/111/
// 134, 22-wide content, one-row gutters). Returns the placed control rects;
// empty when the backend serves no TSConf block (board band stays blank).
std::vector<TsControlRect> PaintTsconfBoard(TextScreen& s, IDebuggerBackend& be,
                                            const std::string& activeControl);

}  // namespace dbg
