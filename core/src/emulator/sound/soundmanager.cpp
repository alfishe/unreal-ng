#include "stdafx.h"
#include "soundmanager.h"

#include "common/dumphelper.h"
#include "common/stringhelper.h"
#include "common/sound/audiohelper.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"

/// region <Constructors / Destructors>

SoundManager::SoundManager(EmulatorContext *context)
{
    _context = context;
    _logger = context->pModuleLogger;

    _beeper = new Beeper(_context, 3.5 * 1'000'000, AUDIO_SAMPLING_RATE);
    _ay8910 = new SoundChip_AY8910(_context);
    _ym2149 = new SoundChip_YM2149();

    /// region <Debug functionality>
    _pcmFile = fopen("/Users/dev/Downloads/int16.pcm", "rb");
    /// endregion </Debug functionality>
}

SoundManager::~SoundManager()
{
    /// region <Debug functionality
    if (_pcmFile)
    {
        fclose(_pcmFile);
    }

    closeWaveFile();
    /// endregion /Debug functionality>

    if (_ym2149)
    {
        delete _ym2149;
    }

    if (_ay8910)
    {
        delete _ay8910;
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
    _ay8910->reset();

    _x = 0.0;
    double oversample_stream_rate = AUDIO_SAMPLING_RATE * 8 * FilterInterpolate::DECIMATE_FACTOR; // 2822400 bits per second for 44100Hz sample rate
    _clockStep = PSG_CLOCK_RATE / oversample_stream_rate;
    std::fill(_beeperBuffer, _beeperBuffer + AUDIO_BUFFER_SAMPLES_PER_FRAME, 0);
    std::fill(_ayBuffer, _ayBuffer + AUDIO_BUFFER_SAMPLES_PER_FRAME, 0);

    // Set FIR parameters
    _leftFIR.setRates(PSG_CLOCK_RATE, AUDIO_SAMPLING_RATE);
    _rightFIR.setRates(PSG_CLOCK_RATE, AUDIO_SAMPLING_RATE);

    // Reset sound rendering state
    _prevFrane = 0;
    _prevFrameTState = 0;
    _prevLeftValue = 0;
    _prevRightValue = 0;

    // New wave file
    closeWaveFile();
    std::string filePath = "unreal.wav";
    openWaveFile(filePath);
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

    int32_t deltaTime = (frameTState - _prevFrameTState) % config.frame;

    /*
    const double ratio = (double)config.frame / (double)SAMPLES_PER_FRAME;

    size_t prevIndex = (floor)((double)_prevFrameTState / ratio);
    size_t sampleIndex = (floor)((double)frameTState / ratio);
    */

    size_t prevIndex = (_prevFrameTState * SAMPLES_PER_FRAME) / config.frame;
    size_t sampleIndex = (frameTState *  SAMPLES_PER_FRAME) / config.frame;



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

/// endregion </Methods>

/// region <Emulation events>
void SoundManager::handleFrameStart()
{
    _audioBufferWrites = 0;

    // Initialize render buffer with values corresponding to 0-bits from #FE (i.e. INT16_MIN)
    if (false)
    {
        for (size_t i = 0; i < _beeperAudioDescriptor.durationInSamples * AUDIO_CHANNELS; i++)
        {
            _beeperBuffer[i] = INT16_MIN;
        }
    }
    else
    {
        memset(_beeperBuffer, 0x00, _beeperAudioDescriptor.memoryBufferSizeInBytes);
    }
}

void SoundManager::handleFrameEnd()
{
    size_t bufferIndex = 0;

    for (size_t i = 0; i <  AUDIO_SAMPLES_PER_VIDEO_FRAME; i++)
    {
        int16_t leftSample;
        int16_t rightSample;

        if (true)
        {
            // Keep AY running on it's clock
            for (int j = 0; j < PSG_CLOCKS_PER_AUDIO_SAMPLE; j++)
            {
                _ay8910->updateState();
            }

            // Add rendered audio samples
            leftSample = _ay8910->outLeft();
            rightSample = _ay8910->outRight();
        }
        else
        {
            _leftFIR.startOversamplingBlock();
            _rightFIR.startOversamplingBlock();

            // Oversample and apply LPF FIR
            for (int j = FilterInterpolate::DECIMATE_FACTOR - 1; j >= 0; j--)
            {
                _x += _clockStep;

                if (_x >= 1.0)
                {
                    _x -= 1.0;
                    _ay8910->updateState(true);
                }

                _leftFIR.recalculateInterpolationCoefficients(j, _ay8910->mixedLeft());
                _rightFIR.recalculateInterpolationCoefficients(j, _ay8910->mixedRight());
            }

            leftSample = _leftFIR.endOversamplingBlock() * INT16_MAX;
            rightSample = _rightFIR.endOversamplingBlock() * INT16_MAX;

            //leftSample = _ay8910->outLeft();
            //rightSample = _ay8910->outRight();
        }

        // Store samples in output buffer
        _ayBuffer[bufferIndex++] = leftSample;
        _ayBuffer[bufferIndex++] = rightSample;
    }

    /// region <Debug functionality>

    // Replace generated audio stream with pcm file
    if (false)
    {
        if (_pcmFile)
        {
            int read = fread(_ayBuffer, 2, SAMPLES_PER_FRAME * AUDIO_CHANNELS, _pcmFile);
            read = read;
        }
    }

    writeToWaveFile((uint8_t*)_beeperBuffer, _beeperAudioDescriptor.memoryBufferSizeInBytes);
    /// endregion </Debug functionality>

    // Enqueue generated sound data via previously registered application callback
    if (_context->pAudioCallback)
    {
        //_context->pAudioCallback(_context->pAudioManagerObj, _beeperBuffer, SAMPLES_PER_FRAME * AUDIO_CHANNELS);
        _context->pAudioCallback(_context->pAudioManagerObj, _ayBuffer, SAMPLES_PER_FRAME * AUDIO_CHANNELS);
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

bool SoundManager::attachAY8910ToPorts()
{
    bool result = false;

    result = _ay8910->attachToPorts(_context->pPortDecoder);

    return result;
}

bool SoundManager::detachAY8910FromPorts()
{
    bool result = true;

    _ay8910->detachFromPorts();

    return result;
}

/// endregion </Port interconnection>