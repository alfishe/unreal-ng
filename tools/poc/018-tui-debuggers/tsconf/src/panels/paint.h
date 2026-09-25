// paint.h - frame painters over the golden §2 geometry (TSConf fork).
//
// PaintTsconfFrame fills the whole 157x30 grid from live backend data:
//   left  x0..33   regs (y0..5) + trace (y6..26)
//   mid   x34..70  watches (y1..13) + memory dump (y14..26)
//   right x72..78  ports / beta128 / stack / pages (y1..26)
//   x80..86        PC history (§5.1, fork panel)
//   x88..156       TSConf register board, 16 controls / 3 columns (§5.2)
//   y28            time delta + AY dump
// The classic-band painters are the v0 approximation inherited from PoC 018a;
// the fork band (PC hist + board + banks window) is golden-exact.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "backend/debugger-backend.h"
#include "screen/textscreen.h"

namespace dbg {

enum class WidgetId {
    Regs = 0,
    Trace = 1,
    Memory = 2,
    Pages = 3
};

struct UiState {
    WidgetId activeWidget = WidgetId::Trace;
    int activeWindow = 1;         // legacy: 0 = regs, 1 = trace, 2 = memory, 3 = banks

    // Trace
    uint16_t traceTop = 0x8000;   // first address shown in the trace window
    uint16_t traceCurs = 0x8011;  // trace cursor address

    // Memory
    uint16_t memTop = 0xC000;     // first address of the memory dump
    uint16_t memCurs = 0xC003;    // cursor cell address of the memory dump
    bool memAscii = false;        // true = ASCII edit mode, false = Hex edit mode

    // Regs cursor (0..25 matching kRegLayout)
    int regsCurs = 0;

    // Watches cursor & user watch addresses
    int watchCurs = 0;            // 0..12 (0..9 regs, 10..12 user watches)
    std::array<uint16_t, 3> userWatches = {0x4000, 0x8000, 0xC000};

    // Passive panels (read-only)
    int portsCurs = 0;            // 0..3 (FE, 7FFD, cmos, EFF7)
    int betaCurs = 0;             // 0..4 (CD, STAT, SECT, T, S)
    int pagesCurs = 0;            // 0..3 (slots 0..3)

    // Stack (editable)
    int stackCurs = 0;            // 0..9 (-2, SP, +2..+10)

    // Bottom bar (Time Delta & AY)
    int bottomField = 0;          // 0 = Time Delta, 1 = AY registers
    int ayCurs = 0;               // 0..15 AY register index

    // In-place editing buffer
    bool isEditing = false;
    std::string editBuf;

    // TSConf fork UI state
    int selBank = 0;              // pages-window cursor row
    bool showBank = false;        // bank cursor visible (BANKS focused)
    std::string tsActive;         // visually selected board control name (§5.3)
    int tsControlIdx = 0;         // 0..15 active board control index
};

void PaintTsconfFrame(TextScreen& s, IDebuggerBackend& be, const UiState& ui);

// Individual painters (composed by PaintTsconfFrame).
void PaintRegs(TextScreen& s, IDebuggerBackend& be, const UiState& ui);      // y0..5
void PaintTrace(TextScreen& s, IDebuggerBackend& be, const UiState& ui);     // y6..26
void PaintSide(TextScreen& s, IDebuggerBackend& be, const UiState& ui);      // mid+right
void PaintBottomBar(TextScreen& s, IDebuggerBackend& be, const UiState& ui);  // y28

}  // namespace dbg
