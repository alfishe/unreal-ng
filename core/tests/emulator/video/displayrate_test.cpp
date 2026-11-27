/// @file displayrate_test.cpp
/// @brief [VIDEO] DisplayRate: the display rate picked for a machine's frame rate, and the frame rate measured from the
/// frames' target times (frame presenter design, section 4.6).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "emulator/video/displayrate.h"

namespace
{
constexpr double kPentagon = 48.83;   // 71680 t-states at 3.5 MHz
constexpr double kSpectrum = 50.08;   // 69888 t-states at 3.5 MHz
}  // namespace

TEST(DisplayRate_Test, WordsBothWays)
{
    DisplayRate rate = DisplayRate::Keep;
    EXPECT_TRUE(ParseDisplayRate("MATCH", rate));
    EXPECT_EQ(rate, DisplayRate::Match);
    EXPECT_STREQ(DisplayRateName(rate), "match");
    EXPECT_TRUE(ParseDisplayRate("keep", rate));
    EXPECT_EQ(rate, DisplayRate::Keep);
    EXPECT_FALSE(ParseDisplayRate("50", rate));
    EXPECT_FALSE(ParseDisplayRate(nullptr, rate));
}

TEST(DisplayRate_Test, FiftyOnASixtyHertzMonitor)
{
    // The Dell U4320Q's modes at 4K
    const std::vector<double> rates{24.0, 30.0, 50.0, 60.0};
    DisplayRateFit pick;
    ASSERT_TRUE(DisplayRatePick(kPentagon, 60.0, rates, pick));
    EXPECT_DOUBLE_EQ(pick.hz, 50.0);
    EXPECT_EQ(pick.repeats, 1);
    EXPECT_NEAR(pick.error, 0.024, 0.001);
    ASSERT_TRUE(DisplayRatePick(kSpectrum, 60.0, rates, pick));
    EXPECT_DOUBLE_EQ(pick.hz, 50.0);
    EXPECT_LT(pick.error, 0.002);
}

TEST(DisplayRate_Test, AMultipleWhenThereIsNoFifty)
{
    const std::vector<double> rates{60.0, 100.0, 120.0, 144.0};
    DisplayRateFit pick;
    ASSERT_TRUE(DisplayRatePick(kSpectrum, 144.0, rates, pick));
    EXPECT_DOUBLE_EQ(pick.hz, 100.0);
    EXPECT_EQ(pick.repeats, 2);
}

TEST(DisplayRate_Test, TheLowerOfEqualFits)
{
    DisplayRateFit pick;
    ASSERT_TRUE(DisplayRatePick(kSpectrum, 60.0, {100.0, 50.0, 60.0}, pick));
    EXPECT_DOUBLE_EQ(pick.hz, 50.0);
}

TEST(DisplayRate_Test, NoSwitchWithoutAFittingRate)
{
    DisplayRateFit pick;
    // 60, 75 and 120 are all at least 18 % off a 50 Hz machine's multiples
    EXPECT_FALSE(DisplayRatePick(kSpectrum, 60.0, {60.0, 75.0, 120.0}, pick));
    // 144 is three refreshes a frame 4.2 % off: within 5 %
    ASSERT_TRUE(DisplayRatePick(kSpectrum, 60.0, {60.0, 144.0}, pick));
    EXPECT_EQ(pick.repeats, 3);
    EXPECT_FALSE(DisplayRatePick(kSpectrum, 60.0, {}, pick));
    EXPECT_FALSE(DisplayRatePick(0.0, 60.0, {50.0}, pick));
}

TEST(DisplayRate_Test, NoSwitchWhenTheCurrentRateFitsAsWell)
{
    DisplayRateFit pick;
    EXPECT_FALSE(DisplayRatePick(kPentagon, 50.0, {50.0, 60.0}, pick));
    // 100 Hz fits as well as 50: no reason to switch
    EXPECT_FALSE(DisplayRatePick(kSpectrum, 100.0, {50.0, 60.0, 100.0}, pick));
}

TEST(DisplayRate_Test, AFittingRateStaysWhileTheMachineSways)
{
    // 49.0 Hz is 2 % off both 100 (two refreshes) and 144 (three): a measured rate swaying across it keeps the one
    // running, whichever it is, instead of switching the display back and forth
    const std::vector<double> rates{60.0, 100.0, 120.0, 144.0};
    DisplayRateFit pick;
    for (const double machineHz : {48.95, 49.0, 49.05})
    {
        EXPECT_FALSE(DisplayRatePick(machineHz, 144.0, rates, pick)) << machineHz;
        EXPECT_FALSE(DisplayRatePick(machineHz, 100.0, rates, pick)) << machineHz;
    }
    // A fitting rate gives way to one at most half as far off: 144 Hz is 4 % off a 50 Hz machine, 100 Hz exact
    ASSERT_TRUE(DisplayRatePick(kSpectrum, 144.0, rates, pick));
    EXPECT_DOUBLE_EQ(pick.hz, 100.0);
}

