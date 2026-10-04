// MultiSoundBoard / MultiSoundRcFilter (core/src/emulator/slots/cards/multisound/multisoundanalog): the weights derived
// from the rev.A2 component values against the figures of hardware-reference.md §4.5, the filter corners, and the
// bilinear filter itself (measured by running a sine through it, not only from its formula).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "emulator/slots/cards/multisound/multisoundanalog.h"

namespace
{
constexpr double kPi = 3.14159265358979323846;

/// Steady-state peak of a sine at hz through the filter (the first half of the run settles the network)
double SinePeak(MultiSoundRcFilter filter, double hz, double fs)
{
    const int samples = static_cast<int>(fs * 0.05);
    double peak = 0.0;
    for (int i = 0; i < samples; i++)
    {
        const double y = filter.Process(std::sin(2.0 * kPi * hz * i / fs));
        if (i >= samples / 2)
            peak = std::max(peak, std::fabs(y));
    }
    return peak;
}
} // namespace

TEST(MultiSoundAnalog_Test, WeightsMatchTheSchematicTable)
{
    // hardware-reference.md §4.5, three decimals
    EXPECT_NEAR(MultiSoundBoard::kWeightFm, 1.000, 0.0005);
    EXPECT_NEAR(MultiSoundBoard::kWeightSsgSide, 0.417, 0.0005);
    EXPECT_NEAR(MultiSoundBoard::kWeightSsgCenter, 0.213, 0.0005);
    EXPECT_NEAR(MultiSoundBoard::kWeightMidi, 1.000, 0.0005);
    EXPECT_NEAR(MultiSoundBoard::kWeightDac, 0.208, 0.0005);
    EXPECT_NEAR(MultiSoundBoard::kWeightExternal, 0.417, 0.0005);
    // Pass band of the SAA network (the reference's 0.825 is its 1 kHz value, checked below)
    EXPECT_NEAR(MultiSoundBoard::kWeightSaa, 10.0 / 12.0, 1e-12);
}

TEST(MultiSoundAnalog_Test, Corners)
{
    EXPECT_NEAR(MultiSoundBoard::DacCornerHz(), 16254.1, 0.5);     // "about 16.3 kHz"
    EXPECT_NEAR(MultiSoundBoard::SaaCornerHz(), 7018.7, 0.5);      // "about 7.2 kHz" in the reference (computed 7.02)
    EXPECT_NEAR(MultiSoundBoard::CouplingCornerHz(10e3), 1.59, 0.01);
}

TEST(MultiSoundAnalog_Test, DacLowPassCornerIsExactAtEveryRate)
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        const MultiSoundRcFilter filter = MultiSoundRcFilter::LowPass1(MultiSoundBoard::DacCornerHz(), fs);
        EXPECT_NEAR(filter.Magnitude(MultiSoundBoard::DacCornerHz(), fs), 1.0 / std::sqrt(2.0), 1e-9) << fs;
        EXPECT_NEAR(filter.Magnitude(0.0, fs), 1.0, 1e-12) << fs;
        // Measured with a sine, not only from the formula
        EXPECT_NEAR(SinePeak(filter, MultiSoundBoard::DacCornerHz(), fs), 1.0 / std::sqrt(2.0), 0.01) << fs;
        EXPECT_NEAR(SinePeak(filter, 1000.0, fs), 1.0, 0.01) << fs;
    }
}

TEST(MultiSoundAnalog_Test, SaaLadderSweep)
{
    constexpr double fs = 96000.0;
    double b[3];
    double a[3];
    MultiSoundBoard::SaaLadder(b, a);
    MultiSoundRcFilter filter;
    filter.Design(b, a, MultiSoundBoard::SaaCornerHz(), fs);

    EXPECT_NEAR(filter.Magnitude(MultiSoundBoard::SaaCornerHz(), fs), 1.0 / std::sqrt(2.0), 1e-9);
    // The reference's mid-band gain 0.825 is the network at 1 kHz
    EXPECT_NEAR(SinePeak(filter, 1000.0, fs) * MultiSoundBoard::kWeightSaa, 0.825, 0.002);
    // Second order: below the corner it falls slowly, above it about 12 dB per octave
    EXPECT_GT(SinePeak(filter, 3500.0, fs), 0.88);
    EXPECT_LT(SinePeak(filter, 28000.0, fs), 0.15);
    // -10.3 dB at 20 kHz for the analog network; the bilinear transform compresses the band above the prewarp
    // point, so the digital version falls a little faster there (-11.6 dB at 96 kHz)
    const double at20k = 20.0 * std::log10(filter.Magnitude(20000.0, fs));
    EXPECT_LT(at20k, -10.0);
    EXPECT_GT(at20k, -12.5);
}

TEST(MultiSoundAnalog_Test, CouplingHighPassRemovesDc)
{
    constexpr double fs = 44100.0;
    MultiSoundRcFilter filter = MultiSoundRcFilter::HighPass1(MultiSoundBoard::CouplingCornerHz(5e3), fs);
    double y = filter.Process(1.0);
    EXPECT_NEAR(y, 1.0, 0.001);                   // a step passes
    for (int i = 0; i < static_cast<int>(fs); i++)
        y = filter.Process(1.0);
    EXPECT_LT(std::fabs(y), 1e-6);                // and decays (3.2 Hz corner, one second)
    EXPECT_NEAR(filter.Magnitude(1000.0, fs), 1.0, 1e-4);
}

TEST(MultiSoundAnalog_Test, PrewarpClampsAboveNyquist)
{
    // 16.25 kHz at 32 kHz is above Nyquist: the design stays stable and passes the audio band
    const MultiSoundRcFilter filter = MultiSoundRcFilter::LowPass1(MultiSoundBoard::DacCornerHz(), 32000.0);
    EXPECT_NEAR(filter.Magnitude(0.0, 32000.0), 1.0, 1e-12);
    EXPECT_GT(filter.Magnitude(1000.0, 32000.0), 0.99);
    EXPECT_NEAR(SinePeak(filter, 1000.0, 32000.0), filter.Magnitude(1000.0, 32000.0), 0.01);
}
