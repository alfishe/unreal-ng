/// @file ttd_timepoint_test.cpp
/// @brief TTD_TimePoint_FrameBoundaryArithmetic — TDD §15.2 Phase 2 named test.
///
/// TDD §15 test specification:
///   TTD_TimePoint_FrameBoundaryArithmetic | Phase 2 |
///   "tInFrame wrap, multiplier scaling"
///
/// TDD §5.1 defines TTDTimePoint as the position within the recorded timeline:
///   struct TTDTimePoint { uint64_t frame; uint32_t tInFrame; }
///
/// The seek engine converts between TTDTimePoint and absolute "globalT"
/// (frame * config.frame + tInFrame) in multiple places:
///   - SeekTo binary search (comparing TTDTimePoint to checkpoint.time)
///   - FindLastAccess (beforeGlobalT ↔ TTDTimePoint)
///   - StepBackInstruction (currentGlobalT - 1)
///   - RecordMemoryWrite / RecordIoWrite (globalT computation)
///
/// This test verifies:
/// 1. operator< / operator== / operator!= on TTDTimePoint
/// 2. globalT ↔ TTDTimePoint conversion at frame boundaries
/// 3. globalT computation with different frame sizes (48K vs Pentagon)
/// 4. tInFrame wrap behavior at frame boundaries
/// 5. Edge cases: frame 0, tInFrame 0, large frame counts

#include <gtest/gtest.h>

#include <cstdint>

#include "debugger/ttd/ttd_checkpoint.h"
#include "debugger/ttd/timetravelmanager.h"

// ===========================================================================
// TTDTimePoint comparison operators
// ===========================================================================

