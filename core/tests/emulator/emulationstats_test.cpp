// EmulationStatsMeter: the measured pace of an instance (emulator/emulationstats.h), driven with a synthetic clock -
// no sleep, no machine. A window of frames publishes fps, speed (host multiplier included), the frame work and the
// interrupts per frame; a gap of a pause starts a new window instead of counting slow frames; nothing published for
// two windows reads as not measuring

#include "pch.h"

#include "emulator/emulationstats.h"

#include <cstdint>

namespace
{
constexpr uint32_t kFrameUs = 20'000;  // a 50 Hz machine
}

TEST(EmulationStats_Test, AWindowOfFramesPublishesFpsSpeedWorkAndInterrupts)
{
    EmulationStatsMeter meter;
    EXPECT_FALSE(meter.Read(0).measuring) << "nothing measured before the first window";

    // 26 frame starts 20 ms apart: the first opens the window, 25 more close it at 0.5 s
    uint64_t now = 1'000'000;
    uint64_t interrupts = 0;
    uint64_t halted = 0;
    uint64_t frameT = 0;
    for (int frame = 0; frame <= 25; frame++)
    {
        meter.OnFrame(now, frame == 10 ? 9'000u : 5'000u, kFrameUs, 1,
                      {interrupts, halted, frameT, static_cast<uint64_t>(100 + frame), frameT});
        now += kFrameUs;
        interrupts += 1;
        halted += 17'500;  // a quarter of each 70000-T frame in HALT
        frameT += 70'000;
    }
    const EmulationStats stats = meter.Read(now);
    ASSERT_TRUE(stats.measuring);
    EXPECT_EQ(stats.windows, 1u);
    EXPECT_EQ(stats.windowFrames, 25u);
    EXPECT_NEAR(stats.windowSeconds, 0.5, 1e-9);
    EXPECT_NEAR(stats.fps, 50.0, 1e-9);
    EXPECT_NEAR(stats.nominalFps, 50.0, 1e-9);
    EXPECT_NEAR(stats.speedPercent, 100.0, 1e-9);
    EXPECT_NEAR(stats.frameWorkUsAvg, (24 * 5'000.0 + 9'000.0) / 25.0, 1e-9);
    EXPECT_EQ(stats.frameWorkUsMax, 9'000.0);
    EXPECT_NEAR(stats.frameBudgetPercent, stats.frameWorkUsAvg / kFrameUs * 100.0, 1e-9);
    EXPECT_NEAR(stats.interruptsPerFrame, 1.0, 1e-9);
    EXPECT_NEAR(stats.haltedPercent, 25.0, 1e-9);
    EXPECT_EQ(stats.frame, 125u) << "the counters as the window ended";
    EXPECT_EQ(stats.tStates, 25u * 70'000u);
}

TEST(EmulationStats_Test, SpeedCountsTheHostMultiplierAndTheRate)
{
    EmulationStatsMeter meter;
    // 2x host speed at 100 frames a second (twice the 50 Hz rate): 4x real time
    uint64_t now = 0;
    for (int frame = 0; frame <= 50; frame++)
    {
        meter.OnFrame(now, 1'000, kFrameUs, 2, {});
        now += kFrameUs / 2;
    }
    const EmulationStats stats = meter.Read(now);
    ASSERT_TRUE(stats.measuring);
    EXPECT_NEAR(stats.fps, 100.0, 1e-9);
    EXPECT_NEAR(stats.speedPercent, 400.0, 1e-9);
    EXPECT_EQ(stats.interruptsPerFrame, 0.0) << "no interrupt accepted (DI)";
}

TEST(EmulationStats_Test, APauseStartsANewWindowAndAStaleReadingIsNotMeasuring)
{
    EmulationStatsMeter meter;
    uint64_t now = 0;
    for (int frame = 0; frame <= 25; frame++, now += kFrameUs)
        meter.OnFrame(now, 1'000, kFrameUs, 1, {});
    ASSERT_EQ(meter.Read(now).windows, 1u);

    // Paused: no frames. Within two windows the last figures stand, after that not measuring and fps 0
    EXPECT_TRUE(meter.Read(now + 900'000).measuring);
    const EmulationStats stale = meter.Read(now + 1'100'000);
    EXPECT_FALSE(stale.measuring);
    EXPECT_EQ(stale.fps, 0.0);
    EXPECT_EQ(stale.speedPercent, 0.0);

    // Resumed 5 s later: the gap is not a 5-second frame; the next window is 50 fps again
    now += 5'000'000;
    for (int frame = 0; frame <= 25; frame++, now += kFrameUs)
        meter.OnFrame(now, 1'000, kFrameUs, 1, {});
    const EmulationStats resumed = meter.Read(now);
    ASSERT_TRUE(resumed.measuring);
    EXPECT_EQ(resumed.windows, 2u);
    EXPECT_NEAR(resumed.fps, 50.0, 1e-9);

    // Restart (the loop starting again) drops the open window too
    meter.Restart();
    meter.OnFrame(now, 1'000, kFrameUs, 1, {});
    EXPECT_EQ(meter.Read(now).windows, 2u);
}
