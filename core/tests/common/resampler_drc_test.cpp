#include "stdafx.h"
#include "pch.h"

#include <cmath>
#include <random>
#include <vector>

#include "common/sound/filters/resampler_drc.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/// DRC resampler tests (audio-sync design, Fix 2 stage).

namespace
{
constexpr size_t FRAME = 903;  // Pentagon-sized input frames

std::vector<int16_t> makeSine(size_t frames, double freq, double rate, double& phase)
{
    std::vector<int16_t> buf(frames * 2);
    for (size_t i = 0; i < frames; i++)
    {
        int16_t v = static_cast<int16_t>(20000.0 * std::sin(phase));
        buf[i * 2] = v;
        buf[i * 2 + 1] = v;
        phase += 2.0 * M_PI * freq / rate;
    }
    return buf;
}
}  // namespace

TEST(ResamplerDRC_Test, UnityBypass_BitExact)
{
    ResamplerDRC rs;
    rs.reset();
    rs.setRatio(1.0);
    ASSERT_TRUE(rs.isBypass());

    std::mt19937 rng(42);
    std::vector<int16_t> in(FRAME * 2), out(FRAME * 2 + 64);

    for (int frame = 0; frame < 50; frame++)
    {
        for (auto& s : in)
            s = static_cast<int16_t>(rng());

        size_t produced = rs.process(in.data(), FRAME, out.data(), FRAME + 32);
        ASSERT_EQ(produced, FRAME) << "Bypass must be 1:1";
        ASSERT_EQ(memcmp(out.data(), in.data(), FRAME * 2 * sizeof(int16_t)), 0)
            << "Bypass must be bit-exact (frame " << frame << ")";
    }
}

TEST(ResamplerDRC_Test, RatioProducesProportionalOutput)
{
    // 48000/44100 conversion: cumulative output count tracks input * ratio
    ResamplerDRC rs;
    rs.reset();
    const double ratio = 48000.0 / 44100.0;
    rs.setRatio(ratio);
    ASSERT_FALSE(rs.isBypass());

    double phase = 0.0;
    size_t totalIn = 0, totalOut = 0;
    std::vector<int16_t> out(2048 * 2);

    for (int frame = 0; frame < 200; frame++)
    {
        auto in = makeSine(FRAME, 440.0, 44100.0, phase);
        totalOut += rs.process(in.data(), FRAME, out.data(), 2048);
        totalIn += FRAME;
    }

    double expected = totalIn * ratio;
    EXPECT_NEAR(static_cast<double>(totalOut), expected, 6.0)
        << "Cumulative output must track input x ratio (fractional phase carried)";
}

TEST(ResamplerDRC_Test, ContinuityAcrossFrameBoundaries)
{
    // A 440 Hz sine through a small trim: consecutive output samples must
    // never jump more than the sine's own maximum slope. A phase/history
    // reset at frame boundaries shows up as a near-full-amplitude step.
    ResamplerDRC rs;
    rs.reset();
    rs.setRatio(1.0005);  // Within DRC trim range

    double phase = 0.0;
    std::vector<int16_t> out(2048 * 2);
    int16_t prev = 0;
    bool first = true;
    double maxDelta = 0.0;

    for (int frame = 0; frame < 300; frame++)
    {
        auto in = makeSine(FRAME, 440.0, 44100.0, phase);
        size_t produced = rs.process(in.data(), FRAME, out.data(), 2048);

        for (size_t i = 0; i < produced; i++)
        {
            int16_t v = out[i * 2];
            if (!first)
                maxDelta = std::max(maxDelta, std::abs(static_cast<double>(v) - prev));
            prev = v;
            first = false;
        }
    }

    // Theoretical max per-sample delta of a 440 Hz / 20000-amplitude sine
    // at 44.1 kHz: A * 2*pi*f/fs = 1254. Allow 30% margin for interpolation.
    EXPECT_LT(maxDelta, 1254.0 * 1.3)
        << "Discontinuity detected - phase/history must persist across frames";
}

TEST(ResamplerDRC_Test, BypassTransition_NoSampleLoss)
{
    // Engaging and disengaging DRC must not lose the carried tail
    ResamplerDRC rs;
    rs.reset();
    rs.setRatio(1.001);

    double phase = 0.0;
    std::vector<int16_t> out(2048 * 2);
    size_t totalIn = 0, totalOut = 0;

    for (int frame = 0; frame < 20; frame++)
    {
        auto in = makeSine(FRAME, 440.0, 44100.0, phase);
        totalOut += rs.process(in.data(), FRAME, out.data(), 2048);
        totalIn += FRAME;
    }

    rs.setRatio(1.0);  // Back to bypass: carried samples must flush
    for (int frame = 0; frame < 20; frame++)
    {
        auto in = makeSine(FRAME, 440.0, 44100.0, phase);
        totalOut += rs.process(in.data(), FRAME, out.data(), 2048);
        totalIn += FRAME;
    }

    // Everything must come out at each period's ratio: first half produced
    // ratio x input, bypass half is 1:1 (within the interpolator's 3-frame
    // carry plus the one-sample engage edge)
    double expected = (totalIn / 2.0) * 1.001 + (totalIn / 2.0) * 1.0;
    EXPECT_NEAR(static_cast<double>(totalOut), expected, 6.0);
}
