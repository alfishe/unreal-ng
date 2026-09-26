// textscreen.h - the (char, attr) text grid of the monitor (TDD-DBG-01 §1).
//
// The whole debugger screen is a pure function of emulator state + UI state,
// painted into this grid. Every repaint starts by filling all cells with
// char 0xB1 and attribute 0x50 (BACKGR), then panels overwrite their cells.
// Frames are collected during painting and drawn last, OVER the text, as
// 1-px lines in pixel back-ends or gutter box-drawing glyphs in terminals.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dbg {

// Named attributes (TDD-DBG-01 §1.6). Attribute byte: paper<<4 | ink.
constexpr uint8_t kBackgr = 0x50;       // cyan paper, black ink (fill)
constexpr uint8_t kWNorm = 0x07;        // unfocused focusable window
constexpr uint8_t kWSel = 0x17;         // focused window
constexpr uint8_t kWCurs = 0x30;        // cursor cell(s) in focused window
constexpr uint8_t kWTitle = 0x59;       // window titles
constexpr uint8_t kWOther = 0x40;       // passive info panels
constexpr uint8_t kWOtheroff = 0x47;    // labels in passive panels / disabled
constexpr uint8_t kWAynum = 0x4F;       // AY register index digit
constexpr uint8_t kWAyon = 0x41;        // AY value, latched register
constexpr uint8_t kWAyoff = 0x40;       // AY value, other registers
constexpr uint8_t kWBank = 0x40;        // page name, r/w mapping
constexpr uint8_t kWBankro = 0x41;      // page name, read != write mapping
constexpr uint8_t kWDihalt1 = 0x1A;     // DiHALT, focused regs window
constexpr uint8_t kWDihalt2 = 0x0A;     // DiHALT, unfocused regs window
constexpr uint8_t kWTracepos = 0x70;    // trace line at PC
constexpr uint8_t kW48k = 0x20;         // 7FFD line under 48K lock / DOS
constexpr uint8_t kWInputcur = 0x60;    // cursor cell of an input field
constexpr uint8_t kWInputbg = 0x40;     // other cells of an input field
constexpr uint8_t kTraceJinfoCursFg = 0x0D;   // jump info ink, cursor line
constexpr uint8_t kTraceJinfoNocursFg = 0x02; // jump info ink, otherwise
constexpr uint8_t kTraceJarrowFg = 0x0D;      // ◄ marker on target line
constexpr uint8_t kFrameColor = 0x01;         // panel frame -> bright blue
constexpr uint8_t kFFrameFrame = 0x04;        // dialog frame -> bright green
constexpr uint8_t kFFrameInside = 0x50;       // dialog body
constexpr uint8_t kFFrameError = 0x52;        // dialog error text
constexpr uint8_t kFrmHeader = 0xD0;          // dialog title bar
constexpr uint8_t kMenuInside = 0x70;         // menu body / enabled item
constexpr uint8_t kMenuHeader = 0xF0;         // menu title
constexpr uint8_t kMenuCursor = 0xE0;         // selected menu item
constexpr uint8_t kMenuItemDis = 0x7A;        // disabled menu item
constexpr uint8_t kAttrTransparent = 0xFF;    // "do not draw this cell"

// Derived attribute transforms (§1.6).
constexpr uint8_t ChangedInk(uint8_t attr) { return attr | 0x08; }               // bright ink
constexpr uint8_t BpxLineAttr(uint8_t attr) { return (attr & ~0x07) | 0x02; }    // red ink

struct ScreenRect {
    int x;
    int y;
    int w;
    int h;
};

struct FrameEntry {
    int x;
    int y;
    int w;
    int h;
    uint8_t color;
};

class TextScreen {
public:
    TextScreen(int width, int height, int frameCapacity = 20);

    int Width() const { return width_; }
    int Height() const { return height_; }

    // Clear to the fill state (char 0xB1, attr 0x50) and drop all frames.
    void Clear();

    // Print a string of raw byte codes at (x, y) with one attribute.
    // Codes are monitor character codes (CP437/CP866 space), not Unicode.
    void Tprint(int x, int y, const std::string& bytes, uint8_t attr);
    void Tprint(int x, int y, const char* bytes, uint8_t attr) {
        Tprint(x, y, std::string(bytes), attr);
    }

    // Keep the paper nibble already in the cell, set the ink nibble (§1.6).
    void TprintFg(int x, int y, const std::string& bytes, uint8_t ink);

    void SetAttr(int x, int y, uint8_t attr);
    void SetChar(int x, int y, uint8_t code, uint8_t attr);
    uint8_t CharAt(int x, int y) const;
    uint8_t AttrAt(int x, int y) const;

    // Fill a rectangle with spaces in one attribute.
    void FillRect(int x, int y, int w, int h, uint8_t attr);

    // Record a frame AROUND the content rect (x,y,w,h), colour index c
    // (drawn as the bright variant). Capacity limited; oldest dropped.
    void Frame(int x, int y, int w, int h, uint8_t color);

    // Dialog frame: clears the frame list first (§1.5 quirk: while an
    // in-screen dialog is open only its frame is visible), fills the body
    // with spaces in kFFrameInside and records a bright-green frame.
    void FilledFrame(int x, int y, int w, int h);

    const std::vector<FrameEntry>& Frames() const { return frames_; }

private:
    bool InBounds(int x, int y) const;

    int width_;
    int height_;
    int frameCapacity_;
    std::vector<uint8_t> chars_;
    std::vector<uint8_t> attrs_;
    std::vector<FrameEntry> frames_;
};

}  // namespace dbg
