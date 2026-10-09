// libsam2695 - DSP building blocks of the effects: delay lines over a shared pool, an exact
// polynomial sine for the modulation LFOs, a biquad, the denormal guard, the idle test, the tail floor.
//
// Every element keeps its whole state in plain members (indices, floats) so the effects serialize
// with the chip: a TTD restore continues a reverb tail sample for sample.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace sam2695
{

// Adding and removing a tiny constant rounds anything below ~1e-27 to exactly zero: no denormal ever
// circulates in a feedback loop, deterministically (no flush-to-zero CPU mode is assumed).
constexpr float kAntiDenormal = 1e-20f;
inline float Flush(float x)
{
    return (x + kAntiDenormal) - kAntiDenormal;
}
inline double Flush(double x)
{
    return (x + 1e-200) - 1e-200;
}

// The idle test of the effects: every value is +0.0 (all bits clear; -0.0 does not count). An effect
// whose state is all +0.0 and whose input block is all +0.0 computes +0.0 everywhere - every product
// and sum of +0.0 operands with the effects' coefficients is +0.0, Flush() of it too - so skipping the
// block leaves the very bits processing it would (README "Idle effects").
inline bool AllPositiveZero(const float* p, uint32_t n)
{
    uint32_t acc = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t bits;
        std::memcpy(&bits, p + i, sizeof bits);
        acc |= bits;
    }
    return acc == 0;
}
inline bool AllPositiveZero(const double* p, uint32_t n)
{
    uint64_t acc = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        uint64_t bits;
        std::memcpy(&bits, p + i, sizeof bits);
        acc |= bits;
    }
    return acc == 0;
}

// The tail floor: where an effect's tail ends (README "Tail floor"). The SAM2695 is a fixed-point DSP
// whose word length the datasheet does not give; its family states 16-bit samples and RAM and up to 20-bit
// audio data (SAM2634 / SAM2635), Dream's later DSP 24 bits (SAM5000). 2^-24 of full scale is half the
// LSB of a 24-bit word, the finest of them: a value below it rounds to zero in any of those words, so a
// tail ends no earlier than the chip's would. -144 dBFS, 1 / 512 of the card's 16-bit LSB.
constexpr float kTailFloor = 0x1p-24f;

// Every value is below the tail floor in magnitude
inline bool AllBelowFloor(const float* p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (!(std::fabs(p[i]) < kTailFloor))
            return false;
    return true;
}
inline bool AllBelowFloor(const double* p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (!(std::fabs(p[i]) < static_cast<double>(kTailFloor)))
            return false;
    return true;
}

// A circular delay line living in a slice [base, base + size) of a float pool. Read(d) returns the
// sample written d writes ago (1 = the newest); read before writing for a delay of exactly d.
struct DelayLine
{
    uint32_t base = 0;
    uint32_t size = 1;
    uint32_t pos = 0; // next write position inside the slice
    // Derived from the contents, not state: how many of the newest values (counting back from the last
    // write, at most size) are below the tail floor. quiet == size: the whole line is.
    uint32_t quiet = 0;

    float Read(const float* pool, uint32_t d) const
    {
        const uint32_t i = pos >= d ? pos - d : pos + size - d;
        return pool[base + i];
    }

    // Fractional delay, linear interpolation (d >= 1)
    float ReadLinear(const float* pool, float d) const
    {
        const uint32_t i = static_cast<uint32_t>(d);
        const float f = d - static_cast<float>(i);
        const float a = Read(pool, i);
        const float b = Read(pool, i + 1);
        return a + (b - a) * f;
    }

    // Fractional delay, 4-point Hermite (d >= 2)
    float ReadCubic(const float* pool, float d) const
    {
        const uint32_t i = static_cast<uint32_t>(d);
        const float t = d - static_cast<float>(i);
        const float x0 = Read(pool, i - 1), x1 = Read(pool, i), x2 = Read(pool, i + 1), x3 = Read(pool, i + 2);
        const float c1 = 0.5f * (x2 - x0);
        const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
        const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
        return ((c3 * t + c2) * t + c1) * t + x1;
    }

    void Write(float* pool, float v)
    {
        pool[base + pos] = v;
        if (++pos == size)
            pos = 0;
    }

    // n writes of an idle block: only the position moves (the line holds +0.0 and receives +0.0)
    void Advance(uint32_t n) { pos = static_cast<uint32_t>((static_cast<uint64_t>(pos) + n) % size); }

    // The whole line, or the last n values written (n <= size), are +0.0
    bool AllZero(const float* pool) const { return AllPositiveZero(pool + base, size); }
    bool RecentZero(const float* pool, uint32_t n) const
    {
        n = n < size ? n : size;
        if (pos >= n)
            return AllPositiveZero(pool + base + pos - n, n);
        return AllPositiveZero(pool + base, pos) && AllPositiveZero(pool + base + size - (n - pos), n - pos);
    }

    // After n writes: the run of below-floor values at the newest end, exactly as Quiet() would count it
    // (a loud tail stops the scan at the first value, a quiet one scans the n new values)
    void TrackQuiet(const float* pool, uint32_t n)
    {
        n = n < size ? n : size;
        uint32_t run = 0, i = pos;
        while (run < n)
        {
            i = i == 0 ? size - 1 : i - 1;
            if (!(std::fabs(pool[base + i]) < kTailFloor))
                break;
            run++;
        }
        quiet = run < n ? run : (quiet + n < size ? quiet + n : size);
    }
    // The run counted from scratch (Clear, a load)
    void DeriveQuiet(const float* pool)
    {
        quiet = 0;
        TrackQuiet(pool, size);
    }
    bool Quiet() const { return quiet == size; }

    // Schroeder all-pass of delay d and gain g on the line: y = (g + z^-d) / (1 + g z^-d) x
    float AllPass(float* pool, float x, uint32_t d, float g)
    {
        const float z = Read(pool, d);
        const float w = Flush(x - g * z);
        Write(pool, w);
        return z + g * w;
    }

    float AllPassFrac(float* pool, float x, float d, float g)
    {
        const float z = ReadLinear(pool, d);
        const float w = Flush(x - g * z);
        Write(pool, w);
        return z + g * w;
    }

    template <class Ar>
    void Serialize(Ar& ar)
    {
        ar(base);
        ar(size);
        ar(pos);
    }
};

// Sine of a 32-bit phase (2^32 = one cycle) by the parabolic approximation refined once
// (error < 0.1 %): pure arithmetic, so every platform computes the same LFO.
inline float PhaseSine(uint32_t phase)
{
    // x in [-pi, pi) as a fraction in [-1, 1)
    const float x = static_cast<float>(static_cast<int32_t>(phase)) * (1.0f / 2147483648.0f);
    const float y = 4.0f * x - 4.0f * x * std::fabs(x);
    return 0.225f * (y * std::fabs(y) - y) + y;
}

// Biquad in transposed direct form II, double precision (the equalizer's low corners sit far below
// the sampling rate; float would add noise there)
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    double Process(double x, double& s1, double& s2) const
    {
        const double y = b0 * x + s1;
        s1 = Flush(b1 * x - a1 * y + s2);
        s2 = Flush(b2 * x - a2 * y);
        return y;
    }
};

} // namespace sam2695
