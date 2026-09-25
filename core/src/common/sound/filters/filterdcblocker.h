#pragma once

#include <cmath>

/// One-pole RC high-pass - the discrete model of an output coupling
/// capacitor (series C into a load R, fc = 1 / (2 pi R C)).
///
///     y[n] = a * (y[n-1] + x[n] - x[n-1]),   a = RC / (RC + dt)
///
/// Unlike a moving-average DC remover (x - mean of the last N samples), it has
/// no window: an impulse or burst decays exponentially and never comes back as
/// a delayed step when it leaves the window, and the passband is flat down to
/// a few times fc instead of losing the bass below ~fs/N.
class FilterDCBlocker
{
public:
    FilterDCBlocker() = default;
    FilterDCBlocker(double sampleRate, double cutoffHz)
    {
        configure(sampleRate, cutoffHz);
    }

    /// Recomputes the coefficient; the filter state is kept (call reset() for a
    /// clean start)
    void configure(double sampleRate, double cutoffHz)
    {
        _sampleRate = sampleRate;
        _cutoffHz = cutoffHz;
        const double rc = 1.0 / (2.0 * kPi * cutoffHz);
        const double dt = 1.0 / sampleRate;
        _a = rc / (rc + dt);
    }

    double filter(double x)
    {
        double y = _a * (_y1 + x - _x1);
        // A decaying tail would sink into denormals after a few seconds of
        // silence (a large slowdown on x86); nothing below this is audible
        if (std::fabs(y) < kDenormalFloor)
            y = 0.0;
        _x1 = x;
        _y1 = y;
        return y;
    }

    void reset()
    {
        _x1 = 0.0;
        _y1 = 0.0;
    }

    double sampleRate() const { return _sampleRate; }
    double cutoffHz() const { return _cutoffHz; }
    double coefficient() const { return _a; }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kDenormalFloor = 1e-30;

    double _sampleRate = 0.0;
    double _cutoffHz = 0.0;
    double _a = 1.0;  // unconfigured: passes the input unchanged
    double _x1 = 0.0;
    double _y1 = 0.0;
};
