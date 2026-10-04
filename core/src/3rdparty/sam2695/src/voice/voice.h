// libsam2695 - one synthesis voice: an SF2 instrument zone playing one sample.
//
// Per control block: modulators (when a source changed), pitch -> 32.32 phase increment, filter
// coefficients, and a ramp of the static gain (attenuation, tremolo, pan) to its value at the block
// end. Per sample: interpolation, filter, the volume envelope (exact: stage boundaries fall on their
// own samples, attack steps linearly, decay / release geometrically), the static ramp, the loop.
// Envelopes and LFOs advance by exactly the samples rendered, so an event inside a block (note-off,
// a new voice) re-targets from the true position.
#pragma once

#include "channel/channel.h"
#include "sam2695/sam2695config.h"
#include "sam2695/soundbank.h"
#include "voice/envelope.h"
#include "voice/filter.h"
#include "voice/lfo.h"

#include <array>
#include <cstdint>

namespace sam2695
{

enum class VoiceState : uint8_t
{
    Off,
    On,        // key held
    Sustained, // key released, held by the sustain pedal
    Released,  // in its release
    Dying      // stolen / cut by its exclusive class: a short fade in a spare slot
};

// The SF2 "EMU" scaling of a bank's static initialAttenuation (not of modulator contributions): what
// the E-mu hardware, FluidSynth and BASSMIDI do, and what banks are voiced for (README "Deviations").
constexpr float kStaticAttenuationScale = 0.4f;
// Length of the fade of a stolen or exclusive-class voice (1.7 ms)
constexpr uint32_t kFadeSamples = 64;

struct VoiceZones
{
    const Zone* presetGlobal = nullptr;
    const Zone* presetZone = nullptr;
    const Zone* instGlobal = nullptr;
    const Zone* instZone = nullptr;
};

// GS drum instrument edit (NRPN 18rr / 1Arr / 1Crr / 1Drr / 1Err, datasheet p.27) for one key
struct DrumNote
{
    uint8_t pitch = 0x40;   // coarse pitch, semitones + 40h
    uint8_t level = 0x7F;   // 7Fh = the kit's own level
    uint8_t pan = 0xFF;     // 0-7Fh absolute pan (40h center); FFh = the kit's own pan
    uint8_t reverb = 0x7F;  // per-note reverb send depth, 7Fh = the part's send unchanged
    uint8_t chorus = 0x7F;  // per-note chorus send depth

    template <class Ar>
    void Serialize(Ar& ar)
    {
        ar(pitch);
        ar(level);
        ar(pan);
        ar(reverb);
        ar(chorus);
    }
};
using DrumTable = std::array<DrumNote, 128>;

// What a voice reads from the chip besides its part: tuning, effect send scaling, the drum edit table
struct VoiceContext
{
    float tuneCents = 0.0f;                 // RPN fine / coarse tuning of the part + GS master tune
    float reverbScale = 1.0f;               // NRPN 3715h / 40h
    float chorusScale = 1.0f;               // NRPN 3716h / 40h
    const DrumTable* drums = nullptr;       // a rhythm part's drum edit table
};

// Portamento: how many cents per internal sample a glide moves for CC 5 = v (README "Portamento")
float PortamentoCentsPerSample(uint8_t time);

struct Voice
{
    VoiceState state = VoiceState::Off;
    uint8_t channel = 0;
    uint8_t key = 0;                        // sounding key (after the GS master key shift)
    uint8_t midiKey = 0;                    // the key of the Note On (note off, poly pressure, drum edits)
    uint8_t velocity = 0;
    uint8_t exclusiveClass = 0;
    bool rhythm = false;
    bool sostenuto = false;                 // held by the sostenuto pedal (CC 66) since it went down
    bool soft = false;                      // started under the soft pedal (CC 67)
    uint64_t noteId = 0;
    int32_t sample = -1;

    std::array<int32_t, kGenCount> base{};  // instrument (absolute) + preset (relative) generator sums
    std::array<float, kGenCount> modSum{};  // modulator contributions
    uint32_t modVersion = 0;                // channel modulation version the sums belong to
    uint8_t modCount = 0;
    std::array<ModulatorDef, kMaxVoiceModulators> mods{};

    uint32_t start = 0, end = 0, loopStart = 0, loopEnd = 0;
    uint8_t loopMode = 0;
    bool wrapped = false;                   // the loop has been passed at least once
    uint64_t phase = 0;                     // 32.32 frames from the start of the sample data
    uint64_t increment = 0;

    Envelope volEnv, modEnv;
    Lfo modLfo, vibLfo;
    VoiceFilter filter;

