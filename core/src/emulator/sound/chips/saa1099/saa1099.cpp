#include "saa1099.h"

#include <algorithm>
#include <bit>

#include "3rdparty/blip_buf/blip_buf.h"
#include "common/modulelogger.h"
#include "emulator/platform.h"
#include "emulator/sound/audio.h"

/// region <Constants>

static const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_SOUND;
static const uint16_t _SUBMODULE = PlatformSoundSubmodulesEnum::SUBMODULE_SOUND_SAA;

namespace
{
// Pulse-density patterns of the chip's output stage, measured on a real SAA1099P
// (published by Dave Hooper; tabulated in rejunity/tt06-psg-saa1099 README, "PDM").
// Bit i is the i-th 4 MHz slot of the 64-slot period (62.5 kHz at an 8 MHz clock).
// A voice shaped by its envelope generator sinks current where the amplitude and the
// envelope patterns are both 1; its amplitude LSB is dropped (Philips TP231: "the
// amplitude is 7/8ths that normally available").
constexpr uint64_t kAmplitudePdm[16] = {
    0x0000000000000000ull, //  0: 0/64
    0x0000000000000F00ull, //  1: 4/64
    0x00000000000FF000ull, //  2: 8/64
    0x00000000000FFF00ull, //  3: 12/64
    0x000FFFF000000000ull, //  4: 16/64
    0x000FFFF000000F00ull, //  5: 20/64
    0x000FFFF0000FF000ull, //  6: 24/64
    0x000FFFF0000FFF00ull, //  7: 28/64
    0xFFF0000FFFF0000Full, //  8: 32/64
    0xFFF0000FFFF00F0Full, //  9: 36/64
    0xFFF0000FFFFFF00Full, // 10: 40/64
    0xFFF0000FFFFFFF0Full, // 11: 44/64
    0xFFFFFFFFFFF0000Full, // 12: 48/64
    0xFFFFFFFFFFF00F0Full, // 13: 52/64
    0xFFFFFFFFFFFFF00Full, // 14: 56/64
    0xFFFFFFFFFFFFFF0Full, // 15: 60/64
};
constexpr uint64_t kEnvelopePdm[16] = {
    0x0000000000000000ull, //  0: 0/64
    0x1000100010001000ull, //  1: 4/64
    0x2020202020202020ull, //  2: 8/64
    0x3020302030203020ull, //  3: 12/64
    0x0C0C0C0C0C0C0C0Cull, //  4: 16/64
    0x1C0C1C0C1C0C1C0Cull, //  5: 20/64
    0x2C2C2C2C2C2C2C2Cull, //  6: 24/64
    0x3C2C3C2C3C2C3C2Cull, //  7: 28/64
    0xC3C3C3C3C3C3C3C3ull, //  8: 32/64
    0xD3C3D3C3D3C3D3C3ull, //  9: 36/64
    0xE3E3E3E3E3E3E3E3ull, // 10: 40/64
    0xF3E3F3E3F3E3F3E3ull, // 11: 44/64
    0xCFCFCFCFCFCFCFCFull, // 12: 48/64
    0xDFCFDFCFDFCFDFCFull, // 13: 52/64
    0xEFEFEFEFEFEFEFEFull, // 14: 56/64
    0xFFEFFFEFFFEFFFEFull, // 15: 60/64
};

// Envelope shapes (Philips TP231 Fig.3 / Table 4): the direction of each phase and
// whether the shape repeats. Shape = control bits D3-D1; bit D1 is "repeat"
enum class EnvPhase : uint8_t { Zero, Max, Up, Down };
struct EnvShape
{
    uint8_t phases;
    bool looping;
    EnvPhase phase[2];
};
constexpr EnvShape kEnvShapes[8] = {
    {1, false, {EnvPhase::Zero, EnvPhase::Zero}}, // 0 zero amplitude
    {1, true, {EnvPhase::Max, EnvPhase::Max}},    // 1 maximum amplitude
    {1, false, {EnvPhase::Down, EnvPhase::Down}}, // 2 single decay
    {1, true, {EnvPhase::Down, EnvPhase::Down}},  // 3 repetitive decay
    {2, false, {EnvPhase::Up, EnvPhase::Down}},   // 4 single triangular
    {2, true, {EnvPhase::Up, EnvPhase::Down}},    // 5 repetitive triangular
    {1, false, {EnvPhase::Up, EnvPhase::Up}},     // 6 single attack
    {1, true, {EnvPhase::Up, EnvPhase::Up}},      // 7 repetitive attack
};

constexpr uint32_t kNoiseSeed = 0x3FFFF;   // 18 bits, all ones (any non-zero seed; the chip's is unknown)
constexpr uint32_t kNoiseTaps = 0x20400;   // x^18 + x^11 + 1, Galois form, right shift
constexpr int kBlipSamples = MAX_SAMPLES_PER_FRAME + 64;
constexpr size_t kBlobSize = 1 + 32 + 4 + 6 * (4 + 3) + 2 * (4 + 4) + 2 * 11 + 4 * 8 + 2 * 4;

// Little-endian blob writer / reader: explicit fields, no struct padding in the blob
struct BlobWriter
{
    uint8_t* p;
    void U8(uint8_t v) { *p++ = v; }
    void U32(uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            *p++ = static_cast<uint8_t>(v >> (8 * i));
    }
    void U64(uint64_t v)
    {
        for (int i = 0; i < 8; i++)
            *p++ = static_cast<uint8_t>(v >> (8 * i));
    }
};
struct BlobReader
{
    const uint8_t* p;
    uint8_t U8() { return *p++; }
    uint32_t U32()
    {
        uint32_t v = 0;
        for (int i = 0; i < 4; i++)
            v |= static_cast<uint32_t>(*p++) << (8 * i);
        return v;
    }
    uint64_t U64()
    {
        uint64_t v = 0;
        for (int i = 0; i < 8; i++)
            v |= static_cast<uint64_t>(*p++) << (8 * i);
        return v;
    }
};
} // namespace

