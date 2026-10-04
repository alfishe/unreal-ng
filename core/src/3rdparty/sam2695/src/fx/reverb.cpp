// libsam2695 - reverb implementation (see reverb.h).
#include "fx/reverb.h"

#include "sam2695/sam2695config.h"

#include <algorithm>
#include <cmath>

namespace sam2695
{

namespace
{

// Dattorro's figure is drawn at 29 761 Hz; every length scales to the chip's rate and the program size.
constexpr double kDattorroRate = 29761.0;
constexpr double kRateScale = kInternalRate / kDattorroRate;
constexpr std::array<uint32_t, 4> kDiffusers = {142, 107, 379, 277};
// tank, in the Line order: modulated all-pass, delay, all-pass, delay (left), then the same (right)
constexpr std::array<uint32_t, 8> kTank = {672, 4453, 1800, 3720, 908, 4217, 2656, 3163};
constexpr double kExcursion = 16.0;     // modulated all-pass excursion at 29 761 Hz
constexpr double kModulationHz = 0.85;  // the tank's modulation LFO
constexpr double kMaxPredelayMs = 40.0;
// Output taps (Dattorro, table 2): line, offset, sign. Left from the right half and vice versa.
struct Tap
{
    int line;
    uint32_t offset;
    float sign;
};
constexpr std::array<std::array<Tap, 7>, 2> kTaps = {{
    {{{5, 266, 1.0f}, {5, 2974, 1.0f}, {6, 1913, -1.0f}, {7, 1996, 1.0f}, {1, 1990, -1.0f}, {2, 187, -1.0f}, {3, 1066, -1.0f}}},
    {{{1, 353, 1.0f}, {1, 3627, 1.0f}, {2, 1228, -1.0f}, {3, 2673, 1.0f}, {5, 2111, -1.0f}, {6, 335, -1.0f}, {7, 121, -1.0f}}},
}};

// The six tank programs. Decay is the time to -60 dB at REV_TIME = 7Fh; damping is the tank's one-pole
// coefficient (Dattorro's plate: 0.0005); bandwidth the input one-pole (Dattorro: 0.9995).
struct TankProgram
{
    double size;
    double decaySeconds;
    double predelayMs;
    double bandwidth;
    double damping;
};
constexpr std::array<TankProgram, 6> kPrograms = {{
    {0.40, 0.55, 2.0, 0.70, 0.45},   // room1: small, dark
    {0.50, 0.85, 4.0, 0.75, 0.40},   // room2
    {0.62, 1.25, 6.0, 0.80, 0.33},   // room3
    {0.82, 1.90, 12.0, 0.85, 0.25},  // hall1
    {1.00, 2.60, 18.0, 0.88, 0.20},  // hall2 (power-up)
    {0.75, 2.10, 0.0, 0.9995, 0.05}, // plate: Dattorro's own voicing
}};

// Delay programs: REV_TIME t gives 2.8 ms x (t + 1) (70 ms at the delay default 18h, 358 ms at 7Fh)
constexpr double kEchoMsPerStep = 2.8;
constexpr uint32_t kMaxEcho = static_cast<uint32_t>(kEchoMsPerStep * 128.0 * kInternalRate / 1000.0) + 2;

uint32_t Scaled(uint32_t len, double scale)
{
    return std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(len * scale)));
}

uint32_t TankPoolSize()
{
    const double scale = kRateScale * 1.0; // the largest program size
    uint32_t total = static_cast<uint32_t>(kMaxPredelayMs * kInternalRate / 1000.0) + 2;
    for (uint32_t d : kDiffusers)
        total += Scaled(d, scale) + 1;
    const uint32_t exc = static_cast<uint32_t>(std::ceil(2.0 * kExcursion * kRateScale)) + 3; // the read swings 0 .. 2 x excursion
    for (size_t i = 0; i < kTank.size(); i++)
        total += Scaled(kTank[i], scale) + 1 + ((i == 0 || i == 4) ? exc : 0);
    return total;
}

} // namespace

void Reverb::Allocate()
{
    _pool.assign(std::max(TankPoolSize(), 2 * kMaxEcho), 0.0f);
    _layoutCharacter = 0xFF;
}

