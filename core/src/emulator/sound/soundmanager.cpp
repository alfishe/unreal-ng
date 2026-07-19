#include "soundmanager.h"

#include "base/featuremanager.h"
#include "common/dumphelper.h"
#include "common/sound/audiohelper.h"
#include "common/sound/audioutils.h"
#include "common/stringhelper.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "stdafx.h"

/// region <Constructors / Destructors>

SoundManager::SoundManager(EmulatorContext* context)
{
    _context = context;
    _logger = context->pModuleLogger;

    _beeper = new Beeper(_context, CPU_CLOCK_RATE, AUDIO_SAMPLING_RATE);
    _turboSound = new SoundChip_TurboSound(_context);

    // Build the device registry based on what this machine has
    // Beeper is always present
    _devices.push_back({AudioSourceType::Beeper, "Beeper", false, false, 1.0f, 0.0f, false});
    // AY 1 is always present (single AY or first chip of TurboSound)
    _devices.push_back({AudioSourceType::AY1_All, "AY 1", false, false, 1.0f, 0.0f, false});
    // AY 2 only if TurboSound (second chip)
    if (_turboSound && _turboSound->getChipCount() > 1)
    {
        _devices.push_back({AudioSourceType::AY2_All, "AY 2", false, false, 1.0f, 0.0f, false});
    }

    // Covox if config flag is set (Pentagon/Scorpion style)
    if (_context->config.sound.covoxFB)
    {
        _covox = new Covox(_context);
        _devices.push_back({AudioSourceType::COVOX, "COVOX", false, false, 1.0f, 0.0f, false});
    }

    // Initialize AY character chains (one per TurboSound chip for independent DSP state)
    // - ChipType::AY uses shorter delay and no LP (preserves square wave harmonics)
    // - Punch: AY preset (gentler - square waves already have rich harmonics)
    _ayChain0.setup(AUDIO_SAMPLING_RATE);
    _ayChain0.setChipType(AudioCharacterChain::ChipType::AY);
    _ayChain0.setPunchPreset(AudioCharacterChain::PunchPreset::AY);
    _ayChain0.setPunchEnabled(true);
    _ayChain0.setRoomMode(AudioCharacterChain::RoomMode::Off);

    _ayChain1.setup(AUDIO_SAMPLING_RATE);
    _ayChain1.setChipType(AudioCharacterChain::ChipType::AY);
    _ayChain1.setPunchPreset(AudioCharacterChain::PunchPreset::AY);
    _ayChain1.setPunchEnabled(true);
    _ayChain1.setRoomMode(AudioCharacterChain::RoomMode::Off);

    // Initialize beeper character chain
    // - ChipType::AY (no LP) - beeper is also square waves, LP kills brightness
    // - Punch: Beeper preset (stronger - 1-bit audio needs attack definition)
    _beeperChain.setup(AUDIO_SAMPLING_RATE);
    _beeperChain.setChipType(AudioCharacterChain::ChipType::AY);
    _beeperChain.setPunchPreset(AudioCharacterChain::PunchPreset::Beeper);
    _beeperChain.setPunchEnabled(true);
    _beeperChain.setRoomMode(AudioCharacterChain::RoomMode::Off);
}

SoundManager::~SoundManager()
{
    if (_covox)
    {
        delete _covox;
    }

    if (_turboSound)
    {
        delete _turboSound;
    }

    if (_beeper)
    {
        delete _beeper;
    }
}

/// endregion </Constructors / Destructors>

/// region <Methods>

void SoundManager::reset()
{
    // Reset all chips state
    _turboSound->reset();
    _beeper->reset();
    if (_covox)
        _covox->reset();

    std::fill(_beeperBuffer, _beeperBuffer + AUDIO_BUFFER_SAMPLES_PER_FRAME, 0);
    std::fill(_outBuffer, _outBuffer + AUDIO_BUFFER_SAMPLES_PER_FRAME, 0);

    // Reset sound rendering state
    _prevFrane = 0;
    _prevFrameTState = 0;
    _prevLeftValue = 0;
    _prevRightValue = 0;

    // Reset beeper lowpass filter state
    _beeperLp1L = _beeperLp2L = 0.0f;
    _beeperLp1R = _beeperLp2R = 0.0f;

    // Reset audio buffer write counter
    _audioBufferWrites = 0;

    // New wave file
    // closeWaveFile();
    // std::string filePath = "unreal.wav";
    // openWaveFile(filePath);
}

