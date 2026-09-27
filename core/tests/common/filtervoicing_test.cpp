#include "stdafx.h"
#include "pch.h"

#include <cmath>
#include <complex>
#include <vector>

#include <gtest/gtest.h>

#include "common/sound/filters/filter_dc.h"
#include "common/sound/filters/filterdcblocker.h"
#include "common/sound/filters/filtervoicing.h"

/// FilterVoicing: the fixed AY tone-voicing EQ (profile table, biquad cascade).
/// Pinned: Flat is an exact bypass; Classic matches its design curve at every
/// core rate; Classic tracks the old moving-average FilterDC it recreates
/// (per 1/3-octave band on square waves, infrasonic "volume-step" energy,
/// peak level); every profile vs its design curve at every core rate;
/// parsing (aliases, case); state semantics.
/// Design: docs/inprogress/2026-09-25-ay-tone-voicing/ay-tone-voicing-tdd.md §9.1

namespace
{
using Preset = FilterVoicing::Preset;

constexpr double kPi = 3.14159265358979323846;
constexpr double kGeneratorRate = 218750.0;  // AY generator rate: old FilterDC and the 5 Hz coupling run here
constexpr double kCouplingHz = 5.0;          // SoundChip_AY8910::OUTPUT_HIGHPASS_HZ
constexpr double kCoreRates[] = {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0};

double ToDb(double gain)
{
    return 20.0 * std::log10(gain);
}

/// Steady-state gain of a single-channel filter step for a sine at @p hz:
/// run @p settleSeconds, then fit the amplitude over whole periods by correlation
template <typename Step>
double MeasureGain(Step&& step, double rate, double hz, double settleSeconds)
{
    const double w = 2.0 * kPi * hz / rate;
    const size_t settle = static_cast<size_t>(settleSeconds * rate);
    const size_t periods = std::max<size_t>(2, static_cast<size_t>(hz * 0.02));
    const size_t measure = static_cast<size_t>(std::round(static_cast<double>(periods) * rate / hz));
    double sc = 0.0, cc = 0.0;
    for (size_t n = 0; n < settle + measure; n++)
    {
        const double x = std::sin(w * static_cast<double>(n));
        const double y = step(x);
        if (n >= settle)
        {
            sc += y * std::sin(w * static_cast<double>(n));
            cc += y * std::cos(w * static_cast<double>(n));
        }
    }
    return 2.0 * std::sqrt(sc * sc + cc * cc) / static_cast<double>(measure);
}

double ClassicGain(double rate, double hz)
{
    FilterVoicing f(rate, Preset::Classic);
    return MeasureGain([&](double x) { return f.process(0, x); }, rate, hz, 0.05);
}

/// Analog prototype of the Classic curve: s/(s+wc) * RBJ peak (s in Hz units)
std::complex<double> ClassicDesign(double hz)
{
    const std::complex<double> s(0.0, hz);
    const double a = std::pow(10.0, 3.06 / 40.0);
    const double w0 = 106.9;
    const double q = 1.0;
    const std::complex<double> hpf = s / (s + 64.2);
    const std::complex<double> peak = (s * s + s * (w0 * a / q) + w0 * w0) / (s * s + s * (w0 / (a * q)) + w0 * w0);
    return hpf * peak;
}

/// The old FilterDC response: 1 - (1024-tap boxcar) at the generator rate
std::complex<double> LegacyResponse(double hz)
{
    const double w = 2.0 * kPi * hz / kGeneratorRate;
    std::complex<double> ma(0.0, 0.0);
    for (size_t k = 0; k < FilterDC<double>::DC_FILTER_BUFFER_SIZE; k++)
        ma += std::polar(1.0, -w * static_cast<double>(k));
    ma /= static_cast<double>(FilterDC<double>::DC_FILTER_BUFFER_SIZE);
    return 1.0 - ma;
}

/// Minimal RBJ low-pass biquad for the energy split (test-side tool)
struct LowPass
{
    double b0, b1, b2, a1, a2, s1 = 0.0, s2 = 0.0;
    LowPass(double rate, double hz, double q)
    {
        const double w0 = 2.0 * kPi * hz / rate;
        const double c = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 - c) / 2.0 / a0;
        b1 = (1.0 - c) / a0;
        b2 = b0;
        a1 = -2.0 * c / a0;
        a2 = (1.0 - alpha) / a0;
    }
    double process(double x)
    {
        const double y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }
};

