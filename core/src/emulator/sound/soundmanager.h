#pragma once
#include <algorithm>
#include <vector>

#include "common/modulelogger.h"
#include "common/sound/audiofilehelper.h"
#include "common/sound/filters/filter_interpolate.h"
#include "common/sound/filters/audio_character_chain.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/beeper.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include "stdafx.h"

class EmulatorContext;

class SoundManager
{
    /// region <Fields>
protected:
    EmulatorContext* _context;
    ModuleLogger* _logger;

    volatile bool _mute = false;  // MUST initialize - sound unmuted by default
    bool _soundEnabled = true;

    AudioFrameDescriptor _beeperAudioDescriptor;                                   // Audio descriptor for the beeper
    int16_t* const _beeperBuffer = (int16_t*)_beeperAudioDescriptor.memoryBuffer;  // Shortcut to it's sample buffer

    AudioFrameDescriptor _outAudioDescriptor;                                // Audio descriptor for mixer output
    int16_t* const _outBuffer = (int16_t*)_outAudioDescriptor.memoryBuffer;  // Shortcut to it's sample buffer

    size_t _prevFrane = 0;
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

    // Audio character chains (punch enhancement + room simulation)
    // Separate chains for AY and beeper allow independent tuning
    AudioCharacterChain _ayChain;      // For AY/TurboSound
    AudioCharacterChain _beeperChain;  // For beeper (digidrums, PWM synths)

    // Beeper lowpass filter (removes ultrasonic harshness, preserves music)
    // 2-pole Butterworth @ 16kHz - steeper rolloff than 1-pole
    bool _beeperFilterEnabled = false;
    float _beeperLp1L = 0.0f, _beeperLp2L = 0.0f;  // Two cascaded 1-pole stages
    float _beeperLp1R = 0.0f, _beeperLp2R = 0.0f;
    static constexpr float BEEPER_LP_COEF = 0.85f;  // ~16kHz with 2 poles @ 44.1kHz

    // Master volume controls
    double _ayVolume = 1.0;
    double _beeperVolume = 1.0;

    // Save to Wave file
    TinyWav _tinyWav;

    // Feature cache flags (updated by FeatureManager::onFeatureChanged)
    bool _feature_sound_enabled = true;
    bool _feature_soundhq_enabled = true;

    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    SoundManager() = delete;                     // Disable default constructor
    SoundManager(const SoundManager&) = delete;  // Disable copy constructor
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

    // TurboSound/AY chip access for debugging
    bool hasTurboSound() const
    {
        return _turboSound != nullptr;
    }
    SoundChip_AY8910* getAYChip(int index) const;
    int getAYChipCount() const;
    bool isMuted() const
    {
        return _mute;
    }

    void updateDAC(uint32_t frameTState, int16_t left, int16_t right);

    // Audio character chains (punch + room simulation)
    AudioCharacterChain& getAYChain() { return _ayChain; }
    AudioCharacterChain& getBeeperChain() { return _beeperChain; }

    // Beeper filter control
    void setBeeperFilterEnabled(bool enabled) { _beeperFilterEnabled = enabled; }
    bool isBeeperFilterEnabled() const { return _beeperFilterEnabled; }

    // Master volume controls
    void setAYVolume(double volume) { _ayVolume = std::clamp(volume, 0.0, 1.0); }
    void setBeeperVolume(double volume) { _beeperVolume = std::clamp(volume, 0.0, 1.0); }
    double getAYVolume() const { return _ayVolume; }
    double getBeeperVolume() const { return _beeperVolume; }

    // Legacy accessor for compatibility
    AudioCharacterChain& getCharacterChain() { return _ayChain; }

    // Feature cache update (called by FeatureManager::onFeatureChanged)
    void UpdateFeatureCache();
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
