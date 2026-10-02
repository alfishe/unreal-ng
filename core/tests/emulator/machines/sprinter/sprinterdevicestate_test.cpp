// The Sprinter reports every automation interface renders (Sprinter tdd-integration §3,
// automation-outcome.md): DeviceState::Sprinter (WebAPI /state/sprinter, CLI state sprinter,
// Lua / Python sprinter_state, MCP aspect sprinter), SprinterPaging (/state/paging), the port
// table views SprinterPortTable / SprinterPortLookup (/state/sprinter/ports[/lookup], MCP
// sprinter_ports) and SprinterText (/state/sprinter/text, MCP sprinter_text). The interfaces
// only convert the trees, so these tests pin the content once.

#include "sprinterfixture.h"

#include <string>

#include "common/stringhelper.h"
#include "emulator/ports/models/sprinter/sprinterporttable.h"
#include "emulator/state/devicestate.h"
#include "emulator/video/sprinter/screensprinter.h"
#include "emulator/video/sprinter/sprintervideoram.h"

namespace
{
const StateNode& Member(const StateNode& node, const std::string& key)
{
    static const StateNode kNone;
    const StateNode* member = node.find(key);
    return member ? *member : kNone;
}

std::string Str(const StateNode& node, const std::string& key) { return Member(node, key).s; }
int64_t Int(const StateNode& node, const std::string& key) { return Member(node, key).i; }
bool Bool(const StateNode& node, const std::string& key) { return Member(node, key).b; }
}  // namespace

class SprinterDeviceState_Test : public SprinterFixture
{
protected:
    StateNode Window(uint8_t window)
    {
        const StateNode report = DeviceState::Sprinter(_context);
        const StateNode& windows = Member(report, "windows");
        return window < windows.items.size() ? windows.items[window] : StateNode();
    }

    /// The row of a port table report with this code and direction, or null
    static const StateNode* Row(const StateNode& table, const std::string& code, const std::string& direction)
    {
        for (const StateNode& row : Member(table, "rows").items)
            if (Str(row, "code") == code && Str(row, "direction") == direction)
                return &row;
        return nullptr;
    }
};

// The index formula is shared by the decoder and the views (sprinterporttable.h): the decoder's
// lookup for the current state equals the formula with that state spelled out
TEST_F(SprinterDeviceState_Test, PortTableIndexMatchesTheDecoder)
{
    Pld().cnf = 0x14;  // map 2
    Pld().pn = 0x20;   // PN5
    Pld().dos = 0;     // TR-DOS on
    for (uint16_t port : {0x7785, 0x21BC, 0x00FE, 0xFFFD})
    {
        EXPECT_EQ(_decoder->LookupIndex(port, true), SprinterPortTable::Index(2, true, false, true, port)) << port;
        EXPECT_EQ(_decoder->LookupIndex(port, false), SprinterPortTable::Index(2, true, false, false, port)) << port;
    }
    EXPECT_EQ(SprinterPortTable::Index(0, false, false, false, 0x7785), 0x009D) << "MAN p. 28";
    EXPECT_EQ(SprinterPortTable::ExamplePort(SprinterPortTable::AddressBits(0x21BC)), 0x20A4);
}

// The state block names the machine's PLD, decoder, clock and frame
TEST_F(SprinterDeviceState_Test, ReportFollowsThePld)
{
    OpenDcp();
    Pld().cnf = 0x0C;  // map 1, CNF bit 2
    Pld().allMode = 0x09;
    Pld().rgMod = 0x01;

    const StateNode report = DeviceState::Sprinter(_context);
    ASSERT_TRUE(Bool(report, "available")) << DeviceState::ToText(report);
    const StateNode& pld = Member(report, "pld");
    EXPECT_EQ(Str(pld, "state"), "configured");
    EXPECT_EQ(Str(pld, "module"), "Standard");
    EXPECT_TRUE(Bool(pld, "dcp_open"));
    EXPECT_TRUE(Bool(Member(pld, "bitstream"), "fast_start"));

    const StateNode& decoder = Member(report, "decoder");
    EXPECT_EQ(Int(decoder, "map"), 1);
    EXPECT_EQ(Str(decoder, "cnf"), "0x0C");

    const StateNode& registers = Member(report, "registers");
    EXPECT_TRUE(Bool(Member(registers, "all_mode"), "keyboard_int"));
    EXPECT_FALSE(Bool(Member(registers, "all_mode"), "zx_screen_shadow"));
    EXPECT_EQ(Int(Member(registers, "rgmod"), "mode_page"), 1);

    EXPECT_EQ(Int(Member(report, "frame"), "lines"), 320);
    EXPECT_EQ(Int(Member(report, "frame"), "t_states"), 71680);
    EXPECT_EQ(Str(Member(report, "cells"), "D0-DF"), "10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F") << "power-on cells";
    EXPECT_EQ(Str(Member(report, "z84c15"), "engine"), "z84c15 library (Z84C15Engine)");
    EXPECT_EQ(Str(Member(report, "cmos"), "chip"), "DS12887A");
    EXPECT_TRUE(Bool(Member(report, "ide"), "emulated")) << "S3b: the IdeAdapter with the SPRINTER decode";
    EXPECT_EQ(Member(Member(report, "bios"), "images").items.size(), 3u);
}

