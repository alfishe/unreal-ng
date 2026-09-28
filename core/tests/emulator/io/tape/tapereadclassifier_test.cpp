#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

#include "emulator/io/tape/tapereadclassifier.h"

/// Unit tests for TapeReadClassifier (loader-follow design §4.1, test T1):
/// the code after an IN from #FE decides whether a program listens to the
/// tape (EAR bit) or reads the keyboard. Each case places the IN at $8000 and
/// classifies from the address after it.
class TapeReadClassifier_Test : public ::testing::Test
{
protected:
    std::array<uint8_t, 0x10000> _memory{};

    static uint8_t Read(void* context, uint16_t address)
    {
        return (*static_cast<std::array<uint8_t, 0x10000>*>(context))[address];
    }

    /// Places `code` (the IN first) at $8000 and classifies the read
    TapeReadKind Classify(const std::vector<uint8_t>& code)
    {
        _memory.fill(0xFF);  // RST 38 after the code: an unconditional transfer stops the tracker
        for (size_t i = 0; i < code.size(); i++)
            _memory[0x8000 + i] = code[i];

        const bool twoByteIn = code.size() >= 2 && (code[0] == 0xDB || code[0] == 0xED);
        return TapeReadClassifier::Classify(&Read, &_memory, static_cast<uint16_t>(0x8000 + (twoByteIn ? 2 : 1)));
    }
};

TEST_F(TapeReadClassifier_Test, RomLdSampleIsEar)
{
    // IN A,(#FE); RRA; RET NC (BREAK check, falls through); XOR C; AND #20
    EXPECT_EQ(Classify({ 0xDB, 0xFE, 0x1F, 0xD0, 0xA9, 0xE6, 0x20 }), TapeReadKind::Ear);
}

TEST_F(TapeReadClassifier_Test, RomKeyScanIsKey)
{
    // IN A,(C); CPL; AND #1F
    EXPECT_EQ(Classify({ 0xED, 0x78, 0x2F, 0xE6, 0x1F }), TapeReadKind::Key);
}

TEST_F(TapeReadClassifier_Test, AnyKeyWaitIsKey)
{
    // IN A,(#FE); OR #E0; INC A; JR Z,loop
    EXPECT_EQ(Classify({ 0xDB, 0xFE, 0xF6, 0xE0, 0x3C, 0x28, 0xF9 }), TapeReadKind::Key);
}

TEST_F(TapeReadClassifier_Test, RomBreakKeyIsKey)
{
    // BREAK-KEY: IN A,(#FE); RRA; RET C; LD A,#FE (overwrites the value)
    EXPECT_EQ(Classify({ 0xDB, 0xFE, 0x1F, 0xD8, 0x3E, 0xFE }), TapeReadKind::Key);
}

TEST_F(TapeReadClassifier_Test, EarShiftedIntoCarryIsEar)
{
    // IN A,(#FE); RLA; RLA; JR NC
    EXPECT_EQ(Classify({ 0xDB, 0xFE, 0x17, 0x17, 0x30, 0xF9 }), TapeReadKind::Ear);
}

TEST_F(TapeReadClassifier_Test, EarShiftedIntoSignIsEar)
{
    // IN A,(#FE); ADD A,A; JP P
    EXPECT_EQ(Classify({ 0xDB, 0xFE, 0x87, 0xF2, 0x00, 0x80 }), TapeReadKind::Ear);
}

TEST_F(TapeReadClassifier_Test, AndMaskFortyIsEar)
{
    // IN A,(#FE); AND #40
    EXPECT_EQ(Classify({ 0xDB, 0xFE, 0xE6, 0x40 }), TapeReadKind::Ear);
}

TEST_F(TapeReadClassifier_Test, BitSixOnEachInRegisterIsEar)
{
    // IN r,(C); BIT 6,r for B, C, D, E, H, L, A
    for (uint8_t reg : { 0, 1, 2, 3, 4, 5, 7 })
    {
        const uint8_t in = static_cast<uint8_t>(0x40 | (reg << 3));
        const uint8_t bit6 = static_cast<uint8_t>(0x70 | reg);
        EXPECT_EQ(Classify({ 0xED, in, 0xCB, bit6 }), TapeReadKind::Ear) << "register " << int(reg);
    }
}

TEST_F(TapeReadClassifier_Test, BitSixOnAnotherRegisterIsNotEar)
{
    // IN E,(C); BIT 6,D: D was not read from the port
    EXPECT_NE(Classify({ 0xED, 0x58, 0xCB, 0x72 }), TapeReadKind::Ear);
}

TEST_F(TapeReadClassifier_Test, CopyToAccumulatorIsFollowed)
{
    // IN E,(C); LD A,E; AND #40
    EXPECT_EQ(Classify({ 0xED, 0x58, 0x7B, 0xE6, 0x40 }), TapeReadKind::Ear);
}

TEST_F(TapeReadClassifier_Test, BreakCheckThenEarTestIsEar)
{
    // A loader checking BREAK from the same IN first: BIT 0,A; JR Z; AND #40
    EXPECT_EQ(Classify({ 0xDB, 0xFE, 0xCB, 0x47, 0x28, 0x10, 0xE6, 0x40 }), TapeReadKind::Ear);
}

TEST_F(TapeReadClassifier_Test, JoystickRowMaskIsKey)
{
    // Sinclair joystick: IN A,(#FE) with A=#EF; AND #1F
    EXPECT_EQ(Classify({ 0xDB, 0xFE, 0xE6, 0x1F }), TapeReadKind::Key);
}

TEST_F(TapeReadClassifier_Test, UnfollowableCodeIsOther)
{
    // IN A,(#FE); AND D (mask kept in a register); IN A,(#FE); JR loop
    EXPECT_EQ(Classify({ 0xDB, 0xFE, 0xA2 }), TapeReadKind::Other);
    EXPECT_EQ(Classify({ 0xDB, 0xFE, 0x18, 0xFC }), TapeReadKind::Other);
}

TEST_F(TapeReadClassifier_Test, BlockInputIsOther)
{
    // INI has no register destination
    EXPECT_EQ(Classify({ 0xED, 0xA2 }), TapeReadKind::Other);
}
