// The Next's copper and sprites reports (docs/inprogress/2026-10-07-zx-next/design-automation-coverage.md, phase B): next_copper and
// next_sprites, read from a machine that was programmed through its ports and registers. Source: D (design; the copper and the
// sprite attribute layout are copper.vhd's and sprites.vhd's, cited in nextcopper.h / nextsprites.h)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/goldentext.h"
#include "_helpers/nextreporthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextreportquery.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/state/devicestate.h"

class NextReportsB_Test : public ::testing::Test
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
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
    void Out(uint16_t port, uint8_t value) { _ports->DecodePortOut(port, value, 0); }
    uint8_t In(uint16_t port) { return _ports->DecodePortIn(port, 0); }
    void Nr(uint8_t reg, uint8_t value)
    {
        Out(0x243B, reg);
        Out(0x253B, value);
    }
    std::string P(const StateNode& node, const char* path) { return NextReportHelper::Path(node, path); }

    /// The copper list from instruction 0, through NR #60 (one byte at a time, auto-increment)
    void CopperList(std::initializer_list<uint16_t> words)
    {
        Nr(0x61, 0);
        Nr(0x62, 0);
        for (uint16_t w : words)
        {
            Nr(0x60, static_cast<uint8_t>(w >> 8));
            Nr(0x60, static_cast<uint8_t>(w & 0xFF));
        }
    }
    /// Five attribute bytes of sprite `slot` through port #303B / #57
    void Sprite(uint8_t slot, std::initializer_list<uint8_t> bytes)
    {
        Out(0x303B, slot);
        for (uint8_t b : bytes)
            Out(0x57, b);
    }
};

// ---------------------------------------------------------------------------------------------------------------- next_copper

TEST_F(NextReportsB_Test, CopperPowerOnState)
{
    const StateNode r = DeviceState::NextCopper(_context, NextCopperQuery{});
    EXPECT_EQ(P(r, "available"), "on");
    EXPECT_EQ(P(r, "control.mode"), "0");
    EXPECT_EQ(P(r, "control.name"), "stopped");
    EXPECT_EQ(P(r, "running"), "off");
    EXPECT_EQ(P(r, "address"), "0");
    EXPECT_EQ(P(r, "pc"), "0");
    EXPECT_EQ(P(r, "list_length"), "0");
    ASSERT_NE(r.find("instructions"), nullptr);
    EXPECT_EQ(r.find("instructions")->items.size(), 64u);
    EXPECT_EQ(P(r, "instructions.0.op"), "nop");
}

TEST_F(NextReportsB_Test, CopperListRoundTripsThroughTheDisassembler)
{
    // WAIT line 100 hpos 12; MOVE NR #15 <- 5; MOVE NR #07 <- 3; NOP; HALT (WAIT hpos 63 line 511)
    CopperList({0x9864, 0x1505, 0x0703, 0x0000, 0xFFFF});
    // the same words through the 16-bit port NR #63 land at 5 and 6
    Nr(0x63, 0x80);
    Nr(0x63, 0x10);  // WAIT line 16 hpos 0
    Nr(0x63, 0x14);
    Nr(0x63, 0xAA);  // MOVE NR #14 <- #AA
    const StateNode r = DeviceState::NextCopper(_context, NextCopperQuery{});
    EXPECT_EQ(P(r, "list_length"), "7");
    EXPECT_EQ(P(r, "instructions.0.word"), "0x9864");
    EXPECT_EQ(P(r, "instructions.0.op"), "wait");
    EXPECT_EQ(P(r, "instructions.0.vpos"), "100");
    EXPECT_EQ(P(r, "instructions.0.hpos"), "12");
    EXPECT_EQ(P(r, "instructions.0.text"), "WAIT line 100 hpos 12");
    EXPECT_EQ(P(r, "instructions.1.op"), "move");
    EXPECT_EQ(P(r, "instructions.1.reg"), "0x15");
    EXPECT_EQ(P(r, "instructions.1.value"), "0x05");
    EXPECT_EQ(P(r, "instructions.1.reg_name"), "Sprite and Layers System");
    EXPECT_EQ(P(r, "instructions.1.text"), "MOVE NR #15 <- #05");
    EXPECT_EQ(P(r, "instructions.2.reg"), "0x07");
    EXPECT_EQ(P(r, "instructions.3.op"), "nop");
    EXPECT_EQ(P(r, "instructions.4.op"), "halt");
    EXPECT_EQ(P(r, "instructions.4.text"), "HALT");
    EXPECT_EQ(P(r, "instructions.5.word"), "0x8010");
    EXPECT_EQ(P(r, "instructions.5.vpos"), "16");
    EXPECT_EQ(P(r, "instructions.6.word"), "0x14AA");
    EXPECT_EQ(P(r, "instructions.6.value"), "0xAA");
}

