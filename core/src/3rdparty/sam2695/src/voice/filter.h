// libsam2695 - the voice filter: resonant 2-pole low-pass (SF2 initialFilterFc / initialFilterQ).
//
// RBJ low-pass biquad, transposed direct form II. Q follows the SF2 meaning (the resonance peak height
// above the DC gain, in centibels) the way SF2 players read it: q = 10^((Q/10 - 3.01) / 20), so Q = 0
// is a Butterworth response without a peak. A resonant filter (q > 1) lowers its DC gain by 1/sqrt(q)
// so it does not overload; below that the DC gain stays 1, so engaging the filter never changes the
// level (FluidSynth applies 1/sqrt(q) always: +1.5 dB at Q = 0, README "Deviations"). Coefficients are recomputed per control block and interpolated linearly
// across it (no zipper noise on cutoff sweeps). A cutoff at or above 0.45 fs with Q = 0 bypasses it.
#pragma once

#include <cmath>
#include <cstdint>

namespace sam2695
{

struct BiquadCoeffs
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
};

inline BiquadCoeffs LowPassCoeffs(double cutoffHz, double qCentibels, double rate, bool& bypass)
{
    const double nyquistGuard = 0.45 * rate;
    bypass = cutoffHz >= nyquistGuard && qCentibels <= 0.0;
    BiquadCoeffs c;
    if (bypass)
        return c;
    const double fc = std::fmin(std::fmax(cutoffHz, 5.0), nyquistGuard);
    const double qDb = qCentibels / 10.0 - 3.01;
    const double q = std::pow(10.0, qDb / 20.0);
    const double gain = q > 1.0 ? 1.0 / std::sqrt(q) : 1.0;
    const double w = 2.0 * 3.14159265358979323846 * fc / rate;
    const double cs = std::cos(w);
    const double alpha = std::sin(w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    c.b0 = static_cast<float>((1.0 - cs) * 0.5 * gain / a0);
    c.b1 = static_cast<float>((1.0 - cs) * gain / a0);
    c.b2 = c.b0;
    c.a1 = static_cast<float>(-2.0 * cs / a0);
    c.a2 = static_cast<float>((1.0 - alpha) / a0);
    return c;
}

struct VoiceFilter
{
    BiquadCoeffs now;   // current (ramping) coefficients
    BiquadCoeffs step;  // per-sample increments
    uint32_t rampLeft = 0;
    float z1 = 0.0f, z2 = 0.0f;
    bool active = false; // false: bypassed (no state kept)

    // Set the target for `n` samples ahead. `first`: jump there (a new voice).
    void Target(const BiquadCoeffs& target, bool bypass, uint32_t n, bool first)
    {
        if (bypass && (!active || first))
        {
            active = false;
            rampLeft = 0;
            return;
        }
        if (!active || first || n == 0)
        {
            if (!active)
                z1 = z2 = 0.0f;
            active = true;
            now = target;
            step = BiquadCoeffs{0, 0, 0, 0, 0};
            rampLeft = 0;
            return;
        }
        // a bypassed target while active: ramp toward the identity response
        const float inv = 1.0f / static_cast<float>(n);
        step.b0 = (target.b0 - now.b0) * inv;
        step.b1 = (target.b1 - now.b1) * inv;
        step.b2 = (target.b2 - now.b2) * inv;
        step.a1 = (target.a1 - now.a1) * inv;
        step.a2 = (target.a2 - now.a2) * inv;
        rampLeft = n;
    }

    float Process(float x)
    {
        const float y = now.b0 * x + z1;
        z1 = now.b1 * x - now.a1 * y + z2;
        z2 = now.b2 * x - now.a2 * y;
        if (rampLeft > 0)
        {
            now.b0 += step.b0;
            now.b1 += step.b1;
            now.b2 += step.b2;
            now.a1 += step.a1;
            now.a2 += step.a2;
            rampLeft--;
        }
        return y;
    }

    void FlushDenormals()
    {
        // -300 dB: a ringing tail this small would only multiply into denormals downstream
        if (std::fabs(z1) < 1e-15f)
            z1 = 0.0f;
        if (std::fabs(z2) < 1e-15f)
            z2 = 0.0f;
    }

    template <class Ar>
    void Serialize(Ar& ar)
    {
        ar(now.b0);
        ar(now.b1);
        ar(now.b2);
        ar(now.a1);
        ar(now.a2);
        ar(step.b0);
        ar(step.b1);
        ar(step.b2);
        ar(step.a1);
        ar(step.a2);
        ar(rampLeft);
        ar(z1);
        ar(z2);
        ar(active);
    }
};

} // namespace sam2695
