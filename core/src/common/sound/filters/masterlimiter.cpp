#include "masterlimiter.h"

#include <cmath>

namespace
{
constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kDcCutoffHz = 5.0;
}

void MasterLimiter::Configure(double sampleRate)
{
    // Keep the DC-blocker cutoff constant in Hz across core rates
    _dcR = static_cast<float>(std::exp(-kTwoPi * kDcCutoffHz / sampleRate));
}

void MasterLimiter::Reset()
{
    _dcX1L = _dcY1L = _dcX1R = _dcY1R = 0.0f;
}

void MasterLimiter::Process(float* interleavedStereo, size_t frames)
{
    for (size_t i = 0; i < frames; i++)
    {
        const float l = interleavedStereo[i * 2];
        const float r = interleavedStereo[i * 2 + 1];

        // DC blocker: y = x - x1 + R * y1
        const float yl = l - _dcX1L + _dcR * _dcY1L;
        const float yr = r - _dcX1R + _dcR * _dcY1R;
        _dcX1L = l;
        _dcX1R = r;
        _dcY1L = yl;
        _dcY1R = yr;

        interleavedStereo[i * 2] = LimitSample(yl);
        interleavedStereo[i * 2 + 1] = LimitSample(yr);
    }
}

float MasterLimiter::LimitSample(float x)
{
    const float a = std::fabs(x);
    if (a <= KNEE_LINEAR)
        return x;

    // Compressive zone: exponential approach to the ceiling. Unit slope at
    // the knee (C1-continuous with the linear region); output magnitude is
    // strictly below CEILING for every finite input.
    const float span = CEILING - KNEE_LINEAR;
    const float over = (a - KNEE_LINEAR) / span;
    const float limited = KNEE_LINEAR + span * (1.0f - std::exp(-over));
    return std::copysign(limited, x);
}
