#include <gtest/gtest.h>

#include "hudanimator.h"
#include "hudtiming.h"

#include <cmath>

TEST(HudEasing_Test, ClampingBounds)
{
    EXPECT_DOUBLE_EQ(HudEasing::Clamp01(-0.5), 0.0);
    EXPECT_DOUBLE_EQ(HudEasing::Clamp01(0.0), 0.0);
    EXPECT_DOUBLE_EQ(HudEasing::Clamp01(0.5), 0.5);
    EXPECT_DOUBLE_EQ(HudEasing::Clamp01(1.0), 1.0);
    EXPECT_DOUBLE_EQ(HudEasing::Clamp01(1.5), 1.0);
}

TEST(HudEasing_Test, LinearAtBoundaries)
{
    EXPECT_DOUBLE_EQ(HudEasing::Linear(0.0), 0.0);
    EXPECT_DOUBLE_EQ(HudEasing::Linear(0.5), 0.5);
    EXPECT_DOUBLE_EQ(HudEasing::Linear(1.0), 1.0);
}

TEST(HudEasing_Test, EaseOutCubicBoundariesAndMidpoint)
{
    EXPECT_DOUBLE_EQ(HudEasing::EaseOutCubic(0.0), 0.0);
    EXPECT_DOUBLE_EQ(HudEasing::EaseOutCubic(1.0), 1.0);

    // Midpoint: 1 - (0.5)^3 = 0.875
    EXPECT_DOUBLE_EQ(HudEasing::EaseOutCubic(0.5), 0.875);
}

TEST(HudEasing_Test, EaseInCubicBoundariesAndMidpoint)
{
    EXPECT_DOUBLE_EQ(HudEasing::EaseInCubic(0.0), 0.0);
    EXPECT_DOUBLE_EQ(HudEasing::EaseInCubic(1.0), 1.0);

    // Midpoint: (0.5)^3 = 0.125
    EXPECT_DOUBLE_EQ(HudEasing::EaseInCubic(0.5), 0.125);
}

TEST(HudEasing_Test, PulseSineSymmetry)
{
    // PulseSine should be 0 at t=0, peak near t=0.5, and return to 0 at t=1
    EXPECT_NEAR(HudEasing::PulseSine(0.0), 0.0, 1e-5);
    EXPECT_NEAR(HudEasing::PulseSine(0.5), 1.0, 1e-5);
    EXPECT_NEAR(HudEasing::PulseSine(1.0), 0.0, 1e-5);
}

TEST(HudAnimator_Test, EvaluateEnterProgression)
{
    // t = 0
    auto s0 = HudAnimator::EvaluateEnter(std::chrono::milliseconds(0), HudAnimation::SlideFade, false, std::chrono::milliseconds(200));
    EXPECT_FALSE(s0.finished);
    EXPECT_FLOAT_EQ(s0.opacity, 0.0f);
    EXPECT_NEAR(s0.slideOffsetEm, 0.75f, 1e-4);

    // t = end
    auto sEnd = HudAnimator::EvaluateEnter(std::chrono::milliseconds(200), HudAnimation::SlideFade, false, std::chrono::milliseconds(200));
    EXPECT_TRUE(sEnd.finished);
    EXPECT_FLOAT_EQ(sEnd.opacity, 1.0f);
    EXPECT_FLOAT_EQ(sEnd.slideOffsetEm, 0.0f);
}

TEST(HudAnimator_Test, EvaluateExitProgression)
{
    // t = 0
    auto s0 = HudAnimator::EvaluateExit(std::chrono::milliseconds(0), HudAnimation::SlideFade, false, std::chrono::milliseconds(200));
    EXPECT_FALSE(s0.finished);
    EXPECT_FLOAT_EQ(s0.opacity, 1.0f);
    EXPECT_FLOAT_EQ(s0.slideOffsetEm, 0.0f);

    // t = end
    auto sEnd = HudAnimator::EvaluateExit(std::chrono::milliseconds(200), HudAnimation::SlideFade, false, std::chrono::milliseconds(200));
    EXPECT_TRUE(sEnd.finished);
    EXPECT_FLOAT_EQ(sEnd.opacity, 0.0f);
}

TEST(HudAnimator_Test, ReducedMotionCollapsesSlideAndScale)
{
    auto s = HudAnimator::EvaluateEnter(std::chrono::milliseconds(50), HudAnimation::SlideFade, true, std::chrono::milliseconds(200));
    EXPECT_FLOAT_EQ(s.scale, 1.0f);
    EXPECT_FLOAT_EQ(s.slideOffsetEm, 0.0f);
}
