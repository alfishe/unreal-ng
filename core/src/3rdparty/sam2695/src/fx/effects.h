// libsam2695 - the effects block and the output stage (datasheet p.15-25, p.36-37, p.42).
//
// Signal flow per control block:
//   voices -> GM bus (dry L/R, reverb send, chorus send) x GM volume / GM pan (NRPN 3722h / 3723h)
//   reverb send -> reverb, chorus send -> chorus (each only while its NRPN 375Fh bit is on)
//   post effects (spatial effect, then the equalizer) on the GM bus (3718h) and / or on the reverb +
//   chorus returns (371Ah); the rest bypasses them
//   x output gain (the mixer's headroom, SynthConfig::outputGain) x master volume (3707h)
//   -> soft or hard clipping (3713h) -> codec output gain / DAC mute (codec port 12h)
// The dry render mode (SynthConfig::effects = false) keeps every gain and skips the reverb, chorus,
// spatial effect, equalizer and clipping: a linear voice mix for analysis.
// Idle effects: an effect whose whole state is +0.0 and whose input block is all +0.0 is skipped (only
// its write positions and LFO phase move); output and state are bit-identical to processing it
// (SynthConfig::skipIdleEffects = false processes every block, for tests and A/B).
#pragma once

#include "fx/chorus.h"
#include "fx/fxparams.h"
#include "fx/reverb.h"
#include "sam2695/sam2695config.h"

#include <cstdint>

namespace sam2695
{

struct FxBuses
{
    float left[kControlBlock];
    float right[kControlBlock];
    float reverb[kControlBlock];
    float chorus[kControlBlock];
};

// Soft clipping: transparent up to the knee, then a rational curve with the same slope at the knee
// that approaches full scale and never reaches it.
constexpr float kSoftClipKnee = 0.75f;
inline float SoftClip(float x)
{
    const float a = x < 0.0f ? -x : x;
    if (a <= kSoftClipKnee)
        return x;
    const float u = (a - kSoftClipKnee) / (1.0f - kSoftClipKnee);
    const float y = kSoftClipKnee + (1.0f - kSoftClipKnee) * u / (1.0f + u);
    return x < 0.0f ? -y : y;
}

class Effects
{
public:
    FxParams params;

    void Allocate();                  // Configure: sizes the delay lines
    void PowerOn();                   // datasheet power-up values, every line cleared
    void ResetReverbChorus();         // GS / GM reset: reverb and chorus programs back to their defaults
    void Changed();                   // re-derive every coefficient after params changed
    void EffectsWordChanged(uint8_t before, uint8_t now); // an effect switched off loses its state

    void Process(FxBuses& in, uint32_t n, uint8_t effectsWord, bool dsp, float outputGain, float* outL, float* outR);
    bool skipIdle = true;     // SynthConfig::skipIdleEffects
    uint64_t skippedBlocks = 0; // effect blocks skipped since Configure (Describe; diagnostics, not state)

    // The effect's state is all +0.0: a block without input skips it (Describe, tests)
    bool ReverbIdle() const { return _reverb.Idle(); }
    bool ChorusIdle() const { return _chorus.Idle(); }
    bool SpatialIdle() const { return _spatial.Idle(); }
    bool EqualizerIdle(bool fourBand) const { return _eq.Idle(fourBand); }

    static double CodecGainDb(uint16_t codec0); // OUTG[5:0], datasheet p.37
    static bool CodecMuted(uint16_t codec0);    // DACSEL = 0 or DACMUTE = 1
    double ReverbDecaySeconds() const { return _reverb.DecaySeconds(); }

    void Sanitize();

    template <class Ar>
    void Serialize(Ar& ar)
    {
        ar(params);
        ar(_reverb);
        ar(_chorus);
        ar(_spatial);
        ar(_eq);
    }

private:
    Reverb _reverb;
    Chorus _chorus;
    Spatial _spatial;
    Equalizer _eq;
};

} // namespace sam2695