TEST_F(NextReportsB_Test, CopperAddressIsReportedWithoutAutoIncrement)
{
    Nr(0x61, 0x10);
    Nr(0x62, 0x02);
    const StateNode first = DeviceState::NextCopper(_context, NextCopperQuery{});
    EXPECT_EQ(P(first, "address"), "528");
    EXPECT_EQ(P(first, "word_index"), "264");
    EXPECT_EQ(P(first, "address_byte"), "msb");
    for (int i = 0; i < 3; i++)
        (void)DeviceState::NextCopper(_context, NextCopperQuery{});
    EXPECT_EQ(P(DeviceState::NextCopper(_context, NextCopperQuery{}), "address"), "528") << "R3: reading the report does not step the write address";
    Nr(0x60, 0x12);
    const StateNode after = DeviceState::NextCopper(_context, NextCopperQuery{});
    EXPECT_EQ(P(after, "address"), "529");
    EXPECT_EQ(P(after, "address_byte"), "lsb");
}

TEST_F(NextReportsB_Test, CopperControlModes)
{
    const char* names[4] = {"stopped", "restart_loop", "continue_loop", "restart_each_frame"};
    for (unsigned mode = 0; mode < 4; mode++)
    {
        Nr(0x62, static_cast<uint8_t>(mode << 6 | 0x03));
        const StateNode r = DeviceState::NextCopper(_context, NextCopperQuery{});
        EXPECT_EQ(P(r, "control.mode"), std::to_string(mode));
        EXPECT_EQ(P(r, "control.name"), names[mode]);
        EXPECT_EQ(P(r, "running"), mode ? "on" : "off");
        EXPECT_EQ(P(r, "address"), "768") << "bits 2:0 of NR #62 are the address MSB";
    }
    Nr(0x64, 0x20);
    EXPECT_EQ(P(DeviceState::NextCopper(_context, NextCopperQuery{}), "line_offset"), "32");
}

TEST_F(NextReportsB_Test, CopperPcFollowsTheRun)
{
    CopperList({0x1411, 0x1422, 0xFFFF});  // MOVE NR #14 <- #11; MOVE NR #14 <- #22; HALT
    Nr(0x62, 0x40);                          // start from index 0
    EXPECT_EQ(P(DeviceState::NextCopper(_context, NextCopperQuery{}), "pc"), "0");
    _ports->Board().Copper().RunTo(200);
    const StateNode r = DeviceState::NextCopper(_context, NextCopperQuery{});
    EXPECT_EQ(P(r, "pc"), "2") << "two MOVEs done, the HALT waits for a line that never comes";
    EXPECT_EQ(P(r, "instructions.2.current"), "on");
    EXPECT_EQ(P(r, "instructions.0.current"), "off");
    EXPECT_EQ(P(r, "running"), "on");
    EXPECT_EQ(_ports->Board().Read(0x14), 0x22);
}

TEST_F(NextReportsB_Test, CopperWindowAndAroundPc)
{
    CopperList({0x1411, 0x1422, 0x1433, 0x1444, 0x1455, 0x1466, 0xFFFF});
    Nr(0x62, 0x40);
    _ports->Board().Copper().RunTo(200);  // pc = 6 (the HALT)
    NextCopperQuery q;
    q.first = 0;
    q.count = 2;
    StateNode r = DeviceState::NextCopper(_context, q);
    EXPECT_EQ(r.find("instructions")->items.size(), 2u);
    ASSERT_NE(r.find("around_pc"), nullptr) << "the pc is outside the window: the words around it come too";
    EXPECT_EQ(P(r, "around_pc.3.index"), "6") << "pc - 3 .. pc + 4 starts at 3";
    EXPECT_EQ(P(r, "around_pc.3.current"), "on");
    q.first = 4;
    q.count = 4;
    r = DeviceState::NextCopper(_context, q);
    EXPECT_EQ(P(r, "instructions.0.index"), "4");
    EXPECT_EQ(P(r, "instructions.2.current"), "on");
    EXPECT_EQ(r.find("around_pc"), nullptr) << "the pc is inside the window";
    q.first = 1020;
    q.count = 64;
    EXPECT_EQ(DeviceState::NextCopper(_context, q).find("instructions")->items.size(), 4u) << "the window stops at the end of the list";
}