/// AY-like unipolar square (0..amplitude) whose level steps down the AY log
/// DAC (1.5 dB per step) every 20 ms, cycling - the volume "barrels" of
/// music drivers that write the level once per frame
double SteppedSquare(size_t n, double rate, double hz, double amplitude)
{
    const size_t frame = static_cast<size_t>(rate * 0.02);
    const int level = 15 - static_cast<int>((n / frame) % 8) * 2;
    const double gain = std::pow(10.0, (level - 15) * 1.5 / 20.0);
    const double phase = std::fmod(static_cast<double>(n) * hz / rate, 1.0);
    return (phase < 0.5 ? amplitude : 0.0) * gain;
}

double UnipolarSquare(size_t n, double rate, double hz, double amplitude)
{
    const double phase = std::fmod(static_cast<double>(n) * hz / rate, 1.0);
    return phase < 0.5 ? amplitude : 0.0;
}
}  // namespace

TEST(FilterVoicing_Test, FlatIsExactBypass)
{
    FilterVoicing f(44100.0, Preset::Flat);
    ASSERT_TRUE(f.isBypass());

    std::vector<int16_t> buffer(2 * 512);
    for (size_t i = 0; i < buffer.size(); i++)
        buffer[i] = static_cast<int16_t>((i * 7919) % 65536 - 32768);
    const std::vector<int16_t> original = buffer;

    f.processInt16(buffer.data(), buffer.size() / 2);
    EXPECT_EQ(buffer, original);
}

TEST(FilterVoicing_Test, ClassicMatchesDesignCurve)
{
    // The implementation against its own design (analog prototype): catches
    // coefficient and sign errors - a flipped a1 turns the high-pass into a
    // low-pass. Comparison with the old filter is ClassicTracksLegacyFilterDC
    for (double rate : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        for (double hz : {30.0, 41.2, 55.0, 100.0, 130.0, 160.0, 300.0, 1000.0})
        {
            const double measured = ToDb(ClassicGain(rate, hz));
            const double design = ToDb(std::abs(ClassicDesign(hz)));
            EXPECT_NEAR(measured, design, 0.1) << rate << " Hz core rate, " << hz << " Hz tone";
        }
    }
}

/// Analog prototype of any profile, built from its table row: HPF (1st order,
/// or 2nd order with Q), RBJ peaking section, 2nd-order LPF with Q - the curve
/// the digital sections must reproduce at every core rate
static double DesignDb(const FilterVoicing::Profile& p, double hz)
{
    std::complex<double> h(1.0, 0.0);
    if (p.hpfOrder == 1)
    {
        const std::complex<double> s(0.0, hz / p.hpfHz);
        h *= s / (s + 1.0);
    }
    else if (p.hpfOrder == 2)
    {
        const std::complex<double> s(0.0, hz / p.hpfHz);
        h *= (s * s) / (s * s + s / p.hpfQ + 1.0);
    }
    if (p.peakHz > 0.0 && p.peakDb != 0.0)
    {
        const double a = std::pow(10.0, p.peakDb / 40.0);
        const std::complex<double> s(0.0, hz / p.peakHz);
        h *= (s * s + s * (a / p.peakQ) + 1.0) / (s * s + s / (a * p.peakQ) + 1.0);
    }
    if (p.lpfHz > 0.0)
    {
        const std::complex<double> s(0.0, hz / p.lpfHz);
        h *= 1.0 / (s * s + s / p.lpfQ + 1.0);
    }
    return ToDb(std::abs(h));
}