TEST(DisplayRate_Test, MeterFindsTheSteadyRate)
{
    FrameRateMeter meter;
    const int64_t period = 20'480'000;  // 48.83 Hz
    int64_t t = 1'000'000'000;
    bool changed = false;
    for (int i = 0; i <= static_cast<int>(FrameRateMeter::kPeriods); i++, t += period + (i % 3) * 1000)
        changed |= meter.Push(t);
    EXPECT_TRUE(changed);
    EXPECT_NEAR(meter.Hz(), 48.83, 0.01);
    // The same rate again is no change
    EXPECT_FALSE(meter.Push(t));
}

TEST(DisplayRate_Test, MeterTakesTheNominalPeriodOfPacedFrames)
{
    // A remote session's audio clock: targets wandering by a few percent, never 1 % steady. The machine's own
    // period gives its rate at the first frame
    FrameRateMeter meter;
    int64_t t = 1'000'000'000;
    EXPECT_TRUE(meter.Push(t, 20'480'000));  // a Pentagon: 71 680 T at 3.5 MHz
    EXPECT_DOUBLE_EQ(meter.Hz(), 48.83);
    for (int i = 0; i < 60; i++)
    {
        t += 20'480'000 + ((i % 4) - 2) * 600'000;
        EXPECT_FALSE(meter.Push(t, 20'480'000));
    }
    EXPECT_DOUBLE_EQ(meter.Hz(), 48.83);
    // Another machine: a Spectrum 128K's 70 908 T at 3.5469 MHz
    EXPECT_TRUE(meter.Push(t + 20'000'000, 19'992'000));
    EXPECT_DOUBLE_EQ(meter.Hz(), 50.02);
    // Turbo (no period): the rate stays
    for (int i = 0; i < 30; i++)
        EXPECT_FALSE(meter.Push(t += (i % 2) ? 2'000'000 : 9'000'000));
    EXPECT_DOUBLE_EQ(meter.Hz(), 50.02);
}

TEST(DisplayRate_Test, MeterIgnoresUnsteadyFrames)
{
    FrameRateMeter meter;
    int64_t t = 0;
    // Turbo: periods all over the place
    for (int i = 0; i < 60; i++)
    {
        t += (i % 2) ? 2'000'000 : 20'000'000;
        EXPECT_FALSE(meter.Push(t));
    }
    EXPECT_EQ(meter.Hz(), 0.0);
}

/// Turbo frames come steady too (a fast host at a fixed turbo rate): they never become the machine's rate, and the
/// display rate never switches for them
TEST(DisplayRate_Test, MeterKeepsTheRateThroughSteadyTurboFrames)
{
    FrameRateMeter meter;
    int64_t t = 1'000'000'000;
    EXPECT_TRUE(meter.Push(t, 20'480'000));
    // The window passes no period for a frame timed by its latch (the GL window) ...
    for (int i = 0; i < 100; i++)
        EXPECT_FALSE(meter.Push(t += 10'000'000)) << "frame " << i;  // 100 Hz, exactly steady
    EXPECT_DOUBLE_EQ(meter.Hz(), 48.83);
    // ... or says what it is (the software window)
    for (int i = 0; i < 100; i++)
        EXPECT_FALSE(meter.PushUnpaced());
    EXPECT_DOUBLE_EQ(meter.Hz(), 48.83);

    // A screen no clock times at all (no period ever): the targets' median is the rate; turbo on it stays unknown
    FrameRateMeter unpaced;
    for (int i = 0; i < 100; i++)
        EXPECT_FALSE(unpaced.PushUnpaced());
    EXPECT_EQ(unpaced.Hz(), 0.0);
}

TEST(DisplayRate_Test, VideoSyncTakesTheFirstStepTheDisplayAllows)
{
    // VRR first, whatever the rates
    EXPECT_EQ(PickVideoSync(true, 120.0, kPentagon), VideoSync::Vrr);
    // A rate fitting the machine's: 50 Hz (2.4 % off a Pentagon), 100 Hz, a Spectrum on 50
    EXPECT_EQ(PickVideoSync(false, 50.0, kPentagon), VideoSync::DisplayRate);
    EXPECT_EQ(PickVideoSync(false, 100.0, kSpectrum), VideoSync::DisplayRate);
    // A rate that beats with it: interpolation
    EXPECT_EQ(PickVideoSync(false, 60.0, kPentagon), VideoSync::Interpolate);
    EXPECT_EQ(PickVideoSync(false, 75.0, kSpectrum), VideoSync::Interpolate);
    EXPECT_EQ(PickVideoSync(false, 120.0, kSpectrum), VideoSync::Interpolate);
    EXPECT_STREQ(VideoSyncName(VideoSync::DisplayRate), "display_rate");
}

