// libsam2695 - chorus, spatial effect and equalizer (see chorus.h).
#include "fx/chorus.h"

#include "sam2695/sam2695config.h"

#include <algorithm>
#include <cmath>

namespace sam2695
{

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kChorusMaxMs = 40.0;
constexpr double kChorusDepthMsPerStep = 0.1;
// line: the longest delay plus the full depth plus the cubic read's margin
constexpr uint32_t kChorusLine =
    static_cast<uint32_t>((kChorusMaxMs + 127 * kChorusDepthMsPerStep) * kInternalRate / 1000.0) + 8;
constexpr double kSpatialMsBase = 0.25;
constexpr double kSpatialMsPerStep = 0.15;
constexpr uint32_t kSpatialLine = static_cast<uint32_t>((kSpatialMsBase + 127 * kSpatialMsPerStep) * kInternalRate / 1000.0) + 4;

double Ms(double ms)
{
    return ms * kInternalRate / 1000.0;
}

} // namespace

// ---- chorus ----

void Chorus::Allocate()
{
    _pool.assign(kChorusLine, 0.0f);
    _line = DelayLine{0, kChorusLine, 0};
}

void Chorus::Clear()
{
    std::fill(_pool.begin(), _pool.end(), 0.0f);
    _line.pos = 0;
    _lfoPhase = 0;
    _idle = true;
    _zeroRun = _line.size;
    _line.quiet = _line.size;
}

void Chorus::Update(const FxParams& p)
{
    _base = static_cast<float>(Ms(kChorusMaxMs * std::exp2((static_cast<double>(p.chorusDelay) - 127.0) / 24.0)));
    _depth = static_cast<float>(Ms(p.chorusDepth * kChorusDepthMsPerStep));
    _feedback = static_cast<float>(p.chorusFeedback / 127.0 * 0.85);
    _level = p.chorusLevel / 127.0f;
    const double hz = 0.05 + p.chorusRate * (9.95 / 127.0);
    _lfoIncrement = static_cast<uint32_t>(hz / kInternalRate * 4294967296.0);
}

void Chorus::Sanitize(const FxParams& p)
{
    if (_line.base != 0 || _line.size != _pool.size() || _line.pos >= _line.size)
    {
        _line = DelayLine{0, static_cast<uint32_t>(_pool.size()), 0};
        Clear();
    }
    Update(p);
    _idle = _line.AllZero(_pool.data());
    _zeroRun = _idle ? _line.size : 0;
    _line.DeriveQuiet(_pool.data());
}

void Chorus::Skip(uint32_t n)
{
    _line.Advance(n);
    _lfoPhase += n * _lfoIncrement; // wraps as n single increments do
}

void Chorus::Process(const float* in, float* outL, float* outR, uint32_t n)
{
    float* pool = _pool.data();
    for (uint32_t i = 0; i < n; i++)
    {
        const float sl = PhaseSine(_lfoPhase);
        const float sr = PhaseSine(_lfoPhase - 0x40000000u);
        _lfoPhase += _lfoIncrement;
        const float yl = _line.ReadCubic(pool, _base + _depth * 0.5f * (1.0f + sl));
        const float yr = _line.ReadCubic(pool, _base + _depth * 0.5f * (1.0f + sr));
        _line.Write(pool, Flush(in[i] + _feedback * 0.5f * (yl + yr)));
        outL[i] += _level * yl;
        outR[i] += _level * yr;
    }
    // idle once the line has taken a run of +0.0 as long as itself (the input is written into it)
    _zeroRun = _line.RecentZero(pool, n) ? std::min(_zeroRun + n, _line.size) : 0;
    _idle = _zeroRun == _line.size;
    if (_idle)
    {
        _line.quiet = _line.size; // all +0.0
        return;
    }
    // the tail-out rule (as the reverb's, reverb.cpp): no input and the whole line below the floor
    _line.TrackQuiet(pool, n);
    if (_line.Quiet() && AllPositiveZero(in, n) && AllBelowFloor(pool, _line.size))
    {
        std::fill(_pool.begin(), _pool.end(), 0.0f);
        _idle = true;
        _zeroRun = _line.size;
        tailsOut++;
    }
}

// ---- spatial effect ----

void Spatial::Allocate()
{
    _pool.assign(kSpatialLine, 0.0f);
    _line = DelayLine{0, kSpatialLine, 0};
}

void Spatial::Clear()
{
    std::fill(_pool.begin(), _pool.end(), 0.0f);
    _line.pos = 0;
    _idle = true;
    _zeroRun = _line.size;
}

void Spatial::Update(const FxParams& p)
{
    _delay = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(Ms(kSpatialMsBase + p.spatialDelay * kSpatialMsPerStep))), 1,
                                  kSpatialLine - 1);
    _volume = p.spatialVolume / 127.0f;
    _mono = p.spatialInput >= 0x40;
}

