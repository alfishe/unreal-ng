// Z84Ctc: control words, time constants, the timer down-counter, the vector (Sprinter test-plan §2.10 T-Z84; moved with the model into the z84c15 library).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <3rdparty/z84c15/z84c15.h>

using Z84Lib::Z84Ctc;
using Trigger = Z84Lib::Z84Ctc::Trigger;
using TriggerKind = Z84Lib::Z84Ctc::TriggerKind;

TEST(Z84Ctc_Test, Timer_CountsThroughThePrescaler)
{
    Z84Ctc ctc;
    uint64_t clock = 1000;
    ctc.SetClock([&] { return clock; });
    ctc.Reset();

    ctc.Write(1, 0x25);  // timer, prescaler 256, time constant follows
    ctc.Write(1, 0x0A);
    EXPECT_EQ(ctc.Read(1), 10);
    clock += 512;
    EXPECT_EQ(ctc.Read(1), 8) << "the header's worked example";
    clock += 8 * 256;
    EXPECT_EQ(ctc.Read(1), 10) << "reloads after zero";

    ctc.Write(2, 0x05);  // prescaler 16
    ctc.Write(2, 0x00);  // 256
    clock += 16;
    EXPECT_EQ(ctc.Read(2), 0xFF);
}

TEST(Z84Ctc_Test, ResetStopsAndVectorGoesToChannel0)
{
    Z84Ctc ctc;
    uint64_t clock = 0;
    ctc.SetClock([&] { return clock; });
    ctc.Write(0, 0xE8);  // bit 0 = 0 on channel 0: the vector
    EXPECT_EQ(ctc.Vector(), 0xE8);
    ctc.Write(3, 0x07);  // reset + time constant follows
    ctc.Write(3, 0x20);
    clock += 1600;  // 100 steps of 16 clocks: 32, then 3 x 32 = 96 and 4 more
    EXPECT_EQ(ctc.Read(3), 0x1C);
    ctc.Write(3, 0x03);  // software reset
    clock += 1600;
    EXPECT_EQ(ctc.Read(3), 0x1C) << "stopped: it keeps its count (MAME z80ctc: m_down = read())";
    EXPECT_FALSE(ctc.GetChannel(3).running);
}

/// region <Counter mode, CLK/TRG inputs, ZC/TO (Z80 CTC data sheet PS0181)>

namespace
{
/// A CTC on a clock of 1 000 units per second, one unit per system clock; TRG0 a 250 Hz clock (an edge every 4 units)
struct CtcBench
{
    uint64_t clock = 0;
    Z84Ctc ctc;

    CtcBench()
    {
        ctc.SetClock([this] { return clock; });
        ctc.SetUnitsPerSecond(1000);
        ctc.SetTrigger(0, Trigger{TriggerKind::Clock, 250, 0});
        ctc.Reset();
    }
};
}  // namespace

// Counter mode: the down-counter steps on each CLK/TRG edge, reloads at zero and pulses ZC/TO
TEST(Z84Ctc_Test, Counter_CountsTheTriggerEdges)
{
    CtcBench b;
    b.ctc.Write(0, 0x55);  // counter, rising edge, time constant follows, control
    b.ctc.Write(0, 10);
    EXPECT_EQ(b.ctc.Read(0), 10);
    b.clock = 3;
    EXPECT_EQ(b.ctc.Read(0), 10) << "no edge yet (one every 4 units)";
    b.clock = 4;
    EXPECT_EQ(b.ctc.Read(0), 9);
    b.clock = 39;
    EXPECT_EQ(b.ctc.Read(0), 1);
    EXPECT_EQ(b.ctc.ZeroCounts(0), 0u);
    b.clock = 40;
    EXPECT_EQ(b.ctc.Read(0), 10) << "zero: the time constant is reloaded";
    EXPECT_EQ(b.ctc.ZeroCounts(0), 1u) << "and ZC/TO pulsed";
    b.clock = 400;
    EXPECT_EQ(b.ctc.ZeroCounts(0), 10u);
    EXPECT_DOUBLE_EQ(b.ctc.OutputHz(0), 25.0) << "250 Hz / 10";
}