TEST_F(NextReportsB_Test, CopperRawIsTheWholeListAsHex)
{
    CopperList({0x9864, 0x1505});
    NextCopperQuery q;
    q.raw = true;
    const StateNode r = DeviceState::NextCopper(_context, q);
    const std::string raw = P(r, "raw");
    ASSERT_EQ(raw.size(), 4096u);
    EXPECT_EQ(raw.substr(0, 8), "98641505");
    EXPECT_EQ(raw.substr(8, 4), "0000");
    EXPECT_TRUE(DeviceState::NextCopper(_context, NextCopperQuery{}).find("raw") == nullptr);
}

TEST_F(NextReportsB_Test, CopperQueryParsing)
{
    NextCopperQuery q;
    std::string error;
    ASSERT_TRUE(NextCopperQueryFromStrings("", "", "", q, error)) << error;
    EXPECT_EQ(q.first, 0u);
    EXPECT_EQ(q.count, 64u);
    EXPECT_FALSE(q.raw);
    ASSERT_TRUE(NextCopperQueryFromStrings("100", "8", "true", q, error)) << error;
    EXPECT_EQ(q.first, 100u);
    EXPECT_EQ(q.count, 8u);
    EXPECT_TRUE(q.raw);
    ASSERT_TRUE(NextCopperQueryFromStrings("0x10", "", "", q, error)) << error;
    EXPECT_EQ(q.first, 16u);
    EXPECT_FALSE(NextCopperQueryFromStrings("1024", "", "", q, error));
    EXPECT_FALSE(NextCopperQueryFromStrings("", "0", "", q, error));
    EXPECT_FALSE(NextCopperQueryFromStrings("", "1025", "", q, error));
    EXPECT_FALSE(NextCopperQueryFromStrings("x", "", "", q, error));
    EXPECT_FALSE(NextCopperQueryFromStrings("", "", "maybe", q, error));
}

TEST_F(NextReportsB_Test, CopperGolden)
{
    CopperList({0x9864, 0x1505, 0x0703, 0x0000, 0xFFFF});
    Nr(0x62, 0x80);
    NextCopperQuery q;
    q.count = 6;
    GoldenText::Expect("next", "copper", DeviceState::ToText(DeviceState::NextCopper(_context, q)));
}

// --------------------------------------------------------------------------------------------------------------- next_sprites

TEST_F(NextReportsB_Test, SpritesPowerOnState)
{
    const StateNode r = DeviceState::NextSprites(_context, NextSpritesQuery{});
    EXPECT_EQ(P(r, "available"), "on");
    EXPECT_EQ(P(r, "enabled"), "off");
    EXPECT_EQ(P(r, "visible_count"), "0");
    EXPECT_EQ(r.find("sprites")->items.size(), 0u) << "default: the visible sprites only";
    EXPECT_EQ(P(r, "pattern_memory.size"), "16384");
    EXPECT_EQ(P(r, "pattern_memory.non_zero_bytes"), "0");
    EXPECT_EQ(P(r, "flags.collision"), "off");
}

TEST_F(NextReportsB_Test, SpritesBasicAttributesDecode)
{
    // x = #134 (X8 in byte 2 bit 0), y = #56, palette offset 3, x/y mirror, rotate, visible, pattern 5, no 5th byte
    Sprite(3, {0x34, 0x56, static_cast<uint8_t>(0x30 | 0x08 | 0x04 | 0x02 | 0x01), 0x85});
    const StateNode r = DeviceState::NextSprites(_context, NextSpritesQuery{});
    ASSERT_EQ(r.find("sprites")->items.size(), 1u);
    EXPECT_EQ(P(r, "sprites.0.index"), "3");
    EXPECT_EQ(P(r, "sprites.0.kind"), "basic");
    EXPECT_EQ(P(r, "sprites.0.x"), "308");
    EXPECT_EQ(P(r, "sprites.0.y"), "86");
    EXPECT_EQ(P(r, "sprites.0.palette_offset"), "3");
    EXPECT_EQ(P(r, "sprites.0.x_mirror"), "on");
    EXPECT_EQ(P(r, "sprites.0.y_mirror"), "on");
    EXPECT_EQ(P(r, "sprites.0.rotate"), "on");
    EXPECT_EQ(P(r, "sprites.0.pattern"), "5");
    EXPECT_EQ(P(r, "sprites.0.four_bit"), "off");
    EXPECT_EQ(P(r, "sprites.0.scale_x"), "1");
    EXPECT_EQ(P(r, "sprites.0.scale_y"), "1");
    EXPECT_EQ(P(r, "sprites.0.visible"), "on");
    EXPECT_EQ(P(r, "sprites.0.bytes"), "34 56 3F 85 00");
    EXPECT_EQ(P(r, "visible_count"), "1");
}

