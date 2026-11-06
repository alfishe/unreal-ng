#include "wildsoundaudio.h"

#include <algorithm>
#include <cstring>

#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/soundchip_ay8910.h"

namespace wildsound
{

WildSoundAudio::WildSoundAudio(EmulatorContext* context, WildSoundCore& core)
    : _context(context),
      _core(core),
      _xm(MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS, 0)
{
    for (auto& buffer : _ay)
        buffer.assign(MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS, 0);
    for (auto& chip : _chips)
    {
        chip = std::make_unique<SoundChip_AY8910>(context);
        chip->setGeneratorRate(kAyClockHz / 8.0);
        chip->setStereoMode(AYStereoMode::ABC);
    }
    SetOutputRate(44100);
}

WildSoundAudio::~WildSoundAudio() = default;

void WildSoundAudio::SetOutputRate(uint32_t rate)
{
    _rate = rate ? rate : 44100;
    _player.SetOutputRate(_rate);
    _ticksPerSample = (kAyClockHz / 8.0) / _rate;
}

void WildSoundAudio::MixerRows(std::vector<CardMixerRow>& out) const
{
    out.push_back({AudioSourceType::WildSoundXm, "WS XM", false});
    out.push_back({AudioSourceType::WildSoundAy1, "WS AY 1", true});
    out.push_back({AudioSourceType::WildSoundAy2, "WS AY 2", true});
}

const int16_t* WildSoundAudio::MixerBuffer(AudioSourceType type) const
{
    switch (type)
    {
        case AudioSourceType::WildSoundXm: return _xm.data();
        case AudioSourceType::WildSoundAy1: return _ay[0].data();
        case AudioSourceType::WildSoundAy2: return _ay[1].data();
        default: return nullptr;
    }
}

int16_t* WildSoundAudio::SsgMixerBuffer(AudioSourceType type)
{
    switch (type)
    {
        case AudioSourceType::WildSoundAy1: return _ay[0].data();
        case AudioSourceType::WildSoundAy2: return _ay[1].data();
        default: return nullptr;
    }
}

void WildSoundAudio::ApplyEvent(const WildSoundEvent& e)
{
    switch (e.kind)
    {
        case WildSoundEvent::AyWrite:
            if (e.reg < 16)
                _chips[e.chip & 1]->writeRegister(e.reg, e.value);
            break;
        case WildSoundEvent::PlayXm:
            _player.Start(_core.Storage());
            break;
        case WildSoundEvent::StopXm:
            _player.Stop();
            break;
        case WildSoundEvent::ChipReset:
            for (auto& chip : _chips)
                chip->reset();
            _player.Stop();
            break;
    }
}

void WildSoundAudio::FrameEnd(size_t samples)
{
    uint64_t frameT = 0;
    if (_context)
        frameT = _context->config.frame;
    Render(samples, frameT ? frameT : 71680);
}

void WildSoundAudio::Render(size_t samples, uint64_t frameT)
{
    samples = std::min<size_t>(samples, MAX_SAMPLES_PER_FRAME);
    _lastSamples = samples;
    std::vector<WildSoundEvent>& events = _core.Events();
    size_t next = 0;

    // The AY chips, sample by sample, with each write applied on the sample its time falls in
    for (size_t i = 0; i < samples; ++i)
    {
        const uint64_t sampleEnd = (static_cast<uint64_t>(i) + 1) * frameT / samples;
        while (next < events.size() && events[next].t < sampleEnd)
            ApplyEvent(events[next++]);

        double sum[2][2] = {};
        int ticks = 0;
        _tickPhase += _ticksPerSample;
        while (_tickPhase >= 1.0)
        {
            _tickPhase -= 1.0;
            for (int c = 0; c < 2; ++c)
            {
                _chips[c]->updateState(true);
                sum[c][0] += _chips[c]->mixedLeft();
                sum[c][1] += _chips[c]->mixedRight();
            }
            ++ticks;
        }
        const bool xm = _core.Mode() == WildSoundMode::Xm;
        for (int c = 0; c < 2; ++c)
        {
            const double l = ticks && !xm ? sum[c][0] / ticks : 0.0;
            const double r = ticks && !xm ? sum[c][1] / ticks : 0.0;
            _ay[c][2 * i] = static_cast<int16_t>(std::clamp(l * INT16_MAX, -32768.0, 32767.0));
            _ay[c][2 * i + 1] = static_cast<int16_t>(std::clamp(r * INT16_MAX, -32768.0, 32767.0));
        }
    }
    // What remains (a frame with no samples mixed, or events at the frame's very end)
    while (next < events.size())
        ApplyEvent(events[next++]);
    events.clear();

    if (samples)
        _player.Render(_core.Storage(), _xm.data(), samples);
}

/// region <State>

size_t WildSoundAudio::StateSize() const
{
    return WildSoundXmPlayer::StateSize() + 2 * _chips[0]->TTDStateSize() + sizeof(double);
}

void WildSoundAudio::SaveState(uint8_t* dst) const
{
    _player.SaveState(dst);
    dst += WildSoundXmPlayer::StateSize();
    for (const auto& chip : _chips)
    {
        chip->TTDSaveState(dst);
        dst += chip->TTDStateSize();
    }
    std::memcpy(dst, &_tickPhase, sizeof(double));
}

void WildSoundAudio::LoadState(const uint8_t* src)
{
    _player.LoadState(src);
    src += WildSoundXmPlayer::StateSize();
    for (auto& chip : _chips)
    {
        chip->TTDLoadState(src);
        src += chip->TTDStateSize();
    }
    std::memcpy(&_tickPhase, src, sizeof(double));
    ++_epoch;  // the voicing restarts with the restored rows
}

/// endregion </State>

}  // namespace wildsound