// The ZX mode's original waits (ALL_MODE bit 2 = 0 at 3.5 MHz) and the tape's real-time clock (tdd-zx-mode.md §3.3, §3.4):
// one source for every automation surface (WebAPI /state/sprinter, MCP inspect_state, CLI, Lua, Python)
TEST_F(SprinterDeviceState_Test, OriginalWaitsAndTapeAreReported)
{
    OpenDcp();
    Pld().allMode = 0xFA;  // ORIGIN.ZX
    Pld().pn = 0x05;       // #7FFD: page 5 at #C000
    _decoder->ApplyOrigWaits();
    StateNode report = DeviceState::Sprinter(_context);
    StateNode waits = Member(Member(report, "clock"), "original_waits");
    EXPECT_TRUE(Bool(waits, "active")) << DeviceState::ToText(report);
    EXPECT_FALSE(Bool(waits, "all_mode_bit2"));
    EXPECT_EQ(Int(waits, "period_t"), 4);
    EXPECT_EQ(Int(waits, "phase_t"), 0);
    ASSERT_EQ(Member(waits, "windows_waiting").items.size(), 4u);
    EXPECT_FALSE(Member(waits, "windows_waiting").items[0].b);
    EXPECT_TRUE(Member(waits, "windows_waiting").items[1].b);
    EXPECT_FALSE(Member(waits, "windows_waiting").items[2].b);
    EXPECT_TRUE(Member(waits, "windows_waiting").items[3].b) << "#7FFD bit 2";
    EXPECT_EQ(Str(Member(report, "tape"), "time_base"), "base_clock");

    Pld().allMode = 0xFE;  // the default ZX mode
    _decoder->ApplyOrigWaits();
    waits = Member(Member(DeviceState::Sprinter(_context), "clock"), "original_waits");
    EXPECT_FALSE(Bool(waits, "active"));
    EXPECT_FALSE(Member(waits, "windows_waiting").items[1].b);
}

// Turbo: the clock block follows hw_turbo_ratio (CNF bit 0 with bit 1, the front-panel switch)
TEST_F(SprinterDeviceState_Test, ClockShowsTheTurbo)
{
    _context->emulatorState.hw_turbo_ratio = 6;
    StateNode clock = Member(DeviceState::Sprinter(_context), "clock");
    EXPECT_EQ(Str(clock, "mhz"), "21");
    EXPECT_EQ(Int(clock, "ratio"), 6);
    _context->emulatorState.hw_turbo_ratio = 1;
    clock = Member(DeviceState::Sprinter(_context), "clock");
    EXPECT_EQ(Str(clock, "mhz"), "3.5");
}

