// RzxKeyframeStore: keyframes at the interval, the budget kept by thinning
// (frame 0 stays, the interval doubles), lookup of the latest one at or
// before a frame, and Clear() freeing everything.

#include <gtest/gtest.h>

#include "emulator/rzx/rzxkeyframes.h"

using namespace rzx;

namespace
{
    Keyframe Make(uint64_t frame, size_t bytes)
    {
        Keyframe keyframe;
        keyframe.cursor.frame = frame;
        keyframe.state.assign(bytes, 0xAA);
        return keyframe;
    }
}  // namespace

TEST(RzxKeyframeStore_Test, TakesOneEveryIntervalAndFindsTheLatestBefore)
{
    RzxKeyframeStore store;
    store.Configure(100, 1u << 20);
    EXPECT_TRUE(store.Due(0));
    store.Add(Make(0, 10));
    EXPECT_FALSE(store.Due(99));
    EXPECT_TRUE(store.Due(100));
    store.Add(Make(100, 10));
    store.Add(Make(200, 10));
    store.Add(Make(150, 10));  // behind the last one: ignored
    EXPECT_EQ(store.Count(), 3u);
    EXPECT_EQ(store.Bytes(), 30u);
    EXPECT_EQ(store.AtOrBefore(199)->cursor.frame, 100u);
    EXPECT_EQ(store.AtOrBefore(200)->cursor.frame, 200u);
    EXPECT_EQ(store.AtOrBefore(5000)->cursor.frame, 200u);
}

/// The worked example of rzxkeyframes.h: over budget, every second keyframe
/// goes, frame 0 stays, the interval doubles
TEST(RzxKeyframeStore_Test, ThinsToStayWithinTheBudget)
{
    RzxKeyframeStore store;
    store.Configure(250, 17 * 60);
    for (uint64_t frame = 0; frame <= 4250; frame += 250)
        store.Add(Make(frame, 60));
    EXPECT_LE(store.Bytes(), store.Budget());
    EXPECT_EQ(store.Count(), 9u);
    EXPECT_EQ(store.Interval(), 500u);
    EXPECT_EQ(store.AtOrBefore(0)->cursor.frame, 0u) << "the start stays";
    EXPECT_EQ(store.AtOrBefore(749)->cursor.frame, 500u);
    EXPECT_FALSE(store.Due(4499));
    EXPECT_TRUE(store.Due(4500));
}

TEST(RzxKeyframeStore_Test, ClearFreesEverythingAndRestartsTheSpacing)
{
    RzxKeyframeStore store;
    store.Configure(100, 250);
    for (uint64_t frame = 0; frame <= 1000; frame += 100)
        store.Add(Make(frame, 100));
    ASSERT_GT(store.Interval(), 100u);
    store.Clear();
    EXPECT_EQ(store.Count(), 0u);
    EXPECT_EQ(store.Bytes(), 0u);
    EXPECT_EQ(store.Interval(), 100u);
    EXPECT_TRUE(store.Due(0));
    EXPECT_EQ(store.AtOrBefore(500), nullptr);
}

TEST(RzxKeyframeStore_Test, IntervalZeroTakesNone)
{
    RzxKeyframeStore store;
    store.Configure(0, 1000);
    EXPECT_FALSE(store.Due(0));
}
