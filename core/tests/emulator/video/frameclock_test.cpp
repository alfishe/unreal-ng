#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "emulator/video/frameclock.h"

/// FrameClock: target times of video frames, audio master with the emulated clock as the stand-in
/// (docs/inprogress/2026-10-26-frame-presenter, section 3). Synthetic frame sequences, no emulator

namespace
{
constexpr int64_t kMs = 1'000'000;
constexpr int64_t kPeriod = 19'968'000;  // ZX 48K: 69888 T-states at 3.5 MHz

struct Sequence
{
    FrameClock clock;
    int64_t latch = 1'000'000 * kMs;
    int64_t audio = 0;  // when the frame's sound is heard: latch + audioDelay
    int64_t audioDelay = 52 * kMs;
    int64_t emulatedDelay = kPeriod;
    int64_t latchDelay = 0;
    int64_t maxDelay = 0;
    std::vector<int64_t> targets;

    FrameTimingResult Frame(bool haveAudio, bool haveEmulated = true, int64_t period = kPeriod)
    {
        latch += period;
        FrameTimingInput in;
        in.latchNs = latch;
        in.periodNs = period;
        in.haveAudio = haveAudio;
        in.audioNs = latch + audioDelay;
        in.haveEmulated = haveEmulated;
        in.emulatedNs = latch;
        in.emulatedDelayNs = emulatedDelay;
        in.latchDelayNs = latchDelay;
        in.maxDelayNs = maxDelay;
        const FrameTimingResult r = clock.Next(in);
        targets.push_back(r.targetNs);
        return r;
    }
    int64_t Interval(size_t i) const { return targets[i] - targets[i - 1]; }
};
}  // namespace

TEST(FrameClock_Test, AudioTargetsPassThrough)
{
    Sequence s;
    for (int i = 0; i < 10; i++)
    {
        const FrameTimingResult r = s.Frame(true);
        EXPECT_EQ(r.source, FrameClockSource::Audio);
        EXPECT_EQ(r.targetNs, s.latch + s.audioDelay);
    }
    EXPECT_EQ(s.clock.GetStats().carried, 0u);
    EXPECT_EQ(s.clock.GetStats().audioFrames, 10u);
}

TEST(FrameClock_Test, NoAudioUsesTheEmulatedClock)
{
    Sequence s;
    const FrameTimingResult r = s.Frame(false);
    EXPECT_EQ(r.source, FrameClockSource::Emulated);
    EXPECT_EQ(r.targetNs, s.latch + s.emulatedDelay);
}

/// The device appears (or this instance gets the sound): the targets move 32 ms later, by at most kSlewNs per
/// frame, never in one step
TEST(FrameClock_Test, SwitchToAudioIsSlewed)
{
    Sequence s;
    for (int i = 0; i < 5; i++)
        s.Frame(false);
    const size_t first = s.targets.size();
    int frames = 0;
    for (; frames < 1000; frames++)
    {
        s.Frame(true);
        if (s.clock.GetStats().carryNs == 0)
            break;
    }
    ASSERT_LT(frames, 1000) << "the carry is paid back";
    for (size_t i = first; i < s.targets.size(); i++)
    {
        EXPECT_LE(std::llabs(s.Interval(i) - kPeriod), FrameClock::kSlewNs) << "frame " << i;
    }
    EXPECT_EQ(s.targets.back(), s.latch + s.audioDelay) << "on the audio clock in the end";
    EXPECT_EQ(s.clock.GetStats().carried, 1u);
    EXPECT_EQ(s.clock.GetStats().cuts, 0u);
}

/// An underrun moves the stream by a callback (5.8 ms): carried, not shown as a 25.8 ms frame
TEST(FrameClock_Test, AudioStepIsCarried)
{
    Sequence s;
    for (int i = 0; i < 5; i++)
        s.Frame(true);
    s.audioDelay += 5'800'000;
    s.Frame(true);
    EXPECT_LE(std::llabs(s.Interval(s.targets.size() - 1) - kPeriod), FrameClock::kSlewNs);
    EXPECT_EQ(s.clock.GetStats().carried, 1u);
}

/// Small steps (the fit's noise, the DRC trim) pass through: the targets follow the sound exactly
TEST(FrameClock_Test, SmallStepsPassThrough)
{
    Sequence s;
    for (int i = 0; i < 5; i++)
        s.Frame(true);
    s.audioDelay += 100'000;
    const FrameTimingResult r = s.Frame(true);
    EXPECT_EQ(r.targetNs, s.latch + s.audioDelay);
    EXPECT_EQ(s.clock.GetStats().carried, 0u);
}

/// A pause or a stall breaks the sequence: the next frame is at its own time, nothing to keep smooth across
TEST(FrameClock_Test, GapCuts)
{
    Sequence s;
    for (int i = 0; i < 5; i++)
        s.Frame(true);
    s.latch += 10 * kPeriod;
    s.audioDelay += 30 * kMs;
    const FrameTimingResult r = s.Frame(true);
    EXPECT_EQ(r.targetNs, s.latch + s.audioDelay);
    EXPECT_EQ(s.clock.GetStats().cuts, 1u);
    EXPECT_EQ(s.clock.GetStats().carryNs, 0);
}

