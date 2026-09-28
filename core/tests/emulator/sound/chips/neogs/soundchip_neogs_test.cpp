// NeoGS card behaviour through its own Z80 (neogs-tdd.md §3.3-§3.6, §8).
//
// Each test puts a small program into flash page 0 (the loader's place), cold
// boots the card and lets the real CPU execute it, so ports, interrupts and
// timing are exercised exactly as firmware sees them. Results land in RAM
// page 3 (window 1, #4000), which is RAM even in ROM mode.

#include <gtest/gtest.h>

#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"

namespace
{
constexpr int64_t kTicksPerMs = 120000;

/// Minimal Z80 program builder
struct Asm
{
    std::vector<uint8_t> code;

    Asm& b(std::initializer_list<uint8_t> bytes)
    {
        code.insert(code.end(), bytes);
        return *this;
    }
    Asm& di() { return b({0xF3}); }
    Asm& ei() { return b({0xFB}); }
    Asm& halt() { return b({0x76}); }
    Asm& im1() { return b({0xED, 0x56}); }
    Asm& out(uint8_t port, uint8_t value) { return b({0x3E, value, 0xD3, port}); }         // LD A,v; OUT (p),A
    Asm& in(uint8_t port) { return b({0xDB, port}); }                                        // IN A,(p)
    Asm& store(uint16_t addr) { return b({0x32, uint8_t(addr), uint8_t(addr >> 8)}); }      // LD (nn),A
    Asm& inTo(uint8_t port, uint16_t addr) { return in(port).store(addr); }
    Asm& load(uint16_t addr) { return b({0x3A, uint8_t(addr), uint8_t(addr >> 8)}); }       // LD A,(nn)
    Asm& jrSelf() { return b({0x18, 0xFE}); }
    Asm& at(size_t address)
    {
        if (code.size() < address)
            code.resize(address, 0x00);
        return *this;
    }
};

struct Card
{
    EmulatorContext ctx{LoggerLevel::LogError};
    NeoGSConfig config;
    std::unique_ptr<SoundChip_NeoGS> chip;

    Card()
    {
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_shift_applied = 0;
        config.volume = 8192;
        chip = std::make_unique<SoundChip_NeoGS>(&ctx, config, 44100);
    }

    void boot(const Asm& program)
    {
        chip->flash().load(program.code.data(), program.code.size());
        chip->reset();
    }

    void runMs(double ms) { chip->runFor(static_cast<int64_t>(ms * kTicksPerMs)); }

    void runFrames(int n)
    {
        for (int i = 0; i < n; i++)
        {
            chip->handleFrameStart();
            chip->handleFrameEnd(SAMPLES_PER_FRAME);
        }
    }

    uint8_t result(int offset) const { return chip->peek(static_cast<uint16_t>(0x4000 + offset)); }
};
} // namespace

/// region <Ports>

TEST(SoundChip_NeoGS, ResetRegisterReads)
{
    Card c;
    Asm p;
    p.di()
        .inTo(0x0F, 0x4000)  // GSCFG0
        .inTo(0x11, 0x4001)  // SCTRL
        .inTo(0x0D, 0x4002)  // INTREQ
        .inTo(0x1B, 0x4003)  // DMA_MOD
        .inTo(0x3E, 0x4004)  // undefined FPGA port
        .inTo(0x80, 0x4005)  // CPLD port
        .b({0x01, 0x0F, 0x55, 0xED, 0x78}).store(0x4006) // LD BC,#550F; IN A,(C): A15..A8 ignored
        .inTo(0x4F, 0x4007)  // A6 = 1: not an FPGA port
        .halt();
    c.boot(p);
    c.runMs(0.1);
    EXPECT_EQ(c.result(0), 0x30);
    EXPECT_EQ(c.result(1), 0x0B);
    EXPECT_EQ(c.result(2), 0x00);
    EXPECT_EQ(c.result(3), 0x00);
    EXPECT_EQ(c.result(4), 0xFF);
    EXPECT_EQ(c.result(5), 0xFF);
    EXPECT_EQ(c.result(6), 0x30);
    EXPECT_EQ(c.result(7), 0xFF);
}