void SoundManager::mute()
{
    _mute = true;
}

void SoundManager::unmute()
{
    _mute = false;
}

const AudioFrameDescriptor& SoundManager::getAudioBufferDescriptor()
{
    return _beeperAudioDescriptor;
}

Beeper& SoundManager::getBeeper()
{
    return *_beeper;
}

void SoundManager::updateDAC(uint32_t frameTState, int16_t left, int16_t right)
{
    CONFIG& config = _context->config;

    // We're transitioned to new frame
    if (_prevFrameTState > frameTState && _prevFrameTState >= config.frame)
    {
        _prevFrameTState -= config.frame;
    }

    [[maybe_unused]] int32_t deltaTime = (frameTState - _prevFrameTState) % config.frame;

    /*
    const double ratio = (double)config.frame / (double)SAMPLES_PER_FRAME;

    size_t prevIndex = (floor)((double)_prevFrameTState / ratio);
    size_t sampleIndex = (floor)((double)frameTState / ratio);
    */

    uint32_t scaledFrame = config.frame * _context->emulatorState.current_z80_frequency_multiplier;

    size_t prevIndex = (_prevFrameTState * SAMPLES_PER_FRAME) / scaledFrame;
    size_t sampleIndex = (frameTState * SAMPLES_PER_FRAME) / scaledFrame;

    // region <If we're over frame duration>
    if (prevIndex >= 882)
    {
        _prevFrameTState = frameTState;
        return;
    }

    if (sampleIndex >= 882)
        sampleIndex = 881;
    // endregion <If we're over frame duration>

    // Fill the gap between previous call and current
    if (sampleIndex > prevIndex)
    {
        for (size_t i = prevIndex; i < sampleIndex && i < _beeperAudioDescriptor.memoryBufferSizeInBytes / 2; i++)
        {
            _beeperBuffer[i * 2] = _prevLeftValue;
            _beeperBuffer[i * 2 + 1] = _prevRightValue;
        }
    }
    else
    {
        // Audio callback not active - this emulator doesn't have audio device access
        // This is normal for headless emulators or emulators that lost audio device ownership
    }

    // Render current samples
    if (sampleIndex != prevIndex)
    {
        _beeperBuffer[sampleIndex * 2] = left;
        _beeperBuffer[sampleIndex * 2 + 1] = right;
    }

    _audioBufferWrites++;

    // Remember timestamp and channel values
    _prevLeftValue = left;
    _prevRightValue = right;
    _prevFrameTState = frameTState;
    _prevFrane = _context->emulatorState.frame_counter;
}

// TurboSound/AY chip access for debugging
SoundChip_AY8910* SoundManager::getAYChip(int index) const
{
    if (!_turboSound)
        return nullptr;

    return _turboSound->getChip(index);
}

int SoundManager::getAYChipCount() const
{
    if (!_turboSound)
        return 0;

    return _turboSound->getChipCount();
}

// Device registry API
AudioDeviceInfo* SoundManager::device(AudioSourceType type)
{
    for (auto& d : _devices)
        if (d.type == type)
            return &d;
    return nullptr;
}

const AudioDeviceInfo* SoundManager::device(AudioSourceType type) const
{
    for (auto& d : _devices)
        if (d.type == type)
            return &d;
    return nullptr;
}

const int16_t* SoundManager::deviceBuffer(AudioSourceType type) const
{
    switch (type)
    {
        case AudioSourceType::MasterMix:
            return _outBuffer;
        case AudioSourceType::Beeper:
            return _beeperBuffer;
        case AudioSourceType::AY1_All:
            return _turboSound ? _turboSound->getChipBuffer(0) : nullptr;
        case AudioSourceType::AY2_All:
            return _turboSound ? _turboSound->getChipBuffer(1) : nullptr;
        case AudioSourceType::COVOX:
            return _covox ? _covox->getBuffer() : nullptr;
        default:
            return nullptr;
    }
}

