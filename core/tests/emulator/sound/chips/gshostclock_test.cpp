// GSHostClock::nextFrameBase and the General Sound cards' frame timeline.
//
// A card runs its CPU in whole instructions, so each frame ends a little past
// its nominal end. If the next frame takes the card's actual time as its base,
// that overshoot is added to every frame and the card's clock runs ahead of the
// machine without bound (found on NeoGS: the card clock crept ahead each frame).
// The base must follow the nominal timeline; the overshoot is paid back by the
// next frame's catch-up.
//
// Card time is observed through the 37.5 kHz interrupt-period counter, which
// every card derives from its own clock: after N host frames it must equal
// N * frame duration * 37.5 kHz, within one period - not N * that + the sum of
// the overshoots.
//
// Runtime justification: the classic card boots the real gs105a firmware (the
// only faithful instruction mix with unaligned lengths), 300 frames of card
// time is about 0.1 s of host time.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>

#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/gshostclock.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/gs/soundchip_gslw.h"

namespace
{
constexpr int kFrames = 300;
constexpr double kIntHz = GSClassicTiming::INT_FREQUENCY_HZ;

void configureHost(EmulatorContext& ctx)
{
    ctx.config.frame = 69888;
    ctx.config.frame_duration_us = 19968;
    ctx.emulatorState.current_z80_frequency_multiplier = 1;
    ctx.emulatorState.hw_turbo_ratio_applied = 1;
}

/// Interrupt periods the card must have generated after `frames` host frames
double expectedPeriods(const EmulatorContext& ctx, int frames)
{
    return frames * (static_cast<double>(ctx.config.frame_duration_us) * 1e-6) * kIntHz;
}
} // namespace

/// region <nextFrameBase>

namespace
{
constexpr int64_t kFrame = 239602;      // classic card units in one Pentagon frame
constexpr double kUnitsPerTact = 12.0 / 3.5; // 12 MHz card against the 3.5 MHz ZX base clock
} // namespace

TEST(GSHostClock_NextFrameBase, CardJustPastTheNominalEndKeepsTheNominalBase)
{
    // Frame 1000..1000+kFrame, the card stopped 7 units late: the next frame
    // starts on the nominal end, so its catch-up pays the 7 units back
    EXPECT_EQ(GSHostClock::nextFrameBase(1000, kFrame, 1000 + kFrame + 7, 69888, kUnitsPerTact), 1000 + kFrame);
}

TEST(GSHostClock_NextFrameBase, CardExactlyOnTheNominalEndKeepsTheNominalBase)
{
    EXPECT_EQ(GSHostClock::nextFrameBase(1000, kFrame, 1000 + kFrame, 69888, kUnitsPerTact), 1000 + kFrame);
}

TEST(GSHostClock_NextFrameBase, FirstFrameStartsAtTheCardTime)
{
    // No previous frame (length 0): nothing nominal to follow
    EXPECT_EQ(GSHostClock::nextFrameBase(0, 0, 0, 0, kUnitsPerTact), 0);
    EXPECT_EQ(GSHostClock::nextFrameBase(0, 0, 55, 0, kUnitsPerTact), 55);
}

TEST(GSHostClock_NextFrameBase, CardAWholeFrameAheadRestartsFromTheCardTime)
{
    // A frame or more past the nominal end is not an overshoot: a reset or a
    // state from another timeline. The base restarts from the card
    EXPECT_EQ(GSHostClock::nextFrameBase(1000, kFrame, 1000 + 2 * kFrame, 69888, kUnitsPerTact), 1000 + 2 * kFrame);
    EXPECT_EQ(GSHostClock::nextFrameBase(1000, kFrame, 0, 69888, kUnitsPerTact), 0) << "card clock reset below the old base";
}

TEST(GSHostClock_NextFrameBase, FrameStartedTwiceInTheSameTactKeepsTheBase)
{
    // A state captured right after a frame start (the card 49 units past its
    // base) and resumed: the main loop starts "a new frame" in the same host
    // tact. The base, and with it the card's 49-unit lead, must not move
    const int64_t base = 7372800;
    EXPECT_EQ(GSHostClock::nextFrameBase(base, kFrame, base + 49, 0, kUnitsPerTact), base);
}

