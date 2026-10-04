#include <gtest/gtest.h>

#include <cstring>

#include "emulator/io/timer/pit8253.h"

/// @brief The 8253 / KR580VI53 counters, mode by mode (Intel 8253 data sheet). Most tests run the CLK at the
///        caller's clock (one pulse per unit of `now`) so pulse counts read directly
class Pit8253_Test : public ::testing::Test
{
protected:
    Pit8253 pit{1000, 1000};
    uint64_t now = 0;

    void Control(uint8_t value) { pit.Write(Pit8253::kControl, value, now); }
    void Count(uint8_t counter, uint16_t value)
    {
        const uint8_t rw = Pit8253::RwOf(pit.GetState().counter[counter]);
        if (rw == 1 || rw == 3)
            pit.Write(counter, static_cast<uint8_t>(value), now);
        if (rw == 2 || rw == 3)
            pit.Write(counter, static_cast<uint8_t>(value >> 8), now);
    }
    /// Let `pulses` CLK pulses pass
    void Pulses(uint64_t pulses)
    {
        now += pulses;
        pit.Advance(now);
    }
    uint16_t Read16(uint8_t counter)
    {
        const uint8_t lo = pit.Read(counter, now);
        const uint8_t hi = pit.Read(counter, now);
        return static_cast<uint16_t>(lo | (hi << 8));
    }
};

/// @brief Power-on: every counter idle, OUT high; the control register reads #FF
TEST_F(Pit8253_Test, PowerOnIsIdle)
{
    for (uint8_t c = 0; c < 3; ++c)
        EXPECT_TRUE(pit.Out(c));
    EXPECT_EQ(pit.Read(Pit8253::kControl, now), 0xFF);
    EXPECT_EQ(pit.OutputPeriod(0), 0u);
}

/// @brief Mode 0: OUT low at the control word, the count loads at the next pulse, OUT rises N pulses after the load
///        and stays high while the element wraps on
TEST_F(Pit8253_Test, Mode0InterruptOnTerminalCount)
{
    Control(0x30);   // counter 0, LSB then MSB, mode 0, binary
    EXPECT_FALSE(pit.Out(0));
    Count(0, 5);
    Pulses(1);       // the load
    EXPECT_EQ(pit.PeekCount(0), 5);
    Pulses(4);
    EXPECT_EQ(pit.PeekCount(0), 1);
    EXPECT_FALSE(pit.Out(0));
    Pulses(1);
    EXPECT_TRUE(pit.Out(0)) << "terminal count";
    EXPECT_EQ(pit.PeekCount(0), 0);
    Pulses(3);
    EXPECT_TRUE(pit.Out(0));
    EXPECT_EQ(pit.PeekCount(0), 0xFFFD) << "the element wraps on";
}

/// @brief Mode 0: the first byte of a new count stops the counting, the second starts the new count
TEST_F(Pit8253_Test, Mode0RewriteStopsAtTheFirstByte)
{
    Control(0x30);
    Count(0, 100);
    Pulses(11);
    EXPECT_EQ(pit.PeekCount(0), 90);
    pit.Write(0, 20, now);   // LSB only so far
    Pulses(5);
    EXPECT_EQ(pit.PeekCount(0), 90) << "held";
    pit.Write(0, 0, now);
    EXPECT_FALSE(pit.Out(0));
    Pulses(1);
    EXPECT_EQ(pit.PeekCount(0), 20);
}

/// @brief Mode 0: GATE low holds the count
TEST_F(Pit8253_Test, Mode0GateHolds)
{
    Control(0x30);
    Count(0, 10);
    Pulses(3);
    pit.SetGate(0, false, now);
    Pulses(100);
    EXPECT_EQ(pit.PeekCount(0), 8);
    pit.SetGate(0, true, now);
    Pulses(8);
    EXPECT_TRUE(pit.Out(0));
}

/// @brief Mode 1: nothing until a gate trigger; then OUT low for N pulses after the load; a trigger restarts it
TEST_F(Pit8253_Test, Mode1OneShotOnGateTrigger)
{
    Control(0x72);   // counter 1, LSB then MSB, mode 1
    pit.SetGate(1, false, now);
    Count(1, 4);
    Pulses(10);
    EXPECT_TRUE(pit.Out(1)) << "no trigger yet";
    pit.SetGate(1, true, now);
    Pulses(1);
    EXPECT_FALSE(pit.Out(1)) << "the one-shot starts at the load";
    Pulses(3);
    EXPECT_FALSE(pit.Out(1));
    Pulses(1);
    EXPECT_TRUE(pit.Out(1));
}