TEST(SoundChip_NeoGS, StatusSideEffectPorts0Aand0B)
{
    Card c;
    Asm p;
    p.di()
        .out(0x00, 5)          // MPAG 5: PG2 = 10, bit 0 = 0
        .in(0x0A)              // data bit <- NOT PG2 bit 0 = 1
        .inTo(0x04, 0x4000)
        .out(0x22, 0x0B)       // PG2 = 11, bit 0 = 1
        .out(0x0A, 0x00)       // write works too: data bit <- 0
        .inTo(0x04, 0x4001)
        .out(0x06, 0x20)       // VOL1 bit 5 must NOT matter
        .out(0x09, 0x00)
        .in(0x0B)              // command bit <- VOL4 bit 5 = 0
        .inTo(0x04, 0x4002)
        .out(0x09, 0x20)
        .out(0x0B, 0x00)       // command bit <- 1
        .inTo(0x04, 0x4003)
        .halt();
    c.boot(p);
    c.runMs(0.1);
    EXPECT_EQ(c.result(0) & 0x80, 0x80);
    EXPECT_EQ(c.result(1) & 0x80, 0x00);
    EXPECT_EQ(c.result(2) & 0x01, 0x00) << "#0B follows VOL4 bit 5, not VOL1";
    EXPECT_EQ(c.result(3) & 0x01, 0x01);
    EXPECT_EQ(c.result(3) & 0x7E, 0x7E) << "undefined status bits read as 1";
}

TEST(SoundChip_NeoGS, MailboxCardSide)
{
    Card c;
    Asm p;
    p.di()
        .inTo(0x01, 0x4000)   // command from host
        .inTo(0x02, 0x4001)   // data from host, clears the data bit
        .inTo(0x04, 0x4002)
        .out(0x03, 0x99)      // reply: sets the data bit
        .inTo(0x04, 0x4003)
        .in(0x05)             // clears the command bit
        .inTo(0x04, 0x4004)
        .halt();
    c.chip->flash().load(p.code.data(), p.code.size());
    c.chip->reset();
    c.chip->sendCommand(0x42);
    c.chip->sendData(0x24);
    c.runMs(0.1);
    EXPECT_EQ(c.result(0), 0x42);
    EXPECT_EQ(c.result(1), 0x24);
    EXPECT_EQ(c.result(2) & 0x81, 0x01);
    EXPECT_EQ(c.result(3) & 0x81, 0x81);
    EXPECT_EQ(c.result(4) & 0x81, 0x80);
    EXPECT_EQ(c.chip->readData(), 0x99);
    EXPECT_EQ(c.chip->getStatusRaw() & 0x80, 0x00);
}

TEST(SoundChip_NeoGS, Port02WriteAnd03ReadHaveNoEffect)
{
    Card c;
    Asm p;
    p.di().out(0x02, 0).inTo(0x04, 0x4000).in(0x03).inTo(0x04, 0x4001).halt();
    c.chip->flash().load(p.code.data(), p.code.size());
    c.chip->reset();
    c.chip->sendData(0x11);
    c.runMs(0.1);
    EXPECT_EQ(c.result(0) & 0x80, 0x80) << "#02 write leaves the data bit";
    EXPECT_EQ(c.result(1) & 0x80, 0x80) << "#03 read leaves the data bit";
}

TEST(SoundChip_NeoGS, Gscfg0AndPageRegisters)
{
    Card c;
    Asm p;
    p.di()
        .out(0x20, 0x44)     // PG0 (window 0 is flash: bits 4:0 used)
        .out(0x23, 0x81)
        .inTo(0x0F, 0x4000)
        .halt();
    // The program must survive PG0 = 0x44 -> flash page 4: put a copy there
    Asm image = p;
    image.at(4 * 0x4000);
    image.code.insert(image.code.end(), p.code.begin(), p.code.end());
    c.boot(image);
    c.runMs(0.1);
    EXPECT_EQ(c.chip->pageRegister(0), 0x44);
    EXPECT_EQ(c.chip->pageRegister(3), 0x81);
    EXPECT_EQ(c.result(0), 0x30);
}

/// endregion </Ports>

/// region <Host ports and resets>