/// endregion </Constants>

Saa1099::~Saa1099()
{
    blip_delete(_blipLeft);
    blip_delete(_blipRight);
}

uint32_t Saa1099::HalfPeriod(uint8_t tone, uint8_t octave)
{
    // The 9-bit divider counts 511 - tone steps of the octave clock (chip clock / 2^(8 - octave))
    return static_cast<uint32_t>(511u - tone) << (8u - (octave & 7u));
}

void Saa1099::Configure(const Saa1099Config& cfg)
{
    _cfg = cfg;
    _cfg.hostTickRate = std::max<uint32_t>(_cfg.hostTickRate, 1);
    _cfg.chipClockHz = std::max<uint32_t>(_cfg.chipClockHz, 1);
    _cfg.outputRate = std::max<uint32_t>(_cfg.outputRate, 1);

    if (!_blipLeft)
        _blipLeft = blip_new(kBlipSamples);
    if (!_blipRight)
        _blipRight = blip_new(kBlipSamples);
    SetOutputRate(_cfg.outputRate);

    ModuleLogger* _logger = _cfg.logger;
    MLOGDEBUG("SAA1099: chip clock %u Hz, host axis %u Hz, output %u Hz, %s mode", _cfg.chipClockHz,
              _cfg.hostTickRate, _cfg.outputRate, _cfg.renderMode == Saa1099RenderMode::HiFi ? "HiFi" : "Authentic");

    _s.clockEnabled = 1;
    Reset(0);
}

void Saa1099::PowerOn()
{
    const uint8_t clockEnabled = _s.clockEnabled;
    const uint64_t hostTime = _s.hostTime;
    const uint64_t ungated = _s.ungatedClock;
    const uint64_t frameStart = _s.frameStart;
    const int32_t emittedLeft = _s.emittedLeft;
    const int32_t emittedRight = _s.emittedRight;

    _s = State{};
    _s.clockEnabled = clockEnabled;
    _s.hostTime = hostTime;
    _s.ungatedClock = ungated;
    _s.frameStart = frameStart;
    _s.emittedLeft = emittedLeft;
    _s.emittedRight = emittedRight;

    for (int i = 0; i < 6; i++)
    {
        _s.toneLevel[i] = 1; // the level SAASound found real software to need after a reset (consensus table)
        _s.toneCounter[i] = HalfPeriod(0, 0);
    }
    for (int n = 0; n < 2; n++)
    {
        _s.noiseLfsr[n] = kNoiseSeed;
        _s.noiseCounter[n] = 256;
    }
}