void SoundManager::setDeviceMute(AudioSourceType type, bool mute)
{
    if (auto* d = device(type))
        d->mute = mute;
}

void SoundManager::setDeviceSolo(AudioSourceType type, bool solo)
{
    if (auto* d = device(type))
        d->solo = solo;
}

void SoundManager::setDeviceVolume(AudioSourceType type, float volume)
{
    if (auto* d = device(type))
        d->volume = std::clamp(volume, 0.0f, 1.0f);
}

void SoundManager::syncAYChainSettings()
{
    // Copy settings from chain 0 to chain 1 (UI edits chain 0, both should match)
    _ayChain1.setChipType(_ayChain0.getChipType());
    _ayChain1.setPunchPreset(_ayChain0.getPunchPreset());
    _ayChain1.setPunchEnabled(_ayChain0.isPunchEnabled());
    _ayChain1.setRoomMode(_ayChain0.getRoomMode());
}

// Legacy volume API delegates to registry
void SoundManager::setAYVolume(double volume)
{
    _ayVolume = std::clamp(volume, 0.0, 1.0);
    setDeviceVolume(AudioSourceType::AY1_All, static_cast<float>(_ayVolume));
    setDeviceVolume(AudioSourceType::AY2_All, static_cast<float>(_ayVolume));
}

void SoundManager::setBeeperVolume(double volume)
{
    _beeperVolume = std::clamp(volume, 0.0, 1.0);
    setDeviceVolume(AudioSourceType::Beeper, static_cast<float>(_beeperVolume));
}

/// endregion </Methods>

/// region <Emulation events>
void SoundManager::handleFrameStart()
{
    _turboSound->handleFrameStart();
    if (_covox)
        _covox->handleFrameStart();

    // Initialize render buffers
    memset(_beeperBuffer, 0x00, _beeperAudioDescriptor.memoryBufferSizeInBytes);
}

void SoundManager::handleStep()
{
    // Fast exit if sound generation disabled
    if (!_feature_sound_enabled)
        return;

    _turboSound->handleStep();
}

