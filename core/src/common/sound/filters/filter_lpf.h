#pragma once

#include <stdafx.h>

class LowPassFilter
{
protected:
    static constexpr int16_t offset = INT16_MAX / 2;
    float _alpha;
    uint16_t _yPrev;

public:
    LowPassFilter() = delete;
    LowPassFilter(float cutoffFreq, float sampleFreq)
    {
        _alpha = cutoffFreq / (cutoffFreq + sampleFreq);
        _yPrev = 0;
    }
    virtual ~LowPassFilter() = default;

    uint16_t filter(uint16_t input)
    {
        input += offset;

        float y = _alpha * input + (1 - _alpha) * _yPrev;
        _yPrev = static_cast<uint16_t>(y);

        return _yPrev - offset;
    }
};