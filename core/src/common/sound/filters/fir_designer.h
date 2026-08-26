#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/// Runtime FIR designer (audio-multirate plan, phase 2).
///
/// Windowed-sinc lowpass design, DC-normalized (sum of coefficients == 1).
/// Replaces all baked coefficient tables: filters design themselves at
/// construction from physical parameters (cutoff Hz, sample rates, window),
/// so any core sample rate is supported with zero new tables or code.
///
/// Validated to reproduce both shipped 44.1 kHz tables BIT-EXACTLY
/// (fir_designer_test.cpp asserts this against embedded golden references):
///   FilterDecimator: kaiser(96,  20000.0, 218750.0, 5.0)
///   UnrealFilter:    hamming(128, 11025.0, 2822400.0)
///
/// The authoritative specification (and per-rate audit tables) lives in
/// docs/inprogress/2026-08-17-audio-sync/ - generate_fir_tables.py is the
/// executable twin of this implementation.
namespace FirDesigner
{

/// Modified Bessel function of the first kind, order 0 (power series).
/// Converges in ~20 terms for the beta range used here.
inline double besselI0(double x)
{
    double sum = 1.0;
    double term = 1.0;
    int k = 1;
    while (true)
    {
        const double f = x / (2.0 * k);
        term *= f * f;
        sum += term;
        k++;
        if (term < 1e-21 * sum)
            return sum;
    }
}

inline double sinc(double x)
{
    return x == 0.0 ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
}

/// Kaiser-windowed sinc lowpass. beta ~5 => ~56 dB stopband (Reference tier),
/// beta ~9 => ~90 dB (HighFidelity tier).
inline std::vector<double> kaiser(size_t taps, double fc, double fs, double beta)
{
    const double M = static_cast<double>(taps - 1);
    const double i0beta = besselI0(beta);
    std::vector<double> h(taps);

    double sum = 0.0;
    for (size_t n = 0; n < taps; n++)
    {
        const double m = static_cast<double>(n) - M / 2.0;
        const double t = 2.0 * m / M;
        const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - t * t))) / i0beta;
        h[n] = 2.0 * fc / fs * sinc(2.0 * fc / fs * m) * w;
        sum += h[n];
    }
    for (auto& x : h)
        x /= sum;  // Unity DC gain
    return h;
}

/// Hamming-windowed sinc lowpass (matches MATLAB fir1 normalization).
inline std::vector<double> hamming(size_t taps, double fc, double fs)
{
    const double M = static_cast<double>(taps - 1);
    std::vector<double> h(taps);

    double sum = 0.0;
    for (size_t n = 0; n < taps; n++)
    {
        const double m = static_cast<double>(n) - M / 2.0;
        const double w = 0.54 - 0.46 * std::cos(2.0 * M_PI * static_cast<double>(n) / M);
        h[n] = 2.0 * fc / fs * sinc(2.0 * fc / fs * m) * w;
        sum += h[n];
    }
    for (auto& x : h)
        x /= sum;
    return h;
}

}  // namespace FirDesigner
