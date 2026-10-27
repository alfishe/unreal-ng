// The +3 FDC I/O trap (zxnext.vhd 2578-2602, 2723-2725, 3825-3896): NR #D8 bit 0 arms it; a read of #2FFD, a read or a write of #3FFD
// takes the M1 NMI path (NR #06 bit 3) and latches the cause in NR #DA (NR #02 bit 4 reads it as a flag) and a written byte in NR #D9
#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextboard.h"
#include "emulator/ports/models/portdecoder_next.h"

namespace
{
constexpr uint16_t kPc = 0x8000;
constexpr uint8_t kRegReset = 0x02, kRegButtons = 0x06, kRegTrap = 0xD8, kRegTrapWrite = 0xD9, kRegTrapCause = 0xDA;
}  // namespace

class NextIoTrap_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    PortDecoder_Next* _ports = nullptr;
    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _ports = dynamic_cast<PortDecoder_Next*>(_emulator->GetContext()->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
    void Nr(uint8_t reg, uint8_t value) { _ports->Board().WriteNextReg(reg, value); }
    uint8_t Rd(uint8_t reg) { return _ports->Board().Read(reg); }
};

TEST_F(NextIoTrap_Test, ItDoesNothingUntilNrD8Bit0)
{
    _ports->DecodePortIn(0x2FFD, kPc);
    _ports->DecodePortOut(0x3FFD, 0x55, kPc);
    EXPECT_EQ(Rd(kRegTrapCause), 0);
    EXPECT_EQ(Rd(kRegReset) & 0x10, 0);
}

TEST_F(NextIoTrap_Test, TheCauseIsWhichPortAndWhichDirection)
{
    Nr(kRegTrap, 0x01);
    _ports->DecodePortIn(0x2FFD, kPc);
    EXPECT_EQ(Rd(kRegTrapCause), 1) << "read of #2FFD";
    EXPECT_EQ(Rd(kRegReset) & 0x10, 0x10) << "NR #02 bit 4 is the flag";
    Nr(kRegReset, 0x00);  // bit 4 written as 0 clears the cause
    EXPECT_EQ(Rd(kRegTrapCause), 0);
    EXPECT_EQ(Rd(kRegReset) & 0x10, 0);
    _ports->DecodePortIn(0x3FFD, kPc);
    EXPECT_EQ(Rd(kRegTrapCause), 2) << "read of #3FFD";
    Nr(kRegReset, 0x00);
    _ports->DecodePortOut(0x3FFD, 0xA7, kPc);
    EXPECT_EQ(Rd(kRegTrapCause), 3) << "write of #3FFD";
    EXPECT_EQ(Rd(kRegTrapWrite), 0xA7) << "NR #D9 holds the written byte";
    Nr(kRegReset, 0x10);  // bit 4 written as 1 keeps it
    EXPECT_EQ(Rd(kRegTrapCause), 3);
}

TEST_F(NextIoTrap_Test, OtherFdPortsAndAWriteOf2ffdAreNotTrapped)
{
    Nr(kRegTrap, 0x01);
    _ports->DecodePortOut(0x2FFD, 0x11, kPc);  // #2FFD is trapped on a read only
    EXPECT_EQ(Rd(kRegTrapCause), 0);
    _ports->DecodePortIn(0x1FFD, kPc);         // the paging port
    _ports->DecodePortIn(0x7FFD, kPc);
    EXPECT_EQ(Rd(kRegTrapCause), 0);
}

TEST_F(NextIoTrap_Test, ItTakesTheM1NmiPathBehindNr06Bit3)
{
    Nr(kRegTrap, 0x01);
    Nr(kRegButtons, 0x00);  // the M1 button's enable (NR #06 bit 3) off
    _ports->DecodePortIn(0x2FFD, kPc);
    EXPECT_FALSE(_ports->Multiface().NmiHold()) << "the cause is latched, no NMI";
    EXPECT_EQ(Rd(kRegTrapCause), 1);
    Nr(kRegReset, 0x00);
    Nr(kRegButtons, 0x08);
    _ports->DecodePortIn(0x2FFD, kPc);
    EXPECT_TRUE(_ports->Multiface().NmiHold()) << "the Multiface session is on its way";
}

TEST_F(NextIoTrap_Test, AResetClearsTheEnableTheByteAndTheCause)
{
    Nr(kRegTrap, 0x01);
    _ports->DecodePortOut(0x3FFD, 0x42, kPc);
    EXPECT_EQ(Rd(kRegTrapCause), 3);
    _ports->Board().Reset(false);
    EXPECT_EQ(Rd(kRegTrap) & 1, 0);
    EXPECT_EQ(Rd(kRegTrapCause), 0);
    EXPECT_EQ(Rd(kRegTrapWrite), 0);
}

// zxnext.vhd 3507-3534: the joystick connector's pin 7 in the I/O mode: NR #0B bit 0 in mode 00, CTC channel 3's ZC/TO toggles it in mode
// 01 (with bit 0 clear only 0 -> 1), the UART's TX in modes 1x; it resets to 1
TEST_F(NextIoTrap_Test, TheJoystickPin7FollowsNr0bAndCtcChannel3)
{
    EXPECT_TRUE(_ports->JoystickPin7()) << "reset: 1";
    Nr(0x0B, 0x80);  // mode 00, bit 0 = 0
    EXPECT_FALSE(_ports->JoystickPin7());
    Nr(0x0B, 0x81);
    EXPECT_TRUE(_ports->JoystickPin7());
    Nr(0x0B, 0x90);  // mode 01, bit 0 = 0: the pin keeps 1 and cannot toggle off
    _ports->Ctc().onZeroCount(3);
    EXPECT_TRUE(_ports->JoystickPin7());
    Nr(0x0B, 0x80);
    EXPECT_FALSE(_ports->JoystickPin7());
    Nr(0x0B, 0x90);
    _ports->Ctc().onZeroCount(2);
    EXPECT_FALSE(_ports->JoystickPin7()) << "only channel 3 counts";
    _ports->Ctc().onZeroCount(3);
    EXPECT_TRUE(_ports->JoystickPin7()) << "0 -> 1 with bit 0 clear";
    _ports->Ctc().onZeroCount(3);
    EXPECT_TRUE(_ports->JoystickPin7()) << "and it stays";
    Nr(0x0B, 0x91);  // bit 0 set: a square wave
    _ports->Ctc().onZeroCount(3);
    EXPECT_FALSE(_ports->JoystickPin7());
    _ports->Ctc().onZeroCount(3);
    EXPECT_TRUE(_ports->JoystickPin7());
    Nr(0x0B, 0xA0);  // mode 10: the UART's TX, idle high
    EXPECT_TRUE(_ports->JoystickPin7());
}
