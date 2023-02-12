#pragma once
#include "stdafx.h"

#include "common/modulelogger.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/chips/soundchip_ym2149.h"
#include "beeper.h"
#include <vector>

class EmulatorContext;



class SoundManager
{
    /// region <Constants>
public:
    static const int OUTPUT_CHANNELS = 2;
    static const int AUDIO_SAMPLING_RATE = 48000;
    static const int AUDIO_BUFFER_DURATION_MILLISEC = 1000;
    static constexpr double SAMPLES_PER_FRAME = AUDIO_SAMPLING_RATE / 50.0;   // 882 audio samples per frame @44100

    static const size_t AUDIO_BUFFERS = 3;

    /// endregion </Constants>

    /// region <Types>
public:
    typedef std::vector<std::vector<float>> AudioBuffer_t;
    /// endregion </Types>

    /// region <Fields>
protected:
    EmulatorContext* _context;
    ModuleLogger* _logger;

    AudioBuffer_t _outBeeper;
    AudioBuffer_t _outBufferLeft;           // Final rendered PCM samples, left channel, @ selected sampling rate
    AudioBuffer_t _outBufferRight;          // Final rendered PCM samples, right channel, @ selected sampling rate

    size_t _outBufferReadOffset;
    size_t _outBufferWriteOffset;

    // Supported sound chips
    Beeper*           _beeper;
    SoundChip_AY8910* _ay8910;
    SoundChip_YM2149* _ym2149;
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

    void initFrame();

    /// endregion </Methods>

    /// region <Wave file export>
public:
    bool openWaveFile(std::string& path);
    void closeWaveFile();

    /// endregion </Wave file export>

    /// region <Port interconnection>

protected:
    bool attachAY8910ToPorts();
    bool detachAY8910FromPorts();

    /// endregion </Port interconnection>
};