// Windows: physical page and kind - ROM, fast RAM, vROM, system RAM, RAM, graphics, port table
TEST_F(SprinterDeviceState_Test, WindowsShowPageAndKind)
{
    // After the PLD reset window 3 shows the port table until the first IN
    EXPECT_EQ(Str(Window(3), "kind"), "port table");
    EXPECT_EQ(Int(Window(3), "page"), 0x40);
    OpenDcp();
    _decoder->UpdateBanks();
    EXPECT_EQ(Str(Window(3), "kind"), "RAM");

    EXPECT_EQ(Str(Window(0), "kind"), "ROM");
    EXPECT_EQ(Int(Window(0), "page"), 8) << "BIOS page after reset";
    EXPECT_FALSE(Bool(Window(0), "writable"));

    Pld().Cell(SprinterCode::Page1) = 0x50;  // a graphics page
    Pld().Cell(SprinterCode::Page2) = 0x21;
    _decoder->UpdateBanks();
    EXPECT_EQ(Str(Window(1), "kind"), "graphics");
    EXPECT_EQ(Int(Window(1), "page"), 0x50);
    EXPECT_EQ(Str(Window(2), "kind"), "RAM");
    EXPECT_EQ(Int(Window(2), "page"), 0x21);
    EXPECT_EQ(Str(Window(2), "cell"), "#EA");

    // IN #FB: fast RAM page ROM_RG bits 1-0 in window 0
    Pld().romRg = 0x02;
    In(0x00FB);
    EXPECT_EQ(Str(Window(0), "kind"), "fast RAM");
    EXPECT_EQ(Int(Window(0), "page"), 2);
    In(0x007B);

    // ROM out (#3C): a RAM page from the cells #E0-#EF - the Spectrum ROM image, read-only
    Pld().romOff = 1;
    Pld().ramSys = 0;
    _decoder->UpdateBanks();
    EXPECT_EQ(Str(Window(0), "kind"), "vROM");
    EXPECT_FALSE(Bool(Window(0), "writable"));

    // #1FFD bit 0 with RAM_SYS: system RAM at 0
    Pld().ramSys = 1;
    Pld().sc = 0x01;
    _decoder->UpdateBanks();
    EXPECT_EQ(Str(Window(0), "kind"), "RAM");
    EXPECT_TRUE(Bool(Window(0), "writable"));

    // The /state/paging view carries the same windows
    const StateNode paging = DeviceState::SprinterPaging(_context);
    EXPECT_EQ(Str(Member(paging, "windows").items[1], "kind"), "graphics");
}

// The port table view: BIOS 3.04's map 0, decoded as tools/machines/sprinter/dcp-table does it
TEST_F(SprinterDeviceState_Test, PortTableDecodesBios304Map0)
{
    if (!LoadTable304())
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";

    DeviceState::SprinterPortQuery query;
    query.map = 0;
    query.dos = 0;
    query.direction = 0;
    const StateNode table = DeviceState::SprinterPortTable(_context, query);
    ASSERT_TRUE(Bool(table, "available"));
    EXPECT_FALSE(Bool(table, "from_machine"));
    EXPECT_EQ(Str(table, "direction"), "w");

    // #2B: the primary IDE channel, OUT #21BC (dcp-table: "001x xxxx 101x x100 (e.g. #20A4)")
    const StateNode* ide = Row(table, "0x2B", "w");
    ASSERT_NE(ide, nullptr) << DeviceState::ToText(table);
    EXPECT_EQ(Str(*ide, "name"), "IdePrimary");
    EXPECT_EQ(Str(*ide, "pattern"), "001x xxxx 101x x100");
    EXPECT_EQ(Str(*ide, "example"), "0x20A4");
    EXPECT_EQ(Int(*ide, "addresses"), 1);

    // #C2 border: every #FE-class write ("xxxx xxxx 111x x110": 8 address combinations)
    const StateNode* border = Row(table, "0xC2", "w");
    ASSERT_NE(border, nullptr);
    EXPECT_EQ(Str(*border, "pattern"), "xxxx xxxx 111x x110");
    EXPECT_EQ(Int(*border, "addresses"), 8);

    // No WD1793 with TR-DOS off; with it on, code #10 at #1F
    EXPECT_EQ(Row(table, "0x10", "w"), nullptr);
    query.dos = 1;
    EXPECT_NE(Row(DeviceState::SprinterPortTable(_context, query), "0x10", "w"), nullptr);
    EXPECT_FALSE(Member(table, "z84c15_ports").items.empty());
}