    // GS extras evaluated with the modulators: LFO1 (the vibrato LFO) to cutoff and amplitude, its rate
    // offset, and the effect sends (0..1 of the dry voice, pre-pan)
    float vibToFc = 0.0f;                   // cents at LFO peak
    float vibToAmp = 0.0f;                  // 0..1 amplitude depth
    float vibRateHz = 0.0f;                 // added to the vibrato LFO frequency
    float reverbSend = 0.0f, chorusSend = 0.0f;
    float portaCents = 0.0f;                // portamento: pitch offset still to glide away (cents)
    float portaRate = 0.0f;                 // cents per sample

    float staticL = 0.0f, staticR = 0.0f;   // attenuation x tremolo x pan, ramped per control block
    float staticStepL = 0.0f, staticStepR = 0.0f;
    float staticRev = 0.0f, staticCho = 0.0f; // attenuation x tremolo x send
    float staticStepRev = 0.0f, staticStepCho = 0.0f;
    uint32_t staticRampLeft = 0;
    float fade = 1.0f;                      // Dying: linear fade to 0 over kFadeSamples
    uint32_t fadeLeft = 0;
    float loudness = 0.0f;                  // last output gain (L + R), for voice stealing

    // Set up from the zones; false when the zone has no playable sample.
    bool Start(const BankModel& bank, const VoiceZones& zones, int32_t sampleIndex, const Channel& ch,
               const VoiceContext& ctx, uint8_t channelIndex, uint8_t noteKey, uint8_t noteMidiKey,
               uint8_t noteVelocity, uint64_t id);

    void NoteOff(bool sustainPedal);
    void Release();
    void Kill(); // fade out over kFadeSamples

    // Re-target the ramps for the next n samples (n = samples to the control-block end).
    void Control(const BankModel& bank, const Channel& ch, const VoiceContext& ctx, uint32_t n, bool first);

    // Add n samples to the mix and the effect sends and advance.
    void Render(const BankModel& bank, Interpolation mode, float* left, float* right, float* reverb, float* chorus,
                uint32_t n, bool mute);

    bool Counts() const { return state == VoiceState::On || state == VoiceState::Sustained || state == VoiceState::Released; }
    float Loudness() const { return loudness; }

    // Clamp indices read from a state blob to the bank (a blob is not trusted).
    void Sanitize(const BankModel& bank);

    template <class Ar>
    void Serialize(Ar& ar)
    {
        ar(state);
        ar(channel);
        ar(key);
        ar(midiKey);
        ar(velocity);
        ar(exclusiveClass);
        ar(rhythm);
        ar(sostenuto);
        ar(soft);
        ar(noteId);
        ar(sample);
        ar(base);
        ar(modSum);
        ar(modVersion);
        ar(modCount);
        for (ModulatorDef& m : mods)
        {
            ar(m.src);
            ar(m.dest);
            ar(m.amount);
            ar(m.amtSrc);
            ar(m.transform);
        }
        ar(start);
        ar(end);
        ar(loopStart);
        ar(loopEnd);
        ar(loopMode);
        ar(wrapped);
        ar(phase);
        ar(increment);
        ar(volEnv);
        ar(modEnv);
        ar(modLfo);
        ar(vibLfo);
        ar(filter);
        ar(vibToFc);
        ar(vibToAmp);
        ar(vibRateHz);
        ar(reverbSend);
        ar(chorusSend);
        ar(portaCents);
        ar(portaRate);
        ar(staticL);
        ar(staticR);
        ar(staticStepL);
        ar(staticStepR);
        ar(staticRev);
        ar(staticCho);
        ar(staticStepRev);
        ar(staticStepCho);
        ar(staticRampLeft);
        ar(fade);
        ar(fadeLeft);
        ar(loudness);
    }

private:
    float G(Gen g) const;
    float Source(uint16_t src, const Channel& ch) const;
    void UpdateModulators(const Channel& ch, const VoiceContext& ctx);
    void PartControls(const Channel& ch, const VoiceContext& ctx);
    uint32_t VibLfoIncrement() const;
    void ComputeEnvTimes(EnvTimes& vol, EnvTimes& mod) const;
    bool LoopActive() const { return loopMode == 1 || (loopMode == 3 && state != VoiceState::Released && state != VoiceState::Dying); }

    template <Interpolation M>
    uint32_t RenderLoop(const BankModel& bank, float* left, float* right, float* reverb, float* chorus, uint32_t n,
                        bool mute, const Envelope::Span& env);
};

} // namespace sam2695
