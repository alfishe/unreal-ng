// What a soft and a hard reset reset on the Next, against zxnext.vhd (core 3.02.03): the `if reset = '1'` block of the NextREG
// process (4930-5106) is the soft reset's list; NR #02 bit 1 loads the FPGA configuration again (zxnext_top_issue5.vhd 1095), so a hard
// reset is the power-on state; the palette RAMs (dpram2, 6962-7026) and the sprite / copper RAMs have no reset input.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextboard.h"
#include "emulator/ports/models/portdecoder_next.h"

class NextReset_Test : public ::testing::Test
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
    NextBoard& Board() { return _ports->Board(); }
    void Nr(uint8_t reg, uint8_t value) { Board().WriteNextReg(reg, value); }
    uint8_t Nr(uint8_t reg) { return Board().Read(reg); }
};

// zxnext.vhd 4603: `nr_register <= X"24"` on every reset, "protection against legacy programs accidentally hitting ports 0x243B, 0x253B"
TEST_F(NextReset_Test, TheRegisterSelectIsNr24AfterEveryReset)
{
    Board().SelectRegister(0x07);
    _ports->PerformReset(false);
    EXPECT_EQ(Board().SelectedRegister(), 0x24);
    Board().SelectRegister(0x07);
    _ports->PerformReset(true);
    EXPECT_EQ(Board().SelectedRegister(), 0x24);
}

// 4930-5106: what the soft reset resets...
TEST_F(NextReset_Test, ASoftResetResetsTheRegistersTheVhdlNames)
{
    Nr(0x06, 0x00);
    Nr(0x08, 0x4A);  // contention off, DAC, turbosound
    Nr(0x09, 0xF0);  // mono for the three chips, sprite tie
    Nr(0x12, 0x20);
    Nr(0x13, 0x21);
    Nr(0x14, 0x00);
    for (uint8_t reg : {0x15, 0x16, 0x17, 0x26, 0x27, 0x30, 0x31, 0x32, 0x33, 0x68, 0x6A, 0x6B, 0x6C, 0x70, 0x71})
        Nr(reg, 0x0F);
    Nr(0x4A, 0x11);
    Nr(0x4B, 0x22);
    Nr(0x4C, 0x03);
    Nr(0x6E, 0x11);
    Nr(0x6F, 0x11);
    Nr(0xA0, 0x39);
    Nr(0xA2, 0xFF);
    Nr(0x0B, 0x80);

    _ports->PerformReset(false);

    EXPECT_EQ(Nr(0x06), 0xA0) << "the CPU speed and 50 / 60 Hz hotkeys come back on (6 and 5)";
    EXPECT_EQ(Nr(0x08) & 0x7F, 0x0A) << "only the contention disable is reset";
    EXPECT_EQ(Nr(0x09), 0xE0) << "only the sprite tie is reset (bit 3 reads 0)";
    EXPECT_EQ(Nr(0x12), 0x08);
    EXPECT_EQ(Nr(0x13), 0x0B);
    EXPECT_EQ(Nr(0x14), 0xE3);
    for (uint8_t reg : {0x15, 0x16, 0x17, 0x26, 0x27, 0x30, 0x31, 0x32, 0x33, 0x68, 0x6A, 0x6B, 0x6C, 0x70, 0x71})
        EXPECT_EQ(Nr(reg), 0x00) << "NR #" << std::hex << int(reg);
    EXPECT_EQ(Nr(0x4A), 0xE3);
    EXPECT_EQ(Nr(0x4B), 0xE3);
    EXPECT_EQ(Nr(0x4C), 0x0F);
    EXPECT_EQ(Nr(0x6E), 0x2C);
    EXPECT_EQ(Nr(0x6F), 0x0C);
    EXPECT_EQ(Nr(0xA0), 0x00);
    EXPECT_EQ(Nr(0xA2), 0x02) << "bit 1 reads 1";
    EXPECT_EQ(Nr(0x0B), 0x01);
}

// ... and what it keeps: the power-on-only signals (joysticks, NR #0A, #7F, #81, #8A, #8F) and the high / low nibble pairs
TEST_F(NextReset_Test, ASoftResetKeepsTheRestAndNr80TakesItsLowNibbleUp)
{
    Nr(0x05, 0xC3);
    Nr(0x0A, 0x1B);
    Nr(0x7F, 0x12);
    Nr(0x81, 0x58);
    Nr(0x8A, 0x2A);
    Nr(0x8F, 0x01);
    Nr(0x80, 0x35);
    const uint8_t nr0a = Nr(0x0A);

    _ports->PerformReset(false);

    EXPECT_EQ(Nr(0x05), 0xC3);
    EXPECT_EQ(Nr(0x0A), nr0a);
    EXPECT_EQ(Board().Stored(0x7F), 0x12);
    EXPECT_EQ(Board().Stored(0x81), 0x58);
    EXPECT_EQ(Nr(0x8A), 0x2A);
    EXPECT_EQ(Board().Stored(0x8F), 0x01);
    EXPECT_EQ(Board().Stored(0x80), 0x55) << "ym / zxnext.vhd 2186: nr_80_expbus(7:4) <= nr_80_expbus(3:0)";

    _ports->PerformReset(true);
    EXPECT_EQ(Nr(0x05), 0x41) << "the configuration load: the declared values";
    EXPECT_EQ(Board().Stored(0x7F), 0xFF);
    EXPECT_EQ(Board().Stored(0x80), 0x00);
}