TEST_F(NextReportsB_Test, SpritesExtendedAnchorWithScaleFourBitAndNinthYBit)
{
    // x #10, y #20 + Y8, visible + extended, pattern 7 + N6 (4-bit pattern 15), unified type, x scale x4, y scale x2
    Sprite(10, {0x10, 0x20, 0x00, 0xC7, static_cast<uint8_t>(0x80 | 0x40 | 0x20 | (2 << 3) | (1 << 1) | 1)});
    const StateNode r = DeviceState::NextSprites(_context, NextSpritesQuery{});
    EXPECT_EQ(P(r, "sprites.0.index"), "10");
    EXPECT_EQ(P(r, "sprites.0.kind"), "anchor");
    EXPECT_EQ(P(r, "sprites.0.y"), "288");
    EXPECT_EQ(P(r, "sprites.0.four_bit"), "on");
    EXPECT_EQ(P(r, "sprites.0.pattern"), "15");
    EXPECT_EQ(P(r, "sprites.0.scale_x"), "4");
    EXPECT_EQ(P(r, "sprites.0.scale_y"), "2");
    EXPECT_EQ(P(r, "sprites.0.unified"), "on");
}

TEST_F(NextReportsB_Test, SpritesRelativeSpriteFollowsItsAnchor)
{
    Sprite(10, {0x10, 0x20, 0x00, 0xC7, static_cast<uint8_t>(0x80 | 0x40 | 0x20 | (2 << 3) | (1 << 1) | 1)});
    // relative: offset +5 / -5, visible, extended with byte 4 bits 7:6 = 01
    Sprite(11, {0x05, 0xFB, 0x00, 0xC2, 0x40});
    NextSpritesQuery q;
    const StateNode r = DeviceState::NextSprites(_context, q);
    ASSERT_EQ(r.find("sprites")->items.size(), 2u);
    EXPECT_EQ(P(r, "sprites.1.index"), "11");
    EXPECT_EQ(P(r, "sprites.1.kind"), "relative");
    EXPECT_EQ(P(r, "sprites.1.anchor"), "10");
    EXPECT_EQ(P(r, "sprites.1.offset_x"), "5");
    EXPECT_EQ(P(r, "sprites.1.offset_y"), "-5");
    EXPECT_EQ(P(r, "sprites.1.x"), "36") << "anchor x 16 + 5 scaled by the unified anchor's x4";
    EXPECT_EQ(P(r, "sprites.1.y"), "278") << "anchor y 288 - 5 scaled by x2";
    EXPECT_EQ(P(r, "visible_count"), "2");
}

TEST_F(NextReportsB_Test, SpritesAllAndPagingFromCount)
{
    Sprite(3, {0x34, 0x56, 0x00, 0x85});
    Sprite(100, {0x01, 0x02, 0x00, 0x81});
    NextSpritesQuery q;
    q.all = true;
    StateNode r = DeviceState::NextSprites(_context, q);
    EXPECT_EQ(r.find("sprites")->items.size(), 128u);
    q.first = 100;
    q.count = 5;
    r = DeviceState::NextSprites(_context, q);
    EXPECT_EQ(r.find("sprites")->items.size(), 5u);
    EXPECT_EQ(P(r, "sprites.0.index"), "100");
    EXPECT_EQ(P(r, "sprites.0.visible"), "on");
    q.all = false;
    q.first = 4;
    q.count = 128;
    r = DeviceState::NextSprites(_context, q);
    ASSERT_EQ(r.find("sprites")->items.size(), 1u) << "from 4: only sprite 100 is visible";
    EXPECT_EQ(P(r, "sprites.0.index"), "100");
    EXPECT_EQ(P(r, "visible_count"), "2") << "the count is over all 128, not the page";
}

TEST_F(NextReportsB_Test, SpritesSwitchesFollowNr15AndNr19)
{
    Nr(0x15, 0x01 | 0x02 | 0x20 | 0x40);
    Nr(0x19, 8);
    Nr(0x19, 200);
    Nr(0x19, 16);
    Nr(0x19, 180);
    const StateNode r = DeviceState::NextSprites(_context, NextSpritesQuery{});
    EXPECT_EQ(P(r, "enabled"), "on");
    EXPECT_EQ(P(r, "over_border"), "on");
    EXPECT_EQ(P(r, "clip_over_border"), "on");
    EXPECT_EQ(P(r, "zero_on_top"), "on");
    EXPECT_EQ(P(r, "clip.x1"), "8");
    EXPECT_EQ(P(r, "clip.x2"), "200");
    EXPECT_EQ(P(r, "clip.y1"), "16");
    EXPECT_EQ(P(r, "clip.y2"), "180");
}