/// Turbo: latch times (no delay but a temporal effect's), no cadence, no carry; leaving turbo starts over on the
/// audio clock
TEST(FrameClock_Test, TurboUsesLatchTimes)
{
    Sequence s;
    for (int i = 0; i < 5; i++)
    {
        const FrameTimingResult r = s.Frame(false, false, kPeriod / 10);
        EXPECT_EQ(r.source, FrameClockSource::Latch);
        EXPECT_EQ(r.targetNs, s.latch) << "nothing to wait for";
    }
    s.latchDelay = 7 * kPeriod;  // ZX DLSS looks ahead
    const int64_t delayed = s.Frame(false, false, kPeriod / 10).targetNs;  // Frame moves s.latch: read it after
    EXPECT_EQ(delayed, s.latch + s.latchDelay);
    const FrameTimingResult r = s.Frame(true);
    EXPECT_EQ(r.targetNs, s.latch + s.audioDelay);
    EXPECT_EQ(s.clock.GetStats().carried, 0u);
}

TEST(FrameClock_Test, NeverOutOfOrder)
{
    Sequence s;
    for (int i = 0; i < 5; i++)
        s.Frame(true);
    // A carried step back by most of a period, then the carry pays back: every frame still after the last one
    s.audioDelay -= kPeriod - 1 * kMs;
    for (int i = 0; i < 200; i++)
        s.Frame(true);
    for (size_t i = 1; i < s.targets.size(); i++)
        EXPECT_GE(s.Interval(i), kPeriod / 2) << "frame " << i;
}

TEST(FrameClock_Test, LipSyncOffset)
{
    FrameClock clock;
    FrameTimingInput in;
    in.latchNs = 100 * kMs;
    in.periodNs = kPeriod;
    in.haveAudio = true;
    in.audioNs = 150 * kMs;
    in.offsetNs = -10 * kMs;
    EXPECT_EQ(clock.Next(in).targetNs, 140 * kMs);
}

/// A long audio path (a Bluetooth headset: 250 ms) the screen cannot hold its frames for: the targets stay at the
/// longest delay it can, in cadence, and say so
TEST(FrameClock_Test, DelayBeyondTheHeldFramesIsClamped)
{
    Sequence s;
    s.maxDelay = 9 * kPeriod;
    for (int i = 0; i < 5; i++)
        EXPECT_FALSE(s.Frame(true).clamped) << "52 ms fits";
    s.audioDelay = 250 * kMs;
    for (int i = 0; i < 700; i++)  // the step is carried, paid back by kSlewNs a frame
    {
        const FrameTimingResult r = s.Frame(true);
        EXPECT_LE(r.targetNs, s.latch + s.maxDelay) << "frame " << i;
        EXPECT_LE(std::llabs(s.Interval(s.targets.size() - 1) - kPeriod), FrameClock::kSlewNs) << "frame " << i;
    }
    EXPECT_EQ(s.targets.back(), s.latch + s.maxDelay) << "at the limit in the end";
    EXPECT_TRUE(s.clock.GetStats().lastClamped);
    EXPECT_EQ(s.clock.GetStats().clamped, 700u);

    // The sound comes back within reach: the clock's own time again
    s.audioDelay = 52 * kMs;
    EXPECT_FALSE(s.Frame(true).clamped);  // the step back is carried as well
    EXPECT_NE(s.clock.GetStats().carryNs, 0);
    for (int i = 0; i < 1000 && s.clock.GetStats().carryNs != 0; i++)
        s.Frame(true);
    EXPECT_FALSE(s.Frame(true).clamped);
    EXPECT_EQ(s.targets.back(), s.latch + s.audioDelay);
    EXPECT_FALSE(s.clock.GetStats().lastClamped);
}

/// An external picture (VDAC2's FT812) at its own rate: its frames are timed with its period, every interval one of
/// its periods, nothing carried
TEST(FrameClock_Test, ExternalPicturePeriod)
{
    constexpr int64_t external = 16'683'350;  // 59.94 Hz
    Sequence s;
    for (int i = 0; i < 50; i++)
    {
        const FrameTimingResult r = s.Frame(true, true, external);
        EXPECT_EQ(r.targetNs, s.latch + s.audioDelay);
    }
    for (size_t i = 1; i < s.targets.size(); i++)
        EXPECT_EQ(s.Interval(i), external) << "frame " << i;
    EXPECT_EQ(s.clock.GetStats().carried, 0u);
    EXPECT_EQ(s.clock.GetStats().cuts, 0u);
    // A latch gap of more than 4 of its periods breaks its sequence
    s.latch += 4 * external;
    s.Frame(true, true, external);
    EXPECT_EQ(s.clock.GetStats().cuts, 1u);
}
