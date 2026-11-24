// The presenters' VRR verdict (platform/vrrprobe.h): from the screen's refreshes against the presents alone. A
// presenter starts on the grid and takes VRR only when the refreshes are seen to follow the frames.

#include <gtest/gtest.h>

#include "platform/vrrprobe.h"

using vrrprobe::Judge;
using vrrprobe::JudgeSteps;
using vrrprobe::Probe;
using vrrprobe::Verdict;

TEST(VrrProbe_Test, TheRefreshesWithTheFramesOffTheGridFollow)
{
    // A Pentagon's 48.83 Hz on a 60 Hz mode: a refresh every frame (20.48 ms)
    EXPECT_EQ(Judge(20.48, 20.40, 20.56, 20.48, 16.67), Verdict::Follows);
    // 50 Hz frames in a 144 Hz mode
    EXPECT_EQ(Judge(20.0, 19.9, 20.1, 20.0, 6.94), Verdict::Follows);
}

TEST(VrrProbe_Test, AGridTheFramesAreNotOnIsAGrid)
{
    // The mode's grid while the frames are off it
    EXPECT_EQ(Judge(16.67, 16.66, 16.68, 20.48, 16.67), Verdict::Grid);
    // Another display's steady grid (a remote session: 32 Hz vblanks, the display says 60)
    EXPECT_EQ(Judge(31.25, 31.2, 31.3, 20.0, 16.67), Verdict::Grid);
}

TEST(VrrProbe_Test, FramesAtTheDisplaysRateTellNothing)
{
    // A 60 Hz machine on 60 Hz, 50.08 Hz on 50 Hz, 48.83 Hz on 50 Hz: on the grid and with the frames at once
    EXPECT_EQ(Judge(16.67, 16.66, 16.68, 16.70, 16.67), Verdict::Unknown);
    EXPECT_EQ(Judge(20.0, 19.99, 20.01, 19.97, 20.0), Verdict::Unknown);
    EXPECT_EQ(Judge(20.0, 19.99, 20.01, 20.48, 20.0), Verdict::Unknown);
    // Nothing measured; a mode change's mix of two grids
    EXPECT_EQ(Judge(0.0, 0.0, 0.0, 20.0, 16.67), Verdict::Unknown);
    EXPECT_EQ(Judge(18.0, 16.67, 20.0, 25.0, 16.67), Verdict::Unknown);
}

TEST(VrrProbe_Test, TheProbeStartsOnTheGridAndTakesVrrWhenItIsSeen)
{
    Probe probe;
    probe.Reset();
    EXPECT_FALSE(probe.Engaged());
    EXPECT_TRUE(probe.Pending());

    EXPECT_TRUE(probe.Feed(Verdict::Follows));
    EXPECT_TRUE(probe.Engaged());
    EXPECT_FALSE(probe.Pending());
    EXPECT_FALSE(probe.Feed(Verdict::Follows));
    // The frames came to the display's rate for a while: VRR stays
    EXPECT_FALSE(probe.Feed(Verdict::Unknown));
    EXPECT_TRUE(probe.Engaged());
    // The window went behind another one: the refreshes back on the grid
    EXPECT_TRUE(probe.Feed(Verdict::Grid));
    EXPECT_FALSE(probe.Engaged());
}

TEST(VrrProbe_Test, PendingEndsWithAVerdictOrWhenNothingWillTell)
{
    Probe probe;
    probe.Reset();
    EXPECT_FALSE(probe.Feed(Verdict::Grid));  // on the grid already: no change, but known
    EXPECT_FALSE(probe.Pending());

    probe.Reset();
    EXPECT_FALSE(probe.Feed(Verdict::Unknown));
    EXPECT_TRUE(probe.Pending());
    EXPECT_FALSE(probe.Feed(Verdict::Unknown));
    EXPECT_FALSE(probe.Pending());
    EXPECT_FALSE(probe.Engaged());
    // Seen later all the same (the machine's rate changed): taken
    EXPECT_TRUE(probe.Feed(Verdict::Follows));
    EXPECT_TRUE(probe.Engaged());
}

TEST(VrrProbe_Test, ScreenStepsOnAGridAreWholeRefreshes)
{
    // ProMotion (frame presenter README, section 9): 50 Hz frames on the 120 Hz grid stay 16.7 or 25 ms, never 20
    EXPECT_EQ(JudgeSteps(16.67, 16.67, 25.0, 20.0, 8.33), Verdict::Grid);
    // 48.83 Hz on a fixed 60 Hz: one or two refreshes
    EXPECT_EQ(JudgeSteps(16.67, 16.67, 33.33, 20.48, 16.67), Verdict::Grid);
    // A display that follows: every step the frame's own
    EXPECT_EQ(JudgeSteps(19.9, 20.0, 20.1, 20.0, 8.33), Verdict::Follows);
    EXPECT_EQ(JudgeSteps(20.4, 20.48, 20.56, 20.48, 16.67), Verdict::Follows);
    // Frames at a whole number of refreshes (60 Hz on 120 Hz, 50 Hz on 50 Hz): nothing to tell by
    EXPECT_EQ(JudgeSteps(16.6, 16.67, 16.7, 16.67, 8.33), Verdict::Unknown);
    EXPECT_EQ(JudgeSteps(20.0, 20.0, 20.0, 20.0, 20.0), Verdict::Unknown);
    // Nothing shown yet; steps on no grid and not with the frames (a stall, a mode change)
    EXPECT_EQ(JudgeSteps(0.0, 0.0, 0.0, 20.0, 8.33), Verdict::Unknown);
    EXPECT_EQ(JudgeSteps(18.0, 21.0, 31.0, 20.0, 8.33), Verdict::Unknown);
}

TEST(VrrProbe_Test, AProbeMayStartEngagedWhereTheSystemSaysTheDisplayCanFollow)
{
    Probe probe;
    probe.Reset(true);
    EXPECT_TRUE(probe.Engaged());
    EXPECT_TRUE(probe.Pending());
    EXPECT_TRUE(probe.Feed(Verdict::Grid));  // ProMotion: on its grid after all
    EXPECT_FALSE(probe.Engaged());
    EXPECT_FALSE(probe.Pending());
}