void SoundManager::handleFrameEnd()
{
    /// region <Process AY through its character chain>
    // AY chain: gentler punch (square waves already have harmonics)
    // Room uses no LP to preserve brightness
    // Process per-chip buffers with separate chain instances to preserve DSP state
    if (_turboSound)
    {
        int16_t* chip0Buf = _turboSound->getChipBuffer(0);
        int16_t* chip1Buf = _turboSound->getChipBuffer(1);
        if (chip0Buf)
            _ayChain0.processInt16(chip0Buf, SAMPLES_PER_FRAME);
        if (chip1Buf)
            _ayChain1.processInt16(chip1Buf, SAMPLES_PER_FRAME);
    }
    /// endregion </Process AY>

    /// region <Process beeper>
    // Optional 2-pole lowpass @ 16kHz - removes ultrasonic harshness, preserves music
    if (_beeperFilterEnabled)
    {
        constexpr float toFloat = 1.0f / 32768.0f;
        constexpr float toInt16 = 32767.0f;

        for (int i = 0; i < SAMPLES_PER_FRAME; i++)
        {
            float left = _beeperBuffer[i * 2] * toFloat;
            float right = _beeperBuffer[i * 2 + 1] * toFloat;

            // Two cascaded 1-pole = 2-pole with -12dB/octave rolloff
            _beeperLp1L += BEEPER_LP_COEF * (left - _beeperLp1L);
            _beeperLp2L += BEEPER_LP_COEF * (_beeperLp1L - _beeperLp2L);

            _beeperLp1R += BEEPER_LP_COEF * (right - _beeperLp1R);
            _beeperLp2R += BEEPER_LP_COEF * (_beeperLp1R - _beeperLp2R);

            _beeperBuffer[i * 2] = static_cast<int16_t>(std::clamp(_beeperLp2L * toInt16, -32768.0f, 32767.0f));
            _beeperBuffer[i * 2 + 1] = static_cast<int16_t>(std::clamp(_beeperLp2R * toInt16, -32768.0f, 32767.0f));
        }
    }

    // Beeper chain: stronger punch for 1-bit synths (digidrums, PWM synths)
    _beeperChain.processInt16(_beeperBuffer, SAMPLES_PER_FRAME);
    /// endregion </Process beeper>

    /// region <Registry-driven mixing with mute/solo/volume + peak calculation>
    // Finalize Covox frame (DC removal etc.) before mixing
    if (_covox)
        _covox->handleFrameEnd();

    // Determine if any device has solo active
    bool soloActive = false;
    for (const auto& d : _devices)
    {
        if (d.solo)
        {
            soloActive = true;
            break;
        }
    }

    // Clear output buffer before mixing
    memset(_outBuffer, 0, SAMPLES_PER_FRAME * AUDIO_CHANNELS * sizeof(int16_t));

    // Mix each device according to audibility rules and compute peaks
    for (auto& d : _devices)
    {
        // Audibility: if any solo is active, only soloed devices are audible;
        // otherwise, non-muted devices are audible
        bool audible = soloActive ? d.solo : !d.mute;

        // Get the device's buffer
        const int16_t* srcBuffer = nullptr;
        switch (d.type)
        {
            case AudioSourceType::Beeper:
                srcBuffer = _beeperBuffer;
                break;
            case AudioSourceType::AY1_All:
                srcBuffer = _turboSound ? _turboSound->getChipBuffer(0) : nullptr;
                break;
            case AudioSourceType::AY2_All:
                srcBuffer = _turboSound ? _turboSound->getChipBuffer(1) : nullptr;
                break;
            case AudioSourceType::COVOX:
                srcBuffer = _covox ? _covox->getBuffer() : nullptr;
                break;
            default:
                break;
        }

        if (!srcBuffer)
            continue;

        // Compute peak and activity (always, even if muted — for UI meters)
        float peak = 0.0f;
        for (size_t i = 0; i < SAMPLES_PER_FRAME * AUDIO_CHANNELS; i++)
        {
            float absVal = std::abs(static_cast<float>(srcBuffer[i])) / 32768.0f;
            if (absVal > peak)
                peak = absVal;
        }
        d.peak = peak;
        d.activeRecently = (peak > 0.001f);

        // Mix into output if audible
        if (audible && d.volume > 0.0f)
        {
            float vol = d.volume;
            for (size_t i = 0; i < SAMPLES_PER_FRAME * AUDIO_CHANNELS; i++)
            {
                int32_t mixed = _outBuffer[i] + static_cast<int32_t>(srcBuffer[i] * vol);
                // Saturating add
                _outBuffer[i] = static_cast<int16_t>(std::clamp(mixed, -32768, 32767));
            }
        }
    }
    /// endregion </Registry-driven mixing>

    // Capture audio for recording BEFORE muting
    // This ensures recordings get the actual audio, not silence
    if (_context->pRecordingManager && _context->pRecordingManager->IsRecording())
    {
        _context->pRecordingManager->CaptureAudio(_outBuffer, SAMPLES_PER_FRAME * AUDIO_CHANNELS);
    }

    // Enqueue generated sound data via previously registered application callback
    // Note: Audio callbacks are cleared when emulator loses audio device access to prevent
    // multiple emulators from using the same audio device simultaneously
    // Use memory_order_acquire to ensure we see the latest values written by the UI thread
    AudioCallback callback = _context->pAudioCallback.load(std::memory_order_acquire);
    void* obj = _context->pAudioManagerObj.load(std::memory_order_acquire);

    if (callback && obj)
    {
        // If muted, send silence instead of actual audio.
        // No need to send silence if sound generation is disabled -
        // buffer was already zeroed out in SoundManager::handleFrameStart() method
        if (_feature_sound_enabled && _mute)
        {
            // Zero out the buffer (silence)
            memset(_outBuffer, 0, SAMPLES_PER_FRAME * AUDIO_CHANNELS * sizeof(int16_t));
        }

        try
        {
            callback(obj, _outBuffer, SAMPLES_PER_FRAME * AUDIO_CHANNELS);
        }
        catch (const std::exception& e)
        {
            // Log error but don't crash - audio callback failure shouldn't stop emulation
            LOGERROR("SoundManager::handleFrameEnd - Audio callback failed: %s\n", e.what());
        }
        catch (...)
        {
            // Log error but don't crash - audio callback failure shouldn't stop emulation
            LOGERROR("SoundManager::handleFrameEnd - Audio callback failed with unknown exception\n");
        }
    }
}

