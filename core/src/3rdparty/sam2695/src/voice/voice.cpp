// libsam2695 - voice implementation.
#include "voice/voice.h"

#include "bank/generators.h"
#include "common/conv.h"
#include "voice/interpolator.h"

#include <algorithm>
#include <cmath>

namespace sam2695
{

namespace
{

constexpr double kPi = 3.14159265358979323846;

// Append or replace (same source, destination, amount source) within list[from .. count)
void MergeModulator(std::array<ModulatorDef, kMaxVoiceModulators>& list, uint8_t& count, uint8_t from,
                    const ModulatorDef& m)
{
    for (uint8_t i = from; i < count; i++)
        if (SameModulator(list[i], m))
        {
            list[i] = m;
            return;
        }
    if (count < kMaxVoiceModulators)
        list[count++] = m;
}

void MergeZoneModulators(const BankModel& bank, const Zone* z, std::array<ModulatorDef, kMaxVoiceModulators>& list,
                         uint8_t& count, uint8_t from)
{
    if (z == nullptr)
        return;
    for (uint32_t i = 0; i < z->modCount; i++)
        MergeModulator(list, count, from, bank.modulators[z->modFirst + i]);
}

double SampleValueScale() { return 1.0 / 8388608.0; } // 24-bit full scale

inline float ReadFrame(const int16_t* d16, const uint8_t* d24, uint32_t i)
{
    const int32_t v = (static_cast<int32_t>(d16[i]) * 256) + (d24 != nullptr ? d24[i] : 0);
    return static_cast<float>(v) * static_cast<float>(SampleValueScale());
}

} // namespace

float Voice::G(Gen g) const
{
    const int i = static_cast<int>(g);
    const float v = static_cast<float>(base[i]) + modSum[i];
    if (g == Gen::Pitch)
        return v;
    const GenInfo& info = GenInfoOf(i);
    return std::clamp(v, static_cast<float>(info.minValue), static_cast<float>(info.maxValue));
}

float Voice::Source(uint16_t src, const Channel& ch) const
{
    // raw value and its range (SF2 8.2.1): 7-bit sources over 128, the pitch wheel over 16384, the
    // pitch-wheel sensitivity in semitones over 127 (so the 12700-cent default amount gives
    // exactly 100 cents per semitone)
    const uint16_t index = src & modsrc::kIndexMask;
    double raw = 0.0;
    double range = 128.0;
    if (src & modsrc::kCcFlag)
        raw = ch.cc[index];
    else
    {
        switch (index)
        {
            case modsrc::kNone:
                raw = range; // "no controller": a constant 1
                break;
            case modsrc::kNoteOnVelocity:
                raw = base[static_cast<int>(Gen::Velocity)] >= 0 ? base[static_cast<int>(Gen::Velocity)] : velocity;
                break;
            case modsrc::kNoteOnKey:
                raw = base[static_cast<int>(Gen::Keynum)] >= 0 ? base[static_cast<int>(Gen::Keynum)] : key;
                break;
            case modsrc::kPolyPressure:
                raw = ch.polyPressure[key];
                break;
            case modsrc::kChannelPressure:
                raw = ch.channelPressure;
                break;
            case modsrc::kPitchWheel:
                raw = ch.pitchBend;
                range = 16384.0;
                break;
            case modsrc::kPitchWheelSensitivity:
                raw = ch.PitchWheelSensitivity();
                range = 127.0;
                break;
            default:
                return 0.0f;
        }
    }
    double x = raw / range;
    if (src & modsrc::kDirection)
        x = 1.0 - x;
    const bool bipolar = (src & modsrc::kPolarity) != 0;
    const int type = (src >> modsrc::kTypeShift) & 0x3F;
    double y = 0.0;
    switch (type)
    {
        case 0: // linear
            y = bipolar ? 2.0 * x - 1.0 : x;
            break;
        case 1: // concave
            y = bipolar ? (x > 0.5 ? ConcaveCurve(2.0 * x - 1.0) : -ConcaveCurve(1.0 - 2.0 * x)) : ConcaveCurve(x);
            break;
        case 2: // convex
            y = bipolar ? (x > 0.5 ? ConvexCurve(2.0 * x - 1.0) : -ConvexCurve(1.0 - 2.0 * x)) : ConvexCurve(x);
            break;
        case 3: // switch
            y = x >= 0.5 ? 1.0 : (bipolar ? -1.0 : 0.0);
            break;
        default:
            return 0.0f; // undefined curve types: the modulator is inactive (8.2.1)
    }
    return static_cast<float>(y);
}

void Voice::UpdateModulators(const Channel& ch)
{
    modSum.fill(0.0f);
    for (uint8_t i = 0; i < modCount; i++)
    {
        const ModulatorDef& m = mods[i];
        const float a = Source(m.src, ch);
        if (a == 0.0f)
            continue;
        const float b = Source(m.amtSrc, ch);
        float v = static_cast<float>(m.amount) * a * b;
        if (m.transform == 2)
            v = std::fabs(v);
        if (m.dest < kGenCount)
            modSum[m.dest] += v;
    }
    modVersion = ch.modulationVersion;
}

void Voice::ComputeEnvTimes(EnvTimes& vol, EnvTimes& mod) const
{
    const double rate = kInternalRate;
    const float keyForScaling =
        static_cast<float>(base[static_cast<int>(Gen::Keynum)] >= 0 ? base[static_cast<int>(Gen::Keynum)] : key);
    const float keyOffset = 60.0f - keyForScaling;
    auto samples = [&](Gen g, float extra = 0.0f) {
        return SecondsToSamples(TimecentsToSeconds(static_cast<double>(G(g)) + extra), rate);
    };
    vol.delay = samples(Gen::DelayVolEnv);
    vol.attack = samples(Gen::AttackVolEnv);
    vol.hold = samples(Gen::HoldVolEnv, keyOffset * G(Gen::KeynumToVolEnvHold));
    vol.decay = samples(Gen::DecayVolEnv, keyOffset * G(Gen::KeynumToVolEnvDecay));
    vol.release = samples(Gen::ReleaseVolEnv);
    vol.sustain = G(Gen::SustainVolEnv);
    mod.delay = samples(Gen::DelayModEnv);
    mod.attack = samples(Gen::AttackModEnv);
    mod.hold = samples(Gen::HoldModEnv, keyOffset * G(Gen::KeynumToModEnvHold));
    mod.decay = samples(Gen::DecayModEnv, keyOffset * G(Gen::KeynumToModEnvDecay));
    mod.release = samples(Gen::ReleaseModEnv);
    mod.sustain = 1.0f - G(Gen::SustainModEnv) / 1000.0f;
}

bool Voice::Start(const BankModel& bank, const VoiceZones& zones, int32_t sampleIndex, const Channel& ch,
                  uint8_t channelIndex, uint8_t noteKey, uint8_t noteVelocity, uint64_t id)
{
    if (sampleIndex < 0 || static_cast<size_t>(sampleIndex) >= bank.samples.size())
        return false;
    const SampleInfo& s = bank.samples[sampleIndex];
    if (s.end <= s.start)
        return false;

    // Generators: instrument global then local (absolute, local wins), plus preset global then local
    // (relative, local wins), only those allowed at preset level (SF2 9.4)
    std::array<int32_t, kGenCount> inst{}, pre{};
    for (int g = 0; g < kGenCount; g++)
        inst[g] = GenInfoOf(g).defaultValue;
    for (const Zone* z : {zones.instGlobal, zones.instZone})
        if (z != nullptr)
            for (int g = 0; g < kGenCount; g++)
                if ((z->setMask >> g) & 1u)
                    inst[g] = z->gens[g];
    for (const Zone* z : {zones.presetGlobal, zones.presetZone})
        if (z != nullptr)
            for (int g = 0; g < kGenCount; g++)
                if (((z->setMask >> g) & 1u) && GenInfoOf(g).presetAllowed)
                    pre[g] = z->gens[g];
    for (int g = 0; g < kGenCount; g++)
        base[g] = inst[g] + pre[g];

    // Modulators: defaults, superseded by identical instrument global, then local ones; the preset
    // level's own list (global superseded by local) is added on top (SF2 9.5.1)
    modCount = 0;
    for (int i = 0; i < kDefaultModulatorCount; i++)
        mods[modCount++] = DefaultModulators()[i];
    MergeZoneModulators(bank, zones.instGlobal, mods, modCount, 0);
    MergeZoneModulators(bank, zones.instZone, mods, modCount, 0);
    const uint8_t presetFrom = modCount;
    MergeZoneModulators(bank, zones.presetGlobal, mods, modCount, presetFrom);
    MergeZoneModulators(bank, zones.presetZone, mods, modCount, presetFrom);

    channel = channelIndex;
    key = noteKey;
    velocity = noteVelocity;
    rhythm = ch.rhythm;
    noteId = id;
    sample = sampleIndex;
    exclusiveClass = static_cast<uint8_t>(std::clamp(base[static_cast<int>(Gen::ExclusiveClass)], 0, 127));

    // Playback region: the sample header moved by the address offset generators (fine + 32768 x coarse)
    const int64_t dataEnd = static_cast<int64_t>(bank.data16.size());
    auto offset = [&](Gen fine, Gen coarse) {
        return static_cast<int64_t>(base[static_cast<int>(fine)]) +
               32768 * static_cast<int64_t>(base[static_cast<int>(coarse)]);
    };
    int64_t st = s.start + offset(Gen::StartAddrsOffset, Gen::StartAddrsCoarseOffset);
    int64_t en = s.end + offset(Gen::EndAddrsOffset, Gen::EndAddrsCoarseOffset);
    int64_t ls = s.loopStart + offset(Gen::StartloopAddrsOffset, Gen::StartloopAddrsCoarseOffset);
    int64_t le = s.loopEnd + offset(Gen::EndloopAddrsOffset, Gen::EndloopAddrsCoarseOffset);
    st = std::clamp<int64_t>(st, 0, dataEnd - 1);
    en = std::clamp<int64_t>(en, st + 1, dataEnd);
    ls = std::clamp<int64_t>(ls, st, en);
    le = std::clamp<int64_t>(le, st, en);
    start = static_cast<uint32_t>(st);
    end = static_cast<uint32_t>(en);
    loopStart = static_cast<uint32_t>(ls);
    loopEnd = static_cast<uint32_t>(le);
    loopMode = static_cast<uint8_t>(base[static_cast<int>(Gen::SampleModes)] & 3);
    if (loopMode == 2 || loopEnd <= loopStart)
        loopMode = 0;
    wrapped = false;
    phase = static_cast<uint64_t>(start) << 32;

    state = VoiceState::On;
    UpdateModulators(ch);
    EnvTimes vt, mt;
    ComputeEnvTimes(vt, mt);
    volEnv.Start(vt, true);
    modEnv.Start(mt, false);
    const double lfoScale = 4294967296.0 / kInternalRate;
    modLfo.Start(SecondsToSamples(TimecentsToSeconds(G(Gen::DelayModLfo)), kInternalRate),
                 static_cast<uint32_t>(AbsCentsToHz(G(Gen::FreqModLfo)) * lfoScale));
    vibLfo.Start(SecondsToSamples(TimecentsToSeconds(G(Gen::DelayVibLfo)), kInternalRate),
                 static_cast<uint32_t>(AbsCentsToHz(G(Gen::FreqVibLfo)) * lfoScale));
    filter = VoiceFilter{};
    staticL = staticR = staticStepL = staticStepR = 0.0f;
    staticRampLeft = 0;
    fade = 1.0f;
    fadeLeft = 0;
    loudness = 0.0f;
    return true;
}

void Voice::NoteOff(bool sustainPedal)
{
    if (state != VoiceState::On)
        return;
    if (sustainPedal)
    {
        state = VoiceState::Sustained;
        return;
    }
    Release();
}

void Voice::Release()
{
    if (state != VoiceState::On && state != VoiceState::Sustained)
        return;
    state = VoiceState::Released;
    volEnv.Release();
    modEnv.Release();
}

void Voice::Kill()
{
    if (state == VoiceState::Off || state == VoiceState::Dying)
        return;
    state = VoiceState::Dying;
    fade = 1.0f;
    fadeLeft = kFadeSamples;
}

void Voice::Control(const BankModel& bank, const Channel& ch, const PitchContext& pc, uint32_t n, bool first)
{
    if (state == VoiceState::Off)
        return;
    if (state == VoiceState::Dying)
        return; // the fade ramp set by Kill() runs to its end

    if (modVersion != ch.modulationVersion)
    {
        UpdateModulators(ch);
        EnvTimes vt, mt;
        ComputeEnvTimes(vt, mt);
        volEnv.Retime(vt);
        modEnv.Retime(mt);
    }
    const SampleInfo& s = bank.samples[sample];
    const float modLfoValue = modLfo.Value();
    const float vibLfoValue = vibLfo.Value();
    const float modEnvValue = modEnv.Level();

    // Pitch
    const int32_t keyOverride = base[static_cast<int>(Gen::Keynum)];
    const float keyForPitch = static_cast<float>(keyOverride >= 0 ? keyOverride : key);
    const int32_t rootOverride = base[static_cast<int>(Gen::OverridingRootKey)];
    const float root = static_cast<float>(rootOverride >= 0 ? rootOverride : s.originalPitch);
    const double cents = G(Gen::ScaleTuning) * (keyForPitch - root) + 100.0 * G(Gen::CoarseTune) + G(Gen::FineTune) +
                         s.pitchCorrection + G(Gen::Pitch) + pc.channelCents + modLfoValue * G(Gen::ModLfoToPitch) +
                         vibLfoValue * G(Gen::VibLfoToPitch) + modEnvValue * G(Gen::ModEnvToPitch);
    const double ratio = std::exp2(cents / 1200.0) * static_cast<double>(s.sampleRate) / kInternalRate;
    const double inc = std::fmin(ratio, 1024.0) * 4294967296.0;
    increment = inc < 1.0 ? 1u : static_cast<uint64_t>(inc);

    // Filter
    const double fcCents =
        G(Gen::InitialFilterFc) + modLfoValue * G(Gen::ModLfoToFilterFc) + modEnvValue * G(Gen::ModEnvToFilterFc);
    bool bypass = false;
    const BiquadCoeffs coeffs = LowPassCoeffs(AbsCentsToHz(fcCents), G(Gen::InitialFilterQ), kInternalRate, bypass);
    filter.Target(coeffs, bypass, n, first);

    // Static gain: attenuation (bank part scaled, modulator part exact), tremolo, pan
    const int attIndex = static_cast<int>(Gen::InitialAttenuation);
    const float attenuation =
        std::clamp(kStaticAttenuationScale * static_cast<float>(base[attIndex]) + modSum[attIndex], 0.0f, 1440.0f) -
        modLfoValue * G(Gen::ModLfoToVolume);
    const float staticGain = static_cast<float>(CentibelsToGain(attenuation));
    const double angle = (G(Gen::Pan) + 500.0) / 1000.0 * (kPi / 2.0);
    const float targetL = staticGain * static_cast<float>(std::cos(angle));
    const float targetR = staticGain * static_cast<float>(std::sin(angle));
    if (first || n == 0)
    {
        staticL = targetL;
        staticR = targetR;
        staticStepL = staticStepR = 0.0f;
        staticRampLeft = 0;
        return;
    }
    const float inv = 1.0f / static_cast<float>(n);
    staticStepL = (targetL - staticL) * inv;
    staticStepR = (targetR - staticR) * inv;
    staticRampLeft = n;
}

template <Interpolation M>
uint32_t Voice::RenderLoop(const BankModel& bank, float* left, float* right, uint32_t n, bool mute,
                           const Envelope::Span& env)
{
    const int16_t* d16 = bank.data16.data();
    const uint8_t* d24 = bank.data24.empty() ? nullptr : bank.data24.data();
    constexpr int kTaps = M == Interpolation::Linear ? 2 : M == Interpolation::Cubic ? 4 : 8;
    constexpr int kBefore = M == Interpolation::Linear ? 0 : M == Interpolation::Cubic ? 1 : 3;
    const bool looping = LoopActive();
    const uint64_t loopLength = static_cast<uint64_t>(loopEnd - loopStart) << 32;
    const int64_t limit = looping ? loopEnd : end;
    const float fadeStep = 1.0f / static_cast<float>(kFadeSamples);
    float envGain = env.gain;
    float taps[8];
    uint32_t i = 0;
    for (; i < n; i++)
    {
        const uint32_t idx = static_cast<uint32_t>(phase >> 32);
        if (!looping && idx >= end)
            break; // the sample has ended
        const int64_t first = static_cast<int64_t>(idx) - kBefore;
        if (first >= static_cast<int64_t>(start) && first + kTaps <= limit && !(wrapped && first < loopStart))
        {
            for (int k = 0; k < kTaps; k++)
                taps[k] = ReadFrame(d16, d24, static_cast<uint32_t>(first + k));
        }
        else
        {
            for (int k = 0; k < kTaps; k++)
            {
                int64_t j = first + k;
                if (looping)
                {
                    while (j >= loopEnd)
                        j -= loopEnd - loopStart;
                    if (wrapped && j < loopStart)
                        j += loopEnd - loopStart;
                }
                taps[k] = (j < static_cast<int64_t>(start) || j >= static_cast<int64_t>(end))
                              ? 0.0f
                              : ReadFrame(d16, d24, static_cast<uint32_t>(j));
            }
        }
        const uint32_t frac = static_cast<uint32_t>(phase);
        float v;
        if constexpr (M == Interpolation::Linear)
            v = InterpolateLinear(taps, static_cast<float>(frac) * (1.0f / 4294967296.0f));
        else if constexpr (M == Interpolation::Cubic)
            v = InterpolateCubic(taps, static_cast<float>(frac) * (1.0f / 4294967296.0f));
        else
            v = InterpolateSinc(taps, frac);
        if (filter.active)
            v = filter.Process(v);
        const float g = envGain * fade;
        if (!mute)
        {
            left[i] += v * g * staticL;
            right[i] += v * g * staticR;
        }
        envGain = env.geometric ? envGain * env.step : envGain + env.step;
        if (staticRampLeft > 0)
        {
            staticL += staticStepL;
            staticR += staticStepR;
            staticRampLeft--;
        }
        if (state == VoiceState::Dying)
            fade = std::fmax(0.0f, fade - fadeStep);
        phase += increment;
        if (looping)
            while ((phase >> 32) >= loopEnd)
            {
                phase -= loopLength;
                wrapped = true;
            }
    }
    loudness = envGain * fade * (staticL + staticR);
    return i;
}

void Voice::Render(const BankModel& bank, Interpolation mode, float* left, float* right, uint32_t n, bool mute)
{
    if (state == VoiceState::Off || n == 0)
        return;
    bool ended = false;
    uint32_t done = 0;
    while (done < n && !ended)
    {
        // one envelope span at a time: its gain evolves exactly inside, stages switch on their sample
        const Envelope::Span env = volEnv.NextSpan(n - done);
        const uint32_t want = env.count == 0 ? n - done : env.count;
        if (volEnv.stage == EnvStage::Delay)
        {
            // the delay holds the whole voice: the sample starts with the attack (as FluidSynth and
            // the E-mu hardware do)
            volEnv.Advance(want);
            done += want;
            continue;
        }
        uint32_t got = 0;
        switch (mode)
        {
            case Interpolation::Linear:
                got = RenderLoop<Interpolation::Linear>(bank, left + done, right + done, want, mute, env);
                break;
            case Interpolation::Cubic:
                got = RenderLoop<Interpolation::Cubic>(bank, left + done, right + done, want, mute, env);
                break;
            case Interpolation::Sinc:
                got = RenderLoop<Interpolation::Sinc>(bank, left + done, right + done, want, mute, env);
                break;
        }
        volEnv.Advance(got);
        done += got;
        if (got < want || volEnv.Done())
            ended = true;
    }
    filter.FlushDenormals();
    modEnv.Advance(n);
    modLfo.Advance(n);
    vibLfo.Advance(n);
    if (state == VoiceState::Dying)
    {
        fadeLeft = fadeLeft > n ? fadeLeft - n : 0;
        if (fadeLeft == 0)
            ended = true;
    }
    if (ended || volEnv.Done())
        state = VoiceState::Off;
}

void Voice::Sanitize(const BankModel& bank)
{
    if (state == VoiceState::Off)
        return;
    const uint32_t frames = static_cast<uint32_t>(bank.data16.size());
    const bool bad = sample < 0 || static_cast<size_t>(sample) >= bank.samples.size() || end > frames ||
                     start >= end || loopEnd > end || loopStart > loopEnd || modCount > kMaxVoiceModulators;
    if (bad)
    {
        state = VoiceState::Off;
        return;
    }
    for (uint8_t i = 0; i < modCount; i++)
        if (mods[i].dest >= kGenCount)
            mods[i].dest = static_cast<uint16_t>(Gen::EndOper);
    if (loopMode == 2 || loopMode > 3 || loopEnd <= loopStart)
        loopMode = 0;
}

} // namespace sam2695
