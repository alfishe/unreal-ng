// The Z84C15's on-chip block (core/src/3rdparty/z84c15/z84c15.h): the fixed ports, the system
// control registers and chip selects, the watchdog, and the interrupt daisy chain with the CTC,
// SIO and PIO (PS0182; research-cpu-z84c15.md section 4).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <3rdparty/z84c15/z84c15.h>

#include "z84testbus.h"

using Z84Lib::Z84C15;

// The chip decodes A7-A0 only (no image): #xx1F is the PIO whatever the high byte; #14-#17 are not its own
TEST(Z84C15_Test, OwnsItsPortsAndChipSelects)
{
    for (uint8_t port : {0x10, 0x13, 0x18, 0x1F, 0xEE, 0xEF, 0xF0, 0xF1, 0xF4})
        EXPECT_TRUE(Z84C15::Owns(port)) << std::hex << +port;
    for (uint8_t port : {0x0F, 0x14, 0x17, 0x20, 0xED, 0xF2, 0xF3, 0xFE, 0xFF})
        EXPECT_FALSE(Z84C15::Owns(port)) << std::hex << +port;
    EXPECT_TRUE(Z84C15::Owns(0x7F1F));
    EXPECT_FALSE(Z84C15::Owns(0x1F00));

    Z84C15 chip;
    chip.PowerOn();
    EXPECT_EQ(chip.system.Cs0End(), 0x10000u) << "CSBR #FF, MCR 1: everything CS0";
    chip.Write(0xEE, 0x02);
    chip.Write(0xEF, 0xFE);
    EXPECT_EQ(chip.Read(0xEF), 0xFE);
    EXPECT_EQ(chip.system.Cs0End(), 0xF000u) << "the loader's boundary";
    EXPECT_FALSE(chip.system.Cs1Selects(0xF000)) << "CS1 off until MCR D1";
    chip.Write(0xEE, 0x03);
    chip.Write(0xEF, 0x03);
    EXPECT_TRUE(chip.system.Cs1Selects(0xF000)) << "the loader: CS1 #F000-#FFFF";
    EXPECT_FALSE(chip.system.Cs1Selects(0xEFFF));
    chip.Write(0xEE, 0x02);
    chip.Write(0xEF, 0xF0);
    EXPECT_EQ(chip.system.Cs0End(), 0x1000u) << "a reload: the stream at fast RAM #1000";
    EXPECT_EQ(chip.Read(0xF0), 0xFB) << "WDTMR power-on value";
}

// /RESET keeps the system registers (MAME sets them at device start only); power-on restores them
TEST(Z84C15_Test, ResetKeepsSystemRegistersPowerOnRestoresThem)
{
    Z84C15 chip;
    chip.Write(0xEE, 0x03);
    chip.Write(0xEF, 0x13);  // MCR: D4 (clock / 1) is stored, nothing more
    chip.Reset();
    EXPECT_EQ(chip.Read(0xEF), 0x13);
    chip.PowerOn();
    chip.Write(0xEE, 0x03);
    EXPECT_EQ(chip.Read(0xEF), 0x01);
}

// The watchdog runs from power-on (WDTMR #FB: enabled, 2^22 clocks); /WDTOUT is an event the board
// may connect. #4E clears it; #B1 stops it, but only after WDTE was cleared
TEST(Z84C15_Test, WatchdogTimesOutUnlessCleared)
{
    uint64_t clock = 0;
    int timeouts = 0;
    Z84C15 chip;
    chip.SetClock([&] { return clock; });
    chip.SetWatchdogHandler([&] { timeouts++; });
    chip.PowerOn();
    ASSERT_TRUE(chip.WatchdogRunning());
    EXPECT_EQ(chip.WatchdogDeadline(), uint64_t{1} << 22);

    clock = (uint64_t{1} << 22) - 1;
    chip.IntPending();
    EXPECT_EQ(timeouts, 0);
    chip.Write(0xF1, 0x4E);  // clear
    clock += 2;
    chip.IntPending();
    EXPECT_EQ(timeouts, 0) << "cleared just in time";
    clock += uint64_t{1} << 22;
    chip.IntPending();
    chip.IntPending();
    EXPECT_EQ(timeouts, 1) << "one event per timeout";

    chip.Write(0xF1, 0xB1);
    EXPECT_TRUE(chip.WatchdogRunning()) << "#B1 needs WDTE = 0 first";
    chip.Write(0xF0, 0x7B);
    chip.Write(0xF1, 0xB1);
    EXPECT_FALSE(chip.WatchdogRunning());

    chip.Write(0xF0, 0x9B);  // enabled again, 2^16
    EXPECT_TRUE(chip.WatchdogRunning());
    clock += uint64_t{1} << 16;
    chip.IntPending();
    EXPECT_EQ(timeouts, 2);
}