void Saa1099::Reset(uint64_t t)
{
    _s.hostTime = t;
    _s.ratioRemainder = 0;
    PowerOn();
    if (_blipLeft)
        blip_clear(_blipLeft);
    if (_blipRight)
        blip_clear(_blipRight);
    _s.frameStart = _s.ungatedClock;
    _s.emittedLeft = 0;
    _s.emittedRight = 0;
    _lastSample[0] = _lastSample[1] = 0;
    RefreshVoices();
    ComputeOutput(_s.outLeft, _s.outRight);
}

void Saa1099::SetClockEnabled(uint64_t t, bool enabled)
{
    Run(t);
    const bool resumed = enabled && !_s.clockEnabled;
    _s.clockEnabled = enabled ? 1 : 0;
    // Stopped, the output holds its last level; running again, it follows the registers latched meanwhile
    if (resumed)
    {
        RefreshVoices();
        UpdateOutput();
    }
}

void Saa1099::SetOutputRate(uint32_t rate)
{
    _cfg.outputRate = std::max<uint32_t>(rate, 1);
    if (_blipLeft)
    {
        blip_set_rates(_blipLeft, _cfg.chipClockHz, _cfg.outputRate);
        blip_clear(_blipLeft);
    }
    if (_blipRight)
    {
        blip_set_rates(_blipRight, _cfg.chipClockHz, _cfg.outputRate);
        blip_clear(_blipRight);
    }
    // Clock span the buffer can hold before EndFrame must drain it (32 samples of headroom)
    const uint64_t capacity = static_cast<uint64_t>(kBlipSamples - 32) * _cfg.chipClockHz / _cfg.outputRate;
    _frameCapacityClocks = static_cast<uint32_t>(std::min<uint64_t>(capacity, 0x7FFFFFFFu));
    _s.frameStart = _s.ungatedClock;
}

void Saa1099::SetRenderMode(Saa1099RenderMode mode)
{
    _cfg.renderMode = mode;
    UpdateOutput();
}

/// region <Bus>

void Saa1099::WriteAddress(uint64_t t, uint8_t value)
{
    Run(t);
    _s.address = value & 0x1F;
    // The address-write strobe clocks an envelope generator in external clock mode
    // (address #18 -> generator 0, #19 -> generator 1: SAASound, MiSTer)
    if (_s.address != 0x18 && _s.address != 0x19)
        return;
    const int e = _s.address - 0x18;
    if (!_s.env[e].external)
        return;
    EnvelopeClock(e);
    RefreshVoices();
    UpdateOutput();
}

void Saa1099::WriteData(uint64_t t, uint8_t value)
{
    Run(t);
    const uint8_t reg = _s.address;
    const uint8_t oldValue = _s.regs[reg];
    _s.regs[reg] = value;

    switch (reg)
    {
        case 0x18:
        case 0x19:
            EnvelopeWrite(reg - 0x18, value);
            break;
        case 0x1C:
            SyncWrite(value);
            break;
        case 0x16:
            // A divider leaving source 3 (clocked by a tone generator) starts a full
            // period of its new rate (SAASound; the references disagree here)
            for (int n = 0; n < 2; n++)
            {
                const uint8_t before = static_cast<uint8_t>((oldValue >> (n * 4)) & 3);
                const uint8_t after = static_cast<uint8_t>((value >> (n * 4)) & 3);
                if (before == 3 && after != 3)
                    _s.noiseCounter[n] = 256u << after;
            }
            break;
        default:
            // Amplitudes, mixers and the noise source act at once; tone and octave
            // numbers wait for the generator's next transition
            if ((reg >= 0x06 && reg <= 0x07) || (reg >= 0x0E && reg <= 0x0F) || reg == 0x13 || reg == 0x17 ||
                (reg >= 0x1A && reg <= 0x1B) || reg >= 0x1D)
            {
                ModuleLogger* _logger = _cfg.logger;
                MLOGDEBUG("SAA1099: write #%02X to unused register #%02X", value, reg);
            }
            break;
    }
    RefreshVoices();
    UpdateOutput();
}

