/// @file painttiming_test.cpp
/// @brief PaintTiming: the software screen's numbers in the present report, from the paints of new frames (it has no
/// screen times).

#include <gtest/gtest.h>

#include <cstdint>

#include "emulator/video/painttiming.h"

namespace
{
constexpr int64_t kMs = 1'000'000;
}  // namespace

TEST(PaintTiming_Test, NothingPaintedFillsNothing)
{
    PaintTiming timing;
    WindowPresentStats out;
    timing.Fill(out);
    EXPECT_EQ(out.timedAt, "") << "no paint yet: the report says there are no times";
    EXPECT_EQ(out.presented, 0u);

    // Frames without serials (a legacy buffer) or targets are not timed
    timing.Painted(0, 10 * kMs, 12 * kMs);
    timing.Painted(5, 0, 12 * kMs);
    timing.Fill(out);
    EXPECT_EQ(out.timedAt, "");
}

TEST(PaintTiming_Test, EachNewFrameCountsOnceAndItsPaintTimeIsMeasuredFromItsTarget)
{
    PaintTiming timing;
    // Frames 1..4 due every 20 ms, painted 3, 5, 7, 9 ms after their time; frame 2 repainted (an expose)
    for (uint64_t serial = 1; serial <= 4; serial++)
    {
        const int64_t target = static_cast<int64_t>(serial) * 20 * kMs;
        timing.Painted(serial, target, target + (1 + 2 * static_cast<int64_t>(serial)) * kMs);
        if (serial == 2)
            timing.Painted(serial, target, target + 15 * kMs);
    }
    WindowPresentStats out;
    timing.Fill(out);
    EXPECT_EQ(out.timedAt, "paint");
    EXPECT_EQ(out.presented, 4u) << "a repaint of the same frame is not another one";
    EXPECT_EQ(out.shown, 4u);
    EXPECT_EQ(out.dropped, 0u);
    EXPECT_DOUBLE_EQ(out.errorP50Ms, 7.0);
    EXPECT_DOUBLE_EQ(out.errorP99Ms, 9.0);
    EXPECT_DOUBLE_EQ(out.errorMaxMs, 9.0);
    EXPECT_EQ(out.late, 0u) << "no refresh grid to be late on";
}

TEST(PaintTiming_Test, FramesSkippedBetweenPaintsAreDroppedAndASerialGoingBackStartsAgain)
{
    PaintTiming timing;
    timing.Painted(10, 100 * kMs, 101 * kMs);
    timing.Painted(13, 160 * kMs, 161 * kMs);  // 11 and 12 never painted
    timing.Painted(14, 180 * kMs, 181 * kMs);
    WindowPresentStats out;
    timing.Fill(out);
    EXPECT_EQ(out.presented, 3u);
    EXPECT_EQ(out.dropped, 2u);

    // A new machine numbers its frames from 1 again: no drop counted for the jump back
    timing.Painted(1, 200 * kMs, 202 * kMs);
    timing.Painted(2, 220 * kMs, 222 * kMs);
    timing.Fill(out);
    EXPECT_EQ(out.presented, 5u);
    EXPECT_EQ(out.dropped, 2u);

    timing.Reset();
    WindowPresentStats cleared;
    timing.Fill(cleared);
    EXPECT_EQ(cleared.timedAt, "");
    EXPECT_EQ(cleared.presented, 0u);
}

TEST(PaintTiming_Test, ThePercentilesCoverTheLastWindowOfPaints)
{
    PaintTiming timing;
    // An early slow stretch, then a whole window of fast paints: the slow ones fall out
    for (uint64_t serial = 1; serial <= 100; serial++)
        timing.Painted(serial, static_cast<int64_t>(serial) * 20 * kMs, static_cast<int64_t>(serial) * 20 * kMs + 50 * kMs);
    for (uint64_t serial = 101; serial <= 100 + PaintTiming::kWindow; serial++)
        timing.Painted(serial, static_cast<int64_t>(serial) * 20 * kMs, static_cast<int64_t>(serial) * 20 * kMs + 2 * kMs);
    WindowPresentStats out;
    timing.Fill(out);
    EXPECT_EQ(out.presented, 100u + PaintTiming::kWindow);
    EXPECT_DOUBLE_EQ(out.errorMaxMs, 2.0);
    EXPECT_DOUBLE_EQ(out.errorP99Ms, 2.0);
}
