#include "stdafx.h"
#include "pch.h"

#include "scorpionmachine_test.h"

#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"

/// region <Fixture wiring>

/// @brief The synthetic bundle and pattern fill must be visible through every
///        Z80 window: fixed banks 5/2, default bank 3 = page 0, and the BASIC 128
///        ROM (bundle page 0) at #0000 after the decoder reset.
TEST_F(ScorpionMachine_Test, PowerOnBankMap)
{
    EXPECT_EQ(BankTag(0x0000), 0xC0) << "boot ROM is bundle page 0 (BASIC 128)";
    EXPECT_EQ(BankTag(0x4000), 0x45) << "fixed window #4000 maps RAM page 5";
    EXPECT_EQ(BankTag(0x8000), 0x42) << "fixed window #8000 maps RAM page 2";
    EXPECT_EQ(BankTag(0xC000), 0x40) << "default bank 3 maps RAM page 0";
}

/// @brief WritePort must reach the decoder's #7FFD handler (bank select).
TEST_F(ScorpionMachine_Test, WritePortSelectsBank)
{
    WritePort(0x7FFD, 0x03);
    EXPECT_EQ(BankTag(0xC000), 0x43) << "OUT (#7FFD),03h maps RAM page 3 at #C000";

    // Fixed windows are unaffected by bank paging
    EXPECT_EQ(BankTag(0x4000), 0x45);
    EXPECT_EQ(BankTag(0x8000), 0x42);
}

/// @brief RunTStates executes real Z80 code: a hand-assembled OUT (C),A at #8000
///        (RAM bank 2) selects page 3 through the full CPU -> decoder -> memory path.
TEST_F(ScorpionMachine_Test, ScriptedOutPagingProgram)
{
    // LD BC,#7FFD / LD A,#03 / OUT (C),A / JR $
    static const uint8_t program[] = {0x01, 0xFD, 0x7F, 0x3E, 0x03, 0xED, 0x79, 0x18, 0xFE};
    for (size_t i = 0; i < sizeof(program); i++)
    {
        DirectWrite(static_cast<uint16_t>(0x8000 + i), program[i]);
    }

    Z80* z80 = _core->GetZ80();
    z80->pc = 0x8000;

    RunTStates(1000);

    EXPECT_EQ(BankTag(0xC000), 0x43) << "the scripted OUT (C),A paged RAM page 3 in at #C000";
}

/// @brief Reads of a memory-mapping port that no peripheral answers must return
///        0xFF (floating bus) - pinned here as the pre-Task-4 baseline.
TEST_F(ScorpionMachine_Test, ReadUndecodedPortReturnsHigh)
{
    EXPECT_EQ(ReadPort(0x1FFD), 0xFF);
}

/// @brief The Scorpion power-on border latch is black (hardware-reference 6):
///        v2.9x ROMs never write #FF during boot, so the machine shows a black
///        border until software sets one. Other models keep their white reset.
TEST_F(ScorpionMachine_Test, PowerOnBorderIsBlack)
{
    EXPECT_EQ(_context->pScreen->GetBorderColor(), COLOR_BLACK);
    EXPECT_EQ(_context->emulatorState.border_attr, 0x00);
    EXPECT_EQ(_context->emulatorState.pFE & 0x07, 0x00) << "pFE border bits must match the black latch";
}

/// endregion </Fixture wiring>
