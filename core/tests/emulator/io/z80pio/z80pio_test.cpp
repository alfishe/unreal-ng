// Z80Pio (core/src/emulator/io/z80pio/z80pio.h): the control words, mode 1 strobes, the interrupt logic and the
// internal daisy chain of the Z80 PIO (Zilog UM0081; MAME z80pio.cpp), without a machine.

#include <gtest/gtest.h>

#include <cstdint>

#include "emulator/io/z80pio/z80pio.h"

namespace
{
constexpr uint8_t kA = 0;
constexpr uint8_t kB = 1;

/// AMX Art 1.1's initialisation of one port: mode 1, the vector, the interrupt control word #87 (enabled, no mask)
void ProgramLikeAmxArt(Z80Pio& pio, uint8_t port, uint8_t vector)
{
    pio.WriteControl(port, 0x4F);
    pio.WriteControl(port, vector);
    pio.WriteControl(port, 0x87);
}
}  // namespace

TEST(Z80Pio_Test, ResetIsModeOneWithInterruptsOffAndKeepsTheVector)
{
    Z80Pio pio;
    pio.WriteControl(kA, 0xF0);
    pio.WriteControl(kA, 0x0F);  // mode 0
    pio.WriteControl(kA, 0x87);
    pio.Reset();
    const Z80Pio::Port& a = pio.GetPort(kA);
    EXPECT_EQ(a.mode, Z80Pio::kModeInput);
    EXPECT_FALSE(a.ie);
    EXPECT_FALSE(a.ip);
    EXPECT_FALSE(a.ius);
    EXPECT_EQ(a.mask, 0xFF);
    EXPECT_EQ(a.vector, 0xF0) << "UM0081: the vector registers are not reset";
}

TEST(Z80Pio_Test, ControlWordsByTheirLowBits)
{
    Z80Pio pio;
    pio.WriteControl(kB, 0xF2);  // bit 0 = 0: the vector
    EXPECT_EQ(pio.GetPort(kB).vector, 0xF2);
    pio.WriteControl(kB, 0x0F);  // xx001111 with 00: mode 0
    EXPECT_EQ(pio.GetPort(kB).mode, Z80Pio::kModeOutput);
    pio.WriteControl(kB, 0x4F);
    EXPECT_EQ(pio.GetPort(kB).mode, Z80Pio::kModeInput);
    pio.WriteControl(kB, 0x8F);  // bidirectional: port A only
    EXPECT_EQ(pio.GetPort(kB).mode, Z80Pio::kModeInput);
    pio.WriteControl(kB, 0x83);  // the enable flip-flop alone
    EXPECT_TRUE(pio.GetPort(kB).ie);
    pio.WriteControl(kB, 0x03);
    EXPECT_FALSE(pio.GetPort(kB).ie);
}

/// z88dk's in_MouseAMXInit writes the vector, #97 (enabled, mask follows), then #4F: by UM0081 and MAME the #4F is
/// the mask, not a mode word; the port is in mode 1 only because reset left it there
TEST(Z80Pio_Test, AWordAfterMaskFollowsIsTheMask)
{
    Z80Pio pio;
    pio.Strobe(kA, 0x01);
    EXPECT_TRUE(pio.GetPort(kA).ip);
    pio.WriteControl(kA, 0x00);
    pio.WriteControl(kA, 0x97);
    EXPECT_FALSE(pio.GetPort(kA).ip) << "a mask that follows drops a pending interrupt";
    EXPECT_FALSE(pio.GetPort(kA).ie) << "held off until the mask is written";
    pio.WriteControl(kA, 0x4F);
    EXPECT_EQ(pio.GetPort(kA).mask, 0x4F);
    EXPECT_EQ(pio.GetPort(kA).mode, Z80Pio::kModeInput);
    EXPECT_TRUE(pio.GetPort(kA).ie);
}

TEST(Z80Pio_Test, ModeOneStrobeLatchesTheLinesAndRequests)
{
    Z80Pio pio;
    ProgramLikeAmxArt(pio, kA, 0xF0);
    EXPECT_EQ(pio.DaisyState(), 0);

    pio.SetLines(kA, 0x01);
    pio.SetStrobe(kA, false);
    EXPECT_EQ(pio.PeekData(kA), 0x01) << "/STB low: the latch is transparent";
    EXPECT_FALSE(pio.GetPort(kA).ip) << "the interrupt comes with the rising edge";
    pio.SetStrobe(kA, true);
    pio.SetLines(kA, 0x00);
    EXPECT_EQ(pio.PeekData(kA), 0x01) << "/STB high: the latched byte";
    EXPECT_EQ(pio.DaisyState(), Z80Pio::kDaisyInt);

    uint8_t vector = 0;
    ASSERT_TRUE(pio.Acknowledge(vector));
    EXPECT_EQ(vector, 0xF0);
    EXPECT_EQ(pio.DaisyState(), Z80Pio::kDaisyIeo) << "under service: IEO low";
    EXPECT_EQ(pio.ReadData(kA), 0x01);
    EXPECT_TRUE(pio.Ready(kA)) << "the CPU read raises RDY";
    pio.Reti();
    EXPECT_EQ(pio.DaisyState(), 0);
}

