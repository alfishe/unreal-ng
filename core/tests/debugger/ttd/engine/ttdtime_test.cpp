/// @file ttdtime_test.cpp
/// @brief Frame table and positions of the time-travel engine: frames of any
/// length (engine decision D21), lookups by frame and by machine time.

#include <gtest/gtest.h>

#include "debugger/ttd/engine/ttdtime.h"

using namespace ttd;

TEST(TTDFrameTable_Test, VariableFrameLengths_LookupsByFrameAndTime)
{
    // A Sprinter-like history: 320-line frames, then 312-line ones, then a x6 turbo frame
    TTDFrameTable t;
    ASSERT_TRUE(t.Append(10, 0));
    ASSERT_TRUE(t.Append(11, 71680));
    ASSERT_TRUE(t.Append(12, 71680 + 69888));
    ASSERT_TRUE(t.Append(13, 71680 + 69888 + 430080));

    TTDMachineTime start = 0;
    ASSERT_TRUE(t.Start(12, start));
    EXPECT_EQ(start, 71680u + 69888u);
    EXPECT_FALSE(t.Start(14, start));
    EXPECT_EQ(t.IndexOf(13), 3);
    EXPECT_EQ(t.IndexOf(9), -1);

    uint64_t frame = 0;
    ASSERT_TRUE(t.FrameAt(0, frame));
    EXPECT_EQ(frame, 10u);
    ASSERT_TRUE(t.FrameAt(71679, frame));
    EXPECT_EQ(frame, 10u);
    ASSERT_TRUE(t.FrameAt(71680, frame));
    EXPECT_EQ(frame, 11u);
    ASSERT_TRUE(t.FrameAt(71680 + 69888 + 430080 + 5, frame));
    EXPECT_EQ(frame, 13u);
}

TEST(TTDFrameTable_Test, RefusesFramesOutOfOrder)
{
    TTDFrameTable t;
    ASSERT_TRUE(t.Append(5, 1000));
    EXPECT_FALSE(t.Append(5, 2000)) << "a frame number must grow";
    EXPECT_FALSE(t.Append(4, 2000));
    EXPECT_FALSE(t.Append(6, 999)) << "a frame cannot start before the previous one";
    EXPECT_EQ(t.Count(), 1u);
}

TEST(TTDFrameTable_Test, StartMovesForward_FirstFrameIsTheEarliestKept)
{
    // A session whose start moved (history released): "start" is the first kept frame (D12)
    TTDFrameTable t;
    ASSERT_TRUE(t.Append(500, 500 * 71680ull));
    ASSERT_TRUE(t.Append(501, 501 * 71680ull));
    EXPECT_EQ(t.FirstFrame(), 500u);
    uint64_t frame = 0;
    EXPECT_FALSE(t.FrameAt(10, frame)) << "nothing is recorded before the first kept frame";
}