TEST(DisplayRate_Test, SmoothMotionByHandNeedsADisplayThatBeatsWithTheMachine)
{
    // 50 Hz and a Pentagon's 48.83 Hz on 60 Hz: a picture per refresh between the frames
    EXPECT_TRUE(SmoothMotionHelps(false, 60.0, 50.0));
    EXPECT_TRUE(SmoothMotionHelps(false, 59.81, 48.83));
    // Each frame for the same number of refreshes, or a display that follows them: nothing between two frames
    EXPECT_FALSE(SmoothMotionHelps(false, 50.0, 50.08));
    EXPECT_FALSE(SmoothMotionHelps(false, 100.0, 50.0));
    EXPECT_FALSE(SmoothMotionHelps(false, 50.0, 48.83));
    EXPECT_FALSE(SmoothMotionHelps(true, 144.0, 50.0));
    // Not known yet: it starts
    EXPECT_TRUE(SmoothMotionHelps(false, 0.0, 50.0));
    EXPECT_TRUE(SmoothMotionHelps(false, 60.0, 0.0));
}

TEST(DisplayRate_Test, VideoSyncKeepsItsStepWhileTheMachineIsNotSteady)
{
    // Paused, turbo, just started: the step before stays; none before: interpolate (the display is unknown to fit)
    EXPECT_EQ(PickVideoSync(false, 50.0, 0.0, VideoSync::DisplayRate), VideoSync::DisplayRate);
    EXPECT_EQ(PickVideoSync(false, 60.0, 0.0, VideoSync::Interpolate), VideoSync::Interpolate);
    EXPECT_EQ(PickVideoSync(false, 50.0, 0.0), VideoSync::Interpolate);
    EXPECT_EQ(PickVideoSync(true, 50.0, 0.0, VideoSync::Interpolate), VideoSync::Vrr);
}

TEST(DisplayRate_Test, SmoothGuardStepsDownOnLateOrSlowPictures)
{
    constexpr int64_t REFRESH = 16'666'667;
    constexpr int64_t QUICK = 1'000'000;
    SmoothGuard guard;
    guard.Reset(100);
    // In time: no step, window after window
    for (uint32_t i = 0; i < SmoothGuard::kWindow * 3; i++)
        EXPECT_FALSE(guard.Note(100, QUICK, REFRESH)) << i;
    // One window of `lates` late presents, all at its start (a stall); the verdict is the window's, not a picture's
    uint64_t late = 100;
    SmoothGuard software;  // no presenter counts: late presents stay 0
    auto window = [&](SmoothGuard& g, uint32_t lates, int64_t slowNs, uint32_t slow) {
        bool down = false;
        for (uint32_t i = 0; i < SmoothGuard::kWindow; i++)
        {
            if (i < lates)
                late++;
            down = g.Note(&g == &guard ? late : 0, i < slow ? slowNs : QUICK, REFRESH);
            if (i + 1 < SmoothGuard::kWindow)
                EXPECT_FALSE(down) << "picture " << i;
        }
        return down;
    };
    // kLates of a window, kBadWindows windows in a row: down
    for (uint32_t w = 0; w + 1 < SmoothGuard::kBadWindows; w++)
        EXPECT_FALSE(window(guard, SmoothGuard::kLates, QUICK, 0)) << "window " << w;
    EXPECT_TRUE(window(guard, SmoothGuard::kLates, QUICK, 0));
    EXPECT_EQ(guard.ClosedLates(), SmoothGuard::kLates);
    // One stall (the owner's Mac: 4 presents late at once, none for seconds around): no step, however many
    for (int stall = 0; stall < 5; stall++)
    {
        EXPECT_FALSE(window(guard, 20, QUICK, 0)) << stall;
        EXPECT_FALSE(window(guard, 0, QUICK, 0)) << stall;
        EXPECT_FALSE(window(guard, 0, QUICK, 0)) << stall;
    }
    // Bad windows broken by a good one start over
    for (uint32_t w = 0; w + 1 < SmoothGuard::kBadWindows; w++)
        EXPECT_FALSE(window(guard, SmoothGuard::kLates, QUICK, 0));
    EXPECT_FALSE(window(guard, 0, QUICK, 0));
    for (uint32_t w = 0; w + 1 < SmoothGuard::kBadWindows; w++)
        EXPECT_FALSE(window(guard, SmoothGuard::kLates, QUICK, 0));
    // One less a window: no step
    for (uint32_t w = 0; w < SmoothGuard::kBadWindows * 2; w++)
        EXPECT_FALSE(window(guard, SmoothGuard::kLates - 1, QUICK, 0));
    // No presenter counts (the software screen): pictures drawn in most of a refresh tell
    bool down = false;
    for (uint32_t w = 0; w < SmoothGuard::kBadWindows; w++)
        down = window(software, 0, REFRESH, SmoothGuard::kLates);
    EXPECT_TRUE(down);
    EXPECT_EQ(software.ClosedSlow(), SmoothGuard::kLates);
    EXPECT_EQ(software.ClosedLates(), 0u);
    // Three quarters of a refresh is still in time
    for (uint32_t w = 0; w < SmoothGuard::kBadWindows; w++)
        EXPECT_FALSE(window(software, 0, REFRESH * 3 / 4, SmoothGuard::kWindow));
    // A presenter set again counts from its number, not from zero
    guard.Reset(5000);
    for (uint32_t i = 0; i < SmoothGuard::kWindow; i++)
        down = guard.Note(5000, QUICK, REFRESH);
    EXPECT_FALSE(down);
}

