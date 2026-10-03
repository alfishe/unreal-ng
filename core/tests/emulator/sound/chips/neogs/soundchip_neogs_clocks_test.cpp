// NeoGS at every card clock (neogs-tdd.md §3.3, §5.2): the four CKSEL values
// of GSCFG0 (#00 24 MHz, #10 12 MHz, #20 20 MHz, #30 10 MHz) and switches
// between them while the card runs.
//
// What must hold at every clock:
// - the card's time follows the host's frame by frame, with no drift;
// - the CPU runs at the selected clock from the instruction after the switch;
// - the timer (and the DAC, same 24 MHz crystal) keeps its 37.5 kHz phase;
// - SPI bytes and DMA bytes take a fixed number of CARD clocks, so their
//   length in time follows the clock;
// - ZX-DMA waits follow the card clock;
// - the host protocol (command -> reply) works at every card clock and every
//   host speed, and while the card changes clock between replies.
//
// Runtime justification: the host tests run a Pentagon with the card for up
// to a few hundred frames per case (12 cases); the card-only tests run tens
// of milliseconds of card time per clock.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/flash/flash29f040b.h"
#include "emulator/io/spi/spidevice.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/gshostclock.h"
#include "emulator/sound/chips/neogs/neogsdma.h"
#include "emulator/sound/chips/neogs/neogsinterrupts.h"
#include "emulator/sound/chips/neogs/neogsmemory.h"
#include "emulator/sound/chips/neogs/neogszxdma.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/soundmanager.h"

namespace
{
struct ClockCase
{
    uint8_t cfg;       // GSCFG0 bits 5:4
    uint32_t hz;
    int64_t ticksPerCycle; // 120 MHz base ticks per card clock
    const char* name;
};

constexpr ClockCase kClocks[] = {
    {0x00, 24000000, 5, "24MHz"},
    {0x10, 12000000, 10, "12MHz"},
    {0x20, 20000000, 6, "20MHz"},
    {0x30, 10000000, 12, "10MHz"},
};

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
    Asm& out(uint8_t port, uint8_t value) { return b({0x3E, value, 0xD3, port}); } // LD A,v; OUT (p),A
    Asm& jrSelf() { return b({0x18, 0xFE}); }
    size_t here() const { return code.size(); }
    /// JR / DJNZ displacement from the byte after the offset to `target`
    uint8_t rel(size_t target) const { return static_cast<uint8_t>(static_cast<int>(target) - static_cast<int>(code.size() + 1)); }
    Asm& at(size_t address)
    {
        if (code.size() < address)
            code.resize(address, 0x00);
        return *this;
    }
};

/// The card on its own (no ZX CPU): the host clock is the frame lifecycle
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
        ctx.emulatorState.hw_turbo_ratio_applied = 1;
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
            chip->handleFrameEnd(0);
        }
    }

    uint64_t steps() const { return chip->getActivityCounters().cpuSteps; }
};

/// DI; OUT (#0F),cfg; JR $ (12 clocks a step)
Asm spinAt(uint8_t cfg)
{
    Asm p;
    p.b({0xF3}).out(0x0F, cfg).jrSelf();
    return p;
}

std::string clockName(const testing::TestParamInfo<ClockCase>& info) { return info.param.name; }
} // namespace

/// region <Card time against host time>

class NeoGSClocks_Card : public testing::TestWithParam<ClockCase>
{
};

