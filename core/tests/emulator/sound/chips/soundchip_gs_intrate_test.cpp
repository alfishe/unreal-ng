// GS 37.5 kHz interrupt-rate accounting (pitch-stability instrumentation,
// 2026-09-20; carried over from gs-int-defer.patch, counters-only scope).
//
// Why this exists: on the General Sound card the periodic interrupt IS the DAC
// sample clock. The firmware ISR (INT0..INT7, copied to #4040 by QTPLAY)
// performs exactly ONE `LD A,(DE)` sample fetch per invocation, so a module's
// playback rate - and therefore its perceived PITCH - is the rate at which
// interrupts are ACCEPTED, not the rate at which the 320-cycle divider
// GENERATES them. Every generated interrupt the CPU never accepts is one
// sample the module never plays: the tune runs flat by exactly that fraction.
//
// The level-held INT flip-flop (BUG-5 fix) defers a request the CPU cannot
// take yet to the first instruction boundary that allows it; only a SECOND
// period arriving before the first is acknowledged is lost (one flip-flop ->
// interruptsCoalesced). These tests count generated
// (GSActivityCounters::interruptPeriods) against accepted
// (::interruptsAccepted) and express the gap in cents, so a pitch error that
// is otherwise only audible becomes a number a regression can pin.
//
// The main-loop DI-window case lives in bootdiag 5_InterruptLevelHoldNoLoss;
// this file covers the ISR-side overrun (the QTDONE quantum-wrap shape) over
// the same runTo path.
//
// Runtime justification: real-ROM POSTs per harness are the only faithful way
// to place the IM2 vector table (I=0x17) and the #4040 ISR slot, same as the
// bootdiag suite.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"

namespace
{
constexpr uint16_t kPortData = SoundChip_GeneralSound::PORT_DATA;
constexpr uint16_t kPortCommand = SoundChip_GeneralSound::PORT_COMMAND;

struct GSIntHarness
{
    EmulatorContext ctx;
    std::unique_ptr<SoundChip_GeneralSound> chip;

    GSIntHarness() : ctx(LoggerLevel::LogError)
    {
        ctx.config.sound.gs_vol = 8000;
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_shift_applied = 0;
        chip = std::make_unique<SoundChip_GeneralSound>(&ctx, 512, 44100);
        chip->loadROM("rom/gs105a.rom");
    }

    void runFrames(int n)
    {
        for (int i = 0; i < n; i++)
        {
            chip->handleFrameStart();
            chip->handleFrameEnd(SAMPLES_PER_FRAME);
        }
    }

    bool waitFlagClear(uint8_t mask, int maxFrames)
    {
        for (int i = 0; i < maxFrames; i++)
        {
            if (!(chip->readStatus() & mask))
                return true;
            runFrames(1);
        }
        return false;
    }

    bool sendDataWait(uint8_t value)
    {
        chip->portDeviceOutMethod(kPortData, value);
        return waitFlagClear(0x80, 1000);
    }

    bool sendCommandWait(uint8_t cmd)
    {
        chip->portDeviceOutMethod(kPortCommand, cmd);
        return waitFlagClear(0x01, 1000);
    }

    // COM16 (put byte): value consumed at dispatch, WTDTL drains lo then hi
    bool putByte(uint16_t addr, uint8_t value)
    {
        chip->portDeviceOutMethod(kPortData, value);
        if (!sendCommandWait(0x16))
            return false;
        return sendDataWait(addr & 0xFF) && sendDataWait(addr >> 8);
    }

    // COM13 (jump): lo at dispatch time, hi via WTDTL
    bool jumpTo(uint16_t addr)
    {
        chip->portDeviceOutMethod(kPortData, addr & 0xFF);
        if (!sendCommandWait(0x13))
            return false;
        return sendDataWait(addr >> 8);
    }

    bool bootToPost()
    {
        for (int sec = 0; sec < 20; sec++)
        {
            runFrames(50);
            if (chip->getActivityCounters().volumeLatchWrites >= 4)
            {
                // Drain the POST's unread reply (INITR0E NUMPG) so the bit7
                // polls below never stall on it (same as bootdiag)
                if (chip->readStatus() & 0x80)
                    (void)chip->portDeviceInMethod(kPortData);
                return true;
            }
        }
        return false;
    }