TEST(SoundChip_NeoGS, HostPort33DecodesExactly)
{
    Card c;
    Asm p;
    p.di().jrSelf();
    c.boot(p);
    c.runMs(0.01);

    const bool ledBefore = c.chip->ledOn();
    c.chip->portDeviceOutMethod(0x33, 0x20);
    EXPECT_NE(c.chip->ledOn(), ledBefore) << "001 toggles the LED";

    const uint64_t nmis = c.chip->getActivityCounters().nmisAccepted;
    c.chip->portDeviceOutMethod(0x33, 0xC0); // 110: nothing
    c.runMs(0.01);
    EXPECT_EQ(c.chip->getActivityCounters().nmisAccepted, nmis);

    c.chip->portDeviceOutMethod(0x33, 0x40); // 010: NMI
    c.runMs(0.01);
    EXPECT_EQ(c.chip->getActivityCounters().nmisAccepted, nmis + 1);
    EXPECT_EQ(c.chip->portDeviceInMethod(0x33), 0xFF);
}

TEST(SoundChip_NeoGS, CardResetRestartsLoaderAndKeepsMailboxAndLatches)
{
    Card c;
    Asm p;
    p.di()
        .out(0x0F, 0x04)   // 8 channels, 24 MHz
        .out(0x22, 0x0B)   // PG2
        .out(0x16, 0x21)   // VOL5
        .halt();
    c.boot(p);
    c.runMs(0.1);
    c.chip->sendData(0x5A);
    c.chip->sendCommand(0xA5);

    c.chip->portDeviceOutMethod(0x33, 0x80);
    EXPECT_EQ(c.chip->gscfg0(), 0x30);
    EXPECT_EQ(c.chip->getCPUReg(GSCpuRegister::PC), 0x0000);
    EXPECT_EQ(c.chip->pageRegister(0), 0);
    EXPECT_EQ(c.chip->pageRegister(2), 0x0B) << "PG2 has no reset";
    EXPECT_EQ(c.chip->getChannelVolume(4), 0x21) << "volumes have no reset";
    EXPECT_EQ(c.chip->getDataFromHost(), 0x5A);
    EXPECT_EQ(c.chip->getCommandFromHost(), 0xA5);
    EXPECT_EQ(c.chip->getStatusRaw() & 0x81, 0x81) << "mailbox survives a card reset";
}

TEST(SoundChip_NeoGS, CpldPort80IsAColdRestart)
{
    Card c;
    Asm p;
    p.di().out(0x16, 0x21).out(0x80, 0x00).jrSelf();
    c.boot(p);
    c.chip->sendData(0x5A);
    c.runMs(0.01);
    EXPECT_EQ(c.chip->getStatusRaw() & 0x80, 0x00) << "mailbox cleared by the cold restart";
    EXPECT_EQ(c.chip->getChannelVolume(4), 0x21) << "the program ran again after the restart";
}

/// endregion </Host ports and resets>

/// region <Interrupts and clocks>

namespace
{
/// IM 1 program: EI; HALT loop; ISR at #38 = EI; RET. `setup` runs first.
Asm interruptProgram(std::initializer_list<std::pair<uint8_t, uint8_t>> setup)
{
    Asm p;
    p.di();
    for (const auto& [port, value] : setup)
        p.out(port, value);
    p.im1().ei();
    const size_t loop = p.code.size();
    p.halt().b({0x18, static_cast<uint8_t>(0x100 - (p.code.size() + 2 - loop))});
    p.at(0x38).b({0xFB, 0xC9}); // EI; RET
    return p;
}

std::vector<int64_t> acceptedInterruptTimes(const SoundChip_NeoGS& chip)
{
    std::vector<int64_t> times;
    for (const GSTraceEvent& e : chip.getPortTraceEvents())
    {
        if (e.side == GSTraceSide::Interrupt && !(e.flags & GSTraceFlags::kNmi))
            times.push_back(e.timestamp);
    }
    return times;
}
} // namespace

