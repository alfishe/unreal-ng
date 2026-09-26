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

TEST(FilterDecimator_Test, AllSupportedCoreRates)
{
    // The output stage designs its filters for whichever core rate is
    // configured - SoundManager's full set 44.1 k .. 192 k, never one pinned
    // frequency. Every (rate x input side) combination must yield a legal
    // design: exact tap scaling, a decimating ratio, unity DC and a stopband
    // in the alias-relevant band above the output Nyquist
    const double rates[] = {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0};
    const double inputRates[] = {FilterDecimator::INPUT_RATE, 437500.0};  // SSG / FM side

    for (double inputRate : inputRates)
    {
        for (double rate : rates)
        {
            SCOPED_TRACE(testing::Message() << "input " << inputRate << " -> " << rate);
            FilterDecimator decimator;
            decimator.configure(rate, FilterDecimator::Quality::Reference, false, inputRate);

            // Taps scale with the input side (96 at 218.75 kHz, 192 at 437.5 kHz)
            EXPECT_EQ(decimator.taps(), size_t(96 * (inputRate > FilterDecimator::INPUT_RATE ? 2 : 1)));
            EXPECT_LE(decimator.taps(), FilterDecimator::MAX_TAPS);
            EXPECT_NEAR(decimator.samplesPerOutput(), inputRate / rate, 1e-9);
            EXPECT_GT(decimator.samplesPerOutput(), 1.0) << "decimation degenerated to upsampling";

            const auto& h = decimator.coefficients();
            EXPECT_NEAR(MagnitudeAt(h, 0.0, inputRate), 1.0, 1e-12) << "DC unity broken";

            // Deep stopband midway between the output and input Nyquists (the
            // band that would alias); fc is 20 kHz with a ~8 kHz transition,
            // so this point is far past it at every combination
            const double aliasProbe = (rate / 2.0 + inputRate / 2.0) / 2.0;
            EXPECT_LT(MagnitudeAt(h, aliasProbe, inputRate), 0.01)
                << aliasProbe << " Hz aliases into the passband at " << rate << " Hz output";
        }
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

/// region <Exact output instants>

namespace
{
/// SNR (dB) of a decimated sine against the ideal sine at each output's exact
/// instant: newest input index - residue (the output's fractional position)
/// - the filter's group delay. The passband gain is fitted out, so only
/// timing error, aliasing and ripple count
double SineSnr(const std::vector<double>& out, const std::vector<double>& instants, double omega)
{
    double num = 0.0;
    double den = 0.0;
    for (size_t i = 0; i < out.size(); i++)
    {
        const double ideal = std::sin(omega * instants[i]);
        num += out[i] * ideal;
        den += ideal * ideal;
    }
    const double gain = num / den;
    double signal = 0.0;
    double error = 0.0;
    for (size_t i = 0; i < out.size(); i++)
    {
        const double ideal = gain * std::sin(omega * instants[i]);
        signal += ideal * ideal;
        error += (out[i] - ideal) * (out[i] - ideal);
    }
    return 10.0 * std::log10(signal / error);
}

double StandaloneSnr(double toneHz, double outputRate, FilterDecimator::Quality quality)
{
    FilterDecimator d;
    d.configure(outputRate, quality);
    const double omega = 2.0 * M_PI * toneHz / FilterDecimator::INPUT_RATE;
    const double groupDelay = (double(d.taps()) - 1.0) / 2.0;
    std::vector<double> out;
    std::vector<double> instants;
    const size_t settle = d.taps() + 8;
    for (size_t k = 0; k < size_t(0.2 * FilterDecimator::INPUT_RATE); k++)
    {
        d.feedSample(std::sin(omega * double(k)));
        if (d.hasOutput())
        {
            const double y = d.getOutput();
            if (k < settle)
                continue;
            out.push_back(y);
            instants.push_back(double(k) - d.phase() - groupDelay);
        }
    }
    return SineSnr(out, instants, omega);
}
}  // namespace

TEST(FilterDecimator_Test, ExactOutputInstants)
{
    // An output falls due between two input samples; evaluating the filter
    // at the newest sample put it up to one input sample (4.57 us) late - a
    // jitter that held a 1 kHz tone to ~35 dB SNR and 10 kHz to ~15 dB with
    // the 96-tap Reference design. At the exact instants the same design
    // reaches ~79 dB (bounded by its passband ripple and stopband)
    for (const double rate : {44100.0, 48000.0, 96000.0})
    {
        for (const double tone : {1000.0, 10000.0})
        {
            SCOPED_TRACE(testing::Message() << "rate " << rate << ", tone " << tone);
            EXPECT_GT(StandaloneSnr(tone, rate, FilterDecimator::Quality::Reference), 75.0);
        }
    }
}

TEST(FilterDecimator_Test, ExactOutputInstantsHighFidelity)
{
    // The 192-tap beta 9 design only pays off at exact instants (~121 dB
    // there; timing jitter capped it at the Reference figure before)
    EXPECT_GT(StandaloneSnr(1000.0, 44100.0, FilterDecimator::Quality::HighFidelity), 110.0);
    EXPECT_GT(StandaloneSnr(10000.0, 44100.0, FilterDecimator::Quality::HighFidelity), 100.0);
}

TEST(FilterDecimator_Test, SlaveUsesItsMastersInstant)
{
    // TSFM order: FM half-tick, SSG tick, FM half-tick. The slave (2x input
    // rate) takes the output instant from its master, scaled to its own rate:
    // 2 x the master's residue back from its newest sample
    FilterDecimator master;
    FilterDecimator slave;
    slave.configure(44100.0, FilterDecimator::Quality::Reference, false, 2.0 * FilterDecimator::INPUT_RATE);
    slave.attachMaster(&master);

    for (const double tone : {1000.0, 10000.0})
    {
        SCOPED_TRACE(testing::Message() << "tone " << tone);
        master.reset();
        slave.reset();
        const double omega = 2.0 * M_PI * tone / (2.0 * FilterDecimator::INPUT_RATE);  // per slave sample
        const double groupDelay = (double(slave.taps()) - 1.0) / 2.0;
        std::vector<double> out;
        std::vector<double> instants;
        size_t s = 0;  // slave samples fed
        for (size_t k = 0; k < size_t(0.2 * FilterDecimator::INPUT_RATE); k++)
        {
            slave.feedSample(std::sin(omega * double(s++)));
            master.feedSample(0.0);
            slave.feedSample(std::sin(omega * double(s++)));
            if (master.hasOutput())
            {
                (void)master.getOutput();
                const double y = slave.getOutput();
                if (s < slave.taps() + 16)
                    continue;
                out.push_back(y);
                instants.push_back(double(s - 1) - 2.0 * master.phase() - groupDelay);
            }
        }
        EXPECT_GT(SineSnr(out, instants, omega), 75.0);
    }
}

TEST(FilterDecimator_Test, IntegerRatioIsThePlainIntegerDesign)
{
    // At an integer ratio every output falls exactly on an input sample: the
    // output must be the integer design's dot product (row 0 of the phase
    // table is the design itself) - to rounding: whether the compiler fuses
    // a multiply-add may differ between the two loops
    FilterDecimator d;
    d.configure(FilterDecimator::INPUT_RATE / 4.0);
    ASSERT_DOUBLE_EQ(d.samplesPerOutput(), 4.0);
    const std::vector<double>& h = d.coefficients();
    std::vector<double> input;
    for (size_t k = 0; k < 4000; k++)
    {
        const double x = std::sin(0.013 * double(k)) + 0.25 * std::sin(0.71 * double(k));
        input.push_back(x);
        d.feedSample(x);
        if (d.hasOutput())
        {
            const double y = d.getOutput();
            ASSERT_EQ(d.phase(), 0.0);
            double expected = 0.0;
            for (size_t i = 0; i < h.size() && i <= k; i++)
                expected += input[k - i] * h[i];
            if (k >= h.size())
                ASSERT_NEAR(y, expected, 1e-14) << "output " << k;
        }
    }
}

/// endregion