// One port: the index into page #40, the code and its name; the Z84C15 answers its own ports
TEST_F(SprinterDeviceState_Test, PortLookupResolvesIndexAndCode)
{
    if (!LoadTable304())
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";
    OpenDcp();
    Pld().cnf = 0x04;  // map 0
    Pld().dos = 1;     // TR-DOS off

    DeviceState::SprinterPortQuery query;
    query.direction = 0;
    StateNode lookup = DeviceState::SprinterPortLookup(_context, 0x21BC, query);
    EXPECT_TRUE(Bool(lookup, "from_machine"));
    ASSERT_EQ(Member(lookup, "results").items.size(), 1u);
    const StateNode& write = Member(lookup, "results").items[0];
    EXPECT_EQ(Str(write, "answered_by"), "PLD");
    EXPECT_EQ(Str(write, "index"), "0x043C") << "/DOS = 1: + #400";
    EXPECT_EQ(Str(write, "code"), "0x2B");
    EXPECT_EQ(Str(write, "name"), "IdePrimary");

    // Both directions of #7FFD: code #C1 (the cell)
    lookup = DeviceState::SprinterPortLookup(_context, 0x7FFD, DeviceState::SprinterPortQuery());
    ASSERT_EQ(Member(lookup, "results").items.size(), 2u);
    for (const StateNode& r : Member(lookup, "results").items)
        EXPECT_EQ(Str(r, "code"), "0xC1") << Str(r, "direction");

    // SIO A control: the chip's trace code #100 + #19
    query.direction = 1;
    lookup = DeviceState::SprinterPortLookup(_context, 0x0019, query);
    const StateNode& sio = Member(lookup, "results").items[0];
    EXPECT_EQ(Str(sio, "answered_by"), "Z84C15");
    EXPECT_EQ(Str(sio, "code"), "0x0119");
    EXPECT_EQ(Str(sio, "name"), "Z84 SIO A control");

    // IN #FB: the fixed decode before the table
    lookup = DeviceState::SprinterPortLookup(_context, 0x00FB, query);
    EXPECT_NE(Str(Member(lookup, "results").items[0], "fixed_decode").find("fast RAM"), std::string::npos);
}

// Parameters arrive as text on every interface: one parser
TEST_F(SprinterDeviceState_Test, QueryAndPortParsing)
{
    DeviceState::SprinterPortQuery query;
    std::string error;
    ASSERT_TRUE(DeviceState::SprinterPortQueryFromStrings("2", "on", "0", "w", query, error)) << error;
    EXPECT_EQ(query.map, 2);
    EXPECT_EQ(query.dos, 1);
    EXPECT_EQ(query.pn5, 0);
    EXPECT_EQ(query.direction, 0);
    ASSERT_TRUE(DeviceState::SprinterPortQueryFromStrings("", "", "", "", query, error));
    EXPECT_EQ(query.map, -1);
    EXPECT_EQ(query.dos, -1);
    EXPECT_EQ(query.direction, -1);
    EXPECT_FALSE(DeviceState::SprinterPortQueryFromStrings("4", "", "", "", query, error));
    EXPECT_NE(error.find("map"), std::string::npos);
    EXPECT_FALSE(DeviceState::SprinterPortQueryFromStrings("", "maybe", "", "", query, error));
    EXPECT_FALSE(DeviceState::SprinterPortQueryFromStrings("", "", "", "x", query, error));

    uint16_t port = 0;
    EXPECT_TRUE(DeviceState::SprinterPortFromString("21BC", port));
    EXPECT_EQ(port, 0x21BC);
    EXPECT_TRUE(DeviceState::SprinterPortFromString("#7ffd", port));
    EXPECT_EQ(port, 0x7FFD);
    EXPECT_TRUE(DeviceState::SprinterPortFromString("0xFE", port));
    EXPECT_EQ(port, 0x00FE);
    EXPECT_FALSE(DeviceState::SprinterPortFromString("", port));
    EXPECT_FALSE(DeviceState::SprinterPortFromString("12345", port));
    EXPECT_FALSE(DeviceState::SprinterPortFromString("#G0", port));
}