TEST(FilterVoicing_Test, EveryPresetMatchesDesignCurveAtEveryCoreRate)
{
    // Every profile at every supported core rate, 44.1 to 192 kHz, must give
    // the same curve. The low-pass is magnitude-matched: a bilinear 6 kHz
    // low-pass would cut 15 kHz ~8 dB more at 44.1 kHz than at 192 kHz
    for (size_t i = 0; i < FilterVoicing::PRESET_COUNT; i++)
    {
        const auto preset = static_cast<Preset>(i);
        const FilterVoicing::Profile& profile = FilterVoicing::profile(preset);
        for (double rate : kCoreRates)
        {
            FilterVoicing f(rate, preset);
            EXPECT_EQ(f.isBypass(), preset == Preset::Flat) << profile.id;
            for (double hz : {40.0, 80.0, 130.0, 250.0, 1000.0, 1500.0, 3000.0, 4500.0, 6000.0, 10000.0, 15000.0})
            {
                f.reset();
                const double measured =
                    ToDb(MeasureGain([&](double x) { return f.process(0, x); }, rate, hz, 0.05));
                const double tolerance = hz <= 6000.0 ? 0.15 : 0.5;
                EXPECT_NEAR(measured, DesignDb(profile, hz), tolerance)
                    << profile.id << ", " << rate << " Hz core rate, " << hz << " Hz tone";
            }
        }
    }
}

TEST(FilterVoicing_Test, RateIndependentResponse)
{
    for (double hz : {41.2, 106.9, 500.0})
    {
        const double reference = ToDb(ClassicGain(44100.0, hz));
        for (double rate : kCoreRates)
            EXPECT_NEAR(ToDb(ClassicGain(rate, hz)), reference, 0.05) << rate << " Hz, " << hz << " Hz tone";
    }
}

TEST(FilterVoicing_Test, ClassicTracksLegacyFilterDC)
{
    // Old path: FilterDC<double> at 218.75 kHz. New path: FilterDCBlocker 5 Hz
    // at 218.75 kHz, then Classic at a 44.1 kHz core rate. Gains measured on
    // the real filters at every odd harmonic of a square wave (the only
    // components it has), then summed per 1/3-octave band that holds one.
    // Tolerance: +-0.75 dB below 200 Hz, +-1.5 dB 200 Hz - 1 kHz, where the old
    // filter's comb ripple (boxcar nulls every 213.6 Hz) lands on harmonics
    // and Classic deliberately does not copy it (worst: 0.62 / 1.29 dB)
    auto legacyGain = [](double hz) {
        FilterDC<double> f;
        return MeasureGain([&](double x) { return f.filter(x); }, kGeneratorRate, hz, 0.005);
    };
    auto newGain = [](double hz) {
        FilterDCBlocker coupling(kGeneratorRate, kCouplingHz);
        const double c = MeasureGain([&](double x) { return coupling.filter(x); }, kGeneratorRate, hz, 0.15);
        return c * ClassicGain(44100.0, hz);
    };

    for (double f0 : {41.2, 55.0, 110.0})
    {
        std::vector<std::pair<double, double>> harmonics;  // (frequency, amplitude 1/k)
        for (int k = 1; k * f0 < 1000.0 * std::pow(2.0, 1.0 / 6.0); k += 2)
            harmonics.emplace_back(k * f0, 1.0 / k);

        std::vector<double> gOld, gNew;
        for (const auto& h : harmonics)
        {
            gOld.push_back(legacyGain(h.first));
            gNew.push_back(newGain(h.first));
        }

        for (int band = 0; band < 19; band++)
        {
            const double center = 20.0 * std::pow(2.0, band / 3.0);
            const double lo = center / std::pow(2.0, 1.0 / 6.0);
            const double hi = center * std::pow(2.0, 1.0 / 6.0);
            double eOld = 0.0, eNew = 0.0;
            for (size_t i = 0; i < harmonics.size(); i++)
            {
                if (harmonics[i].first >= lo && harmonics[i].first < hi)
                {
                    eOld += std::pow(harmonics[i].second * gOld[i], 2.0);
                    eNew += std::pow(harmonics[i].second * gNew[i], 2.0);
                }
            }
            if (eOld == 0.0)
                continue;  // no harmonic in this band

            const double diff = 10.0 * std::log10(eNew / eOld);
            const double tolerance = center < 200.0 ? 0.75 : 1.5;
            EXPECT_LE(std::fabs(diff), tolerance) << f0 << " Hz square, band " << center << " Hz: " << diff << " dB";
        }
    }
}