// A counter whose CLK/TRG is not connected holds its count; the system clock does not move it
TEST(Z84Ctc_Test, Counter_WithoutInputHolds)
{
    CtcBench b;
    b.ctc.Write(1, 0x45);  // counter, time constant follows
    b.ctc.Write(1, 7);
    b.clock = 100000;
    EXPECT_EQ(b.ctc.Read(1), 7);
    EXPECT_EQ(b.ctc.ZeroCounts(1), 0u);
    EXPECT_DOUBLE_EQ(b.ctc.OutputHz(1), 0.0);
}

// ZC/TO of a lower channel drives a higher channel's CLK/TRG (the Sprinter: ZC/TO2 -> TRG3); the cascade's
// interrupt comes at the product of both time constants, with vector base | channel x 2
TEST(Z84Ctc_Test, Cascade_CountsTheLowerChannelsZeroCounts)
{
    CtcBench b;
    b.ctc.SetTrigger(2, Trigger{TriggerKind::Clock, 250, 0});
    b.ctc.SetTrigger(3, Trigger{TriggerKind::Cascade, 0, 2});
    b.ctc.Write(0, 0x00);  // vector base #00
    b.ctc.Write(2, 0x57);  // counter, rising, time constant follows, reset
    b.ctc.Write(2, 4);     // ZC/TO2 every 16 units
    b.ctc.Write(3, 0xD7);  // interrupt, counter, rising, time constant follows, reset
    b.ctc.Write(3, 3);     // an interrupt every 48 units

    b.clock = 47;
    b.ctc.Poll();
    EXPECT_EQ(b.ctc.Read(3), 1);
    EXPECT_FALSE(b.ctc.GetChannel(3).ip);
    b.clock = 48;
    b.ctc.Poll();
    EXPECT_TRUE(b.ctc.GetChannel(3).ip);
    EXPECT_EQ(b.ctc.Read(3), 3);
    EXPECT_EQ(b.ctc.Vector() | (3 << 1), 0x06);
    EXPECT_DOUBLE_EQ(b.ctc.OutputHz(3), 250.0 / 4 / 3);

    // The lower channel reprogrammed mid-count: the higher one keeps counting its pulses from where it was
    b.ctc.Channel(3).ip = 0;
    b.clock = 64;  // one more ZC/TO2 pulse: channel 3 at 2
    EXPECT_EQ(b.ctc.Read(3), 2);
    b.ctc.Write(2, 0x55);  // no reset: a new time constant follows
    b.ctc.Write(2, 2);     // reloaded at the next zero: ZC/TO2 at 80, then every 8 units
    b.clock = 87;
    EXPECT_EQ(b.ctc.Read(3), 1);
    b.clock = 88;
    b.ctc.Poll();
    EXPECT_EQ(b.ctc.Read(3), 3);
    EXPECT_TRUE(b.ctc.GetChannel(3).ip);
}

// A time constant written while the channel counts takes effect at the next zero count; the count goes on
TEST(Z84Ctc_Test, TimeConstantWhileRunning_LoadsAtTheNextZero)
{
    CtcBench b;
    b.ctc.Write(1, 0x05);  // timer, prescaler 16, time constant follows
    b.ctc.Write(1, 10);
    b.clock = 16 * 4;
    EXPECT_EQ(b.ctc.Read(1), 6);
    b.ctc.Write(1, 0x05);  // no reset
    b.ctc.Write(1, 3);
    EXPECT_EQ(b.ctc.Read(1), 6) << "the present count continues";
    b.clock = 16 * 10;
    EXPECT_EQ(b.ctc.Read(1), 3) << "zero: the new constant";
    b.clock = 16 * 13;
    EXPECT_EQ(b.ctc.Read(1), 3);
    EXPECT_EQ(b.ctc.ZeroCounts(1), 2u);
}