void Saa1099::SyncWrite(uint8_t value)
{
    _s.soundEnable = value & 0x01;
    const uint8_t sync = (value >> 1) & 0x01;
    if (sync && !_s.sync)
    {
        // RST: every tone generator restarts its half period with the numbers held
        // in its registers now and waits; writes during RST act only at the first
        // transition after release (Philips TP231 "Synchronization on reset")
        for (int i = 0; i < 6; i++)
        {
            _s.toneFreq[i] = _s.regs[0x08 + i];
            _s.toneOct[i] = static_cast<uint8_t>((_s.regs[0x10 + i / 2] >> ((i & 1) * 4)) & 7);
            _s.toneCounter[i] = HalfPeriod(_s.toneFreq[i], _s.toneOct[i]);
            _s.toneLevel[i] = 1;
        }
    }
    if (!sync && _s.sync)
    {
        // Release: the fixed-rate noise dividers start a full period of the source
        // selected now (SAASound and the MiSTer RTL agree; Philips does not say)
        for (int n = 0; n < 2; n++)
        {
            const uint8_t source = static_cast<uint8_t>((_s.regs[0x16] >> (n * 4)) & 3);
            _s.noiseCounter[n] = 256u << (source == 3 ? 0 : source);
        }
    }
    _s.sync = sync;
}

/// endregion </Bus>

/// region <Time>

void Saa1099::Run(uint64_t t)
{
    if (t <= _s.hostTime)
        return;
    uint64_t dt = t - _s.hostTime;
    _s.hostTime = t;
    while (dt)
    {
        // Chunks keep chunk * chipClock + remainder inside 64 bits
        const uint64_t chunk = std::min<uint64_t>(dt, 1ull << 30);
        dt -= chunk;
        const uint64_t total = chunk * _cfg.chipClockHz + _s.ratioRemainder;
        _s.ratioRemainder = total % _cfg.hostTickRate;
        Advance(total / _cfg.hostTickRate);
    }
}

void Saa1099::Advance(uint64_t chipClocks)
{
    if (!chipClocks)
        return;
    if (_s.clockEnabled)
        StepGenerators(chipClocks);
    else
        _s.ungatedClock += chipClocks; // the output axis runs on, the chip holds
}

void Saa1099::StepGenerators(uint64_t clocks)
{
    const bool authentic = _cfg.renderMode == Saa1099RenderMode::Authentic;
    while (clocks)
    {
        uint64_t step = clocks;
        if (!_s.sync)
        {
            for (int i = 0; i < 6; i++)
                step = std::min<uint64_t>(step, _s.toneCounter[i]);
            for (int n = 0; n < 2; n++)
                if (((_s.regs[0x16] >> (n * 4)) & 3) != 3)
                    step = std::min<uint64_t>(step, _s.noiseCounter[n]);
        }
        if (authentic)
            step = std::min<uint64_t>(step, 2 - (_s.chipClock & 1)); // next PDM slot

        clocks -= step;
        _s.chipClock += step;
        _s.ungatedClock += step;

        bool changed = false;
        if (!_s.sync)
        {
            const uint32_t s32 = static_cast<uint32_t>(step);
            for (int i = 0; i < 6; i++)
            {
                _s.toneCounter[i] -= s32;
                if (_s.toneCounter[i] == 0)
                {
                    ToneTransition(i);
                    changed = true;
                }
            }
            for (int n = 0; n < 2; n++)
            {
                const uint8_t source = static_cast<uint8_t>((_s.regs[0x16] >> (n * 4)) & 3);
                if (source == 3)
                    continue; // fixed-rate divider frozen while a tone generator clocks the noise
                _s.noiseCounter[n] -= s32;
                if (_s.noiseCounter[n] == 0)
                {
                    NoiseShift(n);
                    _s.noiseCounter[n] = 256u << source;
                    changed = true;
                }
            }
        }
        if (changed)
        {
            RefreshVoices();
            UpdateOutput();
        }
        else if (authentic)
            UpdateOutput();
    }
}

/// endregion </Time>

/// region <Generators>

void Saa1099::ToneTransition(int i)
{
    _s.toneLevel[i] ^= 1;
    // New tone and octave numbers are acted upon at a transition (Philips TP231)
    _s.toneFreq[i] = _s.regs[0x08 + i];
    _s.toneOct[i] = static_cast<uint8_t>((_s.regs[0x10 + i / 2] >> ((i & 1) * 4)) & 7);
    _s.toneCounter[i] = HalfPeriod(_s.toneFreq[i], _s.toneOct[i]);

    // Every transition (both edges) of generator 0 / 3 clocks noise generator 0 / 1 in
    // source 3, and of generator 1 / 4 envelope generator 0 / 1 in internal clock mode
    if (i == 0 && (_s.regs[0x16] & 3) == 3)
        NoiseShift(0);
    else if (i == 3 && ((_s.regs[0x16] >> 4) & 3) == 3)
        NoiseShift(1);
    else if (i == 1 && !_s.env[0].external)
        EnvelopeClock(0);
    else if (i == 4 && !_s.env[1].external)
        EnvelopeClock(1);
}