/// One ZX frame is a fixed number of card ticks at every card clock. The card
/// ends each frame on an instruction boundary, a little past the frame end;
/// that overshoot must be paid back by the next frame, not added to it, or
/// the card runs ahead of the host a few ticks every frame
TEST_P(NeoGSClocks_Card, FramesTrackHostTimeWithoutDrift)
{
    const ClockCase k = GetParam();
    Card c;
    c.boot(spinAt(k.cfg));
    c.runFrames(1);
    ASSERT_EQ(c.chip->cardClockHz(), k.hz);

    const int64_t frameTicks = GSHostClock::frameUnits(&c.ctx, SoundChip_NeoGS::TICKS_PER_SECOND);
    const int64_t base = c.chip->cardTicks();
    const uint64_t stepsBase = c.steps();
    const int64_t oneInstruction = 12 * k.ticksPerCycle;
    for (int i = 1; i <= 500; i++)
    {
        c.runFrames(1);
        const int64_t off = c.chip->cardTicks() - base - static_cast<int64_t>(i) * frameTicks;
        ASSERT_GE(off, -oneInstruction) << "frame " << i;
        ASSERT_LE(off, oneInstruction) << "frame " << i << ": the card is " << off << " ticks ahead of the host";
    }

    // 500 frames of JR $ at this clock
    const double expectedSteps = 500.0 * static_cast<double>(frameTicks) / static_cast<double>(oneInstruction);
    EXPECT_NEAR(static_cast<double>(c.steps() - stepsBase), expectedSteps, 2.0);
}

/// The test above cannot see the overshoot: a frame (2 396 160 ticks) is an
/// exact multiple of a 12-clock JR $ at all four card clocks (and of JP $ and
/// of 13-clock instructions too), so every frame ends exactly on an
/// instruction boundary. The loop PUSH AF / POP AF / JR (11 + 10 + 12 clocks)
/// does not divide the frame at any card clock: a frame ends a few ticks past
/// its end. Against the nominal timeline (frame N ends at N * frameTicks) the
/// card may only ever be one instruction ahead, however many frames have run
TEST_P(NeoGSClocks_Card, OvershootOfUnalignedInstructionsDoesNotAccumulate)
{
    const ClockCase k = GetParam();
    const int64_t longestInstruction = 12 * k.ticksPerCycle;
    Card c;
    Asm p;
    p.b({0xF3}).out(0x0F, k.cfg);
    p.b({0xF5, 0xF1, 0x18, 0xFC}); // PUSH AF; POP AF; JR -4 (back to PUSH)
    c.boot(p);
    c.runFrames(1);

    const int64_t frameTicks = GSHostClock::frameUnits(&c.ctx, SoundChip_NeoGS::TICKS_PER_SECOND);
    ASSERT_NE(frameTicks % (33 * k.ticksPerCycle), 0) << "the case must not be frame-aligned, or it proves nothing";

    const int64_t base = c.chip->cardTicks();
    for (int i = 1; i <= 500; i++)
    {
        c.runFrames(1);
        const int64_t off = c.chip->cardTicks() - base - static_cast<int64_t>(i) * frameTicks;
        ASSERT_GE(off, -longestInstruction) << "frame " << i;
        ASSERT_LE(off, longestInstruction) << "frame " << i << ": the card is " << off << " ticks ahead of the host";
    }
}

/// The CPU takes the new clock from the instruction after the OUT, for every
/// pair of clocks
TEST_P(NeoGSClocks_Card, SwitchTakesEffectOnTheNextInstruction)
{
    const ClockCase from = GetParam();
    for (const ClockCase& to : kClocks)
    {
        if (to.cfg == from.cfg)
            continue;
        SCOPED_TRACE(std::string(from.name) + " -> " + to.name);

        // DI; OUT (#0F),from; LD B,100; DJNZ $; OUT (#0F),to; JR $
        Asm p;
        p.b({0xF3}).out(0x0F, from.cfg).b({0x06, 100});
        const size_t loop = p.here();
        p.b({0x10});
        p.b({p.rel(loop)});
        p.out(0x0F, to.cfg).jrSelf();

        Card c;
        c.boot(p);
        c.runMs(0.01);
        ASSERT_EQ(c.chip->cardClockHz(), from.hz);

        // Run to the switch in small steps
        int guard = 0;
        while (c.chip->cardClockHz() != to.hz && guard++ < 10000)
            c.chip->runFor(60);
        ASSERT_EQ(c.chip->cardClockHz(), to.hz);

        c.runMs(0.01);
        const uint64_t before = c.steps();
        c.runMs(10);
        EXPECT_NEAR(static_cast<double>(c.steps() - before), 10.0 * to.hz / 12000.0, 2.0);
    }
}