/// MAME sets IP with the interrupts off: enabling them later interrupts at once (UM0081: "if an interrupt is pending
/// when the enable flag is set, the interrupt is then enabled to the CPU")
TEST(Z80Pio_Test, PendingWhileDisabledInterruptsWhenEnabled)
{
    Z80Pio pio;
    pio.WriteControl(kB, 0xF2);
    pio.Strobe(kB, 0x00);
    EXPECT_TRUE(pio.GetPort(kB).ip);
    EXPECT_EQ(pio.DaisyState(), 0);
    pio.WriteControl(kB, 0x87);
    EXPECT_EQ(pio.DaisyState(), Z80Pio::kDaisyInt);
}

/// Port A sits above port B: with both pending A is taken first, and B waits while A is under service
TEST(Z80Pio_Test, PortAAbovePortB)
{
    Z80Pio pio;
    ProgramLikeAmxArt(pio, kA, 0xF0);
    ProgramLikeAmxArt(pio, kB, 0xF2);
    pio.Strobe(kB, 0x00);
    pio.Strobe(kA, 0x00);

    uint8_t vector = 0;
    ASSERT_TRUE(pio.Acknowledge(vector));
    EXPECT_EQ(vector, 0xF0);
    EXPECT_FALSE(pio.Acknowledge(vector)) << "B is held off by A's service";
    pio.Reti();
    ASSERT_TRUE(pio.Acknowledge(vector));
    EXPECT_EQ(vector, 0xF2);
    pio.Reti();
    EXPECT_FALSE(pio.GetPort(kB).ius);
}

/// B under service, then A requests: the higher port interrupts the lower one's handler (UM0081 nesting)
TEST(Z80Pio_Test, PortANestsIntoPortBsService)
{
    Z80Pio pio;
    ProgramLikeAmxArt(pio, kA, 0xF0);
    ProgramLikeAmxArt(pio, kB, 0xF2);
    pio.Strobe(kB, 0x00);
    uint8_t vector = 0;
    ASSERT_TRUE(pio.Acknowledge(vector));
    EXPECT_EQ(vector, 0xF2);
    pio.Strobe(kA, 0x01);
    EXPECT_EQ(pio.DaisyState(), Z80Pio::kDaisyInt | Z80Pio::kDaisyIeo);
    ASSERT_TRUE(pio.Acknowledge(vector));
    EXPECT_EQ(vector, 0xF0);
    pio.Reti();  // A's handler ends first
    EXPECT_FALSE(pio.GetPort(kA).ius);
    EXPECT_TRUE(pio.GetPort(kB).ius);
    pio.Reti();
    EXPECT_FALSE(pio.GetPort(kB).ius);
}

TEST(Z80Pio_Test, ModeThreeInterruptsOnTheMonitoredLines)
{
    Z80Pio pio;
    pio.WriteControl(kA, 0x10);
    pio.WriteControl(kA, 0xCF);  // mode 3
    pio.WriteControl(kA, 0x0F);  // lines 0-3 inputs
    pio.WriteControl(kA, 0x97);  // enabled, OR, active low, mask follows
    pio.WriteControl(kA, 0xFE);  // monitor line 0 only
    pio.SetLines(kA, 0xFF);
    EXPECT_FALSE(pio.GetPort(kA).ip);
    pio.SetLines(kA, 0xFD);
    EXPECT_FALSE(pio.GetPort(kA).ip) << "line 1 is masked";
    pio.SetLines(kA, 0xFC);
    EXPECT_TRUE(pio.GetPort(kA).ip);
    EXPECT_EQ(pio.ReadData(kA) & 0x0F, 0x0C) << "the input lines as they are";
}

TEST(Z80Pio_Test, StateRoundTrip)
{
    Z80Pio pio;
    ProgramLikeAmxArt(pio, kA, 0xF0);
    ProgramLikeAmxArt(pio, kB, 0xF2);
    pio.Strobe(kA, 0x01);
    uint8_t vector = 0;
    pio.Acknowledge(vector);
    pio.Strobe(kB, 0x00);
    uint8_t saved[Z80Pio::kStateSize];
    pio.SaveState(saved);

    Z80Pio other;
    other.LoadState(saved);
    uint8_t again[Z80Pio::kStateSize];
    other.SaveState(again);
    for (size_t i = 0; i < Z80Pio::kStateSize; i++)
        EXPECT_EQ(saved[i], again[i]) << "byte " << i;
    EXPECT_TRUE(other.GetPort(kA).ius);
    EXPECT_TRUE(other.GetPort(kB).ip);
    EXPECT_EQ(other.DaisyState(), Z80Pio::kDaisyIeo);
}
