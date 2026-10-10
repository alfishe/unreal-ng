/// @file cli-next-format_test.cpp
/// @brief CLI `state next` (cli-next-format.h): the subcommands and the journal's text, on a real NEXT machine (the reports are
/// DeviceState's, tested in nextregjournal_test.cpp and nextskeleton_test.cpp). No CLI socket.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../../automation/cli/src/commands/cli-next-format.h"
#include "../_helpers/emulatortesthelper.h"
#include "../_helpers/goldentext.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_next.h"

TEST(CliNextFormat_Test, TheJournalSwitchesAndPrintsWhoWroteWhat)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    PortDecoder_Next* ports = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
    ASSERT_NE(ports, nullptr);
    auto run = [&](const std::vector<std::string>& args) { return CliNext::StateText(context, args, "\n"); };

    EXPECT_NE(run({"next", "journal"}).find("(off: `state next journal on` first)"), std::string::npos);
    EXPECT_NE(run({"next", "journal", "on"}).find("NextREG journal on"), std::string::npos);
    ports->Board().WriteNextReg(0x07, 0x03);
    ports->Board().WriteNextReg(0x15, 0x01);
    const std::string all = run({"next", "journal"});
    EXPECT_NE(all.find("2 event(s)"), std::string::npos) << all;
    const std::string speed = run({"next", "journal", "regs=07", "sources=nextreg"});
    EXPECT_NE(speed.find("NR0x07 0x00 -> 0x03  CPU Speed (CPU speed 28 MHz)"), std::string::npos) << speed;
    EXPECT_EQ(speed.find("NR0x15"), std::string::npos);
    EXPECT_NE(run({"next", "journal", "regs=zz"}).find("Error:"), std::string::npos);
    EXPECT_NE(run({"next", "journal", "colour=1"}).find("unknown option"), std::string::npos);
    EXPECT_NE(run({"next", "journal", "clear"}).find("0 event(s) held"), std::string::npos);
    EXPECT_NE(run({"next", "regs"}).find("registers"), std::string::npos);
    EXPECT_NE(run({"next", "bogus"}).find("unknown subcommand"), std::string::npos);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

// The report subcommands of design-automation-coverage.md: the text is the core's report (dma, video, nextreg) or a compact table
// (palette, ports); `next nextreg` writes through the board's choke point. Golden files: core/tests/automation/golden/next/cli-*.txt
class CliNextReports_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder_Next* _ports = nullptr;
    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
    }
    void TearDown() override { EmulatorTestHelper::CleanupEmulator(_emulator); }
    std::string State(const std::vector<std::string>& args) { return CliNext::StateText(_context, args, "\n"); }
    std::string Next(const std::vector<std::string>& args) { return CliNext::NextCommand(_emulator, args, "\n"); }
    void Nr(uint8_t reg, uint8_t value)
    {
        _ports->DecodePortOut(0x243B, reg, 0);
        _ports->DecodePortOut(0x253B, value, 0);
    }
};

TEST_F(CliNextReports_Test, DmaAndVideoPrintTheCoreReports)
{
    const std::string dma = State({"next", "dma"});
    EXPECT_NE(dma.find("mode: zxn"), std::string::npos) << dma;
    EXPECT_NE(dma.find("read_mask: 0x7F"), std::string::npos);
    const std::string video = State({"next", "video"});
    EXPECT_NE(video.find("layer_order:"), std::string::npos) << video;
    EXPECT_NE(video.find("name: SLU"), std::string::npos);
    EXPECT_NE(Next({"dma"}).find("mode: zxn"), std::string::npos) << "`next dma` is `state next dma`";
}

TEST_F(CliNextReports_Test, PaletteTakesKeysAndPositionalWords)
{
    Nr(0x43, 0x20);  // sprites first palette selected
    Nr(0x40, 0x02);
    Nr(0x41, 0xFF);
    const std::string selected = State({"next", "palette", "range=2-3"});
    EXPECT_NE(selected.find("Palette sprites_1 selected, index 3"), std::string::npos) << selected;
    EXPECT_NE(selected.find("  2\t0x1FF  R7 G7 B7"), std::string::npos) << selected;
    EXPECT_EQ(selected.find("  4\t"), std::string::npos) << "the range ends at 3";
    const std::string positional = State({"next", "palette", "ula_1", "16-17"});
    EXPECT_NE(positional.find("[0] ula_1"), std::string::npos) << positional;
    EXPECT_NE(positional.find("  16\t"), std::string::npos);
    EXPECT_NE(State({"next", "palette", "palette=zz"}).find("Error: palette:"), std::string::npos);
    EXPECT_NE(State({"next", "palette", "colour=1"}).find("unknown option 'colour'"), std::string::npos);
    EXPECT_NE(State({"next", "palette", "all", "0-1", "extra"}).find("too many words"), std::string::npos);
}

