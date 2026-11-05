/// @file screenbeat_test.cpp
/// @brief ScreenBeat: the frames shown outside the beat pattern and the screen-to-sound offset, from screen times
/// (frame presenter design, sections 5 and 6.4).

#include <gtest/gtest.h>

#include <cstdint>

#include "emulator/video/screenbeat.h"

namespace
{
constexpr int64_t kMs = 1'000'000;

/// The first refresh at or after `t` on a grid of `period` from 0, plus `latency`
int64_t NextRefresh(int64_t t, int64_t period, int64_t latency)
{
    return (t + period - 1) / period * period + latency;
}
}  // namespace

TEST(ScreenBeat_Test, AFixedDisplaysOwnBeatIsNotCounted)
{
    // 50 Hz frames on 60 Hz: each frame at the first refresh after its time, one in five for two refreshes
    ScreenBeat beat;
    const int64_t period = 20 * kMs;
    const int64_t refresh = 16'666'667;
    for (uint64_t n = 1; n <= 300; n++)
    {
        const int64_t target = static_cast<int64_t>(n) * period + 3 * kMs;
        beat.Shown(n, target, NextRefresh(target, refresh, 0), refresh);
    }
    const ScreenBeat::Stats s = beat.GetStats();
    EXPECT_EQ(s.frames, 299u);
    EXPECT_EQ(s.offBeat, 0u);
    EXPECT_EQ(s.window, 300u);
    EXPECT_GE(s.offsetP50Ms, 0.0);
    EXPECT_LE(s.offsetP99Ms, 16.7);
}

TEST(ScreenBeat_Test, AMissedRefreshAndANeverShownFrameAreCounted)
{
    // 50 Hz on 100 Hz: every frame for two refreshes; frame 10 a refresh late, frame 20 never on screen
    ScreenBeat beat;
    const int64_t period = 20 * kMs;
    const int64_t refresh = 10 * kMs;
    for (uint64_t n = 1; n <= 30; n++)
    {
        if (n == 20)
            continue;
        const int64_t target = static_cast<int64_t>(n) * period;
        beat.Shown(n, target, target + (n == 10 ? refresh : 0), refresh);
    }
    const ScreenBeat::Stats s = beat.GetStats();
    EXPECT_EQ(s.frames, 29u);
    // Frame 9 held three refreshes, frame 10 one, frame 20 none
    EXPECT_EQ(s.offBeat, 3u);
}

TEST(ScreenBeat_Test, AFasterMachineSkipsFramesByItsBeat)
{
    // 50 Hz on a 32 Hz remote desktop display: some frames never show, by the beat
    ScreenBeat beat;
    const int64_t period = 20 * kMs;
    const int64_t refresh = 31'250'000;
    uint64_t lastShown = 0;
    for (uint64_t n = 1; n <= 200; n++)
    {
        const int64_t target = static_cast<int64_t>(n) * period;
        const int64_t screen = NextRefresh(target, refresh, 0);
        // The newest frame due at each refresh is the one shown
        const int64_t nextTarget = target + period;
        if (NextRefresh(nextTarget, refresh, 0) == screen)
            continue;
        beat.Shown(n, target, screen, refresh);
        lastShown = n;
    }
    ASSERT_GT(lastShown, 150u);
    const ScreenBeat::Stats s = beat.GetStats();
    EXPECT_GT(s.frames, 100u);
    EXPECT_EQ(s.offBeat, 0u);
}

TEST(ScreenBeat_Test, VrrFramesAtTheirOwnTimesAreOnTheBeat)
{
    // A 48.83 Hz machine on a 144 Hz VRR panel, each frame shown at its time with a little jitter
    ScreenBeat beat;
    const int64_t period = 20'480'000;
    const int64_t shortest = 6'944'444;
    for (uint64_t n = 1; n <= 200; n++)
    {
        const int64_t target = static_cast<int64_t>(n) * period;
        beat.Shown(n, target, target + 2 * kMs + (n % 3 == 0 ? 400'000 : 0), shortest);
    }
    const ScreenBeat::Stats s = beat.GetStats();
    EXPECT_EQ(s.offBeat, 0u);
    EXPECT_NEAR(s.offsetP50Ms, 2.0, 0.01);
    EXPECT_NEAR(s.offsetP99Ms, 2.4, 0.01);
}

TEST(ScreenBeat_Test, RepeatsRepaintsAndBreaksAreNotJudged)
{
    ScreenBeat beat;
    const int64_t refresh = 10 * kMs;
    beat.Shown(1, 20 * kMs, 20 * kMs, refresh);
    beat.Shown(1, 20 * kMs, 30 * kMs, refresh);  // the same picture again
    beat.Shown(2, 0, 35 * kMs, refresh);         // untimed: a repaint
    beat.Shown(2, 40 * kMs, 40 * kMs, refresh);
    beat.Shown(3, 2'000 * kMs, 2'000 * kMs, refresh);  // after a pause
    beat.Shown(1, 3'000 * kMs, 3'000 * kMs, refresh);  // a seek back
    beat.Shown(2, 3'020 * kMs, 3'020 * kMs, refresh);
    const ScreenBeat::Stats s = beat.GetStats();
    EXPECT_EQ(s.frames, 2u);
    EXPECT_EQ(s.offBeat, 0u);
    EXPECT_EQ(s.window, 5u);
}

TEST(ScreenBeat_Test, OffsetsKeepTheLastWindow)
{
    ScreenBeat beat;
    for (uint64_t n = 1; n <= ScreenBeat::kWindow + 100; n++)
    {
        const int64_t target = static_cast<int64_t>(n) * 20 * kMs;
        beat.Shown(n, target, target + (n > 100 ? 5 * kMs : 50 * kMs), 20 * kMs);
    }
    const ScreenBeat::Stats s = beat.GetStats();
    EXPECT_EQ(s.window, ScreenBeat::kWindow);
    EXPECT_NEAR(s.offsetP99Ms, 5.0, 1e-9);
    beat.Reset();
    EXPECT_EQ(beat.GetStats().frames, 0u);
}
