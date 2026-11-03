/// @file displayrate_test.cpp
/// @brief [VIDEO] DisplayRate: the display rate picked for a machine's frame rate, and the frame rate measured from the
/// frames' target times (frame presenter design, section 4.6).

#include <gtest/gtest.h>

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
