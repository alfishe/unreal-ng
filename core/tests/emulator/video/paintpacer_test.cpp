/// @file paintpacer_test.cpp
/// @brief PaintPacer: the software screen paints at each frame's time and not when a frame comes while that paint
/// waits; smooth motion paints a refresh after each paint and when a frame comes.

#include <gtest/gtest.h>

#include <cstdint>

#include "emulator/video/paintpacer.h"

namespace
{
constexpr int64_t kMs = 1'000'000;
constexpr int64_t kRefresh60 = 16'666'667;
}  // namespace

TEST(PaintPacer_Test, AFrameThatComesWhileAPaintWaitsForAFramesTimePaintsNothing)
{
    PaintPacer pacer;
    EXPECT_TRUE(pacer.PaintNow()) << "nothing waits: a frame paints";
    EXPECT_EQ(pacer.Schedule(100 * kMs, false, kRefresh60, 120 * kMs), 20);
    EXPECT_FALSE(pacer.PaintNow()) << "the paint at the frame's time shows the frame that came";
    EXPECT_EQ(pacer.Schedule(105 * kMs, false, kRefresh60, 120 * kMs), -1) << "one paint waits at a time";
    pacer.Fired();
    EXPECT_TRUE(pacer.PaintNow());

    pacer.Schedule(120 * kMs, false, kRefresh60, 140 * kMs);
    pacer.Reset();
    EXPECT_TRUE(pacer.PaintNow()) << "a detached source leaves nothing waiting";
}

TEST(PaintPacer_Test, ThePaintWaitsForTheNextFramesTimeNeverBeforeIt)
{
    PaintPacer pacer;
    EXPECT_EQ(pacer.Schedule(100 * kMs, false, kRefresh60, 120 * kMs + 300'000), 21) << "rounded up";
    pacer.Fired();
    EXPECT_EQ(pacer.Schedule(100 * kMs, false, kRefresh60, 90 * kMs), 1) << "already due: at once";
    pacer.Fired();
    EXPECT_EQ(pacer.Schedule(100 * kMs, false, kRefresh60, 900 * kMs), PaintPacer::kMaxWaitMs);
}

TEST(PaintPacer_Test, SmoothMotionPaintsARefreshOnAndWhenAFrameComes)
{
    PaintPacer pacer;
    EXPECT_EQ(pacer.Schedule(100 * kMs, true, kRefresh60, 175 * kMs), 16) << "a refresh, whole ms";
    EXPECT_TRUE(pacer.PaintNow()) << "smooth motion: a frame that comes paints too";
    EXPECT_EQ(pacer.Schedule(105 * kMs, true, kRefresh60, 175 * kMs), -1) << "still one timer";
    pacer.Fired();

    // Smooth motion off again: the next wait is a frame's, and a frame that comes waits for it
    pacer.Schedule(120 * kMs, false, kRefresh60, 140 * kMs);
    EXPECT_FALSE(pacer.PaintNow());
}
