// MSVC: Must be defined BEFORE any headers that might include <cmath> for M_PI
#define _USE_MATH_DEFINES
#include "masterlimiter_test.h"

#include <common/sound/filters/masterlimiter.h>

#include <cmath>
#include <vector>

/// region <Transfer curve (memoryless)>

TEST_F(MasterLimiter_Test, LinearBelowKneeIsIdentity)
{
    const float values[] = {0.0f, 1.0f, -1.0f, 0.5f, -12345.0f, 20000.0f,
                            -MasterLimiter::KNEE_LINEAR, MasterLimiter::KNEE_LINEAR};
    for (float v : values)
    {
        EXPECT_FLOAT_EQ(v, MasterLimiter::LimitSample(v)) << "value " << v;
    }
}

TEST_F(MasterLimiter_Test, KneeIsContinuousWithUnitSlope)
{
    // C0: no jump at the knee. The step must stay above one float ULP at
    // this magnitude (ULP of 24576 is 0.00195), hence 1.0.
    const float at = MasterLimiter::LimitSample(MasterLimiter::KNEE_LINEAR);
    const float justAbove = MasterLimiter::LimitSample(MasterLimiter::KNEE_LINEAR + 1.0f);
    EXPECT_NEAR(at, MasterLimiter::KNEE_LINEAR, 1e-3f);
    EXPECT_NEAR(justAbove - at, 1.0f, 2e-4f); // slope 1 at the knee, minus O(over^2)

    // C1: local slope approaches 1 for a small step above the knee
    const float d = 0.5f;
    const float y0 = MasterLimiter::LimitSample(MasterLimiter::KNEE_LINEAR + 10.0f);
    const float y1 = MasterLimiter::LimitSample(MasterLimiter::KNEE_LINEAR + 10.0f + d);
    EXPECT_NEAR((y1 - y0) / d, 1.0f, 1e-4f);
}

TEST_F(MasterLimiter_Test, MonotonicAndBelowCeiling)
{
    float prev = -1.0f;
    for (double x = 0.0; x <= 1000000.0; x += 997.0)
    {
        // Never above the ceiling; equality is reachable for huge inputs
        // where the exponential tail rounds to the span (float resolution).
        const float y = std::fabs(MasterLimiter::LimitSample(static_cast<float>(x)));
        EXPECT_LE(y, MasterLimiter::CEILING) << "input " << x;
        EXPECT_LT(y, 32767.0f) << "input " << x; // still below hard full scale
        EXPECT_GE(y, prev - 1e-6f) << "non-monotonic at " << x; // non-decreasing magnitude
        prev = y;
    }
}

TEST_F(MasterLimiter_Test, ExtremeInputsStayFiniteAndBounded)
{
    const float extremes[] = {8.0f * 32768.0f, 1e9f, -1e9f, 1e18f, -1e18f};
    for (float v : extremes)
    {
        const float y = MasterLimiter::LimitSample(v);
        EXPECT_TRUE(std::isfinite(y));
        EXPECT_LE(std::fabs(y), MasterLimiter::CEILING);
        EXPECT_GT(std::fabs(y), MasterLimiter::KNEE_LINEAR);
    }
}

TEST_F(MasterLimiter_Test, CurveIsOddSymmetric)
{
    for (double x = MasterLimiter::KNEE_LINEAR; x < 200000.0; x += 321.0)
    {
        const float y = MasterLimiter::LimitSample(static_cast<float>(x));
        const float ny = MasterLimiter::LimitSample(static_cast<float>(-x));
        EXPECT_FLOAT_EQ(ny, -y) << "input " << x;
    }
}

/// endregion </Transfer curve (memoryless)>

/// region <DC blocker + Process>

