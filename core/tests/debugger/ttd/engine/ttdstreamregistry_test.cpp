/// @file ttdstreamregistry_test.cpp
/// @brief Optional frame-boundary streams (engine decision D19): switched on and
/// off at run time; a stream that is off is never called.

#include <gtest/gtest.h>

#include "debugger/ttd/engine/ttdstreamregistry.h"

using namespace ttd;

TEST(TTDStreamRegistry_Test, StreamsStartOff_AndOffMeansNoCall)
{
    TTDStreamRegistry streams;
    int calls = 0;
    ASSERT_TRUE(streams.Register(3, "screenshot", [&](const TTDPosition&) { ++calls; }));
    EXPECT_FALSE(streams.IsEnabled(3));
    EXPECT_EQ(streams.EnabledMask(), 0u);

    for (uint64_t f = 0; f < 100; ++f)
        streams.CaptureEnabled({0, f, 0});
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(streams.CaptureCalls(), 0u) << "with every stream off the frame-boundary path calls nothing";
}

TEST(TTDStreamRegistry_Test, SwitchOnAndOffAtRunTime)
{
    TTDStreamRegistry streams;
    std::vector<uint64_t> frames;
    ASSERT_TRUE(streams.Register(0, "screenshot", [&](const TTDPosition& at) { frames.push_back(at.frame); }));
    int other = 0;
    ASSERT_TRUE(streams.Register(63, "trace", [&](const TTDPosition&) { ++other; }));

    streams.CaptureEnabled({0, 1, 0});
    ASSERT_TRUE(streams.SetEnabled(0, true));
    streams.CaptureEnabled({0, 2, 0});
    streams.CaptureEnabled({0, 3, 0});
    ASSERT_TRUE(streams.SetEnabled(0, false));
    streams.CaptureEnabled({0, 4, 0});

    EXPECT_EQ(frames, (std::vector<uint64_t>{2, 3}));
    EXPECT_EQ(other, 0);
}

TEST(TTDStreamRegistry_Test, IdsAreStableAndUnique)
{
    TTDStreamRegistry streams;
    ASSERT_TRUE(streams.Register(5, "a", [](const TTDPosition&) {}));
    EXPECT_FALSE(streams.Register(5, "b", [](const TTDPosition&) {})) << "an id is taken once";
    EXPECT_FALSE(streams.Register(TTDStreamRegistry::kMaxStreams, "c", [](const TTDPosition&) {}));
    EXPECT_FALSE(streams.SetEnabled(6, true)) << "an unregistered stream cannot be switched on";
    EXPECT_EQ(streams.Name(5), "a");
}