INSTANTIATE_TEST_SUITE_P(Clocks, NeoGSClocks_Card, testing::ValuesIn(kClocks), clockName);

/// The timer runs from the 24 MHz crystal, not the CPU clock: an interrupt
/// handler that switches the CPU clock on every tick (through all four) leaves
/// the 37.5 kHz period exact
TEST(NeoGSClocks_Timer, KeepsItsPhaseWhileTheCpuClockChangesEveryTick)
{
    // main: DI; LD C,0; IM 1; EI; loop: HALT; JR loop
    // #38: PUSH AF; LD A,C; ADD A,#10; AND #30; LD C,A; OUT (#0F),A; POP AF; EI; RET
    Asm p;
    p.b({0xF3, 0x0E, 0x00, 0xED, 0x56, 0xFB});
    const size_t loop = p.here();
    p.b({0x76, 0x18});
    p.b({p.rel(loop)});
    p.at(0x38).b({0xF5, 0x79, 0xC6, 0x10, 0xE6, 0x30, 0x4F, 0xD3, 0x0F, 0xF1, 0xFB, 0xC9});

    Card c;
    c.boot(p);
    c.chip->startPortTrace();
    c.runMs(100);

    std::vector<int64_t> times;
    for (const GSTraceEvent& e : c.chip->getPortTraceEvents())
    {
        if (e.side == GSTraceSide::Interrupt && !(e.flags & GSTraceFlags::kNmi))
            times.push_back(e.timestamp);
    }
    ASSERT_GE(times.size(), 3700u);
    EXPECT_NEAR(static_cast<double>(times.size()), 3750.0, 2.0);

    // The trace stamps an interrupt after its acceptance: the HALT in
    // progress (4 clocks), the timer's 2-clock sync and the 13-clock IM 1
    // acknowledge, all at the clock of that moment - which this handler
    // changes every time. So one period may move by up to (4 + 2 + 13)
    // clocks of the slowest clock; the tick itself is exact (3,200 ticks),
    // which the total span over all periods shows
    const int64_t slack = (4 + 2 + 13) * 12;
    for (size_t i = 1; i < times.size(); i++)
        ASSERT_NEAR(times[i] - times[i - 1], 3200, slack) << "period " << i;
    const size_t n = times.size() - 1;
    EXPECT_NEAR(static_cast<double>(times[n] - times[0]), 3200.0 * static_cast<double>(n), static_cast<double>(slack));
}

/// endregion </Card time against host time>

/// region <SPI and DMA>

namespace
{
/// An SD card that answers #A5 to every byte
class AnswerA5 : public SpiDevice
{
public:
    void select(bool) override {}
    uint8_t exchange(uint8_t) override { return 0xA5; }
};

/// OUT (#13),A, then `nops` NOPs, then IN A,(#13) into #4000
Asm spiProgram(uint8_t cfg, int nops)
{
    Asm p;
    p.b({0xF3}).out(0x0F, cfg).b({0x3E, 0x00, 0xD3, 0x13});
    for (int i = 0; i < nops; i++)
        p.b({0x00});
    p.b({0xDB, 0x13, 0x32, 0x00, 0x40}).jrSelf();
    return p;
}

uint8_t spiReadAfter(const ClockCase& k, int nops)
{
    Card c;
    AnswerA5 sd;
    c.chip->spi().attach(NeoGSSpi::SD, &sd);
    c.chip->poke(0x4000, 0x00);
    c.boot(spiProgram(k.cfg, nops));
    c.chip->spi().attach(NeoGSSpi::SD, &sd);
    c.runMs(1);
    return c.chip->peek(0x4000);
}
} // namespace

class NeoGSClocks_Spi : public testing::TestWithParam<ClockCase>
{
};