    bool writeBlock(uint16_t addr, const uint8_t* data, size_t len)
    {
        for (size_t i = 0; i < len; i++)
            if (!putByte(static_cast<uint16_t>(addr + i), data[i]))
                return false;
        return true;
    }
};

struct RateResult
{
    uint64_t periods = 0;
    uint64_t accepted = 0;
    uint64_t coalesced = 0;
    uint64_t lost = 0; // periods - accepted: must all be accounted coalesced
    double acceptRatio = 1.0;
    double cents = 0.0;
};

/// ISR of controlled duration, shaped like the firmware's:
///   LD A,(#6000)   ; the DAC sample fetch - the thing that must not be lost
///   LD B,n / DJNZ  ; stand-in for the handler body (QTDONE quantum advance,
///                  ; volume latching, ISR re-programming)
///   EI / RET       ; the canonical tail every Z80 handler ends with
/// Total, measured from the interrupt acknowledge: 48 + 13*n T-states.
std::vector<uint8_t> makeIsr(uint8_t djnzCount)
{
    return {0x3A, 0x00, 0x60, 0x06, djnzCount, 0x10, 0xFE, 0xFB, 0xC9};
}
int isrCycles(uint8_t n) { return 48 + 13 * static_cast<int>(n); }

// DI; IM 2; LD A,#17; LD I,A; LD SP,#4400; EI; JR self
const uint8_t kStub[] = {0xF3, 0xED, 0x5E, 0x3E, 0x17, 0xED, 0x47, 0x31, 0x00, 0x44, 0xFB, 0x18, 0xFE};

RateResult measure(uint8_t djnzCount, int frames, const char* label)
{
    GSIntHarness h;
    EXPECT_TRUE(h.chip->isROMLoaded());
    EXPECT_TRUE(h.bootToPost());

    const std::vector<uint8_t> isr = makeIsr(djnzCount);
    EXPECT_TRUE(h.writeBlock(0x4040, isr.data(), isr.size())) << label << ": ISR upload failed";
    EXPECT_TRUE(h.writeBlock(0x5000, kStub, sizeof(kStub))) << label << ": stub upload failed";
    EXPECT_TRUE(h.jumpTo(0x5000)) << label << ": jump failed";

    h.runFrames(5); // settle: the first frames still carry the dispatcher tail

    const auto& counters = h.chip->getActivityCounters();
    const uint64_t periods0 = counters.interruptPeriods;
    const uint64_t accepted0 = counters.interruptsAccepted;
    const uint64_t coalesced0 = counters.interruptsCoalesced;

    h.runFrames(frames);

    RateResult r;
    r.periods = counters.interruptPeriods - periods0;
    r.accepted = counters.interruptsAccepted - accepted0;
    r.coalesced = counters.interruptsCoalesced - coalesced0;
    r.lost = r.periods - r.accepted;
    r.acceptRatio = r.periods ? static_cast<double>(r.accepted) / static_cast<double>(r.periods) : 1.0;
    r.cents = 1200.0 * std::log2(r.acceptRatio);

    printf("%-30s isr=%4dT  generated=%-7llu accepted=%-7llu coalesced=%-7llu  "
           "rate=%6.2f%%  pitch=%+8.2f cents\n",
           label, isrCycles(djnzCount), (unsigned long long)r.periods, (unsigned long long)r.accepted,
           (unsigned long long)r.coalesced, 100.0 * r.acceptRatio, r.cents);
    return r;
}
} // namespace

/// The 320-cycle divider itself: 12 MHz / 320 = 37.5 kHz, and at the harness
/// frame geometry (19968 us -> 239616 GS cycles) that is exactly 748.8
/// generated interrupts per frame - the reference the accepted rate is
/// measured against.
TEST(SoundChip_GeneralSound_IntRate, GeneratorPeriodIsExact)
{
    GSIntHarness h;
    ASSERT_TRUE(h.chip->isROMLoaded());
    ASSERT_TRUE(h.bootToPost());

    const auto& counters = h.chip->getActivityCounters();
    const uint64_t before = counters.interruptPeriods;
    constexpr int kFrames = 500;
    h.runFrames(kFrames);
    const double perFrame = static_cast<double>(counters.interruptPeriods - before) / kFrames;

    printf("generator: %.4f periods/frame (expected 748.8 = 37.5 kHz)\n", perFrame);
    EXPECT_NEAR(perFrame, 748.8, 0.05) << "the 37.5 kHz divider itself is not exact";
}