// Timer mode with bit 3: the time constant arms the timer, the next CLK/TRG edge starts it
TEST(Z84Ctc_Test, Timer_TriggerStartWaitsForTheEdge)
{
    CtcBench b;
    b.ctc.SetTrigger(1, Trigger{TriggerKind::Clock, 10, 0});  // an edge every 100 units
    b.clock = 30;
    b.ctc.Write(1, 0x9D);  // interrupt, timer, prescaler 16, rising, trigger start, time constant follows
    b.ctc.Write(1, 2);     // 32 clocks once started
    b.clock = 99;
    b.ctc.Poll();
    EXPECT_EQ(b.ctc.Read(1), 2) << "armed, not counting";
    EXPECT_FALSE(b.ctc.GetChannel(1).ip);
    b.clock = 100 + 16;
    EXPECT_EQ(b.ctc.Read(1), 1) << "started at the edge (100)";
    b.clock = 100 + 31;
    b.ctc.Poll();
    EXPECT_FALSE(b.ctc.GetChannel(1).ip);
    b.clock = 100 + 32;
    b.ctc.Poll();
    EXPECT_TRUE(b.ctc.GetChannel(1).ip);
    b.clock = 300;
    EXPECT_EQ(b.ctc.ZeroCounts(1), 6u) << "free-running once started: no new trigger needed";

    // Without a trigger input the armed timer never starts
    b.ctc.SetTrigger(2, Trigger{});
    b.ctc.Write(2, 0x0D);
    b.ctc.Write(2, 2);
    b.clock = 100000;
    EXPECT_EQ(b.ctc.Read(2), 2);
}

// The system clock changes speed (a turbo switch): a timer keeps its count and continues at the new rate;
// a counter on a real-time clock input does not notice
TEST(Z84Ctc_Test, SystemClockPeriodChange_KeepsTheCount)
{
    CtcBench b;
    b.ctc.SetSystemClockPeriod(6, 1);  // 6 units per system clock (3.5 MHz on a 21 MHz time base)
    b.ctc.Write(1, 0x05);  // timer, prescaler 16
    b.ctc.Write(1, 10);
    b.ctc.Write(0, 0x45);  // counter on TRG0
    b.ctc.Write(0, 100);
    b.clock = 6 * 16 * 3;
    EXPECT_EQ(b.ctc.Read(1), 7);
    EXPECT_EQ(b.ctc.Read(0), 100 - 72);
    b.ctc.SetSystemClockPeriod(1, 1);  // six times faster
    EXPECT_EQ(b.ctc.Read(1), 7);
    b.clock += 16 * 2;
    EXPECT_EQ(b.ctc.Read(1), 5) << "two steps at the new rate";
    EXPECT_EQ(b.ctc.Read(0), 100 - 80) << "the trigger clock is real time";
    EXPECT_DOUBLE_EQ(b.ctc.OutputHz(1), 1000.0 / 16 / 10);
}

// Enabling the interrupt on a running channel requests at the following zero counts only
TEST(Z84Ctc_Test, InterruptEnable_OnlyLaterZeroCountsRequest)
{
    CtcBench b;
    b.ctc.Write(0, 0x55);
    b.ctc.Write(0, 2);  // a zero every 8 units
    b.clock = 100;
    b.ctc.Write(0, 0xD5 & ~0x04);  // interrupt on, no reset, no time constant
    b.ctc.Poll();
    EXPECT_FALSE(b.ctc.GetChannel(0).ip) << "the 12 zero counts before the enable do not request";
    EXPECT_EQ(b.ctc.Read(0), 1) << "the control word did not disturb the count";
    b.clock = 104;
    b.ctc.Poll();
    EXPECT_TRUE(b.ctc.GetChannel(0).ip);
}

/// endregion </Counter mode, CLK/TRG inputs, ZC/TO>
