#pragma once
#include "stdafx.h"

#include "common/modulelogger.h"
#include "common/sound/audiofilehelper.h"
#include "common/sound/filters/filter_interpolate.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/beeper.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include <vector>

class EmulatorContext;

class SoundManager
{
    /// region <Fields>
protected:
    EmulatorContext* _context;
    ModuleLogger* _logger;

    volatile bool _mute;

    AudioFrameDescriptor _beeperAudioDescriptor;                                  // Audio descriptor for the beeper
    int16_t* const _beeperBuffer = (int16_t*)_beeperAudioDescriptor.memoryBuffer; // Shortcut to it's sample buffer

    AudioFrameDescriptor _outAudioDescriptor;                                      // Audio descriptor for mixer output
    int16_t* const _outBuffer = (int16_t*)_outAudioDescriptor.memoryBuffer;        // Shortcut to it's sample buffer

    size_t  _prevFrane = 0;
    uint32_t _prevFrameTState = 0;
    int16_t _prevLeftValue;
    int16_t _prevRightValue;

    uint32_t _audioBufferWrites = 0;

    // Supported sound chips
    Beeper* _beeper;
    SoundChip_TurboSound* _turboSound;
    // SoundChip_TurboSoundFM;
    // SoundChip_MoonSound;
    // SoundChip_SAA1099;
    // SoundChip_GeneralSound;

    // Save to Wave file
    TinyWav _tinyWav;

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
    void mute();
    void unmute();

    const AudioFrameDescriptor& getAudioBufferDescriptor();
    Beeper& getBeeper();

    void updateDAC(uint32_t frameTState, int16_t left, int16_t right);
    /// endregion </Methods>

    /// region <Emulation events>
public:
    void handleFrameStart();
    void handleStep();
    void handleFrameEnd();
    /// endregion </Emulation events>

    /// region <Wave file export>
public:
    bool openWaveFile(std::string& path);
    void closeWaveFile();

    void writeToWaveFile(uint8_t* buffer, size_t len);

    /// endregion </Wave file export>

    /// region <Port interconnection>

public:
    bool attachToPorts();
    bool detachFromPorts();

    /// endregion </Port interconnection>
};
