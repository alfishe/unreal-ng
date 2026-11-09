/// @file presentstats_test.cpp
/// @brief The native presenters' shared statistics (platform/nativepresenter.h, presentstats): the rings keep the last
/// kWindow samples, the percentiles come from them.

#include <gtest/gtest.h>

#include <algorithm>

#include "platform/nativepresenter.h"

TEST(PresentStats_Test, TheRingKeepsTheLastWindow)
{
    presentstats::Ring ring;
    for (size_t i = 0; i < presentstats::kWindow + 10; i++)
        ring.Add(static_cast<double>(i));
    ASSERT_EQ(ring.Values().size(), presentstats::kWindow);
    double lowest = ring.Values()[0];
    for (double v : ring.Values())
        lowest = std::min(lowest, v);
    EXPECT_EQ(lowest, 10.0);
}

TEST(PresentStats_Test, PercentilesFromTheSamples)
{
    std::vector<double> errors;
    for (int i = 100; i >= 1; i--)
        errors.push_back(static_cast<double>(i));
    NativePresenter::Stats s;
    presentstats::Fill(s, errors, {2.0, 1.0, 3.0}, {});
    EXPECT_EQ(s.window, 100u);
    EXPECT_EQ(s.errorP50, 51.0);  // index round(0.5 * 99) = 50
    EXPECT_EQ(s.errorP99, 99.0);
    EXPECT_EQ(s.errorMax, 100.0);
    EXPECT_EQ(s.gpuWaitP50, 2.0);
    EXPECT_EQ(s.gpuWaitMax, 3.0);
    EXPECT_EQ(s.drawableWaitP50, 0.0);
    EXPECT_EQ(s.drawableWaitMax, 0.0);
    EXPECT_EQ(presentstats::Percentile({}, 0.5), 0.0);
    EXPECT_EQ(presentstats::Percentile({4.0, 1.0}, 0.0), 1.0);
}
