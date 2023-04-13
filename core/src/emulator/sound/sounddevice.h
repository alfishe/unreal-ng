#pragma once

#include "stdafx.h"

class SoundDevice
{
    /// region <Fields>
protected:
    size_t _clockRate;
    size_t _sampleRate;

    size_t _clockTick;
    size_t _startFrameClockTick;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    SoundDevice(size_t clockRate, size_t sampleRate);
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    virtual void frameStart(size_t tacts);
    virtual void frameEnd(size_t tacts);
    virtual void update(size_t tact, float monoSample);
    virtual void update(size_t tact, float leftSample, float rightSample);
    /// region </Methods>

    /// region <Helper methods>
private:
    void flush(size_t endCTick);
    /// endregion </Helper methods>
};