/// An SD byte takes 16 card clocks, whatever the clock: read right after the
/// OUT the old byte is still there, four NOPs (16 clocks) later the new one.
/// The same program gives the same result at every clock
TEST_P(NeoGSClocks_Spi, SdByteTakesSixteenCardClocks)
{
    const ClockCase k = GetParam();
    EXPECT_EQ(spiReadAfter(k, 0), 0xFF) << "IN right after OUT: the byte is still in flight";
    EXPECT_EQ(spiReadAfter(k, 4), 0xA5) << "16 clocks later: received";

    NeoGSSpi spi;
    spi.start(NeoGSSpi::SD, 0x00, 1000, k.ticksPerCycle);
    EXPECT_EQ(spi.completion(NeoGSSpi::SD), 1000 + 16 * k.ticksPerCycle);
}

/// A clock change halfway through a byte: 8 clocks at the old clock, 8 at the new
TEST_P(NeoGSClocks_Spi, ClockChangeInsideAByte)
{
    const ClockCase from = GetParam();
    for (const ClockCase& to : kClocks)
    {
        NeoGSSpi spi;
        spi.start(NeoGSSpi::SD, 0x00, 1000, from.ticksPerCycle);
        const int64_t half = 1000 + 8 * from.ticksPerCycle;
        spi.onClockChange(half, from.ticksPerCycle, to.ticksPerCycle);
        EXPECT_EQ(spi.completion(NeoGSSpi::SD), half + 8 * to.ticksPerCycle) << from.name << " -> " << to.name;
    }
}

INSTANTIATE_TEST_SUITE_P(Clocks, NeoGSClocks_Spi, testing::ValuesIn(kClocks), clockName);

namespace
{
/// SD card for the DMA: #FF, a start token, 512 bytes, CRC
class TokenSd : public SpiDevice
{
public:
    std::deque<uint8_t> script;
    int exchanges = 0;
    void select(bool) override {}
    uint8_t exchange(uint8_t) override
    {
        exchanges++;
        if (script.empty())
            return 0xFF;
        const uint8_t b = script.front();
        script.pop_front();
        return b;
    }
};

struct DmaHost : NeoGSDma::Host
{
    int64_t unitsPerCycle = 6;
    int64_t stalled = 0;
    void dmaStall(int64_t units) override { stalled += units; }
    int64_t dmaUnitsPerCycle() const override { return unitsPerCycle; }
    void dmaSdByteDone(uint8_t) override {}
};

struct DmaRun
{
    int64_t duration = 0;
    int64_t stalled = 0;
    int exchanges = 0;
};

/// One SD block by DMA; `switchTo` (if > 0) changes the card clock
/// `switchAfterBytes` SD byte times after the start (mid-block)
DmaRun sdBlock(int64_t unitsPerCycle, int64_t switchTo = 0, int switchAfterBytes = 0)
{
    Flash29F040B flash{120e6};
    NeoGSMemory mem{4096, &flash};
    NeoGSInterrupts irq;
    DmaHost host;
    host.unitsPerCycle = unitsPerCycle;
    NeoGSDma dma{host, mem, irq};
    TokenSd sd;
    irq.writeEnable(0x87);
    dma.attachSd(&sd);
    sd.script.assign(4, 0xFF);
    sd.script.push_back(0xFE);
    for (int i = 0; i < 512; i++)
        sd.script.push_back(static_cast<uint8_t>(i));
    sd.script.push_back(0x12);
    sd.script.push_back(0x34);

    dma.writeModuleSelect(2);
    dma.writeRegister(0, 0x01, 0);
    dma.writeRegister(1, 0x00, 0);
    dma.writeRegister(2, 0x00, 0);
    const int64_t start = 1000;
    dma.writeRegister(3, 0x80, start);
    int64_t last = start;
    const int64_t switchAt = start + static_cast<int64_t>(switchAfterBytes) * NeoGSDma::SD_BYTE_CLOCKS * unitsPerCycle;
    while (dma.running(NeoGSDma::SD))
    {
        if (switchTo > 0 && host.unitsPerCycle != switchTo && dma.nextEvent() > switchAt)
        {
            // What the card does when its CPU writes a new clock to GSCFG0
            dma.onClockChange(switchAt, host.unitsPerCycle, switchTo);
            host.unitsPerCycle = switchTo;
        }
        const int64_t t = dma.nextEvent();
        dma.run(t);
        last = t;
    }
    for (int i = 0; i < 512; i++)
        EXPECT_EQ(mem.ram()[0x010000 + i], static_cast<uint8_t>(i)) << i;
    return {last - start, host.stalled, sd.exchanges};
}
} // namespace

