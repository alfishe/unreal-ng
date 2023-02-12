#pragma once

#include "sounddevice.h"

class Beeper : public SoundDevice
{
    /// region Constructors / destructors>
public:
    Beeper(size_t clockRate, size_t samplingRate) : SoundDevice(clockRate, samplingRate) {}
    /// endregion </Constructors / destructors>
};