void Saa1099::NoiseShift(int n)
{
    uint32_t& r = _s.noiseLfsr[n];
    r = (r & 1) ? ((r >> 1) ^ kNoiseTaps) : (r >> 1);
}

void Saa1099::EnvelopeWrite(int e, uint8_t value)
{
    EnvelopeState& env = _s.env[e];
    env.control = value;
    const bool enable = (value & 0x80) != 0;
    if (!enable)
    {
        // D7 is direct-acting: the generator stops and the voice plays unshaped
        env.enabled = 0;
        env.ended = 1;
        env.pending = 0;
        env.phase = 0;
        env.position = 0;
        return;
    }
    env.enabled = 1;

    // D4 (resolution) is direct-acting. A switch keeps the position; going to 3 bits
    // drops its LSB, going back to 4 bits sets it (SAASound's measured behavior)
    const uint8_t res3 = (value & 0x10) ? 1 : 0;
    if (!env.res3 && res3)
        env.position &= 0x0E;
    else if (env.res3 && !res3)
        env.position |= 0x01;
    env.res3 = res3;

    // Shape, clock source and inversion are buffered: they act at once only when no
    // envelope is running (point 3 reached, or the generator was off)
    if (env.ended)
        EnvelopeApply(e, value);
    else
    {
        env.pending = 1;
        env.pendingValue = value;
    }
}

void Saa1099::EnvelopeApply(int e, uint8_t value)
{
    EnvelopeState& env = _s.env[e];
    env.shape = static_cast<uint8_t>((value >> 1) & 7);
    env.invert = value & 0x01;
    env.external = (value >> 5) & 0x01;
    env.res3 = (value >> 4) & 0x01;
    env.phase = 0;
    env.position = 0;
    env.ended = 0;
    env.pending = 0;
}

void Saa1099::EnvelopeClock(int e)
{
    EnvelopeState& env = _s.env[e];
    if (!env.enabled || env.ended)
        return;
    const EnvShape& shape = kEnvShapes[env.shape];
    env.position = static_cast<uint8_t>(env.position + (env.res3 ? 2 : 1));
    if (env.position < 16)
        return;
    env.position = static_cast<uint8_t>(env.position - 16);
    env.phase++;
    if (env.phase < shape.phases)
        return;

    // Point 3 (single shapes: the end, level 0 from now on) or point 4 (repeating
    // shapes: the loop point); a buffered write acts here
    env.phase = 0;
    if (!shape.looping)
    {
        env.ended = 1;
        env.position = 0;
    }
    if (env.pending)
        EnvelopeApply(e, env.pendingValue);
}

void Saa1099::EnvelopeLevels(int e, uint8_t& left, uint8_t& right) const
{
    const EnvelopeState& env = _s.env[e];
    const EnvShape& shape = kEnvShapes[env.shape];
    const uint8_t mask = env.res3 ? 0x0E : 0x0F;
    uint8_t level = 0;
    if (!(env.ended && !shape.looping))
    {
        switch (shape.phase[env.phase & 1])
        {
            case EnvPhase::Zero: level = 0; break;
            case EnvPhase::Max: level = 15; break;
            case EnvPhase::Up: level = env.position; break;
            case EnvPhase::Down: level = static_cast<uint8_t>(15 - env.position); break;
        }
    }
    left = level & mask;
    right = env.invert ? static_cast<uint8_t>(mask - left) : left;
}

bool Saa1099::EnvelopeShapes(int voice) const
{
    return (voice == 2 || voice == 5) && _s.env[voice / 3].enabled;
}

/// endregion </Generators>

/// region <Output>