class NeoGSClocks_Dma : public testing::TestWithParam<ClockCase>
{
};

/// A DMA block costs the same number of card clocks at every clock: its
/// length in ticks is proportional to the tick cost of a clock
TEST_P(NeoGSClocks_Dma, SdBlockCostsTheSameCardClocks)
{
    const ClockCase k = GetParam();
    const DmaRun run = sdBlock(k.ticksPerCycle);
    const DmaRun ref = sdBlock(1);
    EXPECT_EQ(run.exchanges, ref.exchanges);
    EXPECT_EQ(run.stalled, ref.stalled * k.ticksPerCycle) << "2 clocks a byte plus the grant, in card clocks";
    EXPECT_EQ(run.duration, ref.duration * k.ticksPerCycle);
}

/// A clock change in the middle of a block: the bytes before it take the old
/// clock, the bytes after it the new one (the modules count card clocks)
TEST_P(NeoGSClocks_Dma, ClockChangeInsideABlock)
{
    const ClockCase from = GetParam();
    for (const ClockCase& to : kClocks)
    {
        if (to.cfg == from.cfg)
            continue;
        SCOPED_TRACE(std::string(from.name) + " -> " + to.name);
        const DmaRun mixed = sdBlock(from.ticksPerCycle, to.ticksPerCycle, 200);
        const DmaRun ref = sdBlock(1); // durations in card clocks
        const int64_t before = 200 * NeoGSDma::SD_BYTE_CLOCKS;
        EXPECT_EQ(mixed.duration, before * from.ticksPerCycle + (ref.duration - before) * to.ticksPerCycle);
        EXPECT_EQ(mixed.stalled, ref.stalled * to.ticksPerCycle) << "the RAM burst runs after the switch";
    }
}

INSTANTIATE_TEST_SUITE_P(Clocks, NeoGSClocks_Dma, testing::ValuesIn(kClocks), clockName);

namespace
{
struct ZxHost : NeoGSZxDma::Host
{
    NeoGSZxDma* zx = nullptr;
    int64_t unitsPerCycle = 10;
    int64_t hostNow = 0;
    double unitsPerT = 120e6 / 3.5e6;
    std::vector<uint32_t> waits;

    void zxCatchUp() override { zx->run(hostNow); }
    bool zxHostNowUnits(int64_t& now) const override
    {
        now = hostNow;
        return true;
    }
    double zxUnitsPerHostT() const override { return unitsPerT; }
    void zxAddHostWait(uint32_t t) override
    {
        waits.push_back(t);
        hostNow += std::llround(t * unitsPerT);
    }
    int64_t zxUnitsPerCycle() const override { return unitsPerCycle; }
    void zxStall(int64_t) override {}
    int64_t zxStallUntil() const override { return 0; }
    bool zxInstall(bool) override { return true; }
    uint32_t zxFrame() const override { return 100; }
    void zxReschedule() override {}
    void zxLateStart(int64_t) override {}
    void zxTrace(bool, uint32_t, uint8_t) override {}
};

struct ZxFixture
{
    Flash29F040B flash{120e6};
    NeoGSMemory mem{2048, &flash};
    NeoGSInterrupts irq;
    DmaHost dmaHost;
    NeoGSDma dma{dmaHost, mem, irq};
    ZxHost host;
    NeoGSZxDma zx{host, dma, mem};

    ZxFixture(int64_t unitsPerCycle, double hostHz)
    {
        host.zx = &zx;
        host.unitsPerCycle = unitsPerCycle;
        dmaHost.unitsPerCycle = unitsPerCycle;
        host.unitsPerT = 120e6 / hostHz;
        dma.writeModuleSelect(1);
        zx.onModuleSelectWritten();
        dma.writeRegister(0, 0x00, 0);
        dma.writeRegister(1, 0x01, 0);
        dma.writeRegister(2, 0x00, 0);
        dma.writeRegister(3, 0x80, 0);
        zx.onControlWritten(false, 0);
    }