// No handler: the output is not connected (the Sprinter) - nothing happens however long it runs
TEST(Z84C15_Test, WatchdogWithoutHandlerIsNotConnected)
{
    uint64_t clock = 0;
    Z84C15 chip;
    chip.SetClock([&] { return clock; });
    clock = uint64_t{1} << 30;
    EXPECT_FALSE(chip.IntPending());
}

// The CTC in timer mode with its interrupt enabled requests at every zero count; the vector is
// the channel 0 base | channel x 2; RETI ends the service
TEST(Z84C15_Test, CtcInterruptThroughTheChain)
{
    uint64_t clock = 0;
    Z84C15 chip;
    chip.SetClock([&] { return clock; });
    chip.PowerOn();
    chip.Write(0x10, 0xE8);  // vector base
    chip.Write(0x12, 0xA5);  // channel 2: interrupt, timer, prescaler 256, time constant follows
    chip.Write(0x12, 0x0A);  // 10: a zero count every 2 560 clocks

    clock = 2559;
    EXPECT_FALSE(chip.IntPending());
    clock = 2560;
    ASSERT_TRUE(chip.IntPending());
    EXPECT_EQ(chip.AcknowledgeInterrupt(), 0xE8 | (2 << 1));
    EXPECT_FALSE(chip.IntPending()) << "acknowledged, under service";

    clock = 2 * 2560;
    EXPECT_FALSE(chip.IntPending()) << "requested again, but the channel is still under service";
    chip.OnReti();
    EXPECT_TRUE(chip.IntPending());
}

// #F4 orders the devices; a source under service blocks the lower ones (IEO), not the higher ones
TEST(Z84C15_Test, PriorityRegisterOrdersTheChain)
{
    uint64_t clock = 0;
    Z84C15 chip;
    chip.SetClock([&] { return clock; });
    chip.PowerOn();

    // SIO channel A receive interrupt on every character, status affects vector (WR1B bit 2)
    chip.Write(0x1B, 0x02);
    chip.Write(0x1B, 0x40);  // WR2 = #40
    chip.Write(0x1B, 0x01);
    chip.Write(0x1B, 0x04);  // WR1B: status affects vector
    chip.Write(0x19, 0x01);
    chip.Write(0x19, 0x18);  // WR1A: Rx interrupt on all characters
    // CTC channel 0
    chip.Write(0x10, 0x20);
    chip.Write(0x10, 0x85);
    chip.Write(0x10, 0x01);  // a zero count every 16 clocks

    clock = 16;
    chip.sio.Receive(0, 0x1C);
    ASSERT_TRUE(chip.IntPending());
    EXPECT_EQ(chip.AcknowledgeInterrupt(), 0x20) << "#F4 = 0: CTC first";
    EXPECT_FALSE(chip.IntPending()) << "the CTC under service blocks the SIO";
    chip.OnReti();

    chip.Write(0xF4, 0x01);  // SIO first
    clock = 32;
    EXPECT_EQ(chip.AcknowledgeInterrupt(), (0x40 & 0xF1) | (0x06 << 1)) << "SIO A receive: status code 110";
    EXPECT_FALSE(chip.IntPending()) << "the SIO under service blocks the CTC";
    EXPECT_EQ(chip.sio.Read(0x18), 0x1C);
    chip.OnReti();
    EXPECT_TRUE(chip.IntPending()) << "the CTC's request waited";
    EXPECT_EQ(chip.AcknowledgeInterrupt(), 0x20);
}