// 5052-5068: NR #82-#85 are restored by a reset only when bit 7 of NR #85 is 1; NR #86-#89 when bit 7 of NR #89 is 0
TEST_F(NextReset_Test, ThePortEnablesFollowTheirResetTypeBits)
{
    Nr(0x85, 0x00);
    Nr(0x82, 0x12);
    Nr(0x89, 0x80);
    Nr(0x86, 0x34);
    _ports->PerformReset(false);
    EXPECT_EQ(Board().Stored(0x82), 0x12) << "reset type 0: the enables stay";
    EXPECT_EQ(Board().Stored(0x86), 0x34) << "reset type 1 on the bus word: they stay";

    Nr(0x85, 0x85);
    Nr(0x89, 0x05);
    _ports->PerformReset(false);
    EXPECT_EQ(Board().Stored(0x82), 0xFF);
    EXPECT_EQ(Nr(0x85), 0x8F) << "reset type 1 & \"000\" & the four enables";
    EXPECT_EQ(Board().Stored(0x86), 0xFF);
    EXPECT_EQ(Nr(0x89), 0x0F) << "the restore sets the four enables, the reset type bit stays 0";
}

// 6962-7026: the palette index, control and stored byte are reset, the palette RAMs are not; the configuration load gives them
// their power-on contents
TEST_F(NextReset_Test, APaletteSurvivesASoftResetAndNotAHardOne)
{
    Nr(0x43, 0x00);
    Nr(0x40, 3);
    Nr(0x41, 0x1C);
    Nr(0x43, 0x10);  // Layer 2 first palette, auto increment on
    Nr(0x40, 0x80);
    Nr(0x43, 0x40);
    Nr(0x40, 7);
    EXPECT_EQ(Nr(0x40), 7);

    _ports->PerformReset(false);
    EXPECT_EQ(Nr(0x40), 0);
    EXPECT_EQ(Nr(0x43), 0);
    Nr(0x40, 3);
    EXPECT_EQ(Nr(0x41), 0x1C) << "the ULA palette entry written before the soft reset";

    _ports->PerformReset(true);
    Nr(0x40, 3);
    EXPECT_EQ(Nr(0x41), 0xA2) << "the power-on ULA colour 3";
}

// sprites.vhd 520-1030: reset_i clears the indices, the mirror and the status flags; the attribute RAM (sdpram_128_8) is the
// configuration's
TEST_F(NextReset_Test, SpriteRamsStayOnASoftResetAndGoOnAHardOne)
{
    NextSprites& sprites = Board().Sprites();
    sprites.WriteSlotSelect(5);
    sprites.WriteAttribute(0x11);
    Nr(0x34, 9);
    ASSERT_EQ(sprites.UploadSlot(), 5);

    _ports->PerformReset(false);
    EXPECT_EQ(sprites.Attribute(5, 0), 0x11);
    EXPECT_EQ(sprites.UploadSlot(), 0);
    EXPECT_EQ(Nr(0x34), 0);

    _ports->PerformReset(true);
    EXPECT_EQ(sprites.Attribute(5, 0), 0x00);
}

// zxnext.vhd 5003-5014 and copper.vhd: mode, address, stored byte and line offset are reset; the instruction RAM (dpram2) is not
TEST_F(NextReset_Test, TheCopperListStaysOnASoftResetButItsRegistersGo)
{
    Nr(0x61, 0x34);
    Nr(0x62, 0x01);  // address #134
    Nr(0x60, 0xAB);
    Nr(0x64, 0x05);
    ASSERT_EQ(Board().Copper().Instruction(0x9A), 0xAB00);
    ASSERT_EQ(Nr(0x64), 0x05);

    _ports->PerformReset(false);
    EXPECT_EQ(Nr(0x61), 0x00);
    EXPECT_EQ(Nr(0x62), 0x00);
    EXPECT_EQ(Nr(0x64), 0x00);
    EXPECT_EQ(Board().Copper().Instruction(0x9A), 0xAB00) << "block RAM keeps its contents over `reset`";

    _ports->PerformReset(true);
    EXPECT_EQ(Board().Copper().Instruction(0x9A), 0x0000);
}

// zxnext.vhd 5087-5090: the DivMMC entry-point registers return to #83 #01 #00 #CD on every reset; NR #0A bit 4 (the automap enable)
// is not reset (declared 0 only): the automap is off at power on and stays as the program left it over a soft reset. Nothing in the
// firmware's NEXLOAD (nexload.asm 323-349) writes these registers, so a direct NEX load leaves them as they are
TEST_F(NextReset_Test, TheDivMmcEntryPointsReturnOnEveryResetAndTheAutomapEnableDoesNot)
{
    EXPECT_EQ(Nr(0x0A) & 0x10, 0x00) << "automap off at power on";
    Nr(0x0A, 0x11);
    for (uint8_t reg : {0xB8, 0xB9, 0xBA, 0xBB})
        Nr(reg, 0x5A);

    _ports->PerformReset(false);
    EXPECT_EQ(Nr(0xB8), 0x83);
    EXPECT_EQ(Nr(0xB9), 0x01);
    EXPECT_EQ(Nr(0xBA), 0x00);
    EXPECT_EQ(Nr(0xBB), 0xCD);
    EXPECT_EQ(Nr(0x0A) & 0x10, 0x10) << "NR #0A is a power-on-only register";

    _ports->PerformReset(true);
    EXPECT_EQ(Nr(0x0A) & 0x10, 0x00);
}
