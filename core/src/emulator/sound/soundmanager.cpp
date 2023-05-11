#include "stdafx.h"
#include "soundmanager.h"

#include "common/dumphelper.h"
#include "emulator/emulatorcontext.h"

/// region <Constructors / Destructors>

SoundManager::SoundManager(EmulatorContext *context)
{
    _context = context;
    _logger = context->pModuleLogger;

    _beeper = new Beeper(_context, 1500000, AUDIO_SAMPLING_RATE);
    _ay8910 = new SoundChip_AY8910();
    _ym2149 = new SoundChip_YM2149();

    // Reserve buffer capacity for 1 second of stereo PCM samples at selected sampling rate
    constexpr int bufferSize = AUDIO_SAMPLING_RATE * AUDIO_BUFFER_DURATION_MILLISEC / 1000;
    _outBufferLeft.resize(bufferSize);
    _outBufferRight.resize(bufferSize);
}

SoundManager::~SoundManager()
{
    closeWaveFile();

    _outBufferLeft.clear();
    _outBufferRight.clear();

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

    // Reset sound rendering state

    // New wave file
    closeWaveFile();
    std::string filePath = "unreal.wav";
    openWaveFile(filePath);
}

void SoundManager::mute()
{

}

void SoundManager::unmute()
{

}

const AudioFrameDescriptor& SoundManager::getAudioBufferDescriptor()
{
    return _audioFrameDescriptor;
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
        for (size_t i = prevIndex; i < sampleIndex && i < _renderAudioFrameDescriptor.memoryBufferSize / 2; i++)
        {
            _renderAudioBuffer[i * 2] = _prevLeftValue;
            _renderAudioBuffer[i * 2 + 1] = _prevRightValue;
         }
    }

    // Render current samples
    if (sampleIndex != prevIndex)
    {
        _renderAudioBuffer[sampleIndex * 2] = left;
        _renderAudioBuffer[sampleIndex * 2 + 1] = right;
    }

    _audioBufferWrites++;

    // Remember timestamp and channel values
    _prevLeftValue = left;
    _prevRightValue = right;
    _prevFrameTState = frameTState;
}

/// endregion </Methods>

/// region <Emulation events>
void SoundManager::handleFrameStart()
{
    _audioBufferWrites = 0;
    memset(_renderAudioBuffer, 0x00, _renderAudioFrameDescriptor.memoryBufferSize);
}

void SoundManager::handleFrameEnd()
{
    if (_audioBufferWrites > 0)
    {
        std::string dump = DumpHelper::HexDumpBuffer(_renderAudioFrameDescriptor.memoryBuffer, _renderAudioFrameDescriptor.memoryBufferSize);
    }

    if (false)
    {
        const int sampleCount = _renderAudioFrameDescriptor.durationInSamples;
        static int16_t position = -32767;

        for (int i = 0; i < sampleCount; i++)
        {
            _renderAudioBuffer[i * 2] = 0xAAAA;
            _renderAudioBuffer[i * 2 + 1] = 0xBBBB;
        }
    }

    /*
    const int sampleCount = _audioFrameDescriptor.durationInSamples;
    static int16_t position = -32767;

    for (int i = 0; i < sampleCount; i++)
    {
        int16_t value = position;
        _renderAudioBuffer[i * 2] = value;
        _renderAudioBuffer[i * 2 + 1] = value;

        position += 65535 / 100;
    }*/

    // Copy render buffer to external one
    memcpy(_audioBuffer, _renderAudioBuffer, _audioFrameDescriptor.memoryBufferSize);
    writeToWaveFile((uint8_t*)_audioBuffer, _renderAudioFrameDescriptor.memoryBufferSize);
}
/// endregion </Emulation events>

/// region <Wave file export>
bool SoundManager::openWaveFile(std::string& path)
{
    bool result = false;

    tinywav_open_write(
        &_tinyWav,
        AUDIO_CHANNELS,
        AUDIO_SAMPLING_RATE,
        TW_INT16,
        TW_INTERLEAVED,
        path.c_str());

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

    _ay8910->attachToPorts(_context->pPortDecoder);

    return result;
}

bool SoundManager::detachAY8910FromPorts()
{
    bool result = false;

    _ay8910->detachFromPorts();

    return result;
}

/// endregion </Port interconnection>