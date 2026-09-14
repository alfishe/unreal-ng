#include "stdafx.h"
#include "gtest/gtest.h"

#include "emulator/io/mouse/mousedeltaaccumulator.h"

/// Pointer-mapping arithmetic (Kempston Mouse design §8 layer 3): pure numbers, no Qt.

TEST(MouseDeltaAccumulator_Test, IdentityAtOneToOne)
{
    MouseDeltaAccumulator acc;
    auto s = acc.Feed(7.0, -4.0, 1.0, 1.0);
    EXPECT_EQ(s.dx, 7);
    EXPECT_EQ(s.dy, -4);
}

/// 3x upscale: one host pixel is a third of an emulated pixel - slow movement must
/// still produce motion (integer deltas would truncate every event to zero)
TEST(MouseDeltaAccumulator_Test, SubPixelTravelAccumulatesAtHighUpscale)
{
    MouseDeltaAccumulator acc;
    int total = 0;
    for (int i = 0; i < 9; i++)
        total += acc.Feed(1.0, 0.0, 3.0, 3.0).dx;
    EXPECT_EQ(total, 3) << "9 host pixels at 3x must travel exactly 3 emulated pixels";
    EXPECT_NEAR(acc.RemainderX(), 0.0, 1e-9);
}

/// The remainder is carried across events in both directions
TEST(MouseDeltaAccumulator_Test, RemainderCarriesBothSigns)
{
    MouseDeltaAccumulator acc;
    EXPECT_EQ(acc.Feed(1.5, -1.5, 1.0, 1.0).dx, 1);
    EXPECT_NEAR(acc.RemainderX(), 0.5, 1e-9);
    EXPECT_NEAR(acc.RemainderY(), -0.5, 1e-9);

    auto s = acc.Feed(0.5, -0.5, 1.0, 1.0);
    EXPECT_EQ(s.dx, 1);
    EXPECT_EQ(s.dy, -1);
}

/// Fractional device pixel ratio (Windows 150%): host 10 logical px = 15 physical px;
/// image drawn at 2x logical = 3 physical px per emulated px -> 5 emulated px.
/// The same travel on a 1.0 DPR display with the same logical window gives the same
/// result: DPR cancels, motion is DPI-independent
TEST(MouseDeltaAccumulator_Test, DevicePixelRatioCancels)
{
    for (double dpr : {1.0, 1.5, 2.0})
    {
        MouseDeltaAccumulator acc;
        const double logicalTravel = 10.0;
        const double logicalUpscale = 2.0;
        auto s = acc.Feed(logicalTravel * dpr, 0.0, logicalUpscale * dpr, logicalUpscale * dpr);
        EXPECT_EQ(s.dx, 5) << "dpr " << dpr;
    }
}

/// Non-uniform presentation: X and Y factors are independent
TEST(MouseDeltaAccumulator_Test, IndependentAxisFactors)
{
    MouseDeltaAccumulator acc;
    auto s = acc.Feed(12.0, 12.0, 2.0, 4.0);
    EXPECT_EQ(s.dx, 6);
    EXPECT_EQ(s.dy, 3);
}

/// mousescale: power-of-two sensitivity
TEST(MouseDeltaAccumulator_Test, ScaleMultiplies)
{
    MouseDeltaAccumulator acc;
    EXPECT_EQ(acc.Feed(8.0, 0.0, 2.0, 2.0, 2.0).dx, 8);
    EXPECT_EQ(acc.Feed(8.0, 0.0, 2.0, 2.0, 0.5).dx, 2);
}

/// Unlaid-out geometry must not emit garbage motion or poison the accumulator
TEST(MouseDeltaAccumulator_Test, DegenerateGeometryProducesNoMotion)
{
    MouseDeltaAccumulator acc;
    auto s = acc.Feed(100.0, 100.0, 0.0, 0.0);
    EXPECT_EQ(s.dx, 0);
    EXPECT_EQ(s.dy, 0);
    s = acc.Feed(NAN, 1.0, 1.0, 1.0);
    EXPECT_EQ(s.dx, 0);
    EXPECT_EQ(acc.Feed(3.0, 0.0, 1.0, 1.0).dx, 3) << "accumulator still healthy";
}

TEST(MouseWheelAccumulator_Test, WholeNotches)
{
    MouseWheelAccumulator wheel;
    EXPECT_EQ(wheel.Feed(120), 1);
    EXPECT_EQ(wheel.Feed(-240), -2);
}

/// Trackpads and hi-res wheels deliver fractions of a notch
TEST(MouseWheelAccumulator_Test, FractionsAccumulate)
{
    MouseWheelAccumulator wheel;
    int total = 0;
    for (int i = 0; i < 12; i++)
        total += wheel.Feed(10);
    EXPECT_EQ(total, 1);

    total = 0;
    for (int i = 0; i < 6; i++)
        total += wheel.Feed(-40);
    EXPECT_EQ(total, -2);
}