// PIO mode 3: an interrupt when the monitored inputs meet the condition (OR, active low here)
TEST(Z84C15_Test, PioBitControlInterrupt)
{
    Z84C15 chip;
    chip.PowerOn();
    chip.Write(0x1F, 0xCF);  // B: mode 3
    chip.Write(0x1F, 0xFF);  // all inputs
    chip.Write(0x1F, 0x66);  // vector
    chip.Write(0x1F, 0x97);  // interrupt on, OR, active low, mask follows
    chip.Write(0x1F, 0xFE);  // monitor bit 0 only
    EXPECT_FALSE(chip.IntPending());
    chip.pio.SetInputs(1, 0xFE);
    ASSERT_TRUE(chip.IntPending());
    EXPECT_EQ(chip.AcknowledgeInterrupt(), 0x66);
    chip.OnReti();
    EXPECT_FALSE(chip.IntPending()) << "one request per edge";
}

// The core and the chip together: RETI executed by the CPU ends the on-chip service (the chip's
// RETI watcher), and the IM2 acknowledge reads the chip's vector
TEST(Z84C15_Test, CpuRetiAndIm2ReachTheChain)
{
    uint64_t clock = 0;
    Z84C15 chip;
    chip.SetClock([&] { return clock; });
    chip.PowerOn();
    Z84CPU* cpu = chip.Cpu();
    Z84Test::TestBus bus(cpu);
    Z84CpuSetIntVectorFn(cpu, [](Z84CPU*, void* user) { return static_cast<Z84C15*>(user)->AcknowledgeInterrupt(); }, &chip);
    Z84CpuReset(cpu);

    chip.Write(0x10, 0xE0);
    chip.Write(0x11, 0x85);
    chip.Write(0x11, 0x01);  // channel 1, every 16 clocks
    bus.Load(0x80E2, {0x00, 0x90});  // vector #E2 -> handler #9000
    bus.Load(0x9000, {0xED, 0x4D});  // RETI
    Z84CpuSetReg(cpu, Z84CpuRegI, 0x80);
    Z84CpuSetReg(cpu, Z84CpuRegIm, 2);
    Z84CpuSetReg(cpu, Z84CpuRegIff1, 1);
    Z84CpuSetReg(cpu, Z84CpuRegSp, 0xC000);

    clock = 16;
    ASSERT_TRUE(chip.IntPending());
    ASSERT_GT(Z84CpuInt(cpu), 0);
    EXPECT_EQ(Z84CpuGetReg(cpu, Z84CpuRegPc), 0x9000);
    EXPECT_TRUE(chip.AnyUnderService());
    Z84CpuStep(cpu);  // RETI
    EXPECT_FALSE(chip.AnyUnderService());
}

