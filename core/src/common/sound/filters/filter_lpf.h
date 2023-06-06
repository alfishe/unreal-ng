#pragma once

#include <stdafx.h>

#include <iostream>
#include <cmath>

// Constants
const double pi = 3.14159265358979323846;

// LPF filter class
class LowPassFilter
{
private:
    double omega;
    double alpha;
    double yPrev;
    double yPrev2;

public:
    LowPassFilter(double cutoffFrequency, double samplingFrequency)
    {
        omega = 2.0 * pi * cutoffFrequency / samplingFrequency;
        alpha = sin(omega) / (2.0 * pi * cutoffFrequency);
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
