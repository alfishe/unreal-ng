#include <gtest/gtest.h>

#include "emulator/sound/lowlatencytarget.h"

namespace
{
constexpr uint32_t kRate = 48000;
constexpr uint32_t Frames(double ms)
{
    return static_cast<uint32_t>(ms * kRate / 1000.0);
}

/// The device's counters as the frames see them; Window() closes a one-second window with the given least occupancy
struct Device
{
    uint64_t windows = 0;
    uint64_t underruns = 0;
    uint32_t trough = LowLatencyTarget::kNoTrough;
};

double Window(LowLatencyTarget& t, Device& d, double troughMs)
{
    d.windows++;
    d.trough = Frames(troughMs);
    return t.Update(true, d.windows, d.trough, d.underruns, kRate);
}
}  // namespace

/// It starts at the floor and a host that keeps a margin leaves it there
TEST(LowLatencyTarget_Test, HoldsTheFloorWhileTheMarginIsKept)
{
    LowLatencyTarget t(10.0, 10.0, 40.0);
    Device d;
    EXPECT_DOUBLE_EQ(t.Update(true, d.windows, d.trough, d.underruns, kRate), 10.0);
    for (int i = 0; i < 200; i++)
        Window(t, d, 7.0);
    EXPECT_DOUBLE_EQ(t.Target(), 10.0);
    EXPECT_EQ(t.Raises(), 0u);
}

/// A gap raises it at once; a near-gap raises it less; during the holdoff the ring is filling and nothing more is raised
TEST(LowLatencyTarget_Test, GapsAndNearGapsRaiseIt)
{
    LowLatencyTarget t(10.0, 10.0, 40.0);
    Device d;
    t.Update(true, d.windows, d.trough, d.underruns, kRate);
    d.underruns++;
    EXPECT_DOUBLE_EQ(t.Update(true, d.windows, d.trough, d.underruns, kRate), 10.0 + LowLatencyTarget::kRaiseOnGapMs);
    // The ring is still low while it fills: the holdoff windows raise nothing
    for (uint32_t i = 0; i < LowLatencyTarget::kHoldoffWindows; i++)
        Window(t, d, 0.5);
    EXPECT_DOUBLE_EQ(t.Target(), 15.0);
    // Past the holdoff a near-gap raises it again, by the smaller step
    EXPECT_DOUBLE_EQ(Window(t, d, 1.0), 15.0 + LowLatencyTarget::kRaiseOnLowMs);
    EXPECT_EQ(t.Raises(), 2u);
}

/// Quiet spells bring it back down a step at a time, never below the floor; it never goes above the ceiling
TEST(LowLatencyTarget_Test, LowersAfterQuietSpellsWithinFloorAndCeiling)
{
    LowLatencyTarget t(10.0, 10.0, 40.0);
    Device d;
    t.Update(true, d.windows, d.trough, d.underruns, kRate);
    for (int i = 0; i < 20; i++)  // a host that cannot hold anything: gap after gap
    {
        d.underruns++;
        t.Update(true, d.windows, d.trough, d.underruns, kRate);
        for (uint32_t w = 0; w <= LowLatencyTarget::kHoldoffWindows; w++)
            Window(t, d, 0.0);
    }
    EXPECT_DOUBLE_EQ(t.Target(), 40.0);

    for (uint32_t i = 0; i < LowLatencyTarget::kLowerAfterWindows; i++)
        Window(t, d, 20.0);
    EXPECT_DOUBLE_EQ(t.Target(), 40.0 - LowLatencyTarget::kLowerMs);
    // A window close to the guard (between it and the margin) holds: the quiet count starts over
    for (uint32_t i = 0; i + 1 < LowLatencyTarget::kLowerAfterWindows; i++)
        Window(t, d, 20.0);
    Window(t, d, LowLatencyTarget::kGuardMs + 1.0);
    for (uint32_t i = 0; i + 1 < LowLatencyTarget::kLowerAfterWindows; i++)
        Window(t, d, 20.0);
    EXPECT_DOUBLE_EQ(t.Target(), 39.0);

    for (int i = 0; i < 100 * static_cast<int>(LowLatencyTarget::kLowerAfterWindows); i++)
        Window(t, d, 20.0);
    EXPECT_DOUBLE_EQ(t.Target(), 10.0);
}

/// Off: it rests at the start and follows the counters, so turning on again starts afresh (old gaps are not news)
TEST(LowLatencyTarget_Test, OffRestsAtTheFloorAndStartsAfresh)
{
    LowLatencyTarget t(10.0, 10.0, 40.0);
    Device d;
    t.Update(true, d.windows, d.trough, d.underruns, kRate);
    d.underruns++;
    t.Update(true, d.windows, d.trough, d.underruns, kRate);
    EXPECT_GT(t.Target(), 10.0);

    d.underruns += 5;  // gaps while off
    d.windows += 3;
    EXPECT_DOUBLE_EQ(t.Update(false, d.windows, d.trough, d.underruns, kRate), 10.0);
    EXPECT_DOUBLE_EQ(t.Update(true, d.windows, d.trough, d.underruns, kRate), 10.0);
    EXPECT_DOUBLE_EQ(Window(t, d, 7.0), 10.0);
}

/// Started above the floor, a quiet host brings it down to the floor a step per quiet spell; a gap on the way raises it
/// from where it is; off, it rests at the start again
TEST(LowLatencyTarget_Test, StartsAboveTheFloorAndComesDown)
{
    LowLatencyTarget t(15.0, 10.0, 40.0);
    Device d;
    EXPECT_DOUBLE_EQ(t.Update(true, d.windows, d.trough, d.underruns, kRate), 15.0);
    for (uint32_t i = 0; i < 2 * LowLatencyTarget::kLowerAfterWindows; i++)
        Window(t, d, 8.0);
    EXPECT_DOUBLE_EQ(t.Target(), 15.0 - 2 * LowLatencyTarget::kLowerMs);

    d.underruns++;
    EXPECT_DOUBLE_EQ(t.Update(true, d.windows, d.trough, d.underruns, kRate), 13.0 + LowLatencyTarget::kRaiseOnGapMs);

    for (uint32_t i = 0; i < 100 * LowLatencyTarget::kLowerAfterWindows; i++)
        Window(t, d, 8.0);
    EXPECT_DOUBLE_EQ(t.Target(), 10.0);

    EXPECT_DOUBLE_EQ(t.Update(false, d.windows, d.trough, d.underruns, kRate), 15.0);
    // A start outside the range is clamped into it
    EXPECT_DOUBLE_EQ(LowLatencyTarget(5.0, 10.0, 40.0).Target(), 10.0);
    EXPECT_DOUBLE_EQ(LowLatencyTarget(50.0, 10.0, 40.0).Target(), 40.0);
}