// The screen text: a text square's Mode1 byte is its character (two for an 80-column square: the
// right one from Line2, whose own Mode0 must be text too - the renderer takes every byte of the
// right half from Line2, SprinterSquare::TextCode)
TEST_F(SprinterDeviceState_Test, TextReadsTheModeTable)
{
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    Pld().rgMod = 0x00;
    const uint32_t square = SprinterVideoRam::ModeAddress(0, 2, 0);  // a = 0, b = 2: row 2, columns 0-1
    vram.Write(square, 0x10);                                        // text, 640 (80 columns)
    vram.Write(square + 1, 'O');
    vram.Write(square + SprinterVideoRam::kRowBytes, 0x10);          // Line2: text as well
    vram.Write(square + 1 + SprinterVideoRam::kRowBytes, 'K');       // Line2: the right character
    const uint32_t next = SprinterVideoRam::ModeAddress(1, 2, 0);
    vram.Write(next, 0x30);                                          // text, 320 (40 columns): one character
    vram.Write(next + 1, '!');

    const StateNode text = DeviceState::SprinterText(_context);
    ASSERT_TRUE(Bool(text, "available"));
    EXPECT_EQ(Int(text, "columns"), 80);
    const StateNode& lines = Member(text, "lines");
    ASSERT_EQ(lines.items.size(), 32u);
    EXPECT_EQ(Str(lines.items[2], "text"), "OK!");
    EXPECT_EQ(Str(lines.items[2], "codes").substr(0, 8), "4F4B2120");
    EXPECT_EQ(Int(text, "text_squares"), 2);
}

// The mode table per square: one letter a square, each kind decoded by the renderer's rules
TEST_F(SprinterDeviceState_Test, VideoMapDecodesEverySquareKind)
{
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    Pld().rgMod = 0x00;
    auto put = [&](uint8_t a, uint8_t b, uint8_t m0, uint8_t m1, uint8_t m2) {
        const uint32_t at = SprinterVideoRam::ModeAddress(a, b, 0);
        vram.Write(at, m0);
        vram.Write(at + 1, m1);
        vram.Write(at + 2, m2);
    };
    put(0, 0, 0xA2, 0x19, 0x00);  // graphics 320, palette 2, column #080 + 8, row 24 (the renderer's worked example)
    put(1, 0, 0x41, 0x00, 0x07);  // graphics 640, palette 1, low-res, quarter 3
    put(2, 0, 0x30, 'A', 0x00);   // text 40
    put(3, 0, 0x10, 'B', 0x00);   // text 80: Line2 below
    vram.Write(SprinterVideoRam::ModeAddress(3, 0, 0) + SprinterVideoRam::kRowBytes, 0x10);
    vram.Write(SprinterVideoRam::ModeAddress(3, 0, 0) + SprinterVideoRam::kRowBytes + 1, 'C');
    put(4, 0, 0xF0, 0x00, 0x00);  // border
    put(5, 0, 0xFD, 0x00, 0x00);  // blank + frame INT
    put(6, 0, 0xFC, 0x00, 0x00);  // blank

    DeviceState::SprinterVideoQuery query;
    const StateNode video = DeviceState::SprinterVideo(_context, query);
    ASSERT_TRUE(Bool(video, "available")) << DeviceState::ToText(video);
    EXPECT_EQ(Int(video, "mode_page"), 0);
    EXPECT_TRUE(Bool(video, "displayed"));
    const StateNode& map = Member(video, "map");
    ASSERT_EQ(map.items.size(), 32u);
    EXPECT_EQ(map.items[0].s.size(), 40u);
    EXPECT_EQ(map.items[0].s.substr(0, 7), "GgTtB*.");

    const StateNode& row0 = Member(video, "squares").items[0];
    ASSERT_EQ(row0.items.size(), 40u);
    EXPECT_EQ(Str(row0.items[0], "kind"), "graphics_320");
    EXPECT_EQ(Int(row0.items[0], "palette"), 2);
    EXPECT_EQ(Str(row0.items[0], "source_column"), "0x0088");
    EXPECT_EQ(Int(row0.items[0], "source_row"), 24);
    EXPECT_FALSE(Bool(row0.items[0], "low_res"));
    EXPECT_EQ(Str(row0.items[1], "kind"), "graphics_640");
    EXPECT_TRUE(Bool(row0.items[1], "low_res"));
    EXPECT_EQ(Int(row0.items[1], "quarter"), 3);
    EXPECT_EQ(Str(row0.items[2], "chars"), "41");
    EXPECT_EQ(Str(row0.items[3], "chars"), "42 43");
    EXPECT_EQ(Str(row0.items[4], "kind"), "border");
    EXPECT_TRUE(Bool(row0.items[5], "int"));

    // Palettes: graphics 1 and 2, 0 (the zeroed squares are graphics 640, palette 0), text 4-7
    std::string used;
    for (const StateNode& k : Member(video, "palettes_used").items)
        used += std::to_string(k.i);
    EXPECT_EQ(used, "0124567");

    // The whole table, the other page, the map alone
    query.all = true;
    query.page = 1;
    query.squares = false;
    const StateNode whole = DeviceState::SprinterVideo(_context, query);
    EXPECT_EQ(Member(whole, "map").items.size(), 40u);
    EXPECT_EQ(Member(whole, "map").items[0].s.size(), 56u);
    EXPECT_FALSE(Bool(whole, "displayed"));
    EXPECT_EQ(whole.find("squares"), nullptr);
}