/// @brief Mode 2: OUT drops for the one pulse at which the element is 1, period N; a new count waits for the reload
TEST_F(Pit8253_Test, Mode2RateGenerator)
{
    Control(0x34);   // counter 0, mode 2
    Count(0, 4);
    EXPECT_EQ(pit.OutputPeriod(0), 4u);
    Pulses(1);
    EXPECT_EQ(pit.PeekCount(0), 4);
    Pulses(3);
    EXPECT_EQ(pit.PeekCount(0), 1);
    EXPECT_FALSE(pit.Out(0));
    Pulses(1);
    EXPECT_TRUE(pit.Out(0));
    EXPECT_EQ(pit.PeekCount(0), 4) << "reloaded";
    Pulses(4 * 1000 + 2);
    EXPECT_EQ(pit.PeekCount(0), 2) << "whole periods in closed form";

    Count(0, 10);
    EXPECT_EQ(pit.OutputPeriod(0), 4u) << "the old period runs to the reload";
    Pulses(2);
    EXPECT_EQ(pit.PeekCount(0), 10);
    EXPECT_EQ(pit.OutputPeriod(0), 10u);
}

/// @brief Mode 3: high for (N + 1) / 2, low for N / 2 pulses; the element counts down by two in each half
TEST_F(Pit8253_Test, Mode3SquareWave)
{
    Control(0x36);   // counter 0, mode 3 (ROM BIOS Plus's baud divider)
    Count(0, 6);
    Pulses(1);
    EXPECT_TRUE(pit.Out(0));
    EXPECT_EQ(pit.PeekCount(0), 6);
    Pulses(1);
    EXPECT_EQ(pit.PeekCount(0), 4);
    Pulses(2);
    EXPECT_FALSE(pit.Out(0)) << "after 3 pulses high";
    EXPECT_EQ(pit.PeekCount(0), 6);
    Pulses(3);
    EXPECT_TRUE(pit.Out(0));

    // Odd: 5 = 3 high + 2 low
    Control(0x36);
    Count(0, 5);
    Pulses(1);
    Pulses(2);
    EXPECT_TRUE(pit.Out(0));
    Pulses(1);
    EXPECT_FALSE(pit.Out(0));
    Pulses(2);
    EXPECT_TRUE(pit.Out(0));
    Pulses(5 * 777);
    EXPECT_TRUE(pit.Out(0)) << "whole periods in closed form";
    EXPECT_EQ(pit.OutputPeriod(0), 5u);
}

/// @brief Mode 3: GATE low forces OUT high and stops; the rising edge reloads
TEST_F(Pit8253_Test, Mode3GateRestarts)
{
    Control(0x36);
    Count(0, 8);
    Pulses(6);
    EXPECT_FALSE(pit.Out(0));
    pit.SetGate(0, false, now);
    EXPECT_TRUE(pit.Out(0));
    EXPECT_EQ(pit.OutputPeriod(0), 0u);
    Pulses(50);
    pit.SetGate(0, true, now);
    Pulses(1);
    EXPECT_EQ(pit.PeekCount(0), 8);
    Pulses(4);
    EXPECT_FALSE(pit.Out(0));
}

/// @brief Mode 4: one pulse low when the count reaches zero, once
TEST_F(Pit8253_Test, Mode4SoftwareStrobe)
{
    Control(0x38);   // counter 0, mode 4
    Count(0, 3);
    Pulses(1);
    Pulses(2);
    EXPECT_TRUE(pit.Out(0));
    Pulses(1);
    EXPECT_FALSE(pit.Out(0)) << "the strobe";
    Pulses(1);
    EXPECT_TRUE(pit.Out(0));
    Pulses(65536);
    EXPECT_TRUE(pit.Out(0)) << "no second strobe";
}

/// @brief Mode 5: the strobe after a gate trigger
TEST_F(Pit8253_Test, Mode5HardwareStrobe)
{
    Control(0xBA);   // counter 2, mode 5
    Count(2, 2);
    Pulses(10);
    EXPECT_TRUE(pit.Out(2));
    pit.SetGate(2, false, now);
    pit.SetGate(2, true, now);
    Pulses(3);
    EXPECT_FALSE(pit.Out(2));
    Pulses(1);
    EXPECT_TRUE(pit.Out(2));
}

/// @brief BCD: modulus 10000, the element reads BCD; a count of 0 is 10000
TEST_F(Pit8253_Test, BcdCounting)
{
    Control(0x31);   // mode 0, BCD
    Count(0, 0x0100);
    Pulses(1);
    EXPECT_EQ(pit.PeekCount(0), 0x0100);
    Pulses(1);
    EXPECT_EQ(pit.PeekCount(0), 0x0099);
    Pulses(99);
    EXPECT_TRUE(pit.Out(0));
    Pulses(1);
    EXPECT_EQ(pit.PeekCount(0), 0x9999) << "wraps within BCD";

    Control(0x35);   // mode 2, BCD, count 0 = 10000
    Count(0, 0);
    EXPECT_EQ(pit.OutputPeriod(0), 10000u);
}

