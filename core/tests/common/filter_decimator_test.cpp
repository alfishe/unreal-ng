#include "stdafx.h"
#include "pch.h"

#include <cmath>
#include <vector>

#include "common/sound/filters/filter_decimator.h"

/// FilterDecimator TSFM extension tests (design §6.3 / §12.4): input-rate
/// scaling and slave-mode lockstep. The (44100, Reference) bit-identity
/// itself stays owned by FirDesigner_Test.ReproducesShippedDecimatorTable.

/// |H(f)| of a real coefficient vector, evaluated at physical frequency f
/// over its own input rate fs (direct DFT - the vectors are tiny)
static double MagnitudeAt(const std::vector<double>& h, double f, double fs)
{
    double re = 0.0;
    double im = 0.0;
    const double w0 = 2.0 * M_PI * f / fs;
    for (size_t k = 0; k < h.size(); k++)
    {
        const double w = w0 * static_cast<double>(k);
        re += h[k] * std::cos(w);
        im -= h[k] * std::sin(w);
    }
    return std::sqrt(re * re + im * im);
}

TEST(FilterDecimator_Test, InputRateEquivalence)
{
    // §6.3/§12.4: the FM decimator runs at 437.5 kHz (2x the SSG generator
    // rate). Doubling the input rate with doubled taps must keep the
    // response in Hz ~identical: same 20 kHz cutoff, same transition width.
    // A design that kept the tap count would double the transition width in
    // Hz; one that kept the normalized cutoff would halve it in Hz
    const FilterDecimator base;  // (44100, Reference): 96 taps at 218.75 kHz
    FilterDecimator doubled;
    doubled.configure(44100.0, FilterDecimator::Quality::Reference, false, 437500.0);

    ASSERT_EQ(base.taps(), 96u);
    ASSERT_EQ(doubled.taps(), 192u);
    ASSERT_DOUBLE_EQ(doubled.samplesPerOutput(), base.samplesPerOutput() * 2.0);

    const double baseRate = FilterDecimator::INPUT_RATE;
    const double doubledRate = 437500.0;

    // DC: both exactly unity (DC-normalized design)
    EXPECT_NEAR(MagnitudeAt(base.coefficients(), 0.0, baseRate), 1.0, 1e-12);
    EXPECT_NEAR(MagnitudeAt(doubled.coefficients(), 0.0, doubledRate), 1.0, 1e-12);

    // Passband up to 15 kHz: magnitude difference within 0.05 dB (§12.4)
    for (double f : {100.0, 1000.0, 5000.0, 10000.0, 15000.0})
    {
        const double m0 = MagnitudeAt(base.coefficients(), f, baseRate);
        const double m1 = MagnitudeAt(doubled.coefficients(), f, doubledRate);
        const double db = 20.0 * std::log10(m1 / m0);
        EXPECT_LT(std::abs(db), 0.05) << f << " Hz: scaled design deviates " << db << " dB";
    }

    // Stopband sanity for the scaled design (below -46 dB at 40..100 kHz -
    // comfortably above the transition band, well under the beta=5 floor)
    for (double f : {40000.0, 60000.0, 100000.0})
    {
        EXPECT_LT(MagnitudeAt(doubled.coefficients(), f, doubledRate), 0.005)
            << f << " Hz: scaled design leaks stopband energy";
    }
}

TEST(FilterDecimator_Test, SlaveLockstep)
{
    // §6.3/§12.4: a slave decimator produces output exactly when its master
    // does - never earlier, never later, for a million input ticks. The
    // master is fed once per tick, the 2x-rate slave twice (the §6.2
    // interleaving), mirroring the TSFM render loop's cadence
    FilterDecimator master;  // (44100, Reference) at 218.75 kHz
    FilterDecimator slave;
    slave.configure(44100.0, FilterDecimator::Quality::Reference, false, 437500.0);
    slave.attachMaster(&master);

    uint64_t masterOutputs = 0;
    uint64_t slaveOutputs = 0;

    for (uint64_t i = 0; i < 1'000'000; i++)
    {
        const double phase = static_cast<double>(i % 448) / 448.0;
        master.feedSample(phase);
        slave.feedSample(phase);
        slave.feedSample(phase);

        if (master.hasOutput())
        {
            // The slave must answer BEFORE the master's own getOutput()
            // consumes its phase (hasOutput delegates to the master - the
            // render loop asks everyone before anyone consumes)
            EXPECT_TRUE(slave.hasOutput()) << "slave must output with its master at tick " << i;
            (void)master.getOutput();
            (void)slave.getOutput();
            masterOutputs++;
            slaveOutputs++;
        }
    }

    // ~1M / 4.9604 = 201 599 outputs expected; the equality is the contract
    EXPECT_EQ(masterOutputs, slaveOutputs) << "slave drifted out of lockstep";
    EXPECT_GT(masterOutputs, 201'000u) << "master produced implausibly few outputs";
    EXPECT_LT(masterOutputs, 202'000u) << "master produced implausibly many outputs";

    // configure() clears the master wiring: the redesigned filter runs on
    // its own phase again (freshly reset -> no output)
    slave.configure(48000.0, FilterDecimator::Quality::Reference, false, 437500.0);
    EXPECT_FALSE(slave.hasOutput()) << "configure() must detach the slave";
    slave.feedSample(0.5);
    EXPECT_FALSE(slave.hasOutput()) << "a standalone filter needs its full phase, not the master's";
}