void Saa1099::RefreshVoices()
{
    const bool silent = !_s.soundEnable || _s.sync;
    for (int v = 0; v < 6; v++)
    {
        const uint8_t tone = _s.toneLevel[v];
        const uint8_t noise = static_cast<uint8_t>(_s.noiseLfsr[v / 3] & 1);
        const bool toneOn = (_s.regs[0x14] >> v) & 1;
        const bool noiseOn = (_s.regs[0x15] >> v) & 1;

        // The mixer drives the even and the odd PDM periods separately: tone in the
        // even ones, tone gated by "no noise" in the odd ones when both are enabled.
        // Mean: tone alone full, tone + noise half while the noise bit is 1
        uint8_t even = 0;
        uint8_t odd = 0;
        if (toneOn && noiseOn)
        {
            even = tone;
            odd = static_cast<uint8_t>(tone & (noise ^ 1));
        }
        else if (toneOn)
            even = odd = tone;
        else if (noiseOn)
            even = odd = noise;

        const uint8_t amp[2] = {static_cast<uint8_t>(_s.regs[v] & 0x0F), static_cast<uint8_t>(_s.regs[v] >> 4)};
        if (EnvelopeShapes(v))
        {
            // An envelope-shaped voice sinks current while the mixer output is LOW
            // (SAASound and the MiSTer RTL agree), through amplitude AND envelope PDM
            uint8_t envLevel[2];
            EnvelopeLevels(v / 3, envLevel[0], envLevel[1]);
            for (int s = 0; s < 2; s++)
                _voiceMask[v][s] = kAmplitudePdm[amp[s] & 0x0E] & kEnvelopePdm[envLevel[s]];
            even ^= 1;
            odd ^= 1;
        }
        else
        {
            for (int s = 0; s < 2; s++)
                _voiceMask[v][s] = kAmplitudePdm[amp[s]];
        }
        if (silent)
            even = odd = 0;
        _gateEven[v] = even;
        _gateOdd[v] = odd;
        for (int s = 0; s < 2; s++)
            _voiceLevel[v][s] = static_cast<uint16_t>(std::popcount(_voiceMask[v][s]) * (even + odd));
    }
}

void Saa1099::ComputeOutput(int32_t& left, int32_t& right) const
{
    left = right = 0;
    if (_cfg.renderMode == Saa1099RenderMode::Authentic)
    {
        // One PDM slot = two chip clocks; even / odd 64-slot periods alternate
        const unsigned slot = static_cast<unsigned>((_s.chipClock >> 1) & 63);
        const bool oddPeriod = ((_s.chipClock >> 7) & 1) != 0;
        for (int v = 0; v < 6; v++)
        {
            const uint8_t gate = oddPeriod ? _gateOdd[v] : _gateEven[v];
            left += static_cast<int32_t>((_voiceMask[v][0] >> slot) & gate);
            right += static_cast<int32_t>((_voiceMask[v][1] >> slot) & gate);
        }
        // Scaled so that the mean over 128 slots equals the HiFi level
        left *= 128;
        right *= 128;
        return;
    }
    for (int v = 0; v < 6; v++)
    {
        left += _voiceLevel[v][0];
        right += _voiceLevel[v][1];
    }
}

void Saa1099::UpdateOutput()
{
    // With the chip clock stopped the output logic does not run: the output holds its last level whatever the
    // registers say (tdd-saa1099 "Clock gate"; the writes are latched and act when the clock runs again)
    if (_s.clockEnabled)
        ComputeOutput(_s.outLeft, _s.outRight);
    Emit(_s.outLeft, _s.outRight);
}

void Saa1099::Emit(int32_t left, int32_t right)
{
    const int32_t sampleLeft = left * kOutputGain;
    const int32_t sampleRight = right * kOutputGain;
    const int32_t deltaLeft = sampleLeft - _s.emittedLeft;
    const int32_t deltaRight = sampleRight - _s.emittedRight;
    if (!deltaLeft && !deltaRight)
        return;
    const uint64_t pos = std::min<uint64_t>(_s.ungatedClock - _s.frameStart,
                                            _frameCapacityClocks ? _frameCapacityClocks - 1 : 0);
    if (_blipLeft && deltaLeft)
        blip_add_delta(_blipLeft, static_cast<unsigned>(pos), deltaLeft);
    if (_blipRight && deltaRight)
        blip_add_delta(_blipRight, static_cast<unsigned>(pos), deltaRight);
    _s.emittedLeft = sampleLeft;
    _s.emittedRight = sampleRight;
}