// One classifier: the screen description (/state/screen) and the per-square map count the same squares
TEST_F(SprinterDeviceState_Test, ScreenDescriptionSharesTheClassifier)
{
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    Pld().rgMod = 0x00;
    for (uint8_t a = 0; a < 40; a++)
        vram.Write(SprinterVideoRam::ModeAddress(a, 0, 0), a < 10 ? 0xFC : (a < 20 ? 0xF0 : 0x10));

    DeviceState::SprinterVideoQuery query;
    query.squares = false;
    const StateNode video = DeviceState::SprinterVideo(_context, query);
    const StateNode& counts = Member(video, "counts");
    const ScreenState screen = _context->pScreen->DescribeScreenState();
    auto expect = [&](const char* key, const char* label) {
        const std::string fragment = std::string(label) + " " + std::to_string(Int(counts, key));
        EXPECT_NE(screen.videoMode.find(fragment), std::string::npos) << screen.videoMode << " / " << fragment;
    };
    expect("blank", "blank");
    expect("border", "border");
    expect("text_80", "text 80");
    expect("graphics_640", "graphics 640");
    EXPECT_FALSE(screen.videoModeBrief.empty());

    // The machine report's video summary counts the same kinds (over the whole table)
    const StateNode machine = DeviceState::Sprinter(_context);
    const StateNode& squares = Member(Member(machine, "video"), "squares");
    EXPECT_GE(Int(squares, "blank"), Int(counts, "blank"));
    EXPECT_EQ(Int(squares, "border"), Int(counts, "border")) << "no border squares outside the picture here";
}

// The palettes: R, G, B as video RAM holds them, the pen's red byte address
TEST_F(SprinterDeviceState_Test, PaletteReadsRgbInVideoRamOrder)
{
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    const uint32_t pen = 2 * 256 + 5;
    const uint32_t at = SprinterVideoRam::PenAddress(pen);
    vram.Write(at, 0x12);
    vram.Write(at + 1, 0x34);
    vram.Write(at + 2, 0x56);

    const StateNode report = DeviceState::SprinterPalette(_context, 2);
    const StateNode& palettes = Member(report, "palettes");
    ASSERT_EQ(palettes.items.size(), 1u);
    EXPECT_EQ(Int(palettes.items[0], "k"), 2);
    EXPECT_EQ(Str(palettes.items[0], "role"), "graphics 2");
    const StateNode& pen5 = Member(palettes.items[0], "pens").items[5];
    EXPECT_EQ(Str(pen5, "rgb"), "#123456");
    EXPECT_EQ(Str(pen5, "vram"), StringHelper::Format("0x%05X", at));
    EXPECT_EQ(Str(palettes.items[0], "rgb_row").substr(5 * 7, 6), "123456");
    EXPECT_EQ(Member(DeviceState::SprinterPalette(_context, DeviceState::kSprinterPalettesAll), "palettes").items.size(), 8u);

    int k = 0;
    std::string error;
    EXPECT_TRUE(DeviceState::SprinterPaletteFromString("7", k, error));
    EXPECT_EQ(k, 7);
    EXPECT_TRUE(DeviceState::SprinterPaletteFromString("", k, error));
    EXPECT_EQ(k, DeviceState::kSprinterPalettesUsed);
    EXPECT_FALSE(DeviceState::SprinterPaletteFromString("8", k, error));
    DeviceState::SprinterVideoQuery query;
    EXPECT_FALSE(DeviceState::SprinterVideoQueryFromStrings("2", "", "", query, error));
    EXPECT_TRUE(DeviceState::SprinterVideoQueryFromStrings("1", "1", "0", query, error));
    EXPECT_EQ(query.page, 1);
    EXPECT_TRUE(query.all);
    EXPECT_FALSE(query.squares);
}

