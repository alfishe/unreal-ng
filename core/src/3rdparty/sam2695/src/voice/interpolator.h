// libsam2695 - sample interpolation at a 32.32 fixed-point position.
//
// Linear (2 taps), Cubic (4-point Hermite / Catmull-Rom) and Sinc (8 taps, Kaiser-windowed, 256 phases
// with linear interpolation between neighboring phases; the default). The kernel is applied to taps
// i-3 .. i+4 around the integer position i.
#pragma once

#include <cstdint>

namespace sam2695
{

constexpr int kSincTaps = 8;
constexpr int kSincPhases = 256;

// (kSincPhases + 1) rows of kSincTaps coefficients; row p is the kernel for fraction p / kSincPhases.
const float* SincTable();

inline float InterpolateLinear(const float* t, float frac) // t[0] = x[i], t[1] = x[i+1]
{
    return t[0] + (t[1] - t[0]) * frac;
}

inline float InterpolateCubic(const float* t, float frac) // t[0..3] = x[i-1 .. i+2]
{
    const float xm1 = t[0], x0 = t[1], x1 = t[2], x2 = t[3];
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * frac + c2) * frac + c1) * frac + x0;
}

inline float InterpolateSinc(const float* t, uint32_t frac32) // t[0..7] = x[i-3 .. i+4]
{
    const float* table = SincTable();
    const uint32_t phase = frac32 >> 24;                                 // 8 bits: 256 phases
    const float mix = static_cast<float>(frac32 & 0x00FFFFFFu) * (1.0f / 16777216.0f);
    const float* k0 = table + phase * kSincTaps;
    const float* k1 = k0 + kSincTaps;
    float a = 0.0f, b = 0.0f;
    for (int k = 0; k < kSincTaps; k++)
    {
        a += t[k] * k0[k];
        b += t[k] * k1[k];
    }
    return a + (b - a) * mix;
}

} // namespace sam2695