    void read(double gapT)
    {
        host.hostNow += std::llround(gapT * host.unitsPerT);
        (void)zx.onRead(0x0000, 0xC3, false, true);
    }
};
} // namespace

class NeoGSClocks_ZxDma : public testing::TestWithParam<ClockCase>
{
};

/// ZX-DMA fetches take 8 card clocks. A 3.5 MHz host never waits at any card
/// clock; a 14 MHz host doing back-to-back reads waits exactly as long as the
/// fetch outlasts its access (neogs-zxdma-design.md §5.8), which depends on
/// the card clock
TEST_P(NeoGSClocks_ZxDma, WaitsFollowTheCardClock)
{
    const ClockCase k = GetParam();
    {
        ZxFixture slow(k.ticksPerCycle, 3.5e6);
        for (int i = 0; i < 16; i++)
            slow.read(21);
        EXPECT_TRUE(slow.host.waits.empty()) << "21 T apart at 3.5 MHz";
    }

    ZxFixture fast(k.ticksPerCycle, 14e6);
    fast.read(21);
    fast.read(3);
    const double upt = fast.host.unitsPerT;
    const int64_t firstStart = std::llround(21 * upt) - std::llround(3 * upt);
    const int64_t sample = std::llround(24 * upt) - std::llround(2 * upt);
    const int64_t fetch = 8 * k.ticksPerCycle;
    const double needed = static_cast<double>(firstStart + fetch - sample) / upt;
    if (needed <= 0)
    {
        EXPECT_TRUE(fast.host.waits.empty());
    }
    else
    {
        ASSERT_EQ(fast.host.waits.size(), 1u);
        EXPECT_EQ(fast.host.waits[0], static_cast<uint32_t>(std::ceil(needed)));
    }
}

INSTANTIATE_TEST_SUITE_P(Clocks, NeoGSClocks_ZxDma, testing::ValuesIn(kClocks), clockName);

/// endregion </SPI and DMA>

/// region <Host protocol on a running machine>

