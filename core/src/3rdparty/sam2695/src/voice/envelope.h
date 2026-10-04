// libsam2695 - SF2 2.04 DAHDSR envelopes (section 8.1.2 generators 25-40, 9.1.7).
//
// Stage lengths are whole internal samples; the level is a function of the position inside the stage,
// never an accumulated sum, so a long note does not drift.
//
// Volume envelope: delay (silent), attack (amplitude rises linearly 0 -> 1), hold (full), decay
// (attenuation rises linearly in centibels at 1000 cB per decay time, i.e. "100 % change" = 100 dB,
// down to the sustain level), sustain, release (1000 cB per release time from wherever it starts).
// The voice ends when the attenuation reaches 1000 cB (-100 dB).
//
// Modulation envelope: same timing, value 0..1: attack along the SF2 convex curve (as the E-mu
// hardware and FluidSynth shape it; the specification does not draw it), decay linear down to
// 1 - sustain/1000 at a rate of 1 per decay time, release linear down at 1 per release time.
#pragma once

#include "common/conv.h"

#include <cmath>
#include <cstdint>

namespace sam2695
{

enum class EnvStage : uint8_t
{
    Delay,
    Attack,
    Hold,
    Decay,
    Sustain,
    Release,
    Finished
};

constexpr float kSilenceCentibels = 1000.0f;

struct EnvTimes
{
    uint32_t delay = 0, attack = 0, hold = 0;
    uint32_t decay = 0;   // samples for a full (100 %) change
    uint32_t release = 0; // samples for a full (100 %) change
    float sustain = 0.0f; // volume: centibels of attenuation; modulation: level 0..1
};

struct Envelope
{
    EnvStage stage = EnvStage::Finished;
    uint32_t pos = 0;           // samples into the stage
    float startLevel = 0.0f;    // release: level at release start (cB for volume, 0..1 for modulation)
    EnvTimes times;
    bool volume = true;

    void Start(const EnvTimes& t, bool isVolume)
    {
        times = t;
        volume = isVolume;
        stage = EnvStage::Delay;
        pos = 0;
        startLevel = 0.0f;
        Normalize();
    }

    // Volume: centibels of attenuation in every stage but attack/delay. Modulation: 0..1.
    float Level() const
    {
        switch (stage)
        {
            case EnvStage::Delay:
                return volume ? kSilenceCentibels : 0.0f;
            case EnvStage::Attack:
            {
                const float a = times.attack == 0 ? 1.0f : static_cast<float>(pos) / static_cast<float>(times.attack);
                if (!volume)
                    return static_cast<float>(ConvexCurve(a));
                return a <= 0.0f ? kSilenceCentibels : -200.0f * std::log10(a);
            }
            case EnvStage::Hold:
                return volume ? 0.0f : 1.0f;
            case EnvStage::Decay:
            {
                const float slope = 1.0f / static_cast<float>(times.decay);
                if (volume)
                    return std::fmin(kSilenceCentibels * slope * static_cast<float>(pos), times.sustain);
                return std::fmax(1.0f - slope * static_cast<float>(pos), times.sustain);
            }
            case EnvStage::Sustain:
                return times.sustain;
            case EnvStage::Release:
            {
                if (times.release == 0)
                    return volume ? kSilenceCentibels : 0.0f;
                const float d = static_cast<float>(pos) / static_cast<float>(times.release);
                if (volume)
                    return std::fmin(startLevel + kSilenceCentibels * d, kSilenceCentibels);
                return std::fmax(startLevel - d, 0.0f);
            }
            case EnvStage::Finished:
                break;
        }
        return volume ? kSilenceCentibels : 0.0f;
    }

    // Linear amplitude of the volume envelope.
    float Gain() const
    {
        if (stage == EnvStage::Attack)
            return times.attack == 0 ? 1.0f : static_cast<float>(pos) / static_cast<float>(times.attack);
        if (stage == EnvStage::Delay || stage == EnvStage::Finished)
            return 0.0f;
        const float cb = Level();
        return cb >= kSilenceCentibels ? 0.0f : std::pow(10.0f, -cb / 200.0f);
    }

