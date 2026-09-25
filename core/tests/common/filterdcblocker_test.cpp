#include "stdafx.h"
#include "pch.h"

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "common/sound/filters/filterdcblocker.h"

/// FilterDCBlocker: one-pole RC high-pass (output coupling capacitor model).
/// Pinned: exact RC step response, flat passband, -3 dB at fc, no delayed
/// step after a burst (the moving-average DC remover it replaced produced one
/// a window length later), denormal flush, reset / configure semantics.

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 218750.0;  // the AY generator rate the filter runs at
constexpr double kCutoff = 5.0;

/// Steady-state gain for a sine at `hz`: skip `settleSeconds`, then fit the
/// amplitude over whole periods by correlation
double MeasureGain(double hz, double settleSeconds = 1.0)
{
    FilterDCBlocker f(kRate, kCutoff);
    const double w = 2.0 * kPi * hz / kRate;
    const size_t settle = size_t(settleSeconds * kRate);
    const size_t periods = std::max<size_t>(4, size_t(hz * 0.2));
    const size_t measure = size_t(double(periods) * kRate / hz);
    double sc = 0, cc = 0;
    for (size_t n = 0; n < settle + measure; n++)
    {
        const double y = f.filter(std::sin(w * double(n)));
        if (n >= settle)
        {
            sc += y * std::sin(w * double(n));
            cc += y * std::cos(w * double(n));
        }
    }
    return 2.0 * std::sqrt(sc * sc + cc * cc) / double(measure);
}
}  // namespace

TEST(FilterDCBlocker_Test, StepResponseIsTheRcExponential)
{
    // y[n] = a^(n+1) for a unit step from rest - the sampled RC discharge
    FilterDCBlocker f(kRate, kCutoff);
    const double a = f.coefficient();
    const double rc = 1.0 / (2.0 * kPi * kCutoff);
    EXPECT_NEAR(a, rc / (rc + 1.0 / kRate), 1e-15);

    for (int n = 0; n < 2000; n++)
        EXPECT_NEAR(f.filter(1.0), std::pow(a, n + 1), 1e-12) << "sample " << n;

    // Settles: below 1% after 5 time constants (DC is removed, not held)
    const size_t fiveTau = size_t(5.0 * rc * kRate);
    double y = 0;
    for (size_t n = 2000; n < fiveTau; n++)
        y = f.filter(1.0);
    EXPECT_LT(y, 0.01);
    EXPECT_GT(y, 0.0) << "an RC high-pass never overshoots a step";
}

TEST(FilterDCBlocker_Test, FlatPassbandAndMinus3dBAtCutoff)
{
    EXPECT_NEAR(MeasureGain(1000.0), 1.0, 1e-3);
    EXPECT_NEAR(MeasureGain(100.0), 1.0, 2e-3);
    // The bass the moving-average remover lost (-21 dB there) stays
    EXPECT_GT(20.0 * std::log10(MeasureGain(50.0)), -0.05);
    // Analog prototype: |H(fc)| = 1/sqrt(2)
    EXPECT_NEAR(20.0 * std::log10(MeasureGain(kCutoff, 3.0)), -3.01, 0.05);
}

TEST(FilterDCBlocker_Test, BurstDecaysWithoutDelayedStep)
{
    // A 0.3 ms positive burst from silence (an AY envelope restart), then
    // silence. The output must decay smoothly: every later sample-to-sample
    // change is tiny next to the burst height, and the sign never flips back.
    // A moving average of N samples returns a step exactly N samples after
    // the burst instead
    FilterDCBlocker f(kRate, kCutoff);
    const size_t burst = size_t(0.0003 * kRate);
    for (size_t n = 0; n < burst; n++)
        f.filter(1.0);

    double prev = f.filter(0.0);  // the falling edge itself
    ASSERT_LT(prev, 0.0) << "the edge swings below zero (charged capacitor)";
    std::vector<double> tail;
    for (size_t n = 0; n < size_t(0.05 * kRate); n++)
    {
        const double y = f.filter(0.0);
        ASSERT_LE(y, 0.0) << "sign flipped back at sample " << n;
        ASSERT_GE(y, prev) << "tail is not monotonic at sample " << n;
        tail.push_back(y);
        prev = y;
    }
    // Over any 0.5 ms span the tail barely moves; the moving average gave the
    // burst back as a 0.3 ms ramp of 1/16 of its height
    const size_t span = size_t(0.0005 * kRate);
    double maxChange = 0.0;
    for (size_t n = span; n < tail.size(); n++)
        maxChange = std::max(maxChange, std::fabs(tail[n] - tail[n - span]));
    EXPECT_LT(maxChange, 2e-3) << "a step after the burst (the old 1024-sample window artifact)";
}

TEST(FilterDCBlocker_Test, SilenceFlushesToExactZero)
{
    FilterDCBlocker f(kRate, kCutoff);
    f.filter(1.0);
    double y = 1.0;
    // ln(1e30) * tau * rate ~ 480k samples; allow margin
    for (int n = 0; n < 1'000'000 && y != 0.0; n++)
        y = f.filter(0.0);
    EXPECT_EQ(y, 0.0) << "the tail should flush to zero instead of running into denormals";
}

TEST(FilterDCBlocker_Test, ResetAndConfigure)
{
    FilterDCBlocker f(kRate, kCutoff);
    for (int n = 0; n < 100; n++)
        f.filter(0.5);
    f.reset();
    FilterDCBlocker fresh(kRate, kCutoff);
    EXPECT_EQ(f.filter(0.25), fresh.filter(0.25)) << "reset must return to the rest state";

    f.configure(48000.0, 20.0);
    EXPECT_EQ(f.sampleRate(), 48000.0);
    EXPECT_EQ(f.cutoffHz(), 20.0);
    const double rc = 1.0 / (2.0 * kPi * 20.0);
    EXPECT_NEAR(f.coefficient(), rc / (rc + 1.0 / 48000.0), 1e-15);

    // settle(x): as if the input had rested at x - the next sample at x
    // outputs 0, a change passes as is
    f.settle(0.4);
    EXPECT_EQ(f.filter(0.4), 0.0) << "settled at the input level: no output";
    EXPECT_NEAR(f.filter(0.5), f.coefficient() * 0.1, 1e-15) << "only the change passes";

    FilterDCBlocker unconfigured;
    EXPECT_EQ(unconfigured.filter(0.3), 0.3) << "unconfigured filter passes the input";
}