namespace
{
constexpr uint16_t kZxProgram = 0x8000;
constexpr uint16_t kZxReplies = 0x9000;
constexpr uint16_t kZxPolls = 0x8FFC;
constexpr uint16_t kZxDone = 0x8FFF;

/// ZX side: 256 exchanges. Data B to #B3, command #40 to #BB, wait until the
/// card takes the command (status bit 0 clear), wait for its reply (bit 7),
/// read it into #9000+. DE counts the polls. Marks #8FFF = #AA at the end
std::vector<uint8_t> zxProgram()
{
    std::vector<uint8_t> p = {
        0xF3,             // DI
        0x21, 0x00, 0x90, // LD HL,#9000
        0x11, 0x00, 0x00, // LD DE,0
        0x06, 0x00,       // LD B,0
    };
    const size_t loop = p.size();
    p.insert(p.end(), {0x78, 0xD3, 0xB3, 0x3E, 0x40, 0xD3, 0xBB}); // LD A,B; OUT (#B3),A; LD A,#40; OUT (#BB),A
    const size_t waitCmd = p.size();
    p.insert(p.end(), {0x13, 0xDB, 0xBB, 0x1F, 0x38}); // INC DE; IN A,(#BB); RRA; JR C,waitCmd
    p.push_back(static_cast<uint8_t>(static_cast<int>(waitCmd) - static_cast<int>(p.size() + 1)));
    const size_t waitData = p.size();
    p.insert(p.end(), {0x13, 0xDB, 0xBB, 0x17, 0x30}); // INC DE; IN A,(#BB); RLA; JR NC,waitData
    p.push_back(static_cast<uint8_t>(static_cast<int>(waitData) - static_cast<int>(p.size() + 1)));
    p.insert(p.end(), {0xDB, 0xB3, 0x77, 0x23, 0x10}); // IN A,(#B3); LD (HL),A; INC HL; DJNZ loop
    p.push_back(static_cast<uint8_t>(static_cast<int>(loop) - static_cast<int>(p.size() + 1)));
    p.insert(p.end(), {0xED, 0x53, 0xFC, 0x8F,  // LD (#8FFC),DE
                       0x3E, 0xAA, 0x32, 0xFF, 0x8F, // LD A,#AA; LD (#8FFF),A
                       0x18, 0xFE});                  // JR $
    return p;
}

/// Card side: set the clock, then answer every command with data + command.
/// With `cycle`, the card moves to the next clock after every reply; with
/// `delay`, it spends 800 clocks (50 x DEC E / JR NZ) before each reply, a
/// fixed amount of card work whose length in time follows the clock
Asm cardEcho(uint8_t cfg, bool cycle, bool delay = false)
{
    Asm p;
    p.b({0xF3, 0x0E, cfg}).b({0x79, 0xD3, 0x0F}); // DI; LD C,cfg; LD A,C; OUT (#0F),A
    const size_t loop = p.here();
    p.b({0xDB, 0x04, 0x1F, 0x30}); // IN A,(#04); RRA; JR NC,loop
    p.b({p.rel(loop)});
    p.b({0xDB, 0x01, 0x47, 0xDB, 0x02, 0x80}); // IN A,(1); LD B,A; IN A,(2); ADD A,B
    if (delay)
    {
        p.b({0x57, 0x1E, 50}); // LD D,A; LD E,50
        const size_t wait = p.here();
        p.b({0x1D, 0x20});     // DEC E; JR NZ,wait
        p.b({p.rel(wait)});
        p.b({0x7A});           // LD A,D
    }
    p.b({0xD3, 0x03, 0xD3, 0x05}); // OUT (3),A; OUT (5),A
    if (cycle)
        p.b({0x79, 0xC6, 0x10, 0xE6, 0x30, 0x4F, 0xD3, 0x0F}); // LD A,C; ADD A,#10; AND #30; LD C,A; OUT (#0F),A
    p.b({0x18});
    p.b({p.rel(loop)});
    return p;
}

struct HostResult
{
    bool done = false;
    int badReplies = 0;
    uint16_t polls = 0;
    int frames = 0;
    int64_t driftTicks = 0; // card time against host frames at the end
};

HostResult runHostExchange(uint8_t cardCfg, bool cycle, uint8_t hostSpeed, bool delay = false)
{
    HostResult r;
    SoundCardScope gs(TestSound::GeneralSound);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    EXPECT_NE(emulator, nullptr);
    if (!emulator)
        return r;
    EmulatorContext* ctx = emulator->GetContext();
    SoundManager* sm = ctx->pSoundManager;
    EXPECT_TRUE(FitGeneralSoundCard(sm, GSTypeKind::NGS));
    auto* card = dynamic_cast<SoundChip_NeoGS*>(sm->getGeneralSound());
    EXPECT_NE(card, nullptr);
    if (!card)
    {
        EmulatorTestHelper::CleanupEmulator(emulator);
        return r;
    }
    emulator->SetSpeedMultiplier(hostSpeed);

    const Asm cardProgram = cardEcho(cardCfg, cycle, delay);
    card->flash().load(cardProgram.code.data(), cardProgram.code.size());
    card->reset();

    Z80* z80 = ctx->pCore->GetZ80();
    const std::vector<uint8_t> zx = zxProgram();
    for (size_t i = 0; i < zx.size(); i++)
        z80->DirectWrite(static_cast<uint16_t>(kZxProgram + i), zx[i]);
    z80->DirectWrite(kZxDone, 0x00);
    z80->pc = kZxProgram;

    emulator->RunNFrames(1);
    const int64_t frameTicks = GSHostClock::frameUnits(ctx, SoundChip_NeoGS::TICKS_PER_SECOND);
    const int64_t base = card->cardTicks();
    for (r.frames = 1; r.frames <= 300; r.frames++)
    {
        emulator->RunNFrames(1);
        if (z80->DirectRead(kZxDone) == 0xAA)
            break;
    }
    r.done = z80->DirectRead(kZxDone) == 0xAA;
    r.driftTicks = card->cardTicks() - base - static_cast<int64_t>(r.frames) * frameTicks;
    for (int i = 0; i < 256; i++)
    {
        const uint8_t expected = static_cast<uint8_t>(0x40 + (i == 0 ? 0 : 256 - i));
        if (z80->DirectRead(static_cast<uint16_t>(kZxReplies + i)) != expected)
            r.badReplies++;
    }
    r.polls = static_cast<uint16_t>(z80->DirectRead(kZxPolls) | (z80->DirectRead(kZxPolls + 1) << 8));
    EmulatorTestHelper::CleanupEmulator(emulator);
    return r;
}
} // namespace

