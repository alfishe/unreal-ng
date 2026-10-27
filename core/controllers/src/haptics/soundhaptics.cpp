#include "haptics/soundhaptics.h"

#include <algorithm>

namespace controllers
{

namespace
{
/// The shapes that fall to silence and stay there (or repeat falling): 0-3, 8 and 9 are the AY "boom"
bool Decays(uint8_t shape)
{
    shape &= 0x0F;
    return shape <= 3 || shape == 8 || shape == 9;
}
}  // namespace

SoundHaptics::SoundHaptics(HapticMixer& mixer, std::function<std::vector<HostDeviceId>()> targets, bool dspDefaults)
    : _mixer(mixer), _targets(std::move(targets)), _dsp(dspDefaults)
{
    _lowband.Configure(20.0, 150.0, 5.0, 80.0, HapticStreamFrame::kRate);
    _onset.Configure(12.0, 30.0, 0.05, 50.0, HapticStreamFrame::kRate);
}

std::vector<HapticStream> SoundHaptics::Streams() const
{
    if (!_dsp)
        return {};
    return {HapticStream::Mix};
}

void SoundHaptics::OnHapticHold(bool held)
{
    _held.store(held, std::memory_order_release);
    if (!held)
        return;
    for (const HostDeviceId& device : _targets())
        _mixer.StopGameEffects(device);
    _last = {};
}

void SoundHaptics::OnHapticFrame(const HapticFrame& frame)
{
    if (_held.load(std::memory_order_acquire))
        return;
    constexpr float kRelease = 0.7f;      // per frame: about 100 ms to fall away
    constexpr float kEnvelopeVolume = 0.8f;
    constexpr float kNoiseToMotor = 0.6f;
    constexpr float kBeeperFull = 10.0f;  // edges per ms at the full high motor

    float noise = 0;
    bool thump = false;
    for (unsigned chip = 0; chip < std::min<unsigned>(frame.ayCount, HapticFrame::kMaxAy); chip++)
    {
        const HapticAyFrame& ay = frame.ay[chip];
        for (uint8_t volume : ay.noiseVolume)
            noise = std::max(noise, volume >= 16 ? kEnvelopeVolume : volume / 15.0f);
        if (ay.envelopeRestarts > 0 && ay.envelopeUsed && Decays(ay.envelopeShape))
            thump = true;
    }
    _noise = std::max(noise, _noise * kRelease);
    if (_noise < kNoiseFloor)
        _noise = 0;

    float high = 0;
    if (frame.frameTStates > 0 && frame.cpuHz > 0)
    {
        const float ms = frame.frameTStates * 1000.0f / static_cast<float>(frame.cpuHz);
        const float rate = frame.beeperEdges / ms;
        if (rate > kBeeperEdgesPerMs)
            high = std::min(1.0f, rate / kBeeperFull);
    }

    float band = 0;
    bool tick = false;
    if (_dsp && frame.streamCount > 0)
    {
        const HapticStreamFrame& mix = frame.streams[0];
        const size_t count = std::min<size_t>(mix.count, HapticStreamFrame::kMaxSamples);
        band = std::min(1.0f, _lowband.Process(mix.samples.data(), count) * kLowbandToMotor);
        tick = _onset.Process(mix.samples.data(), count);
    }

    _last = {std::max(_noise * kNoiseToMotor, band), high, 0.0f, 0.0f};
    if (_last.low == 0 && _last.high == 0 && !thump && !tick)
        return;
    for (const HostDeviceId& device : _targets())
    {
        if (_last.low > 0 || _last.high > 0)
            _mixer.Play(device, {_last, kContinuousMs, false});
        if (thump)
            _mixer.Play(device, {{1.0f, 0.4f, 0.0f, 0.0f}, kThumpMs, false});
        if (tick)
            _mixer.Play(device, {{0.0f, 0.7f, 0.0f, 0.0f}, kTickMs, false});
    }
    if (thump)
        _last.low = 1.0f;
}

}  // namespace controllers
