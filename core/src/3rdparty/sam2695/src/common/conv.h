// libsam2695 - unit conversions of the SF2 domain (timecents, absolute cents, centibels) and the
// modulator transfer curves.
#pragma once

#include <cmath>
#include <cstdint>

namespace sam2695
{

// Timecents to seconds: 2^(tc / 1200). -32768 means "no time" (SF2 8.1.3 for delays / attacks).
inline double TimecentsToSeconds(double tc)
{
    if (tc <= -32768.0)
        return 0.0;
    return std::exp2(tc / 1200.0);
}

// Seconds to a whole number of internal samples (at least 0).
inline uint32_t SecondsToSamples(double seconds, double rate)
{
    const double n = seconds * rate + 0.5;
    if (n <= 0.0)
        return 0;
    if (n >= 4.0e9)
        return 4000000000u;
    return static_cast<uint32_t>(n);
}

// Absolute cents to Hz: 8.176 Hz * 2^(c / 1200) (SF2 8.1.3, initialFilterFc, freq*LFO)
inline double AbsCentsToHz(double cents)
{
    return 8.17579891564 * std::exp2(cents / 1200.0);
}

// Centibels of attenuation to a linear amplitude factor
inline double CentibelsToGain(double cb)
{
    return std::pow(10.0, -cb / 200.0);
}

// Modulator source curves on a normalized input 0..1 (SF2 8.2.1 / 9.5.3), same shapes as the SF2
// reference implementations: concave(x) = -20/96 * log10((1 - x)^2), convex(x) = 1 - concave(1 - x).
inline double ConcaveCurve(double x)
{
    if (x <= 0.0)
        return 0.0;
    if (x >= 1.0)
        return 1.0;
    const double v = -(20.0 / 96.0) * std::log10((1.0 - x) * (1.0 - x));
    return v > 1.0 ? 1.0 : v;
}

inline double ConvexCurve(double x)
{
    return 1.0 - ConcaveCurve(1.0 - x);
}

} // namespace sam2695
