// libsam2695 - output resampler.
#include "render/resampler.h"

#include "render/kaiser.h"

#include <algorithm>
#include <cmath>

namespace sam2695
{

namespace
{

constexpr int kBaseTaps = 32;
constexpr int kMaxTaps = 128;
constexpr double kPassband = 0.95; // cutoff relative to the lower Nyquist
constexpr double kBeta = 8.0;      // ~80 dB stopband

int TapsFor(uint32_t inRate, uint32_t outRate)
{
    if (outRate >= inRate)
        return kBaseTaps;
    const double widened = kBaseTaps * static_cast<double>(inRate) / outRate;
    int taps = static_cast<int>(std::ceil(widened / 2.0)) * 2;
    return std::min(taps, kMaxTaps);
}

} // namespace

void Resampler::Reserve(uint32_t inRate, uint32_t minOutRate)
{
    const int taps = std::max(TapsFor(inRate, minOutRate), kBaseTaps);
    _kernel.reserve(static_cast<size_t>(kPhases + 1) * taps);
    _history.reserve(static_cast<size_t>(4) * taps);
}

void Resampler::Configure(uint32_t inRate, uint32_t outRate)
{
    _inRate = inRate;
    _outRate = outRate;
    _bypass = inRate == outRate;
    _taps = _bypass ? 0 : TapsFor(inRate, outRate);
    if (!_bypass)
    {
        const double fc = kPassband * std::min(1.0, static_cast<double>(outRate) / inRate);
        const int half = _taps / 2;
        _kernel.assign(static_cast<size_t>(kPhases + 1) * _taps, 0.0f);
        for (int p = 0; p <= kPhases; p++)
        {
            const double frac = static_cast<double>(p) / kPhases;
            double sum = 0.0;
            for (int k = 0; k < _taps; k++)
            {
                const double x = static_cast<double>(k - (half - 1)) - frac;
                const double arg = 3.14159265358979323846 * fc * x;
                const double sinc = std::fabs(arg) < 1e-12 ? 1.0 : std::sin(arg) / arg;
                const double h = fc * sinc * KaiserWindow(x / (half + 0.5), kBeta);
                _kernel[static_cast<size_t>(p) * _taps + k] = static_cast<float>(h);
                sum += h;
            }
            for (int k = 0; k < _taps; k++)
                _kernel[static_cast<size_t>(p) * _taps + k] =
                    static_cast<float>(_kernel[static_cast<size_t>(p) * _taps + k] / sum);
        }
        _history.assign(static_cast<size_t>(4) * _taps, 0.0f);
    }
    Reset();
}

void Resampler::Reset()
{
    std::fill(_history.begin(), _history.end(), 0.0f);
    _write = 0;
    _num = 0;
    // the first output lands on the first input frame: it must sit at the window's center tap
    // (half - 1), behind half + 1 real frames
    _pending = static_cast<uint32_t>(_taps / 2 + 1);
}

size_t Resampler::Process(const float* in, size_t inFrames, size_t& consumed, float* out, size_t maxOut)
{
    consumed = 0;
    if (_bypass)
    {
        const size_t n = std::min(inFrames, maxOut);
        std::copy(in, in + n * 2, out);
        consumed = n;
        return n;
    }
    const size_t taps = static_cast<size_t>(_taps);
    size_t produced = 0;
    while (produced < maxOut)
    {
        while (_pending > 0)
        {
            if (consumed == inFrames)
                return produced;
            const float l = in[consumed * 2], r = in[consumed * 2 + 1];
            consumed++;
            float* a = &_history[static_cast<size_t>(_write) * 2];
            float* b = &_history[(static_cast<size_t>(_write) + taps) * 2];
            a[0] = b[0] = l;
            a[1] = b[1] = r;
            _write = static_cast<uint32_t>((_write + 1) % taps);
            _pending--;
        }
        const double pos = static_cast<double>(_num) / _outRate * kPhases;
        const int phase = std::min(static_cast<int>(pos), kPhases - 1);
        const float mix = static_cast<float>(pos - phase);
        const float* k0 = &_kernel[static_cast<size_t>(phase) * taps];
        const float* k1 = k0 + taps;
        const float* w = &_history[static_cast<size_t>(_write) * 2]; // oldest frame first
        float l0 = 0.0f, r0 = 0.0f, l1 = 0.0f, r1 = 0.0f;
        for (size_t k = 0; k < taps; k++)
        {
            l0 += w[k * 2] * k0[k];
            r0 += w[k * 2 + 1] * k0[k];
            l1 += w[k * 2] * k1[k];
            r1 += w[k * 2 + 1] * k1[k];
        }
        out[produced * 2] = l0 + (l1 - l0) * mix;
        out[produced * 2 + 1] = r0 + (r1 - r0) * mix;
        produced++;
        _num += _inRate;
        _pending = static_cast<uint32_t>(_num / _outRate);
        _num %= _outRate;
    }
    return produced;
}

} // namespace sam2695