TEST_F(MasterLimiter_Test, ConstantDcDecaysToZero)
{
    MasterLimiter limiter;
    limiter.Configure(44100.0);

    // First sample passes at full value (fresh state, below the knee)
    float buf[2] = {8000.0f, 8000.0f};
    limiter.Process(buf, 1);
    EXPECT_FLOAT_EQ(buf[0], 8000.0f);

    // After 100 ms of constant DC the residual must be deeply decayed:
    // y[n] = c * R^n with R the 5 Hz pole at 44100 -> ~4.3% at 4410 samples
    for (size_t i = 0; i < 4410; i++)
    {
        buf[0] = buf[1] = 8000.0f;
        limiter.Process(buf, 1);
    }
    EXPECT_NEAR(buf[0], 8000.0f * 0.0432f, 15.0f);
    EXPECT_LT(std::fabs(buf[0]), 400.0f);
}

TEST_F(MasterLimiter_Test, AudioBandPassesEssentiallyUnchanged)
{
    MasterLimiter limiter;
    limiter.Configure(44100.0);

    // 440 Hz at 0.61 FS: below the knee, far above the 5 Hz corner. The
    // buffer must outlast the 5 Hz natural mode (~100 ms settle) before the
    // envelope is meaningful.
    constexpr size_t frames = 16384;
    std::vector<float> buf(frames * 2);
    for (size_t i = 0; i < frames; i++)
    {
        const double t = 2.0 * M_PI * 440.0 * static_cast<double>(i) / 44100.0;
        buf[i * 2] = static_cast<float>(20000.0 * std::sin(t));
        buf[i * 2 + 1] = buf[i * 2];
    }
    limiter.Process(buf.data(), frames);
    // Compare envelopes, not samples: any highpass adds a phase lead
    // (~0.2 samples at 440 Hz for this pole) so per-sample equality is not
    // the right property. Level must pass essentially unchanged.
    float peakOut = 0.0f;
    for (size_t i = frames - 2048; i < frames; i++) // settled tail only
        peakOut = std::max(peakOut, std::fabs(buf[i * 2]));
    EXPECT_NEAR(peakOut, 20000.0f, 100.0f); // +-0.5% of FS
}

TEST_F(MasterLimiter_Test, DcDecayTimeScalesWithRate)
{
    // Rate compensation: the ~5 Hz cutoff must stay ~5 Hz. Decay to 1/e takes
    // ~fs/(2*pi*5) samples, so the 96 kHz sample count is 96000/44100x the
    // 44.1 kHz one.
    auto samplesToDecayToInvE = [](double rate)
    {
        MasterLimiter limiter;
        limiter.Configure(rate);
        float buf[2] = {8000.0f, 8000.0f};
        size_t n = 0;
        const float threshold = 8000.0f * std::exp(-1.0);
        while (n < 100000)
        {
            limiter.Process(buf, 1);
            n++;
            if (std::fabs(buf[0]) < threshold)
                break;
            buf[0] = buf[1] = 8000.0f;
        }
        return n;
    };

    const size_t n44 = samplesToDecayToInvE(44100.0);
    const size_t n96 = samplesToDecayToInvE(96000.0);
    EXPECT_NEAR(static_cast<double>(n96) / static_cast<double>(n44), 96000.0 / 44100.0, 0.03);
}

TEST_F(MasterLimiter_Test, ResetRestoresFreshBehaviour)
{
    MasterLimiter used;
    used.Configure(44100.0);
    float junk[2] = {30000.0f, -30000.0f};
    for (size_t i = 0; i < 1000; i++)
        used.Process(junk, 1);

    used.Reset();

    MasterLimiter fresh;
    fresh.Configure(44100.0);

    float a[2] = {1000.0f, -2000.0f};
    float b[2] = {1000.0f, -2000.0f};
    for (size_t i = 0; i < 100; i++)
    {
        used.Process(a, 1);
        fresh.Process(b, 1);
        ASSERT_FLOAT_EQ(a[0], b[0]);
        ASSERT_FLOAT_EQ(a[1], b[1]);
    }
}

TEST_F(MasterLimiter_Test, StereoChannelsAreIndependent)
{
    MasterLimiter limiter;
    limiter.Configure(44100.0);

    for (size_t i = 0; i < 500; i++)
    {
        float buf[2] = {8000.0f, 0.0f}; // DC left, silence right
        limiter.Process(buf, 1);
        EXPECT_FLOAT_EQ(buf[1], 0.0f);
    }
}

/// endregion </DC blocker + Process>
