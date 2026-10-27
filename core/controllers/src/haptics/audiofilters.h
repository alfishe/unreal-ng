#pragma once

/// @file audiofilters.h
/// @brief The DSP filters of haptics.md §4 on the core's sample streams (mono, 8 kHz, HapticStreamFrame): `envelope`
/// (a smoothed level with attack and release), `lowband` (the level of a band, default 20-150 Hz, then an envelope)
/// and `onset` (an event when the level rises by more than X dB within Y ms). Integer arithmetic throughout - one-pole
/// sections in Q16 with coefficients from plain arithmetic (no libm), a dB table of literals - so every compiler gives
/// the same levels (owner rule: bit-exact on all platforms). Host output only.

#include <array>
#include <cstddef>
#include <cstdint>

namespace controllers
{

/// A first-order section in Q16: low-pass `y += (x - y) * a`, high-pass `y = a * (y + x - x1)`
class OnePole
{
public:
    /// a = w / (1 + w), w = 2 pi f / rate: the low-pass's coefficient for a corner at @p hz
    static int32_t LowPassCoefficient(double hz, uint32_t rate);
    /// a = 1 / (1 + w): the high-pass's
    static int32_t HighPassCoefficient(double hz, uint32_t rate);
    /// a = 1 / (1 + ms * rate / 1000): a time constant of @p ms (the envelope's attack / release)
    static int32_t TimeCoefficient(double ms, uint32_t rate);
};

/// The rectified level, smoothed: rises with the attack's time constant, falls with the release's. Q15 full scale
/// (32767 = a full-scale square wave)
class EnvelopeFilter
{
public:
    void Configure(double attackMs, double releaseMs, uint32_t rate);
    /// One sample (any sign); the level after it
    int32_t Step(int32_t sample);
    int32_t Level() const
    {
        return static_cast<int32_t>(_level >> 8);
    }
    void Reset()
    {
        _level = 0;
    }

private:
    int32_t _attack = 0;
    int32_t _release = 0;
    int64_t _level = 0;  ///< Q15 << 8
};

/// `lowband`: a high-pass at `low` and two low-passes at `high` (12 dB / octave above the band), then the envelope
class LowbandFilter
{
public:
    void Configure(double lowHz, double highHz, double attackMs, double releaseMs, uint32_t rate);
    /// The frame's samples; the envelope's peak over them, 0..1
    float Process(const int16_t* samples, size_t count);
    void Reset();

private:
    int32_t _hp = 0, _lp = 0;
    int64_t _hpY = 0, _lp1 = 0, _lp2 = 0;  ///< Q8 above the sample
    int32_t _x1 = 0;
    EnvelopeFilter _envelope;
};

/// `onset`: a fast envelope (1 ms attack) kept per millisecond; an event when it exceeds `floor` and the level `within`
/// ms ago by `rise_db`, then quiet for `within` ms
class OnsetFilter
{
public:
    static constexpr unsigned kMaxWithinMs = 250;
    void Configure(double riseDb, double withinMs, double floor, double releaseMs, uint32_t rate);
    /// The frame's samples; true when an onset began in them
    bool Process(const int16_t* samples, size_t count);
    void Reset();

    /// 10^(db / 20) in Q16 for 0..60 dB (whole dB, from a table of literals)
    static int64_t RatioQ16(double db);

private:
    EnvelopeFilter _envelope;
    int64_t _ratio = 0;  ///< Q16
    int32_t _floor = 0;  ///< Q15
    unsigned _within = 1;
    unsigned _samplesPerMs = 8;
    unsigned _inMs = 0;
    int32_t _msPeak = 0;
    std::array<int32_t, kMaxWithinMs + 1> _history{};  ///< per ms, a ring
    unsigned _head = 0;
    unsigned _filled = 0;
    unsigned _quiet = 0;  ///< ms left of the refractory time
};

/// `envelope` as a source: the envelope's peak over the frame, 0..1
float EnvelopeLevel(EnvelopeFilter& filter, const int16_t* samples, size_t count);

}  // namespace controllers