    // The volume gain over the next samples of the current stage, exactly: `count` samples starting at
    // `gain`, each next one gain + delta (attack) or gain x ratio (decay / release: linear in dB).
    struct Span
    {
        uint32_t count = 0;
        float gain = 0.0f;
        float step = 0.0f;
        bool geometric = false;
    };
    Span NextSpan(uint32_t maxCount) const
    {
        Span s;
        s.count = maxCount;
        switch (stage)
        {
            case EnvStage::Delay:
            case EnvStage::Hold:
            case EnvStage::Attack:
            case EnvStage::Decay:
            case EnvStage::Release:
            {
                const uint32_t left = StageLength() - pos;
                s.count = left < maxCount ? left : maxCount;
                break;
            }
            default:
                break;
        }
        s.gain = Gain();
        if (stage == EnvStage::Attack)
            s.step = 1.0f / static_cast<float>(times.attack);
        else if (stage == EnvStage::Decay || stage == EnvStage::Release)
        {
            const uint32_t length = stage == EnvStage::Decay ? times.decay : times.release;
            s.geometric = true;
            s.step = std::pow(10.0f, -(kSilenceCentibels / static_cast<float>(length)) / 200.0f);
        }
        return s;
    }

    void Release()
    {
        if (stage == EnvStage::Finished || stage == EnvStage::Release)
            return;
        if (stage == EnvStage::Delay && volume)
        {
            stage = EnvStage::Finished; // nothing has sounded yet
            return;
        }
        startLevel = Level();
        stage = EnvStage::Release;
        pos = 0;
        Normalize();
    }

    void Advance(uint32_t n)
    {
        while (n > 0 && stage != EnvStage::Finished && stage != EnvStage::Sustain)
        {
            const uint32_t len = StageLength();
            const uint32_t take = len - pos < n ? len - pos : n;
            pos += take;
            n -= take;
            if (pos >= len)
                Next();
        }
    }

    // Times changed by a modulator: keep the stage, keep the position inside it.
    void Retime(const EnvTimes& t)
    {
        const float oldSustain = times.sustain;
        times = t;
        if (stage == EnvStage::Release)
        {
            Normalize();
            return;
        }
        if (stage == EnvStage::Sustain && times.sustain != oldSustain)
        {
            stage = EnvStage::Decay; // move toward the new level at the decay rate
            pos = DecayPosForLevel(oldSustain);
        }
        Normalize();
    }

    bool Done() const { return stage == EnvStage::Finished; }

    template <class Ar>
    void Serialize(Ar& ar)
    {
        ar(stage);
        ar(pos);
        ar(startLevel);
        ar(times.delay);
        ar(times.attack);
        ar(times.hold);
        ar(times.decay);
        ar(times.release);
        ar(times.sustain);
        ar(volume);
    }

private:
    // Length of the current stage in samples (decay / release: until the target level)
    uint32_t StageLength() const
    {
        switch (stage)
        {
            case EnvStage::Delay:
                return times.delay;
            case EnvStage::Attack:
                return times.attack;
            case EnvStage::Hold:
                return times.hold;
            case EnvStage::Decay:
            {
                const double fraction = volume ? static_cast<double>(times.sustain) / kSilenceCentibels
                                               : 1.0 - static_cast<double>(times.sustain);
                return WholeSamples(fraction, times.decay);
            }
            case EnvStage::Release:
            {
                const double fraction = volume ? (kSilenceCentibels - static_cast<double>(startLevel)) / kSilenceCentibels
                                               : static_cast<double>(startLevel);
                return WholeSamples(fraction, times.release);
            }
            default:
                return 0;
        }
    }

    // ceil(fraction x length), tolerant to the float rounding of the level (0.3f is 0.30000001)
    static uint32_t WholeSamples(double fraction, uint32_t length)
    {
        const double x = std::fmax(0.0, std::fmin(1.0, fraction)) * length;
        return static_cast<uint32_t>(std::ceil(x - 1e-4));
    }

    uint32_t DecayPosForLevel(float level) const
    {
        const double fraction = volume ? level / kSilenceCentibels : 1.0 - level;
        return static_cast<uint32_t>(std::fmax(0.0, std::fmin(1.0, fraction)) * times.decay);
    }

    void Next()
    {
        pos = 0;
        switch (stage)
        {
            case EnvStage::Delay:
                stage = EnvStage::Attack;
                break;
            case EnvStage::Attack:
                stage = EnvStage::Hold;
                break;
            case EnvStage::Hold:
                stage = EnvStage::Decay;
                break;
            case EnvStage::Decay:
                stage = EnvStage::Sustain;
                if (volume && times.sustain >= kSilenceCentibels)
                    stage = EnvStage::Finished;
                break;
            case EnvStage::Release:
                stage = EnvStage::Finished;
                break;
            default:
                break;
        }
        Normalize();
    }

    // Skip zero-length stages (Next() normalizes again, at most once per stage), so Level() never
    // divides by a zero stage length.
    void Normalize()
    {
        if (stage == EnvStage::Finished || stage == EnvStage::Sustain)
            return;
        if (StageLength() > pos)
            return;
        Next();
    }
};

} // namespace sam2695
