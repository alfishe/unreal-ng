#pragma once
#include "stdafx.h"

#include "common/modulelogger.h"
#include "emulator/sound/chips/soundchip_ay8910.h"

class EmulatorContext;

class SoundManager
{
    /// region <Constants>
public:
    static const int SAMPLING_RATE = 44100;
    static constexpr double SAMPLES_PER_FRAME = SAMPLING_RATE / 50.0;   // 882 audio samples per frame @44100

    /// endregion </Constants>

    /// region <Fields>
protected:
    EmulatorContext* _context;
    ModuleLogger* _logger;

    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    SoundManager() = delete;                    // Disable default constructor
    SoundManager(const SoundManager&) = delete; // Disable copy constructor
    SoundManager(EmulatorContext* context);
    virtual ~SoundManager();

    /// endregion </Constructors / Destructors>

    /// region <Methods>

public:
    void reset();

    /// endregion </Methods>
};
