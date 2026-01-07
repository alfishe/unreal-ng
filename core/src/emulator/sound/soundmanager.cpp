#include "soundmanager.h"

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
}

SoundManager::~SoundManager()
{
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

    std::fill(_beeperBuffer, _beeperBuffer + AUDIO_BUFFER_SAMPLES_PER_FRAME, 0);
    std::fill(_outBuffer, _outBuffer + AUDIO_BUFFER_SAMPLES_PER_FRAME, 0);

    // Reset sound rendering state
    _prevFrane = 0;
    _prevFrameTState = 0;
    _prevLeftValue = 0;
    _prevRightValue = 0;

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

    /// region <If we're over frame duration>
    if (prevIndex >= 882)
    {
        _prevFrameTState = frameTState;
        return;
    }
    if (sampleIndex >= 882)
        sampleIndex = 881;
    /// endregion <If we're over frame duration>

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
    if (!_turboSound) return nullptr;

    return _turboSound->getChip(index);
}

int SoundManager::getAYChipCount() const
{
    if (!_turboSound) return 0;

    return _turboSound->getChipCount();
}

/// endregion </Methods>

/// region <Emulation events>
void SoundManager::handleFrameStart()
{
    _turboSound->handleFrameStart();

    // Initialize render buffers
    memset(_beeperBuffer, 0x00, _beeperAudioDescriptor.memoryBufferSizeInBytes);
}

void SoundManager::handleStep()
{
    _turboSound->handleStep();
}

void SoundManager::handleFrameEnd()
{
    uint16_t* _ayBuffer = _turboSound->getAudioBuffer();

    /// region <Mix all channels to output buffer>
    AudioUtils::MixAudio((const int16_t*)_ayBuffer, _beeperBuffer, _outBuffer, AUDIO_BUFFER_SAMPLES_PER_FRAME);
    /// endregion </Mix all channels to output buffer>

    // Capture audio for recording BEFORE muting
    // This ensures recordings get the actual audio, not silence
    if (_context->pRecordingManager && _context->pRecordingManager->IsRecording())
    {
        _context->pRecordingManager->CaptureAudio(_outBuffer, SAMPLES_PER_FRAME * AUDIO_CHANNELS);
    }

    // Enqueue generated sound data via previously registered application callback
    // Note: Audio callbacks are cleared when emulator loses audio device access to prevent
    // multiple emulators from using the same audio device simultaneously
    if (_context->pAudioCallback && _context->pAudioManagerObj)
    {
        // If muted, send silence instead of actual audio
        if (_mute)
        {
            // Zero out the buffer (silence)
            memset(_outBuffer, 0, SAMPLES_PER_FRAME * AUDIO_CHANNELS * sizeof(int16_t));
        }

        try
        {
            _context->pAudioCallback(_context->pAudioManagerObj, _outBuffer, SAMPLES_PER_FRAME * AUDIO_CHANNELS);
        }
        catch (const std::exception& e)
        {
            // Log error but don't crash - audio callback failure shouldn't stop emulation
            printf("SoundManager::handleFrameEnd - Audio callback failed: %s\n", e.what());
        }
        catch (...)
        {
            // Log error but don't crash - audio callback failure shouldn't stop emulation
            printf("SoundManager::handleFrameEnd - Audio callback failed with unknown exception\n");
        }
    }
}

/// endregion </Emulation events>

/// region <Wave file export>
bool SoundManager::openWaveFile(std::string& path)
{
    bool result = false;

    int res = tinywav_open_write(
                &_tinyWav,
                AUDIO_CHANNELS,
                AUDIO_SAMPLING_RATE,
                TW_INT16,
                TW_INTERLEAVED,
                path.c_str());

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

    return result;
}

bool SoundManager::detachFromPorts()
{
    bool result = true;

    //_ay8910->detachFromPorts();
    _turboSound->detachFromPorts();

    return result;
}

/// endregion </Port interconnection>