size_t Saa1099::EndFrame(uint64_t t, int16_t* stereo, size_t frames)
{
    Run(t);
    frames = std::min<size_t>(frames, MAX_SAMPLES_PER_FRAME);
    const uint64_t length = std::min<uint64_t>(_s.ungatedClock - _s.frameStart, _frameCapacityClocks);
    _s.frameStart = _s.ungatedClock;
    if (!_blipLeft || !_blipRight || !stereo)
        return 0;

    blip_end_frame(_blipLeft, static_cast<unsigned>(length));
    blip_end_frame(_blipRight, static_cast<unsigned>(length));
    const int want = static_cast<int>(frames);
    const int gotLeft = blip_read_samples(_blipLeft, stereo, want, 1);
    const int gotRight = blip_read_samples(_blipRight, stereo + 1, want, 1);
    if (gotLeft > 0)
        _lastSample[0] = stereo[(gotLeft - 1) * 2];
    if (gotRight > 0)
        _lastSample[1] = stereo[(gotRight - 1) * 2 + 1];
    for (int i = gotLeft; i < want; i++)
        stereo[i * 2] = _lastSample[0];
    for (int i = gotRight; i < want; i++)
        stereo[i * 2 + 1] = _lastSample[1];

    // A caller that reads fewer frames than it renders would fill the buffer: drop the
    // surplus beyond a few frames of slack
    int16_t discard[256];
    while (blip_samples_avail(_blipLeft) > 256)
        blip_read_samples(_blipLeft, discard, 256, 0);
    while (blip_samples_avail(_blipRight) > 256)
        blip_read_samples(_blipRight, discard, 256, 0);
    return frames;
}

/// endregion </Output>

/// region <Report>

void Saa1099::Describe(Saa1099Report& out) const
{
    for (int i = 0; i < 32; i++)
        out.registers[i] = _s.regs[i];
    out.addressLatch = _s.address;
    out.soundEnabled = _s.soundEnable != 0;
    out.sync = _s.sync != 0;
    out.clockEnabled = _s.clockEnabled != 0;
    out.renderMode = _cfg.renderMode;
    out.chipClockHz = _cfg.chipClockHz;
    out.hostTickRate = _cfg.hostTickRate;
    out.outputRate = _cfg.outputRate;
    out.chipClocks = _s.chipClock;
    out.outputLeft = static_cast<uint16_t>(_s.outLeft);
    out.outputRight = static_cast<uint16_t>(_s.outRight);

    for (int i = 0; i < 6; i++)
    {
        Saa1099Report::Tone& t = out.tones[i];
        t.toneRegister = _s.regs[0x08 + i];
        t.octaveRegister = static_cast<uint8_t>((_s.regs[0x10 + i / 2] >> ((i & 1) * 4)) & 7);
        t.toneLatched = _s.toneFreq[i];
        t.octaveLatched = _s.toneOct[i];
        t.level = _s.toneLevel[i];
        t.halfPeriod = HalfPeriod(_s.toneFreq[i], _s.toneOct[i]);
        t.clocksToTransition = _s.toneCounter[i];
        t.frequencyHz = static_cast<double>(_cfg.chipClockHz) / (2.0 * t.halfPeriod);

        Saa1099Report::Voice& v = out.voices[i];
        v.amplitudeLeft = _s.regs[i] & 0x0F;
        v.amplitudeRight = _s.regs[i] >> 4;
        v.toneEnabled = (_s.regs[0x14] >> i) & 1;
        v.noiseEnabled = (_s.regs[0x15] >> i) & 1;
        v.envelopeShaped = EnvelopeShapes(i);
        v.levelLeft = _voiceLevel[i][0];
        v.levelRight = _voiceLevel[i][1];
    }
    for (int n = 0; n < 2; n++)
    {
        Saa1099Report::Noise& r = out.noise[n];
        r.source = static_cast<uint8_t>((_s.regs[0x16] >> (n * 4)) & 3);
        r.lfsr = _s.noiseLfsr[n];
        r.output = static_cast<uint8_t>(_s.noiseLfsr[n] & 1);
        r.clocksToShift = _s.noiseCounter[n];
    }
    for (int e = 0; e < 2; e++)
    {
        const EnvelopeState& env = _s.env[e];
        Saa1099Report::Envelope& r = out.envelopes[e];
        r.controlRegister = env.control;
        r.enabled = env.enabled != 0;
        r.resolution3Bit = env.res3 != 0;
        r.shape = env.shape;
        r.invertRight = env.invert != 0;
        r.externalClock = env.external != 0;
        r.phase = env.phase;
        r.position = env.position;
        r.ended = env.ended != 0;
        r.pending = env.pending != 0;
        r.pendingValue = env.pendingValue;
        EnvelopeLevels(e, r.levelLeft, r.levelRight);
    }
}

/// endregion </Report>

/// region <TTD>

size_t Saa1099::TTDStateSize() const
{
    return kBlobSize;
}

