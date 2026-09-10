#pragma once

#include <stdafx.h>

#include <iostream>
#include <cmath>

// LPF filter class
class LowPassFilter
{
private:
    // Class-scoped: a namespace-scope `pi` in a header leaks into every
    // includer (tape.h pulls this in nearly everywhere) and collides with
    // CarbonCore's deprecated global `pi` on macOS (ObjC++ TUs resolve to
    // the SDK symbol and emit deprecation warnings)
    static constexpr double PI = 3.14159265358979323846;

    double omega;
    double alpha;
    double yPrev;
    double yPrev2;

public:
    LowPassFilter(double cutoffFrequency, double samplingFrequency)
    {
        omega = 2.0 * PI * cutoffFrequency / samplingFrequency;
        alpha = sin(omega) / (2.0 * PI * cutoffFrequency);
        yPrev = 0.0;
        yPrev2 = 0.0;
    }
    virtual ~LowPassFilter() = default;

    int16_t filter(int16_t x)
    {
        double input = static_cast<double>(x);
        double y = alpha * (input + 2.0 * input - input) + 2.0 * cos(omega) * yPrev - yPrev2;

        yPrev2 = yPrev;
        yPrev = y;

        return static_cast<int16_t>(y);
    }
};
