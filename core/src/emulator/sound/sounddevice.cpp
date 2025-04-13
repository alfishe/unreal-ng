#include "sounddevice.h"

/// region <Constructors / destructors>
SoundDevice::SoundDevice(size_t clockRate, size_t sampleRate)
{
    _clockRate = clockRate;
    _sampleRate = sampleRate;

    _clockTick = 0;
    _startFrameClockTick = 0;
}
/// endregion </Constructors / destructors>

/// region <Methods>
void SoundDevice::frameStart(size_t tacts)
{
    size_t endtick = (tacts * _sampleRate) / _clockRate;
    _startFrameClockTick = _clockTick - endtick;
}

void SoundDevice::frameEnd(size_t tacts)
{
    size_t endtick = (tacts * _sampleRate) / _clockRate;
    flush(_startFrameClockTick + endtick);
}

void SoundDevice::update(size_t tact, float monoSample)
{
    (void)tact;
    (void)monoSample;
}
void SoundDevice::update(size_t tact, float leftSample, float rightSample)
{
    (void)tact;
    (void)leftSample;
    (void)rightSample;
}
/// region </Methods>

/// region <Helper methods>
void SoundDevice::flush(size_t endCTick)
{
    (void)endCTick;
}
/// endregion </Helper methods>