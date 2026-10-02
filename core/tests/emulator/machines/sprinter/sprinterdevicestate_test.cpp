// The Sprinter reports every automation interface renders (Sprinter tdd-integration §3,
// automation-outcome.md): DeviceState::Sprinter (WebAPI /state/sprinter, CLI state sprinter,
// Lua / Python sprinter_state, MCP aspect sprinter), SprinterPaging (/state/paging), the port
// table views SprinterPortTable / SprinterPortLookup (/state/sprinter/ports[/lookup], MCP
// sprinter_ports) and SprinterText (/state/sprinter/text, MCP sprinter_text). The interfaces
// only convert the trees, so these tests pin the content once.

#include "sprinterfixture.h"

#include <string>

#include "emulator/ports/models/sprinter/sprinterporttable.h"
#include "emulator/state/devicestate.h"
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

// The screen text: a text square's Mode1 byte is its character (two for an 80-column square)
TEST_F(SprinterDeviceState_Test, TextReadsTheModeTable)
{
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    Pld().rgMod = 0x00;
    const uint32_t square = SprinterVideoRam::ModeAddress(0, 2, 0);  // a = 0, b = 2: row 2, columns 0-1
    vram.Write(square, 0x10);                                        // text, 640 (80 columns)
    vram.Write(square + 1, 'O');
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

TEST(SprinterDeviceStateOther_Test, UnavailableOnOtherMachines)
{
    EmulatorContext context(LoggerLevel::LogError);
    for (const StateNode& node : {DeviceState::Sprinter(&context), DeviceState::SprinterPaging(&context),
                                  DeviceState::SprinterText(&context),
                                  DeviceState::SprinterPortTable(&context, DeviceState::SprinterPortQuery()),
                                  DeviceState::SprinterPortLookup(&context, 0x7FFD, DeviceState::SprinterPortQuery())})
    {
        const std::string text = DeviceState::ToText(node);
        EXPECT_NE(text.find("Not a Sprinter machine"), std::string::npos) << text;
    }
}