/// Smooth motion off for a while (turbo, low latency, the temporal blend, another sync step): the presenter's late
/// presents meanwhile are not the interpolation's pictures. On again, the guard counts from then on: no step down for
/// lates it never drew
TEST(DisplayRate_Test, SmoothGuardCountsNoLatesFromWhileSmoothMotionWasOff)
{
    constexpr int64_t REFRESH = 16'666'667;
    constexpr int64_t QUICK = 1'000'000;
    const auto run = [&](bool tracked) {
        SmoothGuard guard;
        guard.Reset(100);
        for (uint32_t i = 0; i < SmoothGuard::kWindow / 2; i++)
            EXPECT_FALSE(guard.Note(100, QUICK, REFRESH));
        // Off: 50 refreshes, the presenter late on many of them (turbo frames presented at once)
        uint64_t late = 100;
        for (int refresh = 0; refresh < 50; refresh++)
        {
            late += refresh % 5 == 0 ? 1 : 0;
            if (tracked)
                guard.Track(false, late);
        }
        // On again, every picture in time
        for (uint32_t i = 0; i < SmoothGuard::kWindow; i++)
        {
            if (tracked)
                guard.Track(true, late);
            EXPECT_FALSE(guard.Note(late, QUICK, REFRESH));
        }
        return guard.ClosedLates();  // a window closed with kLates or more counts toward a step down
    };
    EXPECT_GE(run(false), SmoothGuard::kLates) << "untracked (the defect): the lates from while it was off count";
    EXPECT_EQ(run(true), 0u);
}

/// A step down is tried again after a wait that doubles with each step down, up to its bound; the settings set again
/// start the waits over. On the owner's Mac a start under load made every present late for its first seconds: the
/// step down was for good before the waits
TEST(DisplayRate_Test, SmoothGuardTriesTheLevelAboveAgainAfterAWaitThatGrows)
{
    constexpr int64_t S = 1'000'000'000;
    SmoothGuard guard;
    EXPECT_FALSE(guard.RetryDue(0)) << "nothing stepped down";
    int64_t now = 100 * S;
    int64_t wait = SmoothGuard::kFirstRetryNs;
    for (int step = 0; step < 8; step++)
    {
        guard.SteppedDown(now);
        EXPECT_FALSE(guard.RetryDue(now + wait - 1)) << "step " << step;
        EXPECT_TRUE(guard.RetryDue(now + wait)) << "step " << step;
        EXPECT_FALSE(guard.RetryDue(now + wait + 1)) << "once";
        now += wait;
        wait = std::min(wait * 2, SmoothGuard::kMaxRetryNs);
    }
    EXPECT_EQ(wait, SmoothGuard::kMaxRetryNs);
    guard.SteppedDown(now);
    guard.StartOver();
    EXPECT_FALSE(guard.RetryDue(now + SmoothGuard::kMaxRetryNs)) << "the settings set again: nothing to retry";
    guard.SteppedDown(now);
    EXPECT_TRUE(guard.RetryDue(now + SmoothGuard::kFirstRetryNs)) << "the first wait again";
    // One level up and still below the top: the next one after the wait as it is now, not doubled again
    now += SmoothGuard::kFirstRetryNs;
    guard.RetryLater(now);
    EXPECT_FALSE(guard.RetryDue(now + 2 * SmoothGuard::kFirstRetryNs - 1));
    EXPECT_TRUE(guard.RetryDue(now + 2 * SmoothGuard::kFirstRetryNs));
}