// The accelerator block (s5-accelerator-outcome.md) and the 21 MHz wait rule
TEST_F(SprinterDeviceState_Test, AcceleratorAndWaitsAreReported)
{
    SprinterAccelerator* accelerator = _decoder->GetAccelerator();
    ASSERT_NE(accelerator, nullptr) << "fast start: configured";
    SprinterAccelState& st = accelerator->State();
    st.mode = 5;
    st.dir = SprinterAccelerator::kDir[5];
    st.length = 64;
    st.fn = 2;
    st.operations = 128;
    st.lastExtraClocks = 189;
    st.buffer[0] = 0x0D;
    Pld().allMode = 0x01;

    const StateNode report = DeviceState::Sprinter(_context);
    const StateNode& accel = Member(report, "accelerator");
    EXPECT_TRUE(Bool(accel, "available"));
    EXPECT_TRUE(Bool(accel, "enabled"));
    EXPECT_EQ(Str(accel, "mode_name"), "copy");
    EXPECT_TRUE(Bool(accel, "armed"));
    EXPECT_EQ(Int(accel, "length"), 64);
    EXPECT_EQ(Str(accel, "function"), "xor");
    EXPECT_EQ(Int(accel, "operations"), 128);
    EXPECT_EQ(Int(accel, "last_extra_clocks"), 189);
    EXPECT_EQ(Str(accel, "buffer_head").substr(0, 2), "0D");
    EXPECT_EQ(Str(accel, "buffer_crc32").size(), 10u);

    const StateNode& waits = Member(Member(report, "clock"), "waits");
    EXPECT_FALSE(Bool(waits, "active")) << "3.5 MHz";
    OpenDcp();
    SetCode(0x007C, false, 0xC6);
    Out(0x007C, 0x03);  // turbo on
    ASSERT_EQ(_context->emulatorState.hw_turbo_ratio, 6);
    const StateNode turbo = Member(Member(DeviceState::Sprinter(_context), "clock"), "waits");
    EXPECT_TRUE(Bool(turbo, "active"));
    bool anyWindow = false;
    for (const StateNode& w : Member(turbo, "windows_waiting").items)
        anyWindow = anyWindow || w.b;
    EXPECT_TRUE(anyWindow) << DeviceState::ToText(turbo);

    const StateNode& z84 = Member(report, "z84c15");
    EXPECT_EQ(Member(Member(z84, "daisy_chain"), "sources").items.size(), 8u);
    EXPECT_FALSE(Str(Member(z84, "daisy_chain"), "order").empty());
    EXPECT_TRUE(Member(z84, "wait_generator").find("memory") != nullptr);
}

// The Covox-Blaster ring: 256 words, the play and write entries marked
TEST_F(SprinterDeviceState_Test, SoundRingMarksPlayAndWrite)
{
    CovoxBlasterState& c = _decoder->GetCovoxBlaster().State();
    c.ring[0x10] = 0x1234;
    c.cnt = 0x10;
    const StateNode ring = DeviceState::SprinterSoundRing(_context);
    ASSERT_TRUE(Bool(ring, "available"));
    EXPECT_EQ(Member(ring, "words").items.size(), 256u);
    EXPECT_EQ(Member(ring, "words").items[0x10].i, 0x1234);
    const StateNode& rows = Member(ring, "rows");
    ASSERT_EQ(rows.items.size(), 16u);
    EXPECT_NE(rows.items[1].s.find("[1234]"), std::string::npos) << rows.items[1].s;
    EXPECT_EQ(Str(ring, "play_index"), "0x10");
}