class NeoGSClocks_Host : public testing::TestWithParam<std::tuple<ClockCase, uint8_t>>
{
};

/// A running Pentagon exchanges 256 command/data pairs with the card: every
/// reply arrives and is right at every card clock and every host speed, and
/// the card's time stays on the host's frames
TEST_P(NeoGSClocks_Host, ExchangesAreRightAndStayInStep)
{
    const auto [k, hostSpeed] = GetParam();
    const HostResult r = runHostExchange(k.cfg, false, hostSpeed);
    ASSERT_TRUE(r.done) << "the ZX program did not finish in 300 frames";
    EXPECT_EQ(r.badReplies, 0);
    EXPECT_LE(std::llabs(r.driftTicks), 24 * k.ticksPerCycle) << "card ticks against host frames";
}

INSTANTIATE_TEST_SUITE_P(ClocksAndHostSpeeds, NeoGSClocks_Host,
                         testing::Combine(testing::ValuesIn(kClocks), testing::Values<uint8_t>(1, 2, 4)),
                         [](const testing::TestParamInfo<std::tuple<ClockCase, uint8_t>>& info) {
                             return std::string(std::get<0>(info.param).name) + "_x" +
                                    std::to_string(std::get<1>(info.param));
                         });

/// The host waits longer for a slower card: with 800 clocks of card work per
/// reply, the host's poll count orders with the card clock (10 < 12 < 20 <
/// 24 MHz) - the card's time is the host's time at every clock
TEST(NeoGSClocks_HostLatency, PollsOrderWithTheCardClock)
{
    const HostResult c10 = runHostExchange(0x30, false, 1, true);
    const HostResult c12 = runHostExchange(0x10, false, 1, true);
    const HostResult c20 = runHostExchange(0x20, false, 1, true);
    const HostResult c24 = runHostExchange(0x00, false, 1, true);
    ASSERT_TRUE(c10.done && c12.done && c20.done && c24.done);
    EXPECT_GT(c10.polls, c12.polls);
    EXPECT_GT(c12.polls, c20.polls);
    EXPECT_GT(c20.polls, c24.polls);

    // 800 clocks = 80 / 66.7 / 40 / 33.3 us; one poll is 33 T (9.4 us at
    // 3.5469 MHz Pentagon timing): the extra polls follow the extra time
    const double pollUs = 33.0 / 3.5;
    EXPECT_NEAR((c10.polls - c24.polls) / 256.0, (80.0 - 33.33) / pollUs, 1.0);
}

/// The card moves to the next clock after every reply (all four in turn):
/// the exchange stays right
TEST(NeoGSClocks_HostSwitching, ClockChangesBetweenRepliesKeepTheExchangeRight)
{
    for (uint8_t hostSpeed : {1, 4})
    {
        SCOPED_TRACE(hostSpeed);
        const HostResult r = runHostExchange(0x30, true, hostSpeed);
        ASSERT_TRUE(r.done);
        EXPECT_EQ(r.badReplies, 0);
        EXPECT_LE(std::llabs(r.driftTicks), 24 * 12);
    }
}

/// endregion </Host protocol on a running machine>