TEST_F(NextReportsB_Test, SpritesPatternMemorySummary)
{
    Out(0x303B, 0x00);  // pattern upload at offset 0
    for (int i = 0; i < 256; i++)
        Out(0x5B, 0xAB);  // pattern 0 (8-bit)
    Out(0x303B, 0x02);  // pattern 2 at #200
    for (int i = 0; i < 10; i++)
        Out(0x5B, 0x01);
    const StateNode r = DeviceState::NextSprites(_context, NextSpritesQuery{});
    EXPECT_EQ(P(r, "pattern_memory.non_zero_bytes"), "266");
    EXPECT_EQ(P(r, "pattern_memory.used_patterns_8bit"), "2");
    ASSERT_NE(r.find("pattern_memory")->find("used"), nullptr);
    EXPECT_EQ(P(r, "pattern_memory.used.0"), "0");
    EXPECT_EQ(P(r, "pattern_memory.used.1"), "2");
}

TEST_F(NextReportsB_Test, SpritesReportDoesNotClearTheCollisionFlags)
{
    // two visible sprites on the same spot with a solid pattern: drawing a line raises the collision flag
    Out(0x303B, 0x00);
    for (int i = 0; i < 256; i++)
        Out(0x5B, 0x01);
    Sprite(0, {0x40, 0x40, 0x00, 0x80});
    Sprite(1, {0x40, 0x40, 0x00, 0x80});
    Nr(0x15, 0x01);
    NextSprites& sprites = _ports->Board().Sprites();
    NextSprites::Pixel line[NextSprites::kGridWidth];
    sprites.DrawLine(0x45, 0x01, _ports->Board().Video(), line, 0xE3);
    EXPECT_EQ(P(DeviceState::NextSprites(_context, NextSpritesQuery{}), "flags.collision"), "on");
    EXPECT_EQ(P(DeviceState::NextSprites(_context, NextSpritesQuery{}), "flags.collision"), "on") << "R3: a report does not read-and-clear";
    EXPECT_EQ(In(0x303B) & 1, 1) << "the port read still sees it, and clears it";
    EXPECT_EQ(P(DeviceState::NextSprites(_context, NextSpritesQuery{}), "flags.collision"), "off");
}

TEST_F(NextReportsB_Test, SpritesQueryParsing)
{
    NextSpritesQuery q;
    std::string error;
    ASSERT_TRUE(NextSpritesQueryFromStrings("", "", "", q, error)) << error;
    EXPECT_EQ(q.first, 0u);
    EXPECT_EQ(q.count, 128u);
    EXPECT_FALSE(q.all);
    ASSERT_TRUE(NextSpritesQueryFromStrings("16", "8", "1", q, error)) << error;
    EXPECT_EQ(q.first, 16u);
    EXPECT_EQ(q.count, 8u);
    EXPECT_TRUE(q.all);
    EXPECT_FALSE(NextSpritesQueryFromStrings("128", "", "", q, error));
    EXPECT_FALSE(NextSpritesQueryFromStrings("", "0", "", q, error));
    EXPECT_FALSE(NextSpritesQueryFromStrings("", "129", "", q, error));
    EXPECT_FALSE(NextSpritesQueryFromStrings("", "", "x", q, error));
}

TEST_F(NextReportsB_Test, SpritesGolden)
{
    Nr(0x15, 0x01);
    Sprite(3, {0x34, 0x56, static_cast<uint8_t>(0x30 | 0x08 | 0x01), 0x85});
    Sprite(10, {0x10, 0x20, 0x00, 0xC7, static_cast<uint8_t>(0x80 | 0x40 | 0x20 | (2 << 3) | (1 << 1) | 1)});
    Sprite(11, {0x05, 0xFB, 0x00, 0xC2, 0x40});
    Out(0x303B, 0x00);
    for (int i = 0; i < 16; i++)
        Out(0x5B, 0x11);
    GoldenText::Expect("next", "sprites", DeviceState::ToText(DeviceState::NextSprites(_context, NextSpritesQuery{})));
}

TEST_F(NextReportsB_Test, ReportsOnAnotherMachineAreUnavailable)
{
    Emulator* other = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(other, nullptr);
    EXPECT_EQ(P(DeviceState::NextCopper(other->GetContext(), NextCopperQuery{}), "available"), "off");
    EXPECT_EQ(P(DeviceState::NextSprites(other->GetContext(), NextSpritesQuery{}), "available"), "off");
    EmulatorTestHelper::CleanupEmulator(other);
}