void Reverb::Layout(uint8_t character)
{
    _layoutCharacter = character;
    uint32_t next = 0;
    auto place = [&next](DelayLine& l, uint32_t size) {
        l.base = next;
        l.size = size;
        l.pos = 0;
        next += size;
    };
    if (character >= 6)
    {
        place(_echo[0], kMaxEcho);
        place(_echo[1], kMaxEcho);
        _predelay = DelayLine{};
        _diffuser.fill(DelayLine{});
        _tank.fill(DelayLine{});
    }
    else
    {
        const double scale = kRateScale * kPrograms[character].size;
        place(_predelay, static_cast<uint32_t>(kMaxPredelayMs * kInternalRate / 1000.0) + 2);
        for (size_t i = 0; i < kDiffusers.size(); i++)
            place(_diffuser[i], Scaled(kDiffusers[i], scale) + 1);
        const uint32_t exc = static_cast<uint32_t>(std::ceil(2.0 * kExcursion * kRateScale)) + 3; // the read swings 0 .. 2 x excursion
        for (size_t i = 0; i < kTank.size(); i++)
            place(_tank[i], Scaled(kTank[i], scale) + 1 + ((i == Ap1 || i == Ap3) ? exc : 0));
        _echo.fill(DelayLine{});
    }
    Clear();
}

void Reverb::Clear()
{
    std::fill(_pool.begin(), _pool.end(), 0.0f);
    _bandState = 0.0f;
    _damp.fill(0.0f);
    _echoDamp.fill(0.0f);
    _lfoPhase = 0;
}

void Reverb::Update(const FxParams& p)
{
    const uint8_t character = p.reverbCharacter & 7;
    if (character != _layoutCharacter)
        Layout(character);
    _level = p.reverbLevel / 127.0f;
    _echoMode = character >= 6;
    _panEcho = character == 7;
    if (_echoMode)
    {
        _echoLength = std::min<uint32_t>(
            kMaxEcho - 1, static_cast<uint32_t>(std::lround(kEchoMsPerStep * (p.reverbTime + 1) * kInternalRate / 1000.0)));
        _echoFeedback = std::min(p.reverbFeedback / 128.0f, 0.95f);
        _rt60 = 0.0;
        return;
    }
    const TankProgram& prog = kPrograms[character];
    const double scale = kRateScale * prog.size;
    _predelayLength = std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(prog.predelayMs * kInternalRate / 1000.0)));
    for (size_t i = 0; i < kDiffusers.size(); i++)
        _diffLength[i] = Scaled(kDiffusers[i], scale);
    uint32_t loop = 0;
    for (size_t i = 0; i < kTank.size(); i++)
    {
        _length[i] = Scaled(kTank[i], scale);
        loop += _length[i];
    }
    for (int side = 0; side < 2; side++)
        for (int k = 0; k < 7; k++)
            _tap[side][k] = std::min(Scaled(kTaps[side][k].offset, scale), _length[kTaps[side][k].line]);
    _excursion = static_cast<float>(kExcursion * kRateScale);
    _lfoIncrement = static_cast<uint32_t>(kModulationHz / kInternalRate * 4294967296.0);
    _bandwidth = static_cast<float>(prog.bandwidth);
    _damping = static_cast<float>(prog.damping);
    // REV_TIME scales the decay time: 7Fh = the program's own, each 32 steps down halves it
    _rt60 = prog.decaySeconds * std::exp2((static_cast<double>(p.reverbTime) - 127.0) / 32.0);
    // the figure-eight loop passes four decay multipliers: g^4 = 10^(-3 x loop time / RT60)
    const double loopSeconds = loop / static_cast<double>(kInternalRate);
    _decay = static_cast<float>(std::fmin(std::pow(10.0, -3.0 * loopSeconds / (4.0 * _rt60)), 0.98));
    _decayDiffusion2 = std::clamp(_decay + 0.15f, 0.25f, 0.5f);
}