/// @brief Update feature cache flags from FeatureManager.
///
/// This method is automatically called by FeatureManager::onFeatureChanged() whenever
/// sound-related feature states change. It updates cached boolean flags to avoid
/// repeated hash map lookups in hot paths (handleStep is called ~70,000 times/frame).
///
/// @note Do NOT call directly - use FeatureManager API to change states.
///
/// **Triggered by (CLI):**
/// ```bash
/// feature sound off       # Disables sound generation (~18% CPU savings)
/// feature sound on        # Re-enables sound generation
/// feature soundhq off     # Switches to low-quality DSP (~15% CPU savings)
/// feature soundhq on      # Switches to high-quality DSP (FIR + oversampling)
/// ```
///
/// **Triggered by (API):**
/// ```cpp
/// context->pFeatureManager->setFeature("sound", false);
/// context->pFeatureManager->setFeature("soundhq", true);
/// ```
///
/// **Propagation Flow:**
/// ```
/// User CLI/API → FeatureManager::setFeature()
///     ↓
/// FeatureManager::onFeatureChanged()
///     ↓
/// SoundManager::UpdateFeatureCache()  ← YOU ARE HERE
///     ↓
/// _feature_sound_enabled, _feature_soundhq_enabled updated
///     ↓
/// Hot paths (handleStep) use cached flags
/// ```
void SoundManager::UpdateFeatureCache()
{
    if (_context && _context->pFeatureManager)
    {
        bool newSoundEnabled = _context->pFeatureManager->isEnabled(Features::kSoundGeneration);
        _feature_soundhq_enabled = _context->pFeatureManager->isEnabled(Features::kSoundHQ);

        _feature_sound_enabled = newSoundEnabled;

        // Propagate HQ flag to TurboSound
        if (_turboSound)
        {
            _turboSound->setHQEnabled(_feature_soundhq_enabled);
        }
    }
    else
    {
        // Fallback: if FeatureManager unavailable, ensure sound is ON by default
        LOGWARNING("SoundManager::UpdateFeatureCache - FeatureManager unavailable, defaulting sound ON");
        _feature_sound_enabled = true;
        _feature_soundhq_enabled = true;
    }
}

/// endregion </Emulation events>

/// region <Wave file export>
bool SoundManager::openWaveFile(std::string& path)
{
    bool result = false;

    int res =
        tinywav_open_write(&_tinyWav, AUDIO_CHANNELS, AUDIO_SAMPLING_RATE, TW_INT16, TW_INTERLEAVED, path.c_str());

    if (res == 0 && _tinyWav.file)
    {
        result = true;
    }

    return result;
}

void SoundManager::closeWaveFile()
{
    if (_tinyWav.file)
    {
        tinywav_close_write(&_tinyWav);
    }
}

void SoundManager::SoundManager::writeToWaveFile(uint8_t* buffer, size_t len)
{
    // Convert length from bytes to samples (stereo sample still counts as single)
    size_t lengthInSamples = len / AUDIO_CHANNELS / sizeof(uint16_t);

    // Save using method with Int16 samples input
    tinywav_write_i(&_tinyWav, buffer, lengthInSamples);
}

/// endregion </Wave file export>

/// region <Port interconnection>

bool SoundManager::attachToPorts()
{
    bool result = false;

    // result = _ay8910->attachToPorts(_context->pPortDecoder);
    result = _turboSound->attachToPorts(_context->pPortDecoder);

    // Attach SOUNDRIVE/Covox to port #FB (all 4 ports decode to same handler)
    if (_covox && _context->pPortDecoder)
    {
        result &= _context->pPortDecoder->RegisterPortHandler(Covox::PORT_RIGHT_B, _covox);
    }

    return result;
}

bool SoundManager::detachFromPorts()
{
    bool result = true;

    //_ay8910->detachFromPorts();
    _turboSound->detachFromPorts();

    // Detach SOUNDRIVE/Covox from port #FB
    if (_covox && _context->pPortDecoder)
    {
        _context->pPortDecoder->UnregisterPortHandler(Covox::PORT_RIGHT_B);
    }

    return result;
}

/// endregion </Port interconnection>