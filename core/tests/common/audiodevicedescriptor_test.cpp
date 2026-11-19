#include <gtest/gtest.h>

#include "common/sound/audiodevicedescriptor.h"

namespace
{
constexpr int64_t kMs = 1'000'000;
}

/// The trough of a window is the least occupancy a callback left; it is published when the window closes
TEST(AudioDeviceDescriptor_Test, TroughIsTheWindowsLeastOccupancy)
{
    AudioDeviceDescriptor d;
    EXPECT_EQ(d.troughFrames.load(), AudioDeviceDescriptor::kNoTrough);
    int64_t t = 1000 * kMs;
    // First window: occupancies 900, 300, 700 every 6 ms; nothing published until it closes
    const uint32_t first[] = {900, 300, 700};
    for (int i = 0; i < 168; i++, t += 6 * kMs)  // the 168th (1002 ms on) closes it
        d.OnCallbackConsumed(first[i % 3], t, false);
    EXPECT_EQ(d.troughFrames.load(), 300u);
    EXPECT_EQ(d.troughWindows.load(), 1u);
    // Second window: never below 500 - the trough rises with it (a window does not keep the old minimum)
    for (int i = 0; i < 168; i++, t += 6 * kMs)
        d.OnCallbackConsumed(500 + static_cast<uint32_t>(i % 2) * 100, t, false);
    EXPECT_EQ(d.troughFrames.load(), 500u);
    EXPECT_EQ(d.troughWindows.load(), 2u);
    EXPECT_EQ(d.underruns.load(), 0u);
}

/// A run of short callbacks is one gap; the empty ring a fresh device starts on is none
TEST(AudioDeviceDescriptor_Test, UnderrunCountsGapsNotCallbacks)
{
    AudioDeviceDescriptor d;
    int64_t t = 1 * kMs;
    d.OnCallbackConsumed(0, t += 6 * kMs, true);  // start-up: the ring not filled yet
    d.OnCallbackConsumed(0, t += 6 * kMs, true);
    EXPECT_EQ(d.underruns.load(), 0u);
    d.OnCallbackConsumed(400, t += 6 * kMs, false);
    d.OnCallbackConsumed(0, t += 6 * kMs, true);  // a gap of three callbacks
    d.OnCallbackConsumed(0, t += 6 * kMs, true);
    d.OnCallbackConsumed(0, t += 6 * kMs, true);
    EXPECT_EQ(d.underruns.load(), 1u);
    d.OnCallbackConsumed(400, t += 6 * kMs, false);
    d.OnCallbackConsumed(0, t += 6 * kMs, true);  // another gap
    EXPECT_EQ(d.underruns.load(), 2u);

    // A fresh device: the window and the gap state start over, the counter keeps its total
    d.ResetTrough();
    EXPECT_EQ(d.troughFrames.load(), AudioDeviceDescriptor::kNoTrough);
    d.OnCallbackConsumed(0, t += 6 * kMs, true);
    EXPECT_EQ(d.underruns.load(), 2u);
}
