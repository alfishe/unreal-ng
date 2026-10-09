// libsam2695 - effects block and output stage (see effects.h).
#include "fx/effects.h"

#include <algorithm>
#include <cmath>

namespace sam2695
{

namespace
{

constexpr uint8_t kReverbBit = 0x20;
constexpr uint8_t kChorusBit = 0x10;
constexpr uint8_t kSpatialBit = 0x08;

} // namespace

void Effects::Allocate()
{
    _reverb.Allocate();
    _chorus.Allocate();
    _spatial.Allocate();
}

void Effects::PowerOn()
{
    params = FxParams{};
    params.SelectReverb(4);
    params.SelectChorus(2);
    _reverb.Clear();
    _chorus.Clear();
    _spatial.Clear();
    _eq.Clear();
    Changed();
}

void Effects::ResetReverbChorus()
{
    params.SelectReverb(4);
    params.SelectChorus(2);
    Changed();
}

void Effects::Changed()
{
    _reverb.Update(params);
    _chorus.Update(params);
    _spatial.Update(params);
    _eq.Update(params);
}

void Effects::EffectsWordChanged(uint8_t before, uint8_t now)
{
    const uint8_t off = static_cast<uint8_t>(before & ~now);
    if (off & kReverbBit)
        _reverb.Clear();
    if (off & kChorusBit)
        _chorus.Clear();
    if (off & kSpatialBit)
        _spatial.Clear();
    if ((before & 0x02) && !(now & 0x02))
        _eq.Clear();
}

void Effects::Sanitize()
{
    _reverb.Sanitize(params);
    _chorus.Sanitize(params);
    _spatial.Sanitize(params);
    _eq.Update(params);
}

double Effects::CodecGainDb(uint16_t codec0)
{
    const int g = codec0 & 0x3F;
    if (g >= 0x11)
        return static_cast<double>(g - 0x39); // +6 dB (3Fh) .. -40 dB (11h) in 1 dB steps, 0 dB = 39h
    return g == 0x10 ? -43.5 : -58.5;
}

bool Effects::CodecMuted(uint16_t codec0)
{
    return (codec0 & 0x40) == 0 || (codec0 & 0x80) != 0;
}

void Effects::Process(FxBuses& in, uint32_t n, uint8_t effectsWord, bool dsp, float outputGain, float* outL, float* outR)
{
    // GM bus: volume (linear, NRPN 3722h / master volume SysEx) and pan as a balance (3723h / 40 00 06)
    const float gmVolume = params.gmVolume / 127.0f;
    const float balance = std::clamp((static_cast<float>(params.gmPan) - 64.0f) / 64.0f, -1.0f, 1.0f);
    const float gl = gmVolume * (balance > 0.0f ? 1.0f - balance : 1.0f);
    const float gr = gmVolume * (balance < 0.0f ? 1.0f + balance : 1.0f);
    for (uint32_t i = 0; i < n; i++)
    {
        in.left[i] *= gl;
        in.right[i] *= gr;
    }

    float postL[kControlBlock], postR[kControlBlock];
    if (dsp)
    {
        float fxL[kControlBlock] = {}, fxR[kControlBlock] = {};
        // an idle effect with an all +0.0 input adds +0.0 to fx* (which never holds -0.0): skipped
        if (effectsWord & kReverbBit)
        {
            for (uint32_t i = 0; i < n; i++)
                in.reverb[i] *= gmVolume;
            if (skipIdle && _reverb.Idle() && AllPositiveZero(in.reverb, n))
            {
                _reverb.Skip(n);
                skippedBlocks++;
            }
            else
                _reverb.Process(in.reverb, fxL, fxR, n);
        }
        if (effectsWord & kChorusBit)
        {
            for (uint32_t i = 0; i < n; i++)
                in.chorus[i] *= gmVolume;
            if (skipIdle && _chorus.Idle() && AllPositiveZero(in.chorus, n))
            {
                _chorus.Skip(n);
                skippedBlocks++;
            }
            else
                _chorus.Process(in.chorus, fxL, fxR, n);
        }
        // post effects: what is routed through them goes to post*, the rest straight to out*
        const bool postGm = params.postGm >= 0x40, postFx = params.postFx >= 0x40;
        for (uint32_t i = 0; i < n; i++)
        {
            postL[i] = (postGm ? in.left[i] : 0.0f) + (postFx ? fxL[i] : 0.0f);
            postR[i] = (postGm ? in.right[i] : 0.0f) + (postFx ? fxR[i] : 0.0f);
            outL[i] = (postGm ? 0.0f : in.left[i]) + (postFx ? 0.0f : fxL[i]);
            outR[i] = (postGm ? 0.0f : in.right[i]) + (postFx ? 0.0f : fxR[i]);
        }
        bool postSilent = AllPositiveZero(postL, n) && AllPositiveZero(postR, n);
        if (effectsWord & kSpatialBit)
        {
            if (skipIdle && postSilent && _spatial.Idle())
            {
                _spatial.Skip(n);
                skippedBlocks++;
            }
            else
            {
                _spatial.Process(postL, postR, n);
                postSilent = AllPositiveZero(postL, n) && AllPositiveZero(postR, n); // the equalizer's input
            }
        }
        const bool fourBand = (effectsWord & 0x03) == 0x03;
        if ((effectsWord & 0x03) >= 0x02)
        {
            if (skipIdle && postSilent && _eq.Idle(fourBand))
                skippedBlocks++;
            else
            {
                _eq.Process(postL, postR, n, fourBand);
                if (postSilent && _eq.TailOut(fourBand)) // the tail-out rule, processing or not
                    eqTailsOut++;
            }
        }
        for (uint32_t i = 0; i < n; i++)
        {
            outL[i] += postL[i];
            outR[i] += postR[i];
        }
    }
    else
    {
        for (uint32_t i = 0; i < n; i++)
        {
            outL[i] = in.left[i];
            outR[i] = in.right[i];
        }
    }

    // output stage
    const float gain = outputGain * (params.masterVolume / 127.0f);
    const float codec = CodecMuted(params.codec0) ? 0.0f : static_cast<float>(std::pow(10.0, CodecGainDb(params.codec0) / 20.0));
    const bool hard = params.clipMode >= 0x40;
    for (uint32_t i = 0; i < n; i++)
    {
        float l = outL[i] * gain, r = outR[i] * gain;
        if (dsp)
        {
            if (hard)
            {
                l = std::clamp(l, -1.0f, 1.0f);
                r = std::clamp(r, -1.0f, 1.0f);
            }
            else
            {
                l = SoftClip(l);
                r = SoftClip(r);
            }
        }
        outL[i] = l * codec;
        outR[i] = r * codec;
    }
}

} // namespace sam2695