/// @brief LSB only / MSB only: the other byte is 0; reads follow RW
TEST_F(Pit8253_Test, LsbOnlyAndMsbOnly)
{
    Control(0x10);   // LSB only, mode 0
    pit.Write(0, 0x34, now);
    Pulses(1);
    EXPECT_EQ(pit.PeekCount(0), 0x0034);
    EXPECT_EQ(pit.Read(0, now), 0x34);

    Control(0x20);   // MSB only
    pit.Write(0, 0x12, now);
    Pulses(1);
    EXPECT_EQ(pit.PeekCount(0), 0x1200);
    EXPECT_EQ(pit.Read(0, now), 0x12);
    EXPECT_EQ(pit.Read(0, now), 0x12) << "MSB only: every read is the MSB";
}

/// @brief The counter latch command freezes the element until it is read; a second latch before that is ignored
TEST_F(Pit8253_Test, CounterLatchCommand)
{
    Control(0x30);
    Count(0, 1000);
    Pulses(1);
    Control(0x00);   // latch counter 0
    Pulses(100);
    Control(0x00);   // ignored: the first latch is not read yet
    EXPECT_EQ(Read16(0), 1000);
    EXPECT_EQ(Read16(0), 900) << "unlatched again: the live element";
}

/// @brief A latch command does not change the mode or stop the counter
TEST_F(Pit8253_Test, LatchKeepsCounting)
{
    Control(0x34);
    Count(0, 50);
    Pulses(11);
    Control(0x00);
    EXPECT_EQ(pit.PeekCount(0), 40);
    Pulses(5);
    EXPECT_EQ(Read16(0), 40);
    EXPECT_EQ(pit.PeekCount(0), 35);
}

/// @brief The emulator's clock: 1.5 MHz CLK from 3.5 MHz T-states is 3 pulses per 7 T, the remainder kept
TEST(Pit8253Clock_Test, ThreeSeventhsOfTheBaseClock)
{
    Pit8253 pit;   // 1.5 MHz from 3.5 MHz
    pit.Write(Pit8253::kControl, 0x30, 0);
    pit.Write(0, 0xFF, 0);
    pit.Write(0, 0xFF, 0);   // 65535
    uint64_t t = 0;
    for (int i = 0; i < 700; ++i)
    {
        t += 1;   // T by T: the fraction must carry
        pit.Advance(t);
    }
    // 300 pulses: one for the load, 299 counted
    EXPECT_EQ(pit.PeekCount(0), 65535 - 299);
    pit.Advance(t + 7000);
    EXPECT_EQ(pit.PeekCount(0), 65535 - 299 - 3000);
}

/// @brief A clock that goes back rebases without counting
TEST(Pit8253Clock_Test, ClockGoingBackRebases)
{
    Pit8253 pit(1000, 1000);
    pit.PowerOn(500);
    pit.Write(Pit8253::kControl, 0x30, 500);
    pit.Write(0, 100, 500);
    pit.Write(0, 0, 500);
    pit.Advance(511);
    EXPECT_EQ(pit.PeekCount(0), 90);
    pit.Advance(10);
    EXPECT_EQ(pit.PeekCount(0), 90);
    pit.Advance(15);
    EXPECT_EQ(pit.PeekCount(0), 85);
}

/// @brief ROM BIOS Plus's board test: counter 1 mode 3, #0010 written twice, two INs must not both read #FF
TEST(Pit8253Clock_Test, BiosPlusBoardTestReadsCounter1Back)
{
    Pit8253 pit;
    uint64_t t = 1000;
    pit.Write(Pit8253::kControl, 0x76, t);
    for (int pass = 0; pass < 2; ++pass)
    {
        t += 60;
        pit.Write(1, 0x10, t);
        t += 60;
        pit.Write(1, 0x00, t);
    }
    t += 12;
    const uint8_t lo = pit.Read(1, t);
    t += 12;
    const uint8_t hi = pit.Read(1, t);
    EXPECT_FALSE(lo == 0xFF && hi == 0xFF);
    EXPECT_EQ(hi, 0x00);
    EXPECT_LE(lo, 0x10);
}

/// @brief The state round-trips (the TTD blob)
TEST(Pit8253Clock_Test, StateRoundTrip)
{
    Pit8253 a;
    a.Write(Pit8253::kControl, 0x36, 0);
    a.Write(0, 156, 0);
    a.Write(0, 0, 0);
    a.Advance(1234);
    Pit8253 b;
    b.SetState(a.GetState());
    EXPECT_EQ(std::memcmp(&a.GetState(), &b.GetState(), sizeof(Pit8253::State)), 0);
    a.Advance(5000);
    b.Advance(5000);
    EXPECT_EQ(a.PeekCount(0), b.PeekCount(0));
    EXPECT_EQ(a.Out(0), b.Out(0));
}