/// An ISR shorter than one 320-cycle period always finishes in time, so every
/// generated interrupt must be accepted - no sample lost, no pitch error.
TEST(SoundChip_GeneralSound_IntRate, ShortIsrLosesNothing)
{
    const RateResult r = measure(20, 100, "ISR well inside one period");
    EXPECT_EQ(r.lost, 0u) << "interrupts dropped even though the ISR fits inside a period";
    EXPECT_EQ(r.coalesced, 0u) << "nothing may coalesce while the ISR fits inside a period";
    EXPECT_NEAR(r.cents, 0.0, 0.5);
}

/// THE REGRESSION. An ISR that straddles a period boundary is the normal case
/// in the real firmware: the per-sample handler is short, but every quantum it
/// falls through to QTDONE (advance QTBUSY, re-LDIR the 18-byte ISR, latch
/// four volumes) and that tail runs with IFF1 still clear. The INT flip-flop
/// holds the request, so the deferred interrupt is taken the instant the
/// handler re-enables - the sample is played LATE, never dropped, and the
/// average rate stays at one per ISR length.
///
/// Losing it instead makes the accepted rate collapse to one per TWO periods
/// (50%), and - because the overrun depends on what the module is doing that
/// quantum - makes the pitch error move with the music: "GS pitch floats".
/// The triage invariant: every lost period must be a counted coalesce.
TEST(SoundChip_GeneralSound_IntRate, IsrStraddlingPeriodDefersRatherThanDrops)
{
    // 360T: 40 cycles past one period. Hardware: accept every 360 cycles
    // (88.9% of generated). Dropping instead: every 640 (50%).
    const RateResult justOver = measure(24, 100, "ISR 40T past one period");
    // 568T: 1.78 periods. Hardware: one per 568 (56.3%). Dropping: 640 (50%).
    const RateResult wellOver = measure(40, 100, "ISR 1.8 periods long");

    EXPECT_NEAR(justOver.acceptRatio, 320.0 / isrCycles(24), 0.03)
        << "a deferred interrupt was dropped: the accepted rate collapsed to one per two periods, "
           "which is a permanently lost DAC sample and an audible pitch error";
    EXPECT_NEAR(wellOver.acceptRatio, 320.0 / isrCycles(40), 0.03)
        << "a deferred interrupt was dropped";
    // Invariant periods == accepted + coalesced, one request may be in flight
    EXPECT_LE(justOver.lost - justOver.coalesced, 1u) << "lost periods not accounted as coalesced";
    EXPECT_LE(wellOver.lost - wellOver.coalesced, 1u) << "lost periods not accounted as coalesced";
}

/// The audible symptom, expressed directly: the pitch error must not depend on
/// what the GS coprocessor happens to be doing. Three handler lengths that all
/// overrun the period by different amounts must land within a few cents of the
/// rate their own duration implies - not scatter across semitones.
TEST(SoundChip_GeneralSound_IntRate, PitchDoesNotFloatWithWorkload)
{
    struct Case { uint8_t n; const char* label; };
    static const Case kCases[] = {
        {21, "workload A (ISR 321T)"},
        {23, "workload B (ISR 347T)"},
        {26, "workload C (ISR 386T)"},
    };

    double worst = 0.0;
    for (const Case& c : kCases)
    {
        const RateResult r = measure(c.n, 100, c.label);
        const double expectedRatio = std::min(1.0, 320.0 / isrCycles(c.n));
        const double errCents = 1200.0 * std::log2(r.acceptRatio / expectedRatio);
        printf("    -> deviation from the hardware-deferred rate: %+.2f cents\n", errCents);
        worst = std::max(worst, std::abs(errCents));
    }

    EXPECT_LT(worst, 10.0) << "accepted interrupt rate depends on GS handler timing - audible pitch float";
}