TEST(SoundChip_NeoGS, TimerIs37500HzAtEveryCpuClock)
{
    for (uint8_t clock : {0x00, 0x10, 0x20, 0x30})
    {
        Card c;
        c.boot(interruptProgram({{0x0F, clock}}));
        c.chip->startPortTrace();
        c.runMs(100);
        const auto times = acceptedInterruptTimes(*c.chip);
        ASSERT_GE(times.size(), 3700u) << "clock " << std::hex << int(clock);
        EXPECT_NEAR(static_cast<double>(times.size()), 3750.0, 2.0) << "clock " << std::hex << int(clock);

        // Acceptance snaps to instruction boundaries (the loop's JR is 12
        // cycles), but the period itself is exact: 3,200 base ticks
        const int64_t slack = 12 * SoundChip_NeoGS::ticksPerCycleFor(clock) + 1;
        for (size_t i = 1; i < times.size(); i++)
            EXPECT_NEAR(times[i] - times[i - 1], 3200, slack) << "period " << i;
        const size_t n = times.size() - 1;
        EXPECT_NEAR(static_cast<double>(times[n] - times[0]), 3200.0 * static_cast<double>(n), static_cast<double>(slack));
    }
}

TEST(SoundChip_NeoGS, TimFreqDividesTheTimer)
{
    Card c;
    c.boot(interruptProgram({{0x0E, 0x02}})); // /4
    c.runMs(100);
    EXPECT_NEAR(static_cast<double>(c.chip->getActivityCounters().interruptsAccepted), 937.5, 2.0);
}

TEST(SoundChip_NeoGS, IntenaMasksTheTimer)
{
    Card c;
    c.boot(interruptProgram({{0x0C, 0x01}})); // d7 = 0: clear the timer enable
    c.runMs(10);
    EXPECT_EQ(c.chip->getActivityCounters().interruptsAccepted, 0u);
    EXPECT_GT(c.chip->getActivityCounters().interruptPeriods, 300u) << "the timer still ticks into INTREQ";
    EXPECT_EQ(c.chip->interrupts().readRequest() & 1, 1);
}

TEST(SoundChip_NeoGS, SoftwareRequestUsesItsVector)
{
    Card c;
    // Timer off, SD DMA enabled and requested by software: vector #F7
    c.boot(interruptProgram({{0x0C, 0x01}, {0x0C, 0x82}, {0x0D, 0x82}}));
    c.chip->startPortTrace();
    c.runMs(0.2);
    bool seen = false;
    for (const GSTraceEvent& e : c.chip->getPortTraceEvents())
    {
        if (e.side == GSTraceSide::Interrupt)
        {
            EXPECT_EQ(e.value, 0xF7);
            seen = true;
            break;
        }
    }
    EXPECT_TRUE(seen);
    EXPECT_EQ(c.chip->interrupts().readRequest() & 0x02, 0) << "the acknowledge cleared it";
    EXPECT_EQ(c.chip->interrupts().readRequest() & 0x01, 0x01) << "the masked timer keeps requesting";
}

TEST(SoundChip_NeoGS, CpuClockFollowsGscfg0)
{
    // JR $ is 12 cycles: steps per millisecond = clock / 12,000
    const struct
    {
        uint8_t cfg;
        double stepsPerMs;
        uint32_t hz;
    } cases[] = {{0x00, 2000.0, 24000000}, {0x10, 1000.0, 12000000}, {0x20, 1666.7, 20000000}, {0x30, 833.3, 10000000}};
    for (const auto& k : cases)
    {
        Card c;
        Asm p;
        p.di().out(0x0F, k.cfg).jrSelf();
        c.boot(p);
        c.runMs(0.1);
        const uint64_t before = c.chip->getActivityCounters().cpuSteps;
        c.runMs(10);
        EXPECT_NEAR(static_cast<double>(c.chip->getActivityCounters().cpuSteps - before), 10 * k.stepsPerMs, 2.0)
            << "GSCFG0 " << std::hex << int(k.cfg);
        EXPECT_EQ(c.chip->cardClockHz(), k.hz);
    }
}

/// endregion </Interrupts and clocks>

/// region <Sound output>

