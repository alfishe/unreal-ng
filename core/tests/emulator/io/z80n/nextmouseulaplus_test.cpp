// The Next's Kempston mouse (ports #FADF / #FBDF / #FFDF) and ULA+ (ports #BF3B / #FF3B), as zxnext.vhd decodes them:
// port_fadf / fbdf / ffdf (2666), port_fadf_dat (3560), the ULA+ block (4520-4583), cpu_requester_dat (4741), the port enable
// word (NR #83 bit 5 the mouse, NR #85 bit 0 ULA+)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/io/z80n/nextboard.h"
#include "emulator/ports/models/portdecoder_next.h"

namespace
{
constexpr uint16_t kPortButtons = 0xFADF, kPortX = 0xFBDF, kPortY = 0xFFDF, kPortUlaPlusSelect = 0xBF3B, kPortUlaPlusData = 0xFF3B;
constexpr uint16_t kPc = 0x8000;
constexpr uint8_t kRegPeripheral1 = 0x05, kRegMouseControl = 0x0A, kRegUlaControl = 0x68, kRegEnable1 = 0x83, kRegEnable3 = 0x85;
constexpr uint8_t kMouseBit = 0x20;   // NR #83 bit 5 (word bit 13)
constexpr uint8_t kUlaPlusBit = 0x01; // NR #85 bit 0 (word bit 24)
constexpr uint8_t kIdleBus = 0xFF;
}  // namespace

class NextMouseUlaPlus_Test : public ::testing::Test
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
    void Nr(uint8_t reg, uint8_t value) { _ports->Board().WriteNextReg(reg, value); }
    uint8_t In(uint16_t port) { return _ports->DecodePortIn(port, kPc); }
    void Out(uint16_t port, uint8_t value) { _ports->DecodePortOut(port, value, kPc); }
};

TEST_F(NextMouseUlaPlus_Test, TheMouseRegistersAreXYAndTheButtonsWithTheWheelOnTop)
{
    Mouse* mouse = _context->pMouse;
    ASSERT_NE(mouse, nullptr);
    mouse->SetPresent(true);
    mouse->SetCounters(0x12, 0x34);
    mouse->SetButtons(0xFF);  // all released (active low)
    EXPECT_EQ(In(kPortX), 0x12);
    EXPECT_EQ(In(kPortY), 0x34);
    EXPECT_EQ(In(kPortButtons) & 0x0F, 0x0F) << "bit 3 is one, the three buttons released";
    mouse->SetButtons(static_cast<uint8_t>(~0x01));  // left down (D0 of the device)
    EXPECT_EQ(In(kPortButtons) & 0x07, 0x05) << "the board's D1 is the left button: pressed -> 0";
    mouse->SetButtons(static_cast<uint8_t>(~0x02));  // right down
    EXPECT_EQ(In(kPortButtons) & 0x07, 0x06) << "D0 is the right button";
    mouse->SetButtons(static_cast<uint8_t>(~0x04));  // middle
    EXPECT_EQ(In(kPortButtons) & 0x07, 0x03) << "D2 is the middle one";
    mouse->SetButtons(0xFF);
    mouse->SetWheel(3);
    EXPECT_EQ(In(kPortButtons) >> 4, 0x03) << "the wheel counter is the upper nibble (always: the board has the wheel)";
}

TEST_F(NextMouseUlaPlus_Test, NrA0SwapsTheButtons)
{
    Mouse* mouse = _context->pMouse;
    ASSERT_NE(mouse, nullptr);
    mouse->SetPresent(true);
    mouse->SetButtons(static_cast<uint8_t>(~0x01));  // left
    EXPECT_EQ(In(kPortButtons) & 0x03, 0x01) << "left pressed: D1 low";
    Nr(kRegMouseControl, 0x08);                        // NR #0A bit 3: reverse the buttons
    EXPECT_EQ(In(kPortButtons) & 0x03, 0x02) << "reversed: D0 low";
}

TEST_F(NextMouseUlaPlus_Test, WithoutAMouseTheInputsIdleAndTheEnableBitSilencesThePorts)
{
    Mouse* mouse = _context->pMouse;
    ASSERT_NE(mouse, nullptr);
    mouse->SetPresent(false);
    EXPECT_EQ(In(kPortX), 0x00) << "no mouse: zero counts";
    EXPECT_EQ(In(kPortY), 0x00);
    EXPECT_EQ(In(kPortButtons), 0x0F) << "buttons released, no wheel";
    mouse->SetPresent(true);
    mouse->SetCounters(7, 9);
    EXPECT_EQ(In(kPortX), 7);
    Nr(kRegEnable1, static_cast<uint8_t>(0xFF & ~kMouseBit));
    EXPECT_EQ(In(kPortX), kIdleBus) << "NR #83 bit 5 off: the port is not decoded";
}

