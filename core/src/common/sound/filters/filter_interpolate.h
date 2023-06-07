#pragma once

#include <algorithm>
#include <cstdio>

/// region <Types>
class CubicInterpolation
{
protected:
    double _c[4];                                   // Cubic polynomial coefficients
    double _y[4];                                   // Four neighboring samples

    /// region <Constructors / destructors>
public:
    CubicInterpolation()
    {
        std::fill(_c, _c + sizeof(_c) / sizeof(_c[0]), 0.0); // Fill _c array with zeroes
        std::fill(_y, _y + sizeof(_y) / sizeof(_y[0]), 0.0); // Fill _y array with zeroes
    }
    /// endregion </Constructors / destructors>

public:
    /// Typical cubic interpolation
    /// @param sample
    /// @return
    double interpolate(double sample)
    {
        // Shift neighboring samples to accommodate newly generated one
        _y[0] = _y[1];
        _y[1] = _y[2];
        _y[2] = _y[3];
        _y[3] = sample;

        // Calculate interpolation polynomial coefficients
        _c[0] = _y[3] - _y[2] - _y[0] + _y[1];
        _c[1] = _y[0] - _y[1] - _c[0];
        _c[2] = _y[2] - _y[0];
        _c[3] = _y[1];

        // Calculate interpolated value using cubic polynomial with coefficients calculated above
        double result = (_c[0] * sample  * sample * sample) + (_c[1] * sample * sample) + (_c[2] * sample) + _c[3];

        return result;
    }

    double interpolate2(double x)
    {
        // Shift neighboring samples to accommodate newly generated one
        _y[0] = _y[1];
        _y[1] = _y[2];
        _y[2] = _y[3];
        _y[3] = x;

        // Calculate interpolation polynomial coefficients
        double y1 = _y[2] - _y[0];
        _c[0] = 0.5 * _y[1] + 0.25 * (_y[0] + _y[2]);
        _c[1] = 0.5 * y1;
        _c[2] = 0.25 * (_y[3] - _y[1] - y1);

        // Calculate interpolated value using cubic polynomial with coefficients calculated above
        // Same as y = c2 * x^2 + c1 * x + c0
        double result = (_c[2] * x + _c[1]) * x + _c[0];

        return result;
    }
};
/// endregion </Types>

/// This filter extracted from ayumi project and adapted for real-time use
/// @see https://github.com/true-grue/ayumi
class FilterInterpolate
{
    /// region <Constants>
public:
    static constexpr size_t FIR_ORDER = 192;        // Decimate polyphase filter order
    static constexpr size_t DECIMATE_FACTOR = 8;    // Oversampling / decimation factor
    /// endregion </Constants>

    /// region <Fields>
protected:
    double _clockStep;
    double _x;

    // Cubic interpolation fields
    double _c[4];                                   // Four calculated interpolation coefficients
    double _y[4];                                   // Four neighboring samples

    double _firBuffer[FIR_ORDER * 2];               // Filter buffer holding intermediate samples
    size_t _firIndex;                               // Index within FIR buffer

    double* _firBlockPos;                           // Pointer to currently processed block in FIR buffer
    /// endregion </Fields>

    /// region <Properties>
public:
    void setRates(size_t psgClockRate, size_t samplingRate)
    {
        size_t oversampledSamplingRate = samplingRate * 8 * DECIMATE_FACTOR;
        _clockStep = (double)psgClockRate / (double)oversampledSamplingRate;
    };
    /// endregion </Properties>

    /// region <Constructors / destructors>
public:
    FilterInterpolate()
    {
        // Set default values;
        setRates(1'750'000, 44100);
    }

    virtual ~FilterInterpolate() = default;
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    static double decimate(double* x);

    void startOversamplingBlock();
    void recalculateInterpolationCoefficients(size_t index, double sample);
    double endOversamplingBlock();

    /// endregion </Methods>
};
