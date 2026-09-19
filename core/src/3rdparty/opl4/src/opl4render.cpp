// libopl4 — render layer implementation (core TDD §8).
#include "opl4render.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

namespace opl4
{

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Normalization: int32 streams are at full 16-bit scale (±32767 unity) with
// kRailShift bits of headroom. Dividing by (32768 << kRailShift) yields
// float in [-1, 1]. The rail is defined in opl4.cpp; mirror it here.
constexpr int kRailShift = 2;
constexpr float kNormScale = 1.0f / static_cast<float>(32768 << kRailShift);

// ---------------------------------------------------------------------------
// Kaiser FIR designer — identical math to the host's FirDesigner::kaiser
// (core/src/common/sound/filters/fir_designer.h) so the two implementations
// can be asserted bit-identical in test (§8.2, §12.1).
// ---------------------------------------------------------------------------
namespace
{

double BesselI0(double x)
{
    double sum = 1.0;
    double term = 1.0;
    int k = 1;
    while (true)
    {
        const double f = x / (2.0 * k);
        term *= f * f;
        sum += term;
        k++;
        if (term < 1e-21 * sum)
            return sum;
    }
}

double Sinc(double x)
{
    return x == 0.0 ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
}

} // namespace

std::vector<double> DesignKaiser(size_t taps, double fc, double fs, double beta)
{
    const double m = static_cast<double>(taps - 1);
    const double i0beta = BesselI0(beta);
    std::vector<double> h(taps);
    double sum = 0.0;
    for (size_t n = 0; n < taps; n++)
    {
        const double mm = static_cast<double>(n) - m / 2.0;
        const double t = 2.0 * mm / m;
        const double w = BesselI0(beta * std::sqrt(std::max(0.0, 1.0 - t * t))) / i0beta;
        h[n] = 2.0 * fc / fs * Sinc(2.0 * fc / fs * mm) * w;
        sum += h[n];
    }
    for (auto& x : h)
        x /= sum; // Unity DC gain
    return h;
}

// ---------------------------------------------------------------------------
// Polyphase resampler.
// ---------------------------------------------------------------------------
void PolyphaseResampler::Configure(double inputRate, double outputRate, Quality q,
                                    double cutoffHz)
{
    _taps = (q == Quality::HighFidelity) ? 192 : 96;
    const double beta = (q == Quality::HighFidelity) ? 9.0 : 5.0;

    _phaseStep = inputRate / outputRate;

    double fc = cutoffHz;
    if (outputRate < inputRate)
        fc = std::min(fc, 0.45 * inputRate); // decimating: guard input Nyquist
    else
        fc = std::min(fc, 0.45 * outputRate); // interpolating: output Nyquist

    // Design against the higher of the two rates so the kernel spans the
    // input history correctly for any ratio (taps count input samples).
    const double designRate = std::max(inputRate, outputRate);
    _coeffs = DesignKaiser(_taps, fc, designRate, beta);

    // Reverse: history[0] is the newest sample; convolve in natural order.
    std::reverse(_coeffs.begin(), _coeffs.end());

    // Polyphase table: the windowed sinc evaluated at every fractional
    // output position. Row f centres the kernel m/2 + (1 - f) input samples
    // behind the newest, so output time advances with the phase. A single
    // fixed FIR blended between neighbouring input positions (the previous
    // scheme) aliased high harmonics and ran the blend backwards in time.
    _phases = 512;
    const double m = static_cast<double>(_taps - 1);
    const double norm = 2.0 * fc / designRate;
    const double i0beta = BesselI0(beta);
    _table.assign((_phases + 1) * _taps, 0.0);
    for (size_t p = 0; p <= _phases; p++)
    {
        const double centre = m / 2.0 + (1.0 - static_cast<double>(p) / static_cast<double>(_phases));
        double sum = 0.0;
        for (size_t i = 0; i < _taps; i++)
        {
            const double x = static_cast<double>(i) - centre;
            const double t = x / (m / 2.0);
            const double w = (std::fabs(t) <= 1.0) ? BesselI0(beta * std::sqrt(1.0 - t * t)) / i0beta : 0.0;
            const double h = norm * Sinc(norm * x) * w;
            _table[p * _taps + i] = h;
            sum += h;
        }
        for (size_t i = 0; i < _taps; i++)
            _table[p * _taps + i] /= sum; // unity DC per phase
    }

    _histL.assign(_taps, 0.0);
    _histR.assign(_taps, 0.0);
    _orderedL.assign(_taps, 0.0); // scratch, no allocation on the audio path
    _orderedR.assign(_taps, 0.0);
    _histPos = 0;
    _phase = 0.0;
}

void PolyphaseResampler::Reset()
{
    std::fill(_histL.begin(), _histL.end(), 0.0);
    std::fill(_histR.begin(), _histR.end(), 0.0);
    std::fill(_orderedL.begin(), _orderedL.end(), 0.0);
    std::fill(_orderedR.begin(), _orderedR.end(), 0.0);
    _histPos = 0;
    _phase = 0.0;
}

double PolyphaseResampler::Convolve(const double* hist, size_t phase) const
{
    // Kernel for this fractional position: linear blend of the two nearest
    // table rows (512 rows keep the kernel interpolation error far below the
    // Kaiser stop band).
    const double pos = static_cast<double>(phase) / 1024.0 * static_cast<double>(_phases);
    const size_t r0 = std::min(static_cast<size_t>(pos), _phases - 1);
    const double t = pos - static_cast<double>(r0);
    const double* k0 = &_table[r0 * _taps];
    const double* k1 = k0 + _taps;
    double acc = 0.0;
    for (size_t i = 0; i < _taps; i++)
        acc += hist[i] * (k0[i] + t * (k1[i] - k0[i]));
    return acc;
}

size_t PolyphaseResampler::Process(float inL, float inR, float* out)
{
    // Push into the history ring (newest at _histPos, older backwards).
    _histL[_histPos] = inL;
    _histR[_histPos] = inR;

    // Materialise an ordered view into pre-allocated scratch:
    // ordered[0] = newest. (Linear gather, exact, allocation-free.)
    for (size_t i = 0; i < _taps; i++)
    {
        _orderedL[i] = _histL[(_histPos + _taps - i) % _taps];
        _orderedR[i] = _histR[(_histPos + _taps - i) % _taps];
    }

    _histPos = (_histPos + 1) % _taps;

    size_t written = 0;
    while (_phase < 1.0)
    {
        const size_t phase1024 = static_cast<size_t>(_phase * 1024.0);
        const double l = Convolve(_orderedL.data(), phase1024);
        const double r = Convolve(_orderedR.data(), phase1024);
        out[written * 2 + 0] = static_cast<float>(l);
        out[written * 2 + 1] = static_cast<float>(r);
        written++;
        _phase += _phaseStep;
    }
    _phase -= 1.0;
    return written;
}

// ---------------------------------------------------------------------------
// Board analog (§8.3): RC low-pass 4.08 kHz + Sallen-Key low-pass f0 27.7 kHz
// Q 1.306, as a minimum-phase FIR (see the class comment).
// ---------------------------------------------------------------------------
void BoardAnalog::Configure(double sampleRate)
{
    // Analog response: RC (R3 = 1k, C2 = 39 nF) x Sallen-Key (R4 = R5 = 1k,
    // C11 = 15 nF, C3 = 2.2 nF): H(s) = w1/(s + w1) * w0^2/(s^2 + (w0/Q)s + w0^2).
    const double w1 = 1.0 / (1000.0 * 39e-9);
    const double rc = 1000.0 * 1000.0 * 15e-9 * 2.2e-9;
    const double w0 = 1.0 / std::sqrt(rc);
    const double q = std::sqrt(rc) / (2.2e-9 * 2000.0);
    auto logMag = [&](double f) {
        const std::complex<double> s(0.0, 2.0 * M_PI * f);
        const std::complex<double> h = (w1 / (s + w1)) * (w0 * w0 / (s * s + (w0 / q) * s + w0 * w0));
        return std::log(std::max(std::abs(h), 1e-9));
    };

    // Minimum phase from the magnitude: real cepstrum of the log magnitude,
    // folded onto positive quefrencies, exponentiated back (N-point DFTs;
    // configure-time only).
    constexpr size_t kN = 1024;
    std::vector<std::complex<double>> buf(kN);
    auto dft = [](std::vector<std::complex<double>>& x, bool inverse) {
        // Iterative radix-2 FFT (n is a power of two).
        const size_t n = x.size();
        for (size_t i = 1, j = 0; i < n; i++)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap(x[i], x[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const std::complex<double> wl = std::polar(1.0, (inverse ? 2.0 : -2.0) * M_PI / static_cast<double>(len));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w = 1.0;
                for (size_t k = 0; k < len / 2; k++)
                {
                    const std::complex<double> u = x[i + k], v = x[i + k + len / 2] * w;
                    x[i + k] = u + v;
                    x[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
        if (inverse)
            for (auto& v : x)
                v /= static_cast<double>(n);
    };
    for (size_t k = 0; k < kN; k++)
    {
        const size_t mirrored = (k <= kN / 2) ? k : kN - k;
        buf[k] = logMag(static_cast<double>(mirrored) * sampleRate / static_cast<double>(kN));
    }
    dft(buf, true); // real cepstrum
    for (size_t k = 1; k < kN / 2; k++)
        buf[k] = 2.0 * buf[k].real();
    buf[0] = buf[0].real();
    buf[kN / 2] = buf[kN / 2].real();
    for (size_t k = kN / 2 + 1; k < kN; k++)
        buf[k] = 0.0;
    dft(buf, false);
    for (auto& v : buf)
        v = std::exp(v);
    dft(buf, true); // minimum-phase impulse response

    double sum = 0.0;
    for (size_t i = 0; i < kTaps; i++)
        sum += buf[i].real();
    for (size_t i = 0; i < kTaps; i++)
        _h[i] = static_cast<float>(buf[i].real() / sum); // unity DC, as the analog chain
    Reset();
}

void BoardAnalog::Reset()
{
    std::fill(std::begin(_xl), std::end(_xl), 0.0f);
    std::fill(std::begin(_xr), std::end(_xr), 0.0f);
    _pos = 0;
}

void BoardAnalog::Process(float& l, float& r)
{
    _pos = (_pos + kTaps - 1) % kTaps;
    _xl[_pos] = l;
    _xr[_pos] = r;
    float accL = 0.0f, accR = 0.0f;
    for (size_t i = 0; i < kTaps; i++)
    {
        const size_t j = (_pos + i) % kTaps;
        accL += _h[i] * _xl[j];
        accR += _h[i] * _xr[j];
    }
    l = accL;
    r = accR;
}

// ---------------------------------------------------------------------------
// Character chain (§8.4).
// ---------------------------------------------------------------------------
void CharacterChain::SetPunch(PunchPreset p, double sampleRate)
{
    switch (p)
    {
    case PunchPreset::Opl4Fm:
        _edgeBlend = 0.02f;
        _transBoost = 0.06f;
        _envAttack = 0.3f;
        _envRelease = 0.9995f;
        _punchOn = true;
        break;
    case PunchPreset::Opl4Pcm:
        _edgeBlend = 0.06f;
        _transBoost = 0.15f;
        _envAttack = 0.3f;
        _envRelease = 0.998f;
        _punchOn = true;
        break;
    case PunchPreset::Custom:
        _punchOn = true; // caller sets coefficients via a future API
        break;
    case PunchPreset::Off:
    default:
        _punchOn = false;
        break;
    }

    // Rate normalisation (§8.4): first-difference gain scales with
    // 2*sin(pi*f/fs), so preset coefficients are stored referenced to
    // 44.1 kHz and mapped with coeff^(44100/fs).
    const double norm = std::pow(44100.0 / sampleRate, 1.0);
    _edgeBlend = static_cast<float>(_edgeBlend * norm);
    _transBoost = static_cast<float>(_transBoost * norm);
    const double aScale = std::pow(0.3, 44100.0 / sampleRate);
    const double rScale = std::pow(_envRelease == 0.9995f ? 0.9995 : 0.998,
                                   44100.0 / sampleRate);
    _envAttack = static_cast<float>(aScale);
    _envRelease = static_cast<float>(rScale);
}

void CharacterChain::SetRoom(RoomMode m, double sampleRate)
{
    _roomOn = (m == RoomMode::Soft);
    // Gentle opposite-channel bleed: ~0.8 ms delay, one-pole LP at ~3 kHz.
    _roomGain = _roomOn ? 0.10f : 0.0f;
    const size_t delaySamples = static_cast<size_t>(sampleRate * 0.0008);
    _delayL.assign(delaySamples + 1, 0.0f);
    _delayR.assign(delaySamples + 1, 0.0f);
    _delayPos = 0;
    _roomLp = static_cast<float>(std::exp(-2.0 * M_PI * 3000.0 / sampleRate));
}

void CharacterChain::Reset()
{
    _prevL = _prevR = 0;
    _envL = _envR = 0;
    std::fill(_delayL.begin(), _delayL.end(), 0.0f);
    std::fill(_delayR.begin(), _delayR.end(), 0.0f);
    _delayPos = 0;
    _roomZL = _roomZR = 0;
}

void CharacterChain::Process(float& l, float& r)
{
    if (_punchOn)
    {
        // Tilt: first difference (a +6 dB/oct tilt) blended by edgeBlend.
        const float dL = l - _prevL;
        const float dR = r - _prevR;
        _prevL = l;
        _prevR = r;

        // Envelope follower gates the transient boost.
        const float absL = std::fabs(l), absR = std::fabs(r);
        _envL = (absL > _envL) ? _envAttack * absL + (1.0f - _envAttack) * _envL
                               : _envRelease * _envL + (1.0f - _envRelease) * absL;
        _envR = (absR > _envR) ? _envAttack * absR + (1.0f - _envAttack) * _envR
                               : _envRelease * _envR + (1.0f - _envRelease) * absR;
        const float transL = std::max(0.0f, absL - _envL) * _transBoost;
        const float transR = std::max(0.0f, absR - _envR) * _transBoost;

        l += _edgeBlend * dL + transL;
        r += _edgeBlend * dR + transR;
    }

    if (_roomOn)
    {
        // Delayed opposite-channel bleed through a gentle lowpass.
        const float dl = _delayL[_delayPos];
        const float dr = _delayR[_delayPos];
        _delayL[_delayPos] = l;
        _delayR[_delayPos] = r;
        _delayPos = (_delayPos + 1) % _delayL.size();
        _roomZL = _roomLp * _roomZL + (1.0f - _roomLp) * dr;
        _roomZR = _roomLp * _roomZR + (1.0f - _roomLp) * dl;
        l += _roomGain * _roomZL;
        r += _roomGain * _roomZR;
    }
}

// ---------------------------------------------------------------------------
// Opl4Render orchestration.
// ---------------------------------------------------------------------------
void Opl4Render::Configure(const Opl4Config& cfg)
{
    _outputRate = cfg.outputRate;
    _mode = cfg.mode;
    _quality = cfg.quality;

    const double fmRate = static_cast<double>(kMasterClockHz) / static_cast<double>(kFmDivider);
    if (_mode == RenderMode::HiFi)
    {
        // Always configured in HiFi — including output == 44100, where the
        // FM grid still decimates 49516.4 -> 44100 (regression: this arm
        // used to skip configuration at the chip rate and ProcessSplit then
        // read an unconfigured resampler).
        _fm.Configure(fmRate, _outputRate, _quality);
        _pcm.Configure(kChipOutputRate, _outputRate, _quality);
    }
    else if (_outputRate != kChipOutputRate)
    {
        _main.Configure(kChipOutputRate, _outputRate, _quality);
    }
    // Split-source renders keep their own resamplers so the mixed path
    // (_main / _fm / _pcm) never shares filter state with ProcessGroup.
    _resFm.Configure(GroupInputRate(ChannelGroup::Fm), _outputRate, _quality);
    _resPcm.Configure(GroupInputRate(ChannelGroup::Pcm), _outputRate, _quality);
    _analog.Configure(_outputRate);
    _groupFm.analog.Configure(_outputRate); // split-source renders (ProcessGroup)
    _groupPcm.analog.Configure(_outputRate);
    // Re-apply the character settings at the (possibly new) rate: a mode or
    // quality change must not silently drop BoardAnalog / punch / room.
    _chainFm.SetPunch(_punchFm, _outputRate);
    _chainPcm.SetPunch(_punchPcm, _outputRate);
    _chainFm.SetRoom(_room, _outputRate);
    _chainPcm.SetRoom(_room, _outputRate);
    ResetRenderState();
}

void Opl4Render::ResetRenderState()
{
    _main.Reset();
    _fm.Reset();
    _pcm.Reset();
    _resFm.Reset();
    _resPcm.Reset();
    _analog.Reset();
    _chainFm.Reset();
    _chainPcm.Reset();
    _groupFm.Reset();
    _groupPcm.Reset();
    _carryMain.Reset();
    _dcX1L = _dcX1R = _dcY1L = _dcY1R = 0;
}

void Opl4Render::SetPunchPreset(ChannelGroup g, PunchPreset p)
{
    if (g == ChannelGroup::Fm)
    {
        _punchFm = p;
        _chainFm.SetPunch(p, _outputRate);
    }
    else
    {
        _punchPcm = p;
        _chainPcm.SetPunch(p, _outputRate);
    }
}

bool Opl4Render::UnityBypass() const
{
    return _mode == RenderMode::Authentic && _outputRate == kChipOutputRate
        && !_boardAnalogOn && !_chainFm.Active() && !_chainPcm.Active();
}

void Opl4Render::PostStages(float& l, float& r)
{
    if (_boardAnalogOn)
        _analog.Process(l, r);
    // DC blocker last (§8.6) — but keep unity exact when nothing is active.
    if (!UnityBypass())
    {
        const float yl = l - _dcX1L + _dcR * _dcY1L;
        const float yr = r - _dcX1R + _dcR * _dcY1R;
        _dcX1L = l;
        _dcX1R = r;
        _dcY1L = yl;
        _dcY1R = yr;
        l = yl;
        r = yr;
    }
}

size_t Opl4Render::ProcessChip(const int32_t* chipStereo, size_t frames,
                                float* out, size_t maxOutFrames,
                                size_t* consumedFrames)
{
    if (UnityBypass())
    {
        // R6: bit-identical path. Normalized float output in [-1, 1].
        const size_t n = std::min(frames, maxOutFrames);
        for (size_t i = 0; i < n; i++)
        {
            out[i * 2 + 0] = static_cast<float>(chipStereo[i * 2 + 0]) * kNormScale;
            out[i * 2 + 1] = static_cast<float>(chipStereo[i * 2 + 1]) * kNormScale;
        }
        if (consumedFrames)
            *consumedFrames = n;
        return n;
    }

    size_t written = 0;
    size_t i = 0;
    const auto emit = [&](float l, float r) {
        if (_chainFm.Active() || _chainPcm.Active())
        {
            // Chip stream: FM/PCM chains were applied pre-sum upstream in
            // the device; on the mixed chip stream run the FM chain.
            _chainFm.Process(l, r);
        }
        PostStages(l, r);
        out[written * 2 + 0] = l;
        out[written * 2 + 1] = r;
        written++;
    };
    ResampleCarry& carry = _carryMain;
    for (; carry.pos < carry.count && written < maxOutFrames; carry.pos++)
        emit(carry.frame[carry.pos * 2 + 0], carry.frame[carry.pos * 2 + 1]);
    // Resampler scratch: one input frame can emit floor(out/in)+1 output
    // frames; 12 covers output rates up to ~529 kHz (spec caps at 192 kHz).
    float frame[24];
    while (i < frames && written < maxOutFrames)
    {
        size_t n;
        if (_outputRate == kChipOutputRate)
        {
            frame[0] = static_cast<float>(chipStereo[i * 2 + 0]) * kNormScale;
            frame[1] = static_cast<float>(chipStereo[i * 2 + 1]) * kNormScale;
            n = 1;
        }
        else
        {
            n = _main.Process(static_cast<float>(chipStereo[i * 2 + 0]) * kNormScale,
                              static_cast<float>(chipStereo[i * 2 + 1]) * kNormScale, frame);
        }
        i++;
        size_t j = 0;
        for (; j < n && written < maxOutFrames; j++)
            emit(frame[j * 2 + 0], frame[j * 2 + 1]);
        if (j < n)
        {
            std::copy(frame + j * 2, frame + n * 2, carry.frame);
            carry.count = n - j;
            carry.pos = 0;
        }
    }
    if (consumedFrames)
        *consumedFrames = i;
    return written;
}

size_t Opl4Render::ProcessSplit(const int32_t* fmStereo, size_t fmFrames,
                                 const int32_t* pcmStereo, size_t pcmFrames,
                                 float* out, size_t maxOutFrames,
                                 size_t* consumedFm, size_t* consumedPcm)
{
    // HiFi: resample both grids to the output rate and sum (§8.2). The
    // staging vectors are persistent FIFOs of resampler OUTPUT: the two
    // grids produce output at the same average rate but not in lockstep, so
    // a call can stage more than it may emit (bounded by the slower side);
    // the tail stays staged for the next call. Dropping it instead (the old
    // behaviour) lost ~11% of FM content per frame at output 44100 and grew
    // the FM input backlog without bound at every other rate.
    //
    // Staging capacity: each input frame can emit floor(out/in)+1 frames;
    // 12 covers every supported rate pair, plus a slack for the FIR delay.
    const size_t fmHad = _fmStage.size() / 2;
    const size_t pcmHad = _pcmStage.size() / 2;
    const size_t fmNeed = (fmHad + fmFrames * 12 + 32) * 2;
    const size_t pcmNeed = (pcmHad + pcmFrames * 12 + 32) * 2;
    if (_fmStage.size() < fmNeed)
        _fmStage.resize(fmNeed);
    if (_pcmStage.size() < pcmNeed)
        _pcmStage.resize(pcmNeed);

    size_t fmOut = fmHad;
    for (size_t i = 0; i < fmFrames; i++)
        fmOut += _fm.Process(static_cast<float>(fmStereo[i * 2 + 0]) * kNormScale,
                             static_cast<float>(fmStereo[i * 2 + 1]) * kNormScale,
                             _fmStage.data() + fmOut * 2);
    size_t pcmOut = pcmHad;
    for (size_t i = 0; i < pcmFrames; i++)
        pcmOut += _pcm.Process(static_cast<float>(pcmStereo[i * 2 + 0]) * kNormScale,
                               static_cast<float>(pcmStereo[i * 2 + 1]) * kNormScale,
                               _pcmStage.data() + pcmOut * 2);

    // Trim the stages to what was actually produced: the resize above only
    // reserved room, and counting that unfilled room as staged output on
    // the next call emitted silence and grew the backlog without bound.
    _fmStage.resize(fmOut * 2);
    _pcmStage.resize(pcmOut * 2);

    const size_t n = std::min({fmOut, pcmOut, maxOutFrames});
    for (size_t i = 0; i < n; i++)
    {
        // Character chains act on the separate FM and PCM taps before the
        // sum (§8.4): this path is intentionally not bit-identical to the
        // plain path — characterEnabled == false is the reference path.
        float fmL = _fmStage[i * 2 + 0], fmR = _fmStage[i * 2 + 1];
        float pcmL = _pcmStage[i * 2 + 0], pcmR = _pcmStage[i * 2 + 1];
        _chainFm.Process(fmL, fmR);
        _chainPcm.Process(pcmL, pcmR);
        float l = fmL + pcmL;
        float r = fmR + pcmR;
        PostStages(l, r);
        out[i * 2 + 0] = l;
        out[i * 2 + 1] = r;
    }
    // Keep the unemitted tail staged.
    _fmStage.erase(_fmStage.begin(), _fmStage.begin() + static_cast<std::ptrdiff_t>(n * 2));
    _pcmStage.erase(_pcmStage.begin(), _pcmStage.begin() + static_cast<std::ptrdiff_t>(n * 2));
    if (consumedFm)
        *consumedFm = fmFrames; // all offered inputs were pushed through the resampler
    if (consumedPcm)
        *consumedPcm = pcmFrames;
    return n;
}

double Opl4Render::GroupInputRate(ChannelGroup g) const
{
    if (g == ChannelGroup::Fm && _mode == RenderMode::HiFi)
        return static_cast<double>(kMasterClockHz) / static_cast<double>(kFmDivider);
    return static_cast<double>(kChipOutputRate);
}

bool Opl4Render::GroupBypass(ChannelGroup g) const
{
    const CharacterChain& chain = (g == ChannelGroup::Fm) ? _chainFm : _chainPcm;
    return GroupInputRate(g) == static_cast<double>(kChipOutputRate)
        && _outputRate == kChipOutputRate && !_boardAnalogOn && !chain.Active();
}

size_t Opl4Render::ProcessGroup(ChannelGroup g, const int32_t* stereo, size_t frames,
                                float* out, size_t maxOutFrames, size_t* consumedFrames)
{
    CharacterChain& chain = (g == ChannelGroup::Fm) ? _chainFm : _chainPcm;
    GroupStage& st = (g == ChannelGroup::Fm) ? _groupFm : _groupPcm;
    PolyphaseResampler& res = (g == ChannelGroup::Fm) ? _resFm : _resPcm;
    const double inRate = GroupInputRate(g);

    if (GroupBypass(g))
    {
        // R6 mirror: normalized float output in [-1, 1].
        const size_t n = std::min(frames, maxOutFrames);
        for (size_t i = 0; i < n; i++)
        {
            out[i * 2 + 0] = static_cast<float>(stereo[i * 2 + 0]) * kNormScale;
            out[i * 2 + 1] = static_cast<float>(stereo[i * 2 + 1]) * kNormScale;
        }
        if (consumedFrames)
            *consumedFrames = n;
        return n;
    }

    size_t written = 0;
    size_t i = 0;
    const auto emit = [&](float l, float r) {
        chain.Process(l, r);
        if (_boardAnalogOn)
            st.analog.Process(l, r);
        // DC blocker (§8.6), per-group state.
        const float yl = l - st.dcX1L + _dcR * st.dcY1L;
        const float yr = r - st.dcX1R + _dcR * st.dcY1R;
        st.dcX1L = l;
        st.dcX1R = r;
        st.dcY1L = yl;
        st.dcY1R = yr;
        out[written * 2 + 0] = yl;
        out[written * 2 + 1] = yr;
        written++;
    };
    // Resampler outputs carried from the previous call come first (see
    // ResampleCarry).
    ResampleCarry& carry = st.carry;
    for (; carry.pos < carry.count && written < maxOutFrames; carry.pos++)
        emit(carry.frame[carry.pos * 2 + 0], carry.frame[carry.pos * 2 + 1]);
    // Same scratch sizing argument as ProcessChip: one input frame can emit
    // floor(out/in)+1 output frames; 12 covers every supported rate.
    float frame[24];
    while (i < frames && written < maxOutFrames)
    {
        size_t n;
        if (inRate == static_cast<double>(_outputRate))
        {
            frame[0] = static_cast<float>(stereo[i * 2 + 0]) * kNormScale;
            frame[1] = static_cast<float>(stereo[i * 2 + 1]) * kNormScale;
            n = 1;
        }
        else
        {
            n = res.Process(static_cast<float>(stereo[i * 2 + 0]) * kNormScale,
                            static_cast<float>(stereo[i * 2 + 1]) * kNormScale, frame);
        }
        i++;
        size_t j = 0;
        for (; j < n && written < maxOutFrames; j++)
            emit(frame[j * 2 + 0], frame[j * 2 + 1]);
        if (j < n)
        {
            std::copy(frame + j * 2, frame + n * 2, carry.frame);
            carry.count = n - j;
            carry.pos = 0;
        }
    }
    if (consumedFrames)
        *consumedFrames = i;
    return written;
}

} // namespace opl4
