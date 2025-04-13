#pragma once

#include "sounddevice.h"
#include "common/modulelogger.h"

class EmulatorContext;

class Beeper : public SoundDevice
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_SOUND;
    const uint16_t _SUBMODULE = PlatformSoundSubmodulesEnum::SUBMODULE_SOUND_BEEPER;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
protected:
    EmulatorContext* _context;

    uint64_t _prevFrameTState;               // Store previous call tState counter
    uint8_t _portFEState;                    // Store previous masked port FE state
    /// endregion </Fields>

    /// region Constructors / destructors>
public:
    Beeper() = delete;
    Beeper(EmulatorContext* context, size_t clockRate, size_t samplingRate) : SoundDevice(clockRate, samplingRate), _context(context) {}
    virtual ~Beeper() = default;
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    void reset();
    void handlePortOut(uint8_t value, uint32_t frameTState);
    /// endregion>

    /// region <Helper methods>
protected:
    void prepareBeeperDACTable();
    /// endregion </Helper methods>
};