TEST(GSHostClock_NextFrameBase, PausedMidFrameKeepsTheLeadOverTheHost)
{
    // The frame stopped 1000 host tacts in; the card had run to its position
    // for that tact plus 31 units of overshoot. A new frame start (host clock
    // frozen) re-anchors at the host's tact with the same lead
    const int64_t base = 5000;
    const int64_t atTact = base + static_cast<int64_t>(std::llround(1000 * kUnitsPerTact));
    EXPECT_EQ(GSHostClock::nextFrameBase(base, kFrame, atTact + 31, 1000, kUnitsPerTact), atTact);
}

TEST(GSHostClock_NextFrameBase, ZxClockRewoundRestartsFromTheCardTime)
{
    EXPECT_EQ(GSHostClock::nextFrameBase(1000, kFrame, 1000 + 500, -1, kUnitsPerTact), 1000 + 500);
}

TEST(GSHostClock_NextFrameBase, CardFarFromTheHostRestartsFromTheCardTime)
{
    // Short of the nominal end but a whole frame away from where the host says
    // it should be: the timeline does not match, trust the card
    const int64_t base = 5000;
    EXPECT_EQ(GSHostClock::nextFrameBase(base, kFrame, base + 100, 100000, kUnitsPerTact), base + 100);
}

TEST(GSHostClock_NextFrameBase, OvershootNeverAccumulatesOverManyFrames)
{
    // A card that always ends 13 units late, frame after frame
    int64_t base = 0, length = 0, cardNow = 0;
    for (int i = 0; i < 100000; i++)
    {
        base = GSHostClock::nextFrameBase(base, length, cardNow, 69888, kUnitsPerTact);
        length = kFrame;
        cardNow = base + kFrame + 13;
    }
    EXPECT_EQ(cardNow, 100000 * kFrame + 13);
}

/// endregion </nextFrameBase>

/// region <Cards against host time>

/// Classic card running the real firmware: its instructions do not divide the
/// frame, so every frame overshoots
TEST(GSHostClock_Cards, ClassicCardTimeFollowsTheHostWithoutDrift)
{
    EmulatorContext ctx(LoggerLevel::LogError);
    configureHost(ctx);
    ctx.config.sound.gs_vol = 8000;
    SoundChip_GeneralSound chip(&ctx, 512, 44100);
    chip.loadROM("rom/gs105a.rom");

    for (int i = 0; i < kFrames; i++)
    {
        chip.handleFrameStart();
        chip.handleFrameEnd(0);
    }

    const double periods = static_cast<double>(chip.getActivityCounters().interruptPeriods);
    EXPECT_NEAR(periods, expectedPeriods(ctx, kFrames), 2.0) << "the card's clock must not run ahead of the host";
}

/// Lightweight player: advances in 320-cycle quanta, the sub-quantum remainder
/// carries. Same timeline contract
TEST(GSHostClock_Cards, LightweightCardTimeFollowsTheHostWithoutDrift)
{
    EmulatorContext ctx(LoggerLevel::LogError);
    configureHost(ctx);
    ctx.config.sound.gs_vol = 8000;
    SoundChip_GSLightweight chip(&ctx, 512);

    for (int i = 0; i < kFrames; i++)
    {
        chip.handleFrameStart();
        chip.handleFrameEnd(0);
    }

    const double periods = static_cast<double>(chip.getActivityCounters().interruptPeriods);
    EXPECT_NEAR(periods, expectedPeriods(ctx, kFrames), 2.0) << "the card's clock must not run ahead of the host";
}

/// A frame that never ran to its end (the emulator paused between
/// handleFrameStart calls) must not leave the next frame's base ahead of the
/// card: the card keeps running and keeps its pace afterwards
TEST(GSHostClock_Cards, LightweightCardRecoversAfterFramesThatDidNotRun)
{
    EmulatorContext ctx(LoggerLevel::LogError);
    configureHost(ctx);
    ctx.config.sound.gs_vol = 8000;
    SoundChip_GSLightweight chip(&ctx, 512);

    for (int i = 0; i < 10; i++)
    {
        chip.handleFrameStart();
        chip.handleFrameEnd(0);
    }
    // Starts without ends: three skipped frames
    for (int i = 0; i < 3; i++)
        chip.handleFrameStart();

    const uint64_t before = chip.getActivityCounters().interruptPeriods;
    for (int i = 0; i < kFrames; i++)
    {
        chip.handleFrameStart();
        chip.handleFrameEnd(0);
    }
    const double periods = static_cast<double>(chip.getActivityCounters().interruptPeriods - before);
    EXPECT_NEAR(periods, expectedPeriods(ctx, kFrames), 2.0);
}

/// endregion </Cards against host time>