TEST(FilterVoicing_Test, ClassicRestoresInfrasonicRatio)
{
    // Energy below 35 Hz relative to the rest, on a square with per-frame
    // volume steps (0.6 s at 218.75 kHz, first 0.2 s settling). Absolute
    // values depend on the signal (here ~-21.7 legacy / -6.8 flat / -22.1
    // classic); asserted relative: Classic within 1 dB of the old filter,
    // Flat (the coupling alone) at least 10 dB above it
    const size_t total = static_cast<size_t>(kGeneratorRate * 0.6);
    const size_t settle = static_cast<size_t>(kGeneratorRate * 0.2);

    auto ratioDb = [&](auto&& chain) {
        LowPass lp1(kGeneratorRate, 35.0, 0.5412);  // 4th-order Butterworth split at 35 Hz
        LowPass lp2(kGeneratorRate, 35.0, 1.3066);
        double eLow = 0.0, eTotal = 0.0;
        for (size_t n = 0; n < total; n++)
        {
            const double y = chain(SteppedSquare(n, kGeneratorRate, 110.0, 16384.0));
            const double low = lp2.process(lp1.process(y));
            if (n >= settle)
            {
                eLow += low * low;
                eTotal += y * y;
            }
        }
        return 10.0 * std::log10(eLow / (eTotal - eLow));
    };

    FilterDC<double> legacy;
    FilterDCBlocker flatCoupling(kGeneratorRate, kCouplingHz);
    FilterDCBlocker classicCoupling(kGeneratorRate, kCouplingHz);
    FilterVoicing classic(kGeneratorRate, Preset::Classic);

    const double legacyDb = ratioDb([&](double x) { return legacy.filter(x); });
    const double flatDb = ratioDb([&](double x) { return flatCoupling.filter(x); });
    const double classicDb = ratioDb([&](double x) { return classic.process(0, classicCoupling.filter(x)); });

    EXPECT_NEAR(classicDb, legacyDb, 1.0) << "classic " << classicDb << " dB vs legacy " << legacyDb << " dB";
    EXPECT_GE(flatDb - legacyDb, 10.0) << "flat " << flatDb << " dB vs legacy " << legacyDb << " dB";
}

TEST(FilterVoicing_Test, ClassicHeadroomNotWorseThanLegacy)
{
    // Classic brings back the old edge overshoot (the point of the profile);
    // its peak may exceed the old filter's by at most 0.25 dB (measured
    // ~0.12-0.13 dB at 41/55 Hz). Driven at -6 dBFS so nothing saturates
    for (double hz : {41.2, 55.0, 110.0})
    {
        FilterDC<double> legacy;
        FilterDCBlocker coupling(kGeneratorRate, kCouplingHz);
        FilterVoicing classic(kGeneratorRate, Preset::Classic);

        const size_t total = static_cast<size_t>(kGeneratorRate * 0.5);
        const size_t settle = static_cast<size_t>(kGeneratorRate * 0.3);
        double peakLegacy = 0.0, peakClassic = 0.0;
        for (size_t n = 0; n < total; n++)
        {
            const double x = UnipolarSquare(n, kGeneratorRate, hz, 16384.0);
            const double yl = legacy.filter(x);
            const double yc = classic.process(0, coupling.filter(x));
            if (n >= settle)
            {
                peakLegacy = std::max(peakLegacy, std::fabs(yl));
                peakClassic = std::max(peakClassic, std::fabs(yc));
            }
        }
        EXPECT_LE(ToDb(peakClassic / peakLegacy), 0.25) << hz << " Hz: classic " << peakClassic << ", legacy " << peakLegacy;
    }
}