void Spatial::Sanitize(const FxParams& p)
{
    if (_line.base != 0 || _line.size != _pool.size() || _line.pos >= _line.size)
    {
        _line = DelayLine{0, static_cast<uint32_t>(_pool.size()), 0};
        Clear();
    }
    Update(p);
    _idle = _line.AllZero(_pool.data());
    _zeroRun = _idle ? _line.size : 0;
}

void Spatial::Process(float* left, float* right, uint32_t n)
{
    float* pool = _pool.data();
    for (uint32_t i = 0; i < n; i++)
    {
        const float d = _line.Read(pool, _delay) * _volume;
        _line.Write(pool, _mono ? left[i] + right[i] : left[i] - right[i]);
        left[i] += d;
        right[i] -= d;
    }
    _zeroRun = _line.RecentZero(pool, n) ? std::min(_zeroRun + n, _line.size) : 0;
    _idle = _zeroRun == _line.size;
}

// ---- equalizer ----

double Equalizer::BandFrequency(int band, uint8_t value)
{
    static constexpr double kTop[4] = {4700.0, 4200.0, 4200.0, 18750.0};
    const double f = kTop[band & 3] * (value & 0x7F) / 127.0;
    return std::clamp(f, 20.0, 0.45 * kInternalRate);
}

double Equalizer::BandGainDb(uint8_t value)
{
    return (static_cast<double>(value & 0x7F) - 64.0) * 12.0 / 64.0;
}

void Equalizer::Clear()
{
    _state.fill(0.0);
}

bool Equalizer::Idle(bool fourBand) const
{
    // [channel][band][s1, s2]: the shelves are bands 0 and 3, the peaking bands 1 and 2
    for (int ch = 0; ch < 2; ch++)
    {
        const double* st = &_state[ch * 8];
        if (!AllPositiveZero(st, 2) || !AllPositiveZero(st + 6, 2) || (fourBand && !AllPositiveZero(st + 2, 4)))
            return false;
    }
    return true;
}

bool Equalizer::TailOut(bool fourBand)
{
    // the tail-out rule (as the reverb's, reverb.cpp) after a block without input: the memory of the
    // bands in use below the floor becomes +0.0
    if (Idle(fourBand))
        return false;
    for (int ch = 0; ch < 2; ch++)
    {
        const double* st = &_state[ch * 8];
        if (!AllBelowFloor(st, 2) || !AllBelowFloor(st + 6, 2) || (fourBand && !AllBelowFloor(st + 2, 4)))
            return false;
    }
    for (int ch = 0; ch < 2; ch++)
    {
        double* st = &_state[ch * 8];
        std::fill(st, st + 2, 0.0);
        std::fill(st + 6, st + 8, 0.0);
        if (fourBand)
            std::fill(st + 2, st + 6, 0.0);
    }
    return true;
}

void Equalizer::Update(const FxParams& p)
{
    // RBJ "Audio EQ Cookbook": shelves with slope S = 1, peaking bands with Q = 0.707
    for (int b = 0; b < 4; b++)
    {
        const double a = std::pow(10.0, BandGainDb(p.eqLevel[b]) / 40.0);
        const double w = 2.0 * kPi * BandFrequency(b, p.eqFreq[b]) / kInternalRate;
        const double cs = std::cos(w), sn = std::sin(w);
        double b0, b1, b2, a0, a1, a2;
        if (b == 0 || b == 3)
        {
            const double alpha = sn / 2.0 * std::sqrt(2.0); // S = 1
            const double sq = 2.0 * std::sqrt(a) * alpha;
            const double sign = b == 0 ? 1.0 : -1.0; // low shelf / high shelf
            b0 = a * ((a + 1) - sign * (a - 1) * cs + sq);
            b1 = sign * 2 * a * ((a - 1) - sign * (a + 1) * cs);
            b2 = a * ((a + 1) - sign * (a - 1) * cs - sq);
            a0 = (a + 1) + sign * (a - 1) * cs + sq;
            a1 = -sign * 2 * ((a - 1) + sign * (a + 1) * cs);
            a2 = (a + 1) + sign * (a - 1) * cs - sq;
        }
        else
        {
            const double alpha = sn / (2.0 * 0.70710678118654752);
            b0 = 1 + alpha * a;
            b1 = -2 * cs;
            b2 = 1 - alpha * a;
            a0 = 1 + alpha / a;
            a1 = -2 * cs;
            a2 = 1 - alpha / a;
        }
        _band[b] = Biquad{b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    }
}

void Equalizer::Process(float* left, float* right, uint32_t n, bool fourBand)
{
    float* io[2] = {left, right};
    for (int ch = 0; ch < 2; ch++)
    {
        double* st = &_state[ch * 8];
        for (uint32_t i = 0; i < n; i++)
        {
            double x = io[ch][i];
            for (int b = 0; b < 4; b++)
                if (fourBand || b == 0 || b == 3)
                    x = _band[b].Process(x, st[b * 2], st[b * 2 + 1]);
            io[ch][i] = static_cast<float>(x);
        }
    }
}

} // namespace sam2695