void Saa1099::TTDSaveState(uint8_t* dst) const
{
    BlobWriter w{dst};
    w.U8(kStateVersion);
    for (uint8_t r : _s.regs)
        w.U8(r);
    w.U8(_s.address);
    w.U8(_s.soundEnable);
    w.U8(_s.sync);
    w.U8(_s.clockEnabled);
    for (int i = 0; i < 6; i++)
    {
        w.U32(_s.toneCounter[i]);
        w.U8(_s.toneLevel[i]);
        w.U8(_s.toneFreq[i]);
        w.U8(_s.toneOct[i]);
    }
    for (int n = 0; n < 2; n++)
    {
        w.U32(_s.noiseLfsr[n]);
        w.U32(_s.noiseCounter[n]);
    }
    for (const EnvelopeState& env : _s.env)
    {
        w.U8(env.control);
        w.U8(env.enabled);
        w.U8(env.res3);
        w.U8(env.shape);
        w.U8(env.invert);
        w.U8(env.external);
        w.U8(env.phase);
        w.U8(env.position);
        w.U8(env.ended);
        w.U8(env.pending);
        w.U8(env.pendingValue);
    }
    w.U64(_s.hostTime);
    w.U64(_s.ratioRemainder);
    w.U64(_s.chipClock);
    w.U64(_s.ungatedClock);
    // The output level: with the clock stopped it is held, not derivable from the registers (version 2)
    w.U32(static_cast<uint32_t>(_s.outLeft));
    w.U32(static_cast<uint32_t>(_s.outRight));
}

void Saa1099::TTDLoadState(const uint8_t* src)
{
    BlobReader r{src};
    if (r.U8() != kStateVersion)
    {
        ModuleLogger* _logger = _cfg.logger;
        MLOGWARNING("SAA1099 TTD restore refused: blob layout version %u, expected %u", src[0], kStateVersion);
        return;
    }
    for (uint8_t& reg : _s.regs)
        reg = r.U8();
    _s.address = static_cast<uint8_t>(r.U8() & 0x1F);
    _s.soundEnable = r.U8() & 1;
    _s.sync = r.U8() & 1;
    _s.clockEnabled = r.U8() & 1;
    for (int i = 0; i < 6; i++)
    {
        _s.toneCounter[i] = r.U32();
        _s.toneLevel[i] = r.U8() & 1;
        _s.toneFreq[i] = r.U8();
        _s.toneOct[i] = r.U8() & 7;
        if (_s.toneCounter[i] == 0)
            _s.toneCounter[i] = HalfPeriod(_s.toneFreq[i], _s.toneOct[i]);
    }
    for (int n = 0; n < 2; n++)
    {
        _s.noiseLfsr[n] = r.U32() & 0x3FFFF;
        _s.noiseCounter[n] = std::max<uint32_t>(r.U32(), 1);
    }
    for (EnvelopeState& env : _s.env)
    {
        env.control = r.U8();
        env.enabled = r.U8() & 1;
        env.res3 = r.U8() & 1;
        env.shape = r.U8() & 7;
        env.invert = r.U8() & 1;
        env.external = r.U8() & 1;
        env.phase = r.U8() & 1;
        env.position = r.U8() & 15;
        env.ended = r.U8() & 1;
        env.pending = r.U8() & 1;
        env.pendingValue = r.U8();
    }
    _s.hostTime = r.U64();
    _s.ratioRemainder = r.U64() % _cfg.hostTickRate;
    _s.chipClock = r.U64();
    _s.ungatedClock = r.U64();
    _s.outLeft = static_cast<int32_t>(r.U32());
    _s.outRight = static_cast<int32_t>(r.U32());

    // The output buffers belong to the host: restart the frame here from silence and
    // step to the restored level at its start. The output is unipolar and blip_buf
    // integrates its steps, so the level must be in the buffer as a step
    if (_blipLeft)
        blip_clear(_blipLeft);
    if (_blipRight)
        blip_clear(_blipRight);
    _s.frameStart = _s.ungatedClock;
    _s.emittedLeft = 0;
    _s.emittedRight = 0;
    RefreshVoices();
    UpdateOutput();
}

uint64_t Saa1099::TTDHashState() const
{
    uint8_t blob[kBlobSize];
    TTDSaveState(blob);
    uint64_t h = 14695981039346656037ull;
    for (uint8_t b : blob)
    {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}

/// endregion </TTD>
