#pragma once
#include "stdafx.h"

#include "common/modulelogger.h"
#include "common/sound/audiofilehelper.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/chips/soundchip_ym2149.h"
#include "beeper.h"
#include <vector>

class EmulatorContext;

static const int AUDIO_CHANNELS = 2;
static const int AUDIO_SAMPLING_RATE = 44100;
static const int FRAMES_PER_SECOND = 50;
static constexpr int AUDIO_BUFFER_DURATION_MILLISEC = 1000 / FRAMES_PER_SECOND;
static constexpr int SAMPLES_PER_FRAME = AUDIO_SAMPLING_RATE / FRAMES_PER_SECOND;   // 882 audio samples per frame @44100
static constexpr int AUDIO_BUFFER_SAMPLES_PER_FRAME = SAMPLES_PER_FRAME * AUDIO_CHANNELS;
static constexpr int AUDIO_BUFFER_SIZE_PER_FRAME = SAMPLES_PER_FRAME * AUDIO_CHANNELS * sizeof(uint16_t);

struct AudioFrameDescriptor
{
    static constexpr uint32_t samplingRate = AUDIO_SAMPLING_RATE;
    static constexpr uint8_t channels = AUDIO_CHANNELS;
    static constexpr size_t durationInMs = AUDIO_BUFFER_DURATION_MILLISEC;
    static constexpr size_t durationInSamples = SAMPLES_PER_FRAME;
    static constexpr size_t memoryBufferSizeInBytes = AUDIO_BUFFER_SIZE_PER_FRAME;

    uint8_t memoryBuffer[memoryBufferSizeInBytes] = {};
};

class SoundManager
{
    /// region <Fields>
protected:
    EmulatorContext* _context;
    ModuleLogger* _logger;

    AudioFrameDescriptor _audioFrameDescriptor;                                 // Frame that exposed outside
    int16_t* const _audioBuffer = (int16_t*)_audioFrameDescriptor.memoryBuffer; // Shortcut to it's sample buffer

    volatile bool _mute;

    size_t  _prevFrane = 0;
    uint32_t _prevFrameTState = 0;
    int16_t _prevLeftValue;
    int16_t _prevRightValue;

    uint32_t _audioBufferWrites = 0;

    // Supported sound chips
    Beeper*           _beeper;
    SoundChip_AY8910* _ay8910;
    SoundChip_YM2149* _ym2149;
    // SoundChip_TurboSound;
    // SoundChip_TurboSoundFM;
    // SoundChip_MoonSound;
    // SoundChip_SAA1099;
    // SoundChip_GeneralSound;

    // Save to Wave file
    TinyWav _tinyWav;

    /// endregion </Fields>

    /// region <Debug functionality>
public:
    std::vector<int16_t> _continuousAudioBuffer;
    void saveContinuousWaveFile(const std::vector<int16_t>& audioBuffer);

    FILE* _logFile;
    FILE* _pcmFile;
    /// endregion </Debug functionality>

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
    bool attachAY8910ToPorts();
    bool detachAY8910FromPorts();

    /// endregion </Port interconnection>
};