TEST_F(CliNextReports_Test, PortsDescribeAndEnableWord)
{
    const std::string text = State({"next", "ports", "6B", "w"});
    EXPECT_NE(text.find("Port 0x006B write: DMA (zxnDMA)"), std::string::npos) << text;
    EXPECT_NE(text.find("enabled: on (NR 0x82 bit 5"), std::string::npos);
    EXPECT_NE(text.find("Internal port enable word 0xFFFFFFFF"), std::string::npos);
    EXPECT_NE(State({"next", "ports"}).find("Internal port enable word"), std::string::npos);
    EXPECT_EQ(State({"next", "ports"}).find("Port 0x"), std::string::npos) << "no port asked: no description";
    EXPECT_NE(State({"next", "ports", "port=zz"}).find("Error: port:"), std::string::npos);
}

TEST_F(CliNextReports_Test, NextRegReadsAndTheNextCommandWrites)
{
    Nr(0x07, 0x02);
    const std::string read = State({"next", "nextreg", "07"});
    EXPECT_NE(read.find("name: CPU Speed"), std::string::npos) << read;
    EXPECT_NE(read.find("decoded: CPU speed 14 MHz"), std::string::npos);
    EXPECT_NE(State({"next", "nextreg", "reg=ZZ"}).find("Error: reg:"), std::string::npos);

    _ports->Board().Journal().SetEnabled(true);
    const std::string written = Next({"nextreg", "07", "03"});
    EXPECT_NE(written.find("NR #07 <- #03 (was #02, reads #33) through nextreg"), std::string::npos) << written;
    const std::string viaPort = Next({"nextreg", "#07", "0x01", "port"});
    EXPECT_NE(viaPort.find("through port"), std::string::npos) << viaPort;
    const auto events = _ports->Board().Journal().Query(NextRegJournalQuery{});
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].source, NextRegSource::NextReg);
    EXPECT_EQ(events[1].source, NextRegSource::Port);
    EXPECT_NE(Next({"nextreg"}).find("Usage:"), std::string::npos);
    EXPECT_NE(Next({"nextreg", "100", "1"}).find("Error: bad register"), std::string::npos);
    EXPECT_NE(Next({"nextreg", "07", "03", "copper"}).find("door must be"), std::string::npos);
}

TEST_F(CliNextReports_Test, CopperAndSpritesPrintTheirTables)
{
    // copper list: WAIT line 100 hpos 12, MOVE NR #15 <- 5, HALT
    Nr(0x61, 0);
    Nr(0x62, 0);
    for (uint8_t b : {0x98, 0x64, 0x15, 0x05, 0xFF, 0xFF})
        Nr(0x60, b);
    const std::string copper = State({"next", "copper", "count=4"});
    EXPECT_NE(copper.find("Copper stopped (mode 0), write address 6"), std::string::npos) << copper;
    EXPECT_NE(copper.find("> 0\t0x9864  WAIT line 100 hpos 12"), std::string::npos) << "the pc marker";
    EXPECT_NE(copper.find("  1\t0x1505  MOVE NR #15 <- #05  (Sprite and Layers System)"), std::string::npos) << copper;
    EXPECT_EQ(copper.find("  4\t"), std::string::npos) << "count=4";
    EXPECT_NE(State({"next", "copper", "2", "1", "raw=true"}).find("raw: 98641505FFFF"), std::string::npos);
    EXPECT_NE(State({"next", "copper", "from=2000"}).find("Error: from:"), std::string::npos);

    _ports->DecodePortOut(0x303B, 5, 0);
    for (uint8_t b : {0x10, 0x20, 0x08, 0x83})
        _ports->DecodePortOut(0x57, b, 0);
    const std::string sprites = State({"next", "sprites"});
    EXPECT_NE(sprites.find("1 visible"), std::string::npos) << sprites;
    EXPECT_NE(sprites.find("#5 basic x 16 y 32 pattern 3 palette +0 xmirror scale 1x1  [10 20 08 83 00]"), std::string::npos) << sprites;
    EXPECT_NE(State({"next", "sprites", "all=true", "count=2"}).find("(hidden)"), std::string::npos);
    EXPECT_NE(State({"next", "sprites", "count=0"}).find("Error: count:"), std::string::npos);
}