namespace
{
/// Average of the last quarter of a frame's samples (blip has settled; its DC
/// leak droops a held level by ~1% over a few frames, as on the classic card)
void settledLevels(const SoundChip_NeoGS& chip, SoundChip_NeoGS& mutableChip, double& left, double& right)
{
    (void)chip;
    const int16_t* buffer = mutableChip.getBuffer();
    left = right = 0;
    const int from = SAMPLES_PER_FRAME * 3 / 4;
    for (int i = from; i < SAMPLES_PER_FRAME; i++)
    {
        left += buffer[2 * i];
        right += buffer[2 * i + 1];
    }
    left /= SAMPLES_PER_FRAME - from;
    right /= SAMPLES_PER_FRAME - from;
}
} // namespace

TEST(SoundChip_NeoGS, FourChannelOutputIsHardLeftDoubled)
{
    Card c;
    Asm p;
    p.di().out(0x06, 63).load(0x6000).jrSelf(); // VOL1 = 63; latch channel 1 from #6000
    c.boot(p);
    c.chip->poke(0x6000, 0xFF); // +127 (RAM page 3, window 1)
    c.runFrames(3);
    double l, r;
    settledLevels(*c.chip, *c.chip, l, r);
    // Raw L = 2 x 127 x 63 = 16,002; Volume 8192 scales by 8192 / 16384
    EXPECT_NEAR(l, 8001.0, 160.0);
    EXPECT_NEAR(r, 0.0, 40.0) << "no cross-feed on NeoGS";
    EXPECT_EQ(c.chip->getChannelSample(0), 0xFF);
}

TEST(SoundChip_NeoGS, EightChannelModeRoutesChannelSevenRight)
{
    Card c;
    Asm p;
    p.di()
        .out(0x0F, 0x04)     // 8CHANS (ROM mode, 24 MHz)
        .out(0x18, 63)       // VOL7
        .load(0x6600)        // A10:A8 = 6 -> channel 7
        .jrSelf();
    c.boot(p);
    c.chip->poke(0x6600, 0x00); // -128
    c.runFrames(3);
    double l, r;
    settledLevels(*c.chip, *c.chip, l, r);
    EXPECT_NEAR(l, 0.0, 40.0);
    EXPECT_NEAR(r, -128.0 * 63.0 / 2.0, 80.0);
    EXPECT_EQ(c.chip->getChannelSample(6), 0x00);
}

TEST(SoundChip_NeoGS, Inv7bReinterpretsLatchedBytes)
{
    Card c;
    Asm p;
    p.di().out(0x06, 63).load(0x6000).out(0x0F, 0xB0).jrSelf(); // latch #80, then INV7B
    c.boot(p);
    c.chip->poke(0x6000, 0x80);
    c.runFrames(3);
    double l, r;
    settledLevels(*c.chip, *c.chip, l, r);
    EXPECT_NEAR(l, 2.0 * -128.0 * 63.0 / 2.0, 160.0) << "#80 is -128 in two's complement";
}

TEST(SoundChip_NeoGS, OpcodeFetchesInTheWindowLatchToo)
{
    Card c;
    Asm p;
    p.di().out(0x06, 63).b({0xC3, 0x00, 0x60}); // JP #6000
    c.boot(p);
    c.chip->poke(0x6000, 0x18); // JR $ at #6000: every fetch latches #18 / #FE
    c.chip->poke(0x6001, 0xFE);
    c.runMs(0.1);
    EXPECT_GT(c.chip->getActivityCounters().dacFetches, 50u);
}

/// endregion </Sound output>

/// region <TTD>

TEST(SoundChip_NeoGS, TtdRoundTripContinuesIdentically)
{
    Card a;
    a.boot(interruptProgram({{0x0F, 0x20}, {0x06, 40}}));
    a.runMs(3.3);
    std::vector<uint8_t> blob(a.chip->TTDStateSize());
    a.chip->TTDSaveState(blob.data());

    Card b;
    b.boot(interruptProgram({}));
    b.chip->TTDLoadState(blob.data());
    EXPECT_EQ(b.chip->TTDHashState(), a.chip->TTDHashState());

    a.runMs(5);
    b.runMs(5);
    EXPECT_EQ(b.chip->TTDHashState(), a.chip->TTDHashState());
    EXPECT_EQ(b.chip->cardTicks(), a.chip->cardTicks());
}

/// endregion </TTD>