TEST(TTD_TimePoint_Test, Equality_SameFrameSameTInFrame)
{
    ttd::TTDTimePoint a{5, 1000};
    ttd::TTDTimePoint b{5, 1000};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(TTD_TimePoint_Test, Equality_DifferentFrame)
{
    ttd::TTDTimePoint a{5, 0};
    ttd::TTDTimePoint b{6, 0};
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(TTD_TimePoint_Test, Equality_DifferentTInFrame)
{
    ttd::TTDTimePoint a{5, 100};
    ttd::TTDTimePoint b{5, 200};
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(TTD_TimePoint_Test, LessThan_FrameDominates)
{
    // Earlier frame is always less, regardless of tInFrame.
    ttd::TTDTimePoint a{4, 69999};
    ttd::TTDTimePoint b{5, 0};
    EXPECT_TRUE(a < b);
    EXPECT_FALSE(b < a);
}

TEST(TTD_TimePoint_Test, LessThan_SameFrameTInFrameBreaks)
{
    ttd::TTDTimePoint a{5, 100};
    ttd::TTDTimePoint b{5, 200};
    EXPECT_TRUE(a < b);
    EXPECT_FALSE(b < a);
}

TEST(TTD_TimePoint_Test, LessThan_EqualPoints)
{
    // A point is not less than itself.
    ttd::TTDTimePoint a{10, 35000};
    EXPECT_FALSE(a < a);
}

// ===========================================================================
// globalT ↔ TTDTimePoint conversion (the core arithmetic)
// ===========================================================================

TEST(TTD_TimePoint_Test, GlobalTConversion_OriginZeroZero)
{
    // (0, 0) ↔ globalT 0
    const uint32_t frameT = 69888;  // ZX Spectrum 48K
    const uint64_t globalT = 0;
    ttd::TTDTimePoint tp{globalT / frameT, static_cast<uint32_t>(globalT % frameT)};
    EXPECT_EQ(tp.frame, 0u);
    EXPECT_EQ(tp.tInFrame, 0u);
}

TEST(TTD_TimePoint_Test, GlobalTConversion_FrameBoundary)
{
    // Exactly one frame: (1, 0) ↔ globalT = frameT
    const uint32_t frameT = 69888;
    const uint64_t globalT = frameT;
    ttd::TTDTimePoint tp{globalT / frameT, static_cast<uint32_t>(globalT % frameT)};
    EXPECT_EQ(tp.frame, 1u);
    EXPECT_EQ(tp.tInFrame, 0u);
}

TEST(TTD_TimePoint_Test, GlobalTConversion_MidFrame)
{
    // Mid-frame: (5, 1000) ↔ globalT = 5 * frameT + 1000
    const uint32_t frameT = 69888;
    const uint64_t globalT = 5ULL * frameT + 1000;
    ttd::TTDTimePoint tp{globalT / frameT, static_cast<uint32_t>(globalT % frameT)};
    EXPECT_EQ(tp.frame, 5u);
    EXPECT_EQ(tp.tInFrame, 1000u);
}

TEST(TTD_TimePoint_Test, GlobalTConversion_LastTStateInFrame)
{
    // Last t-state before frame boundary: (3, frameT-1)
    const uint32_t frameT = 69888;
    const uint64_t globalT = 3ULL * frameT + (frameT - 1);
    ttd::TTDTimePoint tp{globalT / frameT, static_cast<uint32_t>(globalT % frameT)};
    EXPECT_EQ(tp.frame, 3u);
    EXPECT_EQ(tp.tInFrame, frameT - 1);
}

TEST(TTD_TimePoint_Test, GlobalTConversion_LargeFrameCount)
{
    // 1 hour of recording at 50 fps = 180000 frames
    const uint32_t frameT = 69888;
    const uint64_t oneHour = 180000ULL * frameT;
    ttd::TTDTimePoint tp{oneHour / frameT, static_cast<uint32_t>(oneHour % frameT)};
    EXPECT_EQ(tp.frame, 180000u);
    EXPECT_EQ(tp.tInFrame, 0u);
}

// ===========================================================================
// Frame-size independence (48K vs Pentagon vs 128K)
// ===========================================================================

TEST(TTD_TimePoint_Test, FrameSize_48K_vs_Pentagon)
{
    // ZX Spectrum 48K: frame = 69888 t-states
    // Pentagon 128: frame = 71680 t-states
    // The same globalT maps to different (frame, tInFrame) depending on frameT.
    const uint64_t globalT = 200000;

    const uint32_t frameT48K = 69888;
    ttd::TTDTimePoint tp48K{globalT / frameT48K,
                             static_cast<uint32_t>(globalT % frameT48K)};
    EXPECT_EQ(tp48K.frame, 2u);
    EXPECT_EQ(tp48K.tInFrame, static_cast<uint32_t>(200000 - 2 * frameT48K));

    const uint32_t frameTPentagon = 71680;
    ttd::TTDTimePoint tpPentagon{globalT / frameTPentagon,
                                  static_cast<uint32_t>(globalT % frameTPentagon)};
    EXPECT_EQ(tpPentagon.frame, 2u);
    EXPECT_EQ(tpPentagon.tInFrame, static_cast<uint32_t>(200000 - 2 * frameTPentagon));

    // The tInFrame values differ because frame sizes differ.
    EXPECT_NE(tp48K.tInFrame, tpPentagon.tInFrame);
}

// ===========================================================================
// tInFrame wrap behavior
// ===========================================================================

TEST(TTD_TimePoint_Test, TInFrameWrap_FrameBoundaryAdvance)
{
    // As tInFrame approaches frameT, the next t-state wraps to the next frame.
    const uint32_t frameT = 69888;
    ttd::TTDTimePoint before{5, frameT - 1};
    ttd::TTDTimePoint after{6, 0};  // next t-state after wrap

    // The wrap is a discrete jump: before < after.
    EXPECT_TRUE(before < after);

    // Converting before to globalT and adding 1 should still be in frame 5
    // (before is the last t-state, not yet wrapped).
    const uint64_t globalBefore = before.frame * frameT + before.tInFrame;
    EXPECT_EQ(globalBefore / frameT, 5u);

    // Adding 1 t-state wraps to frame 6.
    const uint64_t globalAfter = globalBefore + 1;
    EXPECT_EQ(globalAfter / frameT, 6u);
    EXPECT_EQ(globalAfter % frameT, 0u);
}

// ===========================================================================
// Monotonicity invariant
// ===========================================================================

TEST(TTD_TimePoint_Test, Monotonicity_GlobalTOrderingMatchesTimePointOrdering)
{
    // For any two TTDTimePoints a < b, their globalT values must satisfy
    // globalT(a) < globalT(b). This is the invariant the seek engine relies
    // on for binary search.
    const uint32_t frameT = 69888;

    // Generate several points and verify pairwise consistency.
    ttd::TTDTimePoint points[] = {
        {0, 0},
        {0, 1},
        {0, frameT - 1},
        {1, 0},
        {1, 1},
        {10, 35000},
        {10, 35001},
        {100, 0},
    };

    auto toGlobalT = [frameT](const ttd::TTDTimePoint& tp) {
        return tp.frame * frameT + tp.tInFrame;
    };

    for (size_t i = 0; i < std::size(points); ++i)
    {
        for (size_t j = i + 1; j < std::size(points); ++j)
        {
            // points[i] < points[j] (monotonically ordered above)
            EXPECT_TRUE(points[i] < points[j])
                << "points[" << i << "] not < points[" << j << "]";

            EXPECT_LT(toGlobalT(points[i]), toGlobalT(points[j]))
                << "globalT ordering violated for (" << i << "," << j << ")";
        }
    }
}