TEST_F(CliNextReports_Test, GoldenTexts)
{
    Nr(0x43, 0x30);
    Nr(0x40, 0x00);
    Nr(0x41, 0xE0);
    Nr(0x41, 0x1C);
    GoldenText::Expect("next", "cli-palette", State({"next", "palette", "range=0-3"}));
    Nr(0x82, 0xDF);
    GoldenText::Expect("next", "cli-ports", State({"next", "ports", "6B", "w"}));
    Nr(0x61, 0);
    Nr(0x62, 0);
    for (uint8_t b : {0x98, 0x64, 0x15, 0x05, 0x07, 0x03, 0xFF, 0xFF})
        Nr(0x60, b);
    GoldenText::Expect("next", "cli-copper", State({"next", "copper", "count=5"}));
    _ports->DecodePortOut(0x303B, 3, 0);
    for (uint8_t b : {0x34, 0x56, 0x39, 0x85})
        _ports->DecodePortOut(0x57, b, 0);
    _ports->DecodePortOut(0x303B, 10, 0);
    for (uint8_t b : {0x10, 0x20, 0x00, 0xC7, 0xF3})
        _ports->DecodePortOut(0x57, b, 0);
    _ports->DecodePortOut(0x303B, 11, 0);
    for (uint8_t b : {0x05, 0xFB, 0x00, 0xC2, 0x40})
        _ports->DecodePortOut(0x57, b, 0);
    GoldenText::Expect("next", "cli-sprites", State({"next", "sprites"}));
}

// Prints the output the recipe .recipe/machines/next.md quotes (UNREAL_PRINT_NEXT_RECIPE=1; otherwise it checks only that the story runs):
// a program copies 256 bytes into the ROM through the DMA, the tilemap is placed in bank 7, a port is switched off
TEST_F(CliNextReports_Test, RecipeStory)
{
    const bool print = std::getenv("UNREAL_PRINT_NEXT_RECIPE") != nullptr;
    auto show = [&](const char* command, const std::string& text) {
        if (print)
            std::printf("$ %s\n%s\n", command, text.c_str());
    };
    auto out = [&](uint16_t port, std::initializer_list<uint8_t> bytes) {
        for (uint8_t b : bytes)
            _ports->DecodePortOut(port, b, 0);
    };
    // WR0 A->B #8000 length #0100, WR1 A memory inc, WR2 B memory inc, WR4 continuous B=#0100, LOAD, ENABLE: a copy into the ROM
    out(0x6B, {0x7D, 0x00, 0x80, 0x00, 0x01, 0x14, 0x10, 0xAD, 0x00, 0x01, 0xCF});
    show("state next dma", State({"next", "dma"}));
    EXPECT_NE(State({"next", "dma"}).find("dst_kind: rom"), std::string::npos);

    Nr(0x6B, 0xA0);  // tilemap on, attributes in the map
    Nr(0x6E, 0xA0);  // map base: bank 7, offset #2000
    Nr(0x15, 0x08);  // layer order LUS
    show("state next video", State({"next", "video"}));
    EXPECT_NE(State({"next", "video"}).find("map_bank: 7"), std::string::npos);

    Nr(0x82, 0xDF);
    show("state next ports 6B w", State({"next", "ports", "6B", "w"}));
    _ports->Board().Journal().SetEnabled(true);
    show("next nextreg 07 03", Next({"nextreg", "07", "03"}));
    show("state next nextreg 07", State({"next", "nextreg", "07"}));
    show("state next journal regs=07", State({"next", "journal", "regs=07"}));
    show("state next palette sprites_1 0-3", State({"next", "palette", "sprites_1", "0-3"}));
    // a copper list: wait for line 100, switch the layer order, halt
    Nr(0x61, 0);
    Nr(0x62, 0);
    for (uint8_t b : {0x98, 0x64, 0x15, 0x05, 0xFF, 0xFF})
        Nr(0x60, b);
    show("state next copper count=3", State({"next", "copper", "count=3"}));
    // a sprite, and the pattern it uses
    _ports->DecodePortOut(0x303B, 0, 0);
    for (int i = 0; i < 256; i++)
        _ports->DecodePortOut(0x5B, 0x11, 0);
    _ports->DecodePortOut(0x303B, 0, 0);
    for (uint8_t b : {0x40, 0x40, 0x00, 0x80})
        _ports->DecodePortOut(0x57, b, 0);
    show("state next sprites", State({"next", "sprites"}));
}

TEST(CliNextFormat_Test, TheNextCommandWithoutAnEmulatorSaysSo)
{
    EXPECT_NE(CliNext::NextCommand(nullptr, {"nextreg", "07", "03"}, "\n").find("No emulator"), std::string::npos);
}

TEST(CliNextFormat_Test, TheNextCommandOnAnotherMachineIsRefused)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EXPECT_NE(CliNext::NextCommand(emulator, {"nextreg", "07", "03"}, "\n").find("Error: not a ZX Spectrum Next"), std::string::npos);
    EXPECT_NE(CliNext::StateText(emulator->GetContext(), {"next", "dma"}, "\n").find("Error: not a ZX Spectrum Next"), std::string::npos);
    EmulatorTestHelper::CleanupEmulator(emulator);
}