// The Sprinter's playback tick (Bad Apple, dontBlink: IM2 with only vector #06 in the table): TRG2 a 875 kHz clock,
// channel 2 counts it by 112, ZC/TO2 drives TRG3, channel 3 counts those by 160 and interrupts at 48.83 Hz with
// vector base #00 -> #06. The clock here is the board's: 42 MHz crystal ticks (875 kHz = an edge every 48)
TEST(Z84C15_Test, CtcCascadeInterruptsWithVector06)
{
    using Z84Lib::Z84Ctc;
    uint64_t clock = 0;
    Z84C15 chip;
    chip.SetClock([&] { return clock; });
    chip.SetUnitsPerSecond(42'000'000);
    chip.SetSystemClockPeriod(12, 1);  // 3.5 MHz
    chip.ctc.SetTrigger(2, Z84Ctc::Trigger{Z84Ctc::TriggerKind::Clock, 875'000, 0});
    chip.ctc.SetTrigger(3, Z84Ctc::Trigger{Z84Ctc::TriggerKind::Cascade, 0, 2});
    chip.PowerOn();
    chip.Write(0x10, 0x00);  // vector base #00
    chip.Write(0x12, 0x57);
    chip.Write(0x12, 112);
    chip.Write(0x13, 0xD7);
    chip.Write(0x13, 160);

    constexpr uint64_t kTick = 112 * 160 * 48;  // 860 160 ticks = 20.48 ms
    EXPECT_DOUBLE_EQ(chip.ctc.OutputHz(3), 875'000.0 / 112 / 160);
    clock = kTick - 1;
    EXPECT_FALSE(chip.IntPending());
    clock = kTick;
    ASSERT_TRUE(chip.IntPending());
    EXPECT_EQ(chip.AcknowledgeInterrupt(), 0x06);
    chip.OnReti();

    // A turbo switch to 21 MHz does not move the tick: the inputs are real time
    chip.SetSystemClockPeriod(2, 1);
    clock = 2 * kTick - 1;
    EXPECT_FALSE(chip.IntPending());
    clock = 2 * kTick;
    ASSERT_TRUE(chip.IntPending());
    EXPECT_EQ(chip.AcknowledgeInterrupt(), 0x06);
}

// The watchdog counts system clocks: a clock speed change keeps the clocks counted so far
TEST(Z84C15_Test, WatchdogKeepsItsCountAcrossAClockChange)
{
    uint64_t clock = 0;
    int timeouts = 0;
    Z84C15 chip;
    chip.SetClock([&] { return clock; });
    chip.SetWatchdogHandler([&] { timeouts++; });
    chip.SetSystemClockPeriod(6, 1);
    chip.PowerOn();  // 2^22 system clocks
    clock = 6 * (uint64_t{1} << 21);  // half way at 6 units per clock
    chip.SetSystemClockPeriod(1, 1);
    EXPECT_EQ(chip.WatchdogDeadline(), clock + (uint64_t{1} << 21));
    clock += (uint64_t{1} << 21) - 1;
    chip.IntPending();
    EXPECT_EQ(timeouts, 0);
    clock++;
    chip.IntPending();
    EXPECT_EQ(timeouts, 1);
}

// SaveState / LoadState mid-count: a counter, its cascade and an armed timer resume exactly
TEST(Z84C15_Test, StateRoundTripKeepsTheCtcMidCount)
{
    using Z84Lib::Z84Ctc;
    uint64_t clock = 0;
    Z84C15 chip;
    chip.SetClock([&] { return clock; });
    chip.SetUnitsPerSecond(1000);
    chip.ctc.SetTrigger(1, Z84Ctc::Trigger{Z84Ctc::TriggerKind::Clock, 10, 0});
    chip.ctc.SetTrigger(2, Z84Ctc::Trigger{Z84Ctc::TriggerKind::Clock, 250, 0});
    chip.ctc.SetTrigger(3, Z84Ctc::Trigger{Z84Ctc::TriggerKind::Cascade, 0, 2});
    chip.PowerOn();
    chip.Write(0x12, 0x57);
    chip.Write(0x12, 4);
    chip.Write(0x13, 0xD7);
    chip.Write(0x13, 3);
    chip.SetSystemClockPeriod(3, 1);
    clock = 30;
    chip.Write(0x11, 0x1D);  // timer, trigger start
    chip.Write(0x11, 2);
    clock = 40;  // channel 2 at 2 (10 edges), channel 3 at 1... zero at 48

    std::vector<uint8_t> saved(Z84C15::kStateSize);
    chip.SaveState(saved.data());
    const uint8_t c2 = chip.ctc.Read(2);
    const uint8_t c3 = chip.ctc.Read(3);

    chip.SetSystemClockPeriod(1, 1);
    chip.PowerOn();
    chip.LoadState(saved.data());
    EXPECT_EQ(chip.ctc.SystemClockNum(), 3u);
    EXPECT_EQ(chip.ctc.Read(2), c2);
    EXPECT_EQ(chip.ctc.Read(3), c3);
    EXPECT_EQ(chip.ctc.Read(1), 2) << "still armed";
    clock = 47;
    EXPECT_FALSE(chip.IntPending());
    clock = 48;
    EXPECT_TRUE(chip.IntPending());
    clock = 100 + 3 * 16;  // started at the edge at 100, 16 x 3 units per step
    EXPECT_EQ(chip.ctc.Read(1), 1);
    std::vector<uint8_t> again(Z84C15::kStateSize);
    chip.LoadState(saved.data());
    chip.SaveState(again.data());
    EXPECT_EQ(again, saved);
}