// The video change log (/video/changes): RGMOD / HOLD / frame height writes with their PC, palette and
// mode table writes counted per frame
TEST_F(SprinterDeviceState_Test, VideoChangesLogLatchesAndTables)
{
    _context->pScreen->InitFrame();  // a frame begins: the log starts
    OpenDcp();
    SetCode(0x00C5, false, 0xC5);  // RgMod
    SetCode(0x00CB, false, 0xCB);  // Hold
    _decoder->DecodePortOut(0x00C5, 0x01, 0x8123);
    _decoder->DecodePortOut(0x00CB, 0x66, 0x8130);
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    vram.Write(SprinterVideoRam::PenAddress(0x405), 0xA8);       // a palette byte
    vram.Write(SprinterVideoRam::ModeAddress(3, 4, 1), 0x10);    // a mode table byte
    vram.Write(0x0100, 0x55);                                    // a screen byte: not a table

    const StateNode report = DeviceState::VideoChanges(_context, 2);
    ASSERT_TRUE(Bool(report, "available"));
    const StateNode& frames = Member(report, "frames");
    ASSERT_FALSE(frames.items.empty());
    const StateNode& current = frames.items.back();
    EXPECT_TRUE(Bool(current, "current"));
    EXPECT_EQ(Str(Member(current, "start"), "frame_lines"), "320");
    const StateNode& writes = Member(current, "writes");
    ASSERT_GE(writes.items.size(), 2u) << DeviceState::ToText(current);
    EXPECT_EQ(Str(Member(writes.items[0], "changes"), "rgmod"), "0x00 -> 0x01");
    EXPECT_EQ(Str(writes.items[0], "pc"), "0x8123");
    EXPECT_EQ(Str(Member(writes.items[1], "changes"), "hold"), "0x77 -> 0x66");
    const StateNode& tables = Member(current, "tables");
    EXPECT_EQ(Int(Member(tables, "palette"), "count"), 1);
    EXPECT_EQ(Int(Member(tables, "mode_table"), "count"), 1);
}

// The generic text read (/video/text, automation audit G8) falls back to the Sprinter's text squares, and the
// screen mode report carries the per-mode notes on every interface (G12)
TEST_F(SprinterDeviceState_Test, VideoTextAndScreenModeCoverTheSprinter)
{
    EXPECT_FALSE(Bool(DeviceState::VideoText(_context, 0), "available")) << "no text squares: no text layer";
    Pld().allMode = 0x01;  // Sprinter mode (bit 0 = 0: the Spectrum screen, read with the ZX OCR)
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    Pld().rgMod = 0x00;
    const uint32_t square = SprinterVideoRam::ModeAddress(0, 1, 0);
    vram.Write(square, 0x30);  // text 40
    vram.Write(square + 1, 'Z');
    const StateNode text = DeviceState::VideoText(_context, 0);
    ASSERT_TRUE(Bool(text, "available")) << DeviceState::ToText(text);
    EXPECT_EQ(Str(text, "layer"), "sprinter_text");
    EXPECT_EQ(Int(text, "columns"), 80);
    EXPECT_EQ(Str(Member(text, "lines").items[1], "text"), "Z");
    Pld().allMode = 0x00;
    EXPECT_FALSE(Bool(DeviceState::VideoText(_context, 0), "available")) << "Spectrum mode: a ZX screen";

    const StateNode mode = DeviceState::ScreenMode(_context);
    EXPECT_EQ(Str(mode, "framebuffer"), "736x288");
    EXPECT_FALSE(Str(mode, "sprinter_modes").empty());
    EXPECT_FALSE(Str(mode, "video_mode_brief").empty());
}

TEST(SprinterDeviceStateOther_Test, UnavailableOnOtherMachines)
{
    EmulatorContext context(LoggerLevel::LogError);
    for (const StateNode& node : {DeviceState::Sprinter(&context), DeviceState::SprinterPaging(&context),
                                  DeviceState::SprinterText(&context),
                                  DeviceState::SprinterVideo(&context, DeviceState::SprinterVideoQuery()),
                                  DeviceState::SprinterPalette(&context, DeviceState::kSprinterPalettesUsed),
                                  DeviceState::SprinterSoundRing(&context),
                                  DeviceState::SprinterPortTable(&context, DeviceState::SprinterPortQuery()),
                                  DeviceState::SprinterPortLookup(&context, 0x7FFD, DeviceState::SprinterPortQuery())})
    {
        const std::string text = DeviceState::ToText(node);
        EXPECT_NE(text.find("Not a Sprinter machine"), std::string::npos) << text;
    }
}
