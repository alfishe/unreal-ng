// test-main.cpp - micro test harness: golden fork-band compare against the
// oracle dumps (unreal_dbg_render.py -> tests/golden/tsconf*.txt) plus mock
// behavior checks for the fork additions (PC history ring, TSConf block).
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "backend/mock-backend.h"
#include "model/model.h"
#include "panels/paint-tsconf.h"
#include "panels/paint.h"
#include "screen/glyphs.h"
#include "screen/textscreen.h"
#include "ui/keymap.h"

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

std::vector<std::string> ReadLines(const std::string& path) {
    std::ifstream in(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

// Split one dump_text row into per-cell UTF-8 glyph strings (skip "%3d  ").
std::vector<std::string> DecodeCells(const std::string& line) {
    std::vector<std::string> cells;
    for (size_t i = 5; i < line.size();) {
        const unsigned char c = static_cast<unsigned char>(line[i]);
        size_t len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        cells.push_back(line.substr(i, len));
        i += len;
    }
    return cells;
}

// Split one dump_attr row into attribute bytes (skip "%3d  ").
std::vector<uint8_t> DecodeAttrs(const std::string& line) {
    std::vector<uint8_t> attrs;
    std::istringstream in(line.substr(5));
    std::string tok;
    while (in >> tok) attrs.push_back(static_cast<uint8_t>(std::stoul(tok, nullptr, 16)));
    return attrs;
}

// Compare a cell range of the painted screen with the golden dumps.
void CompareBand(const dbg::TextScreen& s, const std::vector<std::string>& text,
                 const std::vector<std::string>& attr, int x0, int x1, int y0, int y1,
                 const std::string& what) {
    for (int y = y0; y <= y1; ++y) {
        const std::vector<std::string> cells = DecodeCells(text[static_cast<size_t>(y) + 2]);
        const std::vector<uint8_t> attrs = DecodeAttrs(attr[static_cast<size_t>(y)]);
        for (int x = x0; x <= x1; ++x) {
            const std::string want = cells[static_cast<size_t>(x)];
            const std::string got = dbg::GlyphOf(s.CharAt(x, y));
            Check(got == want, what + " text @" + std::to_string(x) + "," + std::to_string(y) +
                                   " got '" + got + "' want '" + want + "'");
            Check(s.AttrAt(x, y) == attrs[static_cast<size_t>(x)],
                  what + " attr @" + std::to_string(x) + "," + std::to_string(y));
        }
    }
}

}  // namespace

int main() {
    const std::string goldenDir = std::string(DBGPOC_SOURCE_DIR) + "/tests/golden/";
    const std::vector<std::string> text = ReadLines(goldenDir + "tsconf.txt");
    const std::vector<std::string> attr = ReadLines(goldenDir + "tsconf_attr.txt");
    Check(text.size() == 32, "tsconf.txt row count");  // 2 header + 30 rows
    Check(attr.size() == 30, "tsconf_attr.txt row count");
    if (text.size() < 32 || attr.size() < 30) return 1;

    // -- golden fork band: PC history + TSConf board (x80..156, all rows) ----
    dbg::MockBackend be(dbg::CreateTestSetByName("golden-sample"));
    dbg::UiState ui;
    const dbg::UiStateSeed& seed = be.UiSeed();
    ui.traceTop = seed.traceTop;
    ui.traceCurs = seed.traceCurs;
    ui.memTop = seed.memTop;
    ui.memCurs = seed.memCurs;
    ui.activeWindow = seed.activeWindow;
    dbg::TextScreen screen(157, 30, 50);
    dbg::PaintTsconfFrame(screen, be, ui);
    CompareBand(screen, text, attr, 80, 156, 0, 29, "fork-band");
    // pages window fork variant (x72..78, title + 4 rows; rightmost col background)
    CompareBand(screen, text, attr, 72, 78, 22, 26, "banks");

    // -- board layout: 16 placed controls at the oracle coordinates ----------
    const std::vector<dbg::TsControlRect> rects = dbg::PaintTsconfBoard(screen, be, "");
    Check(rects.size() == 16, "board control count");
    struct Want {
        const char* name;
        int x, y, h;
    };
    const Want wants[16] = {
        {"vconfig", 88, 0, 6},    {"tsconfig", 88, 7, 6},      {"sysconfig", 88, 14, 2},
        {"cacheconfig", 88, 17, 4}, {"memconfig", 88, 22, 5},  {"bitmap", 111, 0, 3},
        {"tiles0", 111, 4, 3},   {"tiles1", 111, 8, 3},       {"palsel", 111, 12, 3},
        {"misc", 111, 16, 3},    {"fmaddr", 111, 20, 2},      {"mempages", 111, 23, 4},
        {"sprites", 134, 0, 1},  {"dma", 134, 2, 15},         {"interrupt", 134, 18, 5},
        {"intmask", 134, 24, 3},
    };
    for (size_t i = 0; i < rects.size() && i < 16; ++i) {
        const bool same = rects[i].name == wants[i].name && rects[i].x == wants[i].x &&
                          rects[i].y == wants[i].y && rects[i].h == wants[i].h;
        Check(same, "board rect #" + std::to_string(i) + " (" + rects[i].name + ")");
    }

    // -- PC history ring behaviour --------------------------------------------
    std::vector<dbg::PcHistEntry> hist = be.GetPcHistory(0);
    Check(hist.size() == 28, "seeded ring size");
    Check(!hist.empty() && hist[0].addr == 0x800F && hist[0].page == 2, "seeded newest");
    Check(!hist.empty() && hist.back().addr == 0x7FF4 && hist.back().page == 5,
          "seeded oldest");
    be.Step(0);  // "call 8020" at PC=8011: one M1 fetch recorded
    hist = be.GetPcHistory(0);
    Check(hist.size() == 29, "ring grows after step");
    Check(!hist.empty() && hist[0].addr == 0x8011 && hist[0].page == 2, "newest after step");
    Check(be.GetRegs(0).pc == 0x8020, "step landed in the call target");

    // -- TSConf block served ---------------------------------------------------
    dbg::TsConfState ts;
    Check(be.GetTsConf(ts), "GetTsConf true");
    Check(ts.sysconf == 0x02 && ts.zclk == 2, "SysConfig seed");
    Check(ts.vpage == 0x05, "VPage seed");
    Check(ts.intFrame, "IntMask FRAME seed");
    Check(ts.page[1] == 0x05, "MemPages seed");

    // -- TSConf interactivity and navigation tests -----------------------------
    {
        dbg::UiState ui;
        ui.activeWidget = dbg::WidgetId::Regs;

        // 4 focusable widgets
        const dbg::WidgetId expForward[] = {
            dbg::WidgetId::Trace,
            dbg::WidgetId::Memory,
            dbg::WidgetId::Pages,
            dbg::WidgetId::Regs
        };
        for (const auto exp : expForward) {
            dbg::FocusNextWidget(ui, true);
            Check(ui.activeWidget == exp, "TSConf forward cycle to widget " + std::to_string(static_cast<int>(exp)));
        }

        // Reverse cycle (4 focusable widgets)
        const dbg::WidgetId expReverse[] = {
            dbg::WidgetId::Pages,
            dbg::WidgetId::Memory,
            dbg::WidgetId::Trace,
            dbg::WidgetId::Regs
        };
        for (const auto exp : expReverse) {
            dbg::FocusPrevWidget(ui, true);
            Check(ui.activeWidget == exp, "TSConf reverse cycle to widget " + std::to_string(static_cast<int>(exp)));
        }

        // Read-only check for pages
        ui.activeWidget = dbg::WidgetId::Pages;
        Check(!dbg::HandleCharInput(be, ui, 'A'), "TSConf pages rejects char input");
        Check(!dbg::HandleEnterKey(be, ui), "TSConf pages rejects enter key");

        // Reg edit test
        ui.activeWidget = dbg::WidgetId::Regs;
        ui.regsCurs = 0; // A
        dbg::HandleCharInput(be, ui, '5');
        dbg::HandleCharInput(be, ui, 'A');
        dbg::HandleEnterKey(be, ui);
        Check(be.GetRegs(0).a == 0x5A, "Register A updated to 0x5A");

        // Memory edit test
        ui.activeWidget = dbg::WidgetId::Memory;
        ui.memCurs = 0x9000;
        ui.memAscii = false;
        dbg::HandleCharInput(be, ui, 'C');
        dbg::HandleCharInput(be, ui, 'D');
        Check(be.ReadMemory(0, 0x9000, 1)[0] == 0xCD, "Memory at 0x9000 updated to 0xCD");

        // Mouse click tests in TSConf
        // 1. Click Regs: HL register (row 4, x=4)
        Check(dbg::HandleMouseClick(be, ui, 4, 4, true), "Click on Regs handled");
        Check(ui.activeWidget == dbg::WidgetId::Regs, "Regs focused by click");
        Check(ui.regsCurs == 4, "Reg HL selected (index 4)");

        // 2. Click Trace (row 2: y=8, x=10)
        ui.traceTop = 0x8000;
        Check(dbg::HandleMouseClick(be, ui, 10, 8, true), "Click on Trace handled");
        Check(ui.activeWidget == dbg::WidgetId::Trace, "Trace focused by click");

        // 3. Click Memory: ascii area (row 2: y=17, byte 4: x=67)
        ui.memTop = 0x8000;
        Check(dbg::HandleMouseClick(be, ui, 67, 17, true), "Click on Memory ascii handled");
        Check(ui.activeWidget == dbg::WidgetId::Memory, "Memory focused by click");
        Check(ui.memCurs == 0x8000 + 2 * 8 + 4, "Memory cursor positioned");
        Check(ui.memAscii, "Memory in ASCII mode");

        // 4. Click Pages (row 1: y=24, x=74)
        Check(dbg::HandleMouseClick(be, ui, 74, 24, true), "Click on Pages handled");
        Check(ui.activeWidget == dbg::WidgetId::Pages, "Pages focused by click");
        Check(ui.pagesCurs == 1, "Pages row 1 selected");

        // 5. Clicks on passive areas rejected
        Check(!dbg::HandleMouseClick(be, ui, 136, 5, true), "Click on TsBoard rejected");
        Check(ui.activeWidget == dbg::WidgetId::Pages, "Focus remains on Pages");

        Check(!dbg::HandleMouseClick(be, ui, 64, 28, true), "Click on Bottom (AY) rejected");
        Check(ui.activeWidget == dbg::WidgetId::Pages, "Focus remains on Pages");

        Check(!dbg::HandleMouseClick(be, ui, 40, 3, true), "Click on Watches rejected");
        Check(!dbg::HandleMouseClick(be, ui, 74, 3, true), "Click on Ports rejected");
        Check(!dbg::HandleMouseClick(be, ui, 74, 7, true), "Click on Beta128 rejected");
        Check(!dbg::HandleMouseClick(be, ui, 74, 15, true), "Click on Stack rejected");
        Check(ui.activeWidget == dbg::WidgetId::Pages, "Focus remains on Pages");
    }

    if (failures == 0) std::printf("all tests passed\n");
    else std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
