#include "gsaudioout.h"

#include <algorithm>

#include "3rdparty/blip_buf/blip_buf.h"
#include "emulator/sound/audio.h"

GSAudioOut::GSAudioOut(double clockHz, size_t sampleRate)
    : _clockHz(clockHz)
    , _sampleRate(sampleRate)
{
    _blipL = blip_new(MAX_SAMPLES_PER_FRAME + 64);
    _blipR = blip_new(MAX_SAMPLES_PER_FRAME + 64);
    blip_set_rates(_blipL, _clockHz, static_cast<double>(_sampleRate));
    blip_set_rates(_blipR, _clockHz, static_cast<double>(_sampleRate));
}

GSAudioOut::~GSAudioOut()
{
    blip_delete(_blipL);
    blip_delete(_blipR);
}

void GSAudioOut::setRates(double clockHz, size_t sampleRate)
{
    _clockHz = clockHz;
    _sampleRate = sampleRate;
    blip_set_rates(_blipL, _clockHz, static_cast<double>(_sampleRate));
    blip_set_rates(_blipR, _clockHz, static_cast<double>(_sampleRate));
    clear();
}

void GSAudioOut::clear()
{
    if (_blipL)
        blip_clear(_blipL);
    if (_blipR)
        blip_clear(_blipR);
}

void GSAudioOut::reset()
{
    _lastL = 0;
    _lastR = 0;
    clear();
}

bool GSAudioOut::set(int64_t position, int64_t frameLength, int32_t left, int32_t right, bool synthesize)
{
    bool stepped = false;
    if (synthesize && frameLength > 0)
    {
        const int32_t deltaL = left - _lastL;
        const int32_t deltaR = right - _lastR;
        if (deltaL != 0 || deltaR != 0)
        {
            // Clamped to the frame: a host speed switch mid-frame can move the
            // card past the frame length computed at its start
            const int64_t clamped = std::clamp<int64_t>(position, 0, frameLength - 1);
            if (deltaL != 0)
                blip_add_delta(_blipL, static_cast<unsigned>(clamped), deltaL);
            if (deltaR != 0)
                blip_add_delta(_blipR, static_cast<unsigned>(clamped), deltaR);
            stepped = true;
        }
    }
    _lastL = left;
    _lastR = right;
    return stepped;
}

int GSAudioOut::endFrame(int64_t frameLength, int samples, int16_t* buffer)
{
    blip_end_frame(_blipL, static_cast<unsigned>(frameLength));
    blip_end_frame(_blipR, static_cast<unsigned>(frameLength));

    samples = std::clamp(samples, 0, static_cast<int>(MAX_SAMPLES_PER_FRAME));
    const int samplesL = blip_read_samples(_blipL, &buffer[0], samples, 1 /* stereo stride */);
    const int samplesR = blip_read_samples(_blipR, &buffer[1], samples, 1 /* stereo stride */);
    for (int i = samplesL; i < samples; i++)
        buffer[i * 2] = 0;
    for (int i = samplesR; i < samples; i++)
        buffer[i * 2 + 1] = 0;
    return samples;
}
