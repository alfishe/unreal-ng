#pragma once
#include "stdafx.h"

#include "common/modulelogger.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/chips/soundchip_ym2149.h"
#include <vector>

class EmulatorContext;



class SoundManager
{
    /// region <Constants>
public:
    static const int CHANNELS = 2;
    static const int SAMPLING_RATE = 44100;
    static constexpr double SAMPLES_PER_FRAME = SAMPLING_RATE / 50.0;   // 882 audio samples per frame @44100

    /// endregion </Constants>

    /// region <Fields>
protected:
    EmulatorContext* _context;
    ModuleLogger* _logger;

    typedef std::vector<std::vector<uint16_t>> AudioBufferType;
    AudioBufferType _outBuffer;         // Final rendered stereo PCM samples @44100
    size_t _outBufferReadOffset;
    size_t _outBufferWriteOffset;

    // Supported sound chips
    SoundChip_AY8910 _ay8910;
    SoundChip_YM2149 _ym2149;
    // SoundChip_TurboSound;
    // SoundChip_TurboSoundFM;
    // SoundChip_MoonSound;
    // SoundChip_SAA1099;
    // SoundChip_GeneralSound;

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

    /// endregion </Methods>

    /// region <Wave file export>
public:
    bool openWaveFile(std::string& path);
    void closeWaveFile();

    /// endregion </Wave file export>

    /// region <Port interconnection>

protected:
    bool attachAY8910ToPorts();
    bool detachAY810FromPorts();

    /// endregion </Port interconnection>
};