void Reverb::Sanitize(const FxParams& p)
{
    bool ok = _layoutCharacter == (p.reverbCharacter & 7);
    auto check = [&](const DelayLine& l) {
        ok = ok && l.size >= 1 && l.pos < l.size && static_cast<size_t>(l.base) + l.size <= _pool.size();
    };
    if (ok && _layoutCharacter >= 6)
        for (const DelayLine& l : _echo)
            check(l);
    else if (ok)
    {
        check(_predelay);
        for (const DelayLine& l : _diffuser)
            check(l);
        for (const DelayLine& l : _tank)
            check(l);
    }
    if (!ok)
        _layoutCharacter = 0xFF; // the next Update lays out again and clears
    Update(p);
}

void Reverb::Process(const float* in, float* outL, float* outR, uint32_t n)
{
    float* pool = _pool.data();
    if (_echoMode)
    {
        for (uint32_t i = 0; i < n; i++)
        {
            const float yl = _echo[0].Read(pool, _echoLength);
            const float yr = _echo[1].Read(pool, _echoLength);
            // a gentle high cut in the loop keeps the repeats from building up brightness
            _echoDamp[0] = Flush(yl + 0.25f * (_echoDamp[0] - yl));
            _echoDamp[1] = Flush(yr + 0.25f * (_echoDamp[1] - yr));
            if (_panEcho)
            {
                // ping-pong: the left echo feeds the right line, the right one feeds back left
                _echo[0].Write(pool, Flush(in[i] + _echoFeedback * _echoDamp[1]));
                _echo[1].Write(pool, _echoDamp[0]);
                outL[i] += _level * yl;
                outR[i] += _level * yr;
            }
            else
            {
                _echo[0].Write(pool, Flush(in[i] + _echoFeedback * _echoDamp[0]));
                outL[i] += _level * yl;
                outR[i] += _level * yl;
            }
        }
        return;
    }
    const float gain = 0.6f * _level;
    for (uint32_t i = 0; i < n; i++)
    {
        float x = _predelay.Read(pool, _predelayLength);
        _predelay.Write(pool, in[i]);
        _bandState = Flush(_bandState + _bandwidth * (x - _bandState));
        x = _bandState;
        x = _diffuser[0].AllPass(pool, x, _diffLength[0], _inDiffusion1);
        x = _diffuser[1].AllPass(pool, x, _diffLength[1], _inDiffusion1);
        x = _diffuser[2].AllPass(pool, x, _diffLength[2], _inDiffusion2);
        x = _diffuser[3].AllPass(pool, x, _diffLength[3], _inDiffusion2);

        const float lfo = PhaseSine(_lfoPhase);
        const float lfoQ = PhaseSine(_lfoPhase + 0x40000000u);
        _lfoPhase += _lfoIncrement;
        // cross feedback: each half takes the other half's last delay output
        const float fromRight = _tank[D4].Read(pool, _length[D4]);
        const float fromLeft = _tank[D2].Read(pool, _length[D2]);
        for (int side = 0; side < 2; side++)
        {
            const int ap1 = side == 0 ? Ap1 : Ap3;
            const int d1 = ap1 + 1, ap2 = ap1 + 2, d2 = ap1 + 3;
            float a = x + _decay * (side == 0 ? fromRight : fromLeft);
            const float mod = static_cast<float>(_length[ap1]) + _excursion * (1.0f + (side == 0 ? lfo : lfoQ));
            a = _tank[ap1].AllPassFrac(pool, a, mod, -0.70f);
            const float t = _tank[d1].Read(pool, _length[d1]);
            _tank[d1].Write(pool, Flush(a));
            _damp[side] = Flush(t + _damping * (_damp[side] - t));
            const float b = _tank[ap2].AllPass(pool, _damp[side] * _decay, _length[ap2], _decayDiffusion2);
            _tank[d2].Write(pool, Flush(b));
        }
        float y[2] = {0.0f, 0.0f};
        for (int side = 0; side < 2; side++)
            for (int k = 0; k < 7; k++)
            {
                const Tap& tp = kTaps[side][k];
                y[side] += tp.sign * _tank[tp.line].Read(pool, _tap[side][k]);
            }
        outL[i] += gain * y[0];
        outR[i] += gain * y[1];
    }
}

} // namespace sam2695