TEST(FilterVoicing_Test, ReferenceFitsDocumented)
{
    // The analysis offered two simpler fits; Classic is the best of the three
    // against the old response (mean |dB error| on 40 log points, 20 Hz -
    // 1 kHz, unsmoothed: classic ~0.34, HPF2 39 Hz Q0.62 ~0.83, HPF1 55 Hz
    // ~0.99; the proposal's 0.39 / 0.99 / 1.24 are 1/3-octave smoothed)
    auto meanError = [](auto&& response) {
        double sum = 0.0;
        const int points = 40;
        for (int i = 0; i < points; i++)
        {
            const double hz = 20.0 * std::pow(50.0, i / double(points - 1));
            sum += std::fabs(ToDb(std::abs(response(hz))) - ToDb(std::abs(LegacyResponse(hz))));
        }
        return sum / points;
    };
    const double classic = meanError([](double hz) { return ClassicDesign(hz); });
    const double hpf2 = meanError([](double hz) {
        const std::complex<double> s(0.0, hz);
        return s * s / (s * s + s * (39.0 / 0.62) + 39.0 * 39.0);
    });
    const double hpf1 = meanError([](double hz) {
        const std::complex<double> s(0.0, hz);
        return s / (s + 55.0);
    });

    EXPECT_LE(classic, 0.4);
    EXPECT_LT(classic, hpf2);
    EXPECT_LT(hpf2, hpf1);
}

TEST(FilterVoicing_Test, DenormalFlushAfterSilence)
{
    FilterVoicing f(44100.0, Preset::Classic);
    for (int n = 0; n < 2000; n++)
        f.process(0, n < 100 ? 20000.0 : 0.0);
    EXPECT_GT(f.stateMagnitude(), 0.0);

    for (int n = 0; n < 44100 * 10; n++)
        f.process(0, 0.0);
    EXPECT_EQ(f.stateMagnitude(), 0.0) << "the decaying tail must flush to exact zero, not denormals";
}

TEST(FilterVoicing_Test, SetupKeepsStateSetPresetClears)
{
    FilterVoicing f(44100.0, Preset::Classic);
    for (int n = 0; n < 500; n++)
        f.process(0, 10000.0 * std::sin(n * 0.01));
    const double before = f.stateMagnitude();
    ASSERT_GT(before, 0.0);

    f.setup(48000.0);  // rate change: coefficients re-derived, state kept (no step)
    EXPECT_EQ(f.stateMagnitude(), before);

    f.setPreset(Preset::Classic);  // profile selection: clean start
    EXPECT_EQ(f.stateMagnitude(), 0.0);
}

TEST(FilterVoicing_Test, ParsePresetAcceptsLegacyAlias)
{
    Preset p = Preset::Flat;
    EXPECT_TRUE(FilterVoicing::parsePreset("legacy", p));
    EXPECT_EQ(p, Preset::Classic);
    EXPECT_TRUE(FilterVoicing::parsePreset("FLAT", p));
    EXPECT_EQ(p, Preset::Flat);
    EXPECT_TRUE(FilterVoicing::parsePreset("Classic", p));
    EXPECT_EQ(p, Preset::Classic);

    p = Preset::Classic;
    EXPECT_FALSE(FilterVoicing::parsePreset("loud", p));
    EXPECT_FALSE(FilterVoicing::parsePreset("", p));
    EXPECT_EQ(p, Preset::Classic) << "a failed parse must not touch the output";
}

TEST(FilterVoicing_Test, AllPresetsOfferedAndParsed)
{
    ASSERT_TRUE(FilterVoicing::isVisible(Preset::Tv));

    Preset p = Preset::Flat;
    EXPECT_TRUE(FilterVoicing::parsePreset("tv", p));
    EXPECT_EQ(p, Preset::Tv);
    EXPECT_TRUE(FilterVoicing::parsePreset("TV", p)) << "IDs are case-insensitive";

    size_t visible = 0;
    FilterVoicing::forEachVisible([&](const FilterVoicing::Profile&) { visible++; });
    EXPECT_EQ(visible, FilterVoicing::PRESET_COUNT);
}

TEST(FilterVoicing_Test, TableRowsFollowEnumOrder)
{
    // profile() indexes the table by the enum value: a row out of place would
    // silently play another profile's curve under this one's name
    for (size_t i = 0; i < FilterVoicing::PRESET_COUNT; i++)
    {
        const auto preset = static_cast<Preset>(i);
        EXPECT_EQ(FilterVoicing::profile(preset).preset, preset) << "row " << i;
        Preset parsed = Preset::Flat;
        ASSERT_TRUE(FilterVoicing::parsePreset(FilterVoicing::presetId(preset), parsed));
        EXPECT_EQ(parsed, preset) << FilterVoicing::presetId(preset);
    }
}
