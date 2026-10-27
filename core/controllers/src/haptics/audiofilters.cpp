#include "haptics/audiofilters.h"

#include <algorithm>

namespace controllers
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int64_t kOne = 65536;  ///< 1.0 in Q16

/// v * a / 65536, truncated toward zero (division, not a shift: defined the same for negative values everywhere)
int64_t MulQ16(int64_t v, int64_t a)
{
    return v * a / kOne;
}

int32_t ToQ16(double value)
{
    return static_cast<int32_t>(std::clamp(value, 0.0, 1.0) * 65536.0 + 0.5);
}

/// 10^(i / 20) in Q16, i = 0..60 dB
constexpr int64_t kDbQ16[61] = {
    65536,    73533,    82505,    92572,    103868,   116541,   130762,   146717,   164619,   184706,   207243,
    232531,   260904,   292739,   328458,   368536,   413504,   463959,   520571,   584090,   655360,   735326,
    825049,   925721,   1038676,  1165413,  1307615,  1467168,  1646190,  1847055,  2072430,  2325305,  2609035,
    2927386,  3284581,  3685360,  4135042,  4639593,  5205710,  5840902,  6553600,  7353260,  8250494,  9257206,
    10386756, 11654132, 13076151, 14671683, 16461899, 18470554, 20724303, 23253050, 26090352, 29273856, 32845807,
    36853601, 41350421, 46395935, 52057095, 58409021, 65536000};
}  // namespace

int32_t OnePole::LowPassCoefficient(double hz, uint32_t rate)
{
    const double w = 2.0 * kPi * hz / static_cast<double>(rate);
    return ToQ16(w / (1.0 + w));
}

int32_t OnePole::HighPassCoefficient(double hz, uint32_t rate)
{
    const double w = 2.0 * kPi * hz / static_cast<double>(rate);
    return ToQ16(1.0 / (1.0 + w));
}

int32_t OnePole::TimeCoefficient(double ms, uint32_t rate)
{
    return ToQ16(1.0 / (1.0 + std::max(0.0, ms) * static_cast<double>(rate) / 1000.0));
}

void EnvelopeFilter::Configure(double attackMs, double releaseMs, uint32_t rate)
{
    _attack = OnePole::TimeCoefficient(attackMs, rate);
    _release = OnePole::TimeCoefficient(releaseMs, rate);
    _level = 0;
}

int32_t EnvelopeFilter::Step(int32_t sample)
{
    const int64_t rectified = static_cast<int64_t>(std::min(32767, sample < 0 ? -sample : sample)) << 8;
    _level += MulQ16(rectified - _level, rectified > _level ? _attack : _release);
    return Level();
}

float EnvelopeLevel(EnvelopeFilter& filter, const int16_t* samples, size_t count)
{
    int32_t peak = 0;
    for (size_t i = 0; i < count; i++)
        peak = std::max(peak, filter.Step(samples[i]));
    return static_cast<float>(peak) / 32767.0f;
}

void LowbandFilter::Configure(double lowHz, double highHz, double attackMs, double releaseMs, uint32_t rate)
{
    _hp = OnePole::HighPassCoefficient(lowHz, rate);
    _lp = OnePole::LowPassCoefficient(highHz, rate);
    _envelope.Configure(attackMs, releaseMs, rate);
    Reset();
}

void LowbandFilter::Reset()
{
    _hpY = _lp1 = _lp2 = 0;
    _x1 = 0;
    _envelope.Reset();
}

float LowbandFilter::Process(const int16_t* samples, size_t count)
{
    int32_t peak = 0;
    for (size_t i = 0; i < count; i++)
    {
        const int32_t x = samples[i];
        _hpY = MulQ16(_hpY + (static_cast<int64_t>(x - _x1) << 8), _hp);
        _x1 = x;
        _lp1 += MulQ16(_hpY - _lp1, _lp);
        _lp2 += MulQ16(_lp1 - _lp2, _lp);
        peak = std::max(peak, _envelope.Step(static_cast<int32_t>(_lp2 / 256)));
    }
    return static_cast<float>(peak) / 32767.0f;
}

int64_t OnsetFilter::RatioQ16(double db)
{
    const int index = static_cast<int>(std::clamp(db, 0.0, 60.0) + 0.5);
    return kDbQ16[index];
}

void OnsetFilter::Configure(double riseDb, double withinMs, double floor, double releaseMs, uint32_t rate)
{
    _envelope.Configure(1.0, releaseMs, rate);
    _ratio = RatioQ16(riseDb);
    _floor = static_cast<int32_t>(std::clamp(floor, 0.0, 1.0) * 32767.0 + 0.5);
    _within = static_cast<unsigned>(std::clamp(withinMs, 1.0, static_cast<double>(kMaxWithinMs)) + 0.5);
    _samplesPerMs = std::max(1u, rate / 1000);
    Reset();
}

void OnsetFilter::Reset()
{
    _envelope.Reset();
    _history.fill(0);
    _head = 0;
    _filled = 0;
    _inMs = 0;
    _msPeak = 0;
    _quiet = 0;
}

bool OnsetFilter::Process(const int16_t* samples, size_t count)
{
    bool fired = false;
    for (size_t i = 0; i < count; i++)
    {
        _msPeak = std::max(_msPeak, _envelope.Step(samples[i]));
        if (++_inMs < _samplesPerMs)
            continue;
        // A millisecond: compare with the level `within` ms ago, then keep this one
        const size_t size = _history.size();
        const int32_t past = _history[(_head + size - _within) % size];
        if (_quiet > 0)
            _quiet--;
        else if (_filled >= _within && _msPeak > _floor &&
                 static_cast<int64_t>(_msPeak) * kOne > _ratio * static_cast<int64_t>(past))
        {
            fired = true;
            _quiet = _within;
        }
        _history[_head] = _msPeak;
        _head = static_cast<unsigned>((_head + 1) % size);
        _filled = std::min<unsigned>(_filled + 1, static_cast<unsigned>(size));
        _inMs = 0;
        _msPeak = 0;
    }
    return fired;
}

}  // namespace controllers