TEST_F(NextMouseUlaPlus_Test, ThePaletteIsWrittenAndReadAsGgGrrrBb)
{
    Out(kPortUlaPlusSelect, 0x05);                 // mode 0, entry 5
    Out(kPortUlaPlusData, 0xE5);                   // G = 7, R = 1, B = 1
    EXPECT_EQ(In(kPortUlaPlusData), 0xE5) << "read back through the same format";
    // the entry sits in the ULA palette at 192 + 5: R 1, G 7, B 1 (blue low bit = B1 | B0)
    const uint16_t entry = _ports->Board().Video().PaletteEntry(0, 192 + 5);
    EXPECT_EQ((entry >> 6) & 7, 1u) << "R";
    EXPECT_EQ((entry >> 3) & 7, 7u) << "G";
    EXPECT_EQ(entry & 7, 3u) << "B: the two bits and the or of them";
    Nr(0x43, 0x40);                                // the write select: the second ULA palette
    Out(kPortUlaPlusData, 0x1C);                   // G = 0, R = 7
    EXPECT_EQ((_ports->Board().Video().PaletteEntry(4, 192 + 5) >> 6) & 7, 7u) << "into the second palette";
}

TEST_F(NextMouseUlaPlus_Test, ModeOneSetsTheEnableWhichIsNr68Bit3)
{
    EXPECT_EQ(_ports->Board().Stored(kRegUlaControl) & 0x08, 0);
    Out(kPortUlaPlusSelect, 0x40);                 // mode 1
    EXPECT_EQ(In(kPortUlaPlusData), 0x00);
    Out(kPortUlaPlusData, 0x01);
    EXPECT_EQ(In(kPortUlaPlusData), 0x01);
    EXPECT_EQ(_ports->Board().Stored(kRegUlaControl) & 0x08, 0x08) << "the same flop as NR #68 bit 3";
    Nr(kRegUlaControl, 0x00);
    EXPECT_EQ(In(kPortUlaPlusData), 0x00) << "NR #68 turns it off again";
}

TEST_F(NextMouseUlaPlus_Test, TheUlaPlusPortsFollowTheirEnableBit)
{
    Out(kPortUlaPlusSelect, 0x03);
    Out(kPortUlaPlusData, 0xAA);
    EXPECT_EQ(In(kPortUlaPlusData), 0xAA);
    Nr(kRegEnable3, static_cast<uint8_t>(0xFF & ~kUlaPlusBit));
    Out(kPortUlaPlusData, 0x00);
    EXPECT_EQ(In(kPortUlaPlusData), kIdleBus) << "NR #85 bit 0 off: #FF3B reads the idle bus";
    Nr(kRegEnable3, 0xFF);
    EXPECT_EQ(In(kPortUlaPlusData), 0xAA) << "the write while it was off was ignored";
    (void)kRegPeripheral1;
}

// ps2_mouse.v xydelta: NR #0A bits 1:0 scale the motion as the counters take it: 00 twice, 01 as is, 10 half, 11 a quarter
// (arithmetic shifts, the sign kept)
TEST_F(NextMouseUlaPlus_Test, TheDpiBitsScaleTheMotion)
{
    Mouse* mouse = _context->pMouse;
    ASSERT_NE(mouse, nullptr);
    mouse->SetPresent(true);
    mouse->SetCounters(0, 0);
    Nr(kRegMouseControl, 0x00);
    mouse->Move(3, -2);
    EXPECT_EQ(mouse->GetX(), 6);
    EXPECT_EQ(mouse->GetY(), 0xFC);
    Nr(kRegMouseControl, 0x02);
    mouse->Move(-5, 6);  // -5 >> 1 = -3, 6 >> 1 = 3
    EXPECT_EQ(mouse->GetX(), 3);
    EXPECT_EQ(mouse->GetY(), 0xFF);
    Nr(kRegMouseControl, 0x03);
    mouse->Move(9, -9);  // 9 >> 2 = 2, -9 >> 2 = -3
    EXPECT_EQ(mouse->GetX(), 5);
    EXPECT_EQ(mouse->GetY(), 0xFC);
    Nr(kRegMouseControl, 0x01);
    mouse->Move(1, 1);
    EXPECT_EQ(mouse->GetX(), 6) << "01 is the default: one for one";
}
