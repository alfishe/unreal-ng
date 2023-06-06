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

    _beeper = new Beeper(_context, 1500000, AUDIO_SAMPLING_RATE);
    _ay8910 = new SoundChip_AY8910();
    _ym2149 = new SoundChip_YM2149();

    // Reserve buffer capacity for 1 second of stereo PCM samples at selected sampling rate
    constexpr int bufferSize = AUDIO_SAMPLING_RATE * AUDIO_BUFFER_DURATION_MILLISEC / 1000;
    _outBufferLeft.resize(bufferSize);
    _outBufferRight.resize(bufferSize);

    /// region <Debug functionality>
    _logFile = fopen("unreal-tape-log.txt", "w"); // open the file in write mode
    _pcmFile = fopen("/Users/dev/Downloads/int16.pcm", "rb");
    /// endregion </Debug functionality>
}

SoundManager::~SoundManager()
{
    /// region <Debug functionality
    fclose(_pcmFile);
    fclose(_logFile);
    saveContinuousWaveFile(_continuousAudioBuffer);
    /// endregion /Debug functionality>

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
    _prevFrane = 0;
    _prevFrameTState = 0;
    _prevLeftValue = 0;
    _prevRightValue = 0;

    /// region <Debug functionality
    _continuousAudioBuffer = {};
    /// endregion /Debug functionality>

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

    /// region <Debug functionality
    {
        Z80& cpu = *_context->pCore->GetZ80();
        size_t clockCount = cpu.clock_count;

        static constexpr double TSTATES_TO_AUDIO_INDEX = 44100.0 / 3'500'000.0;

        size_t curTime = (config.frame * _context->emulatorState.frame_counter) + frameTState;
        size_t prevTime = (config.frame * _prevFrane) + _prevFrameTState;
        size_t deltaTime = curTime - prevTime;

        size_t curIndex = curTime * TSTATES_TO_AUDIO_INDEX;
        size_t prevIndex = prevTime * TSTATES_TO_AUDIO_INDEX;
        size_t samplesToFill = curIndex - prevIndex;


        std::string logLine = StringHelper::Format("[%d] [%0d] L: %04X R: %04X\n", clockCount, curIndex, (uint16_t)left, (uint16_t)right);
        fwrite(logLine.c_str(), 1, logLine.size(), _logFile);

        /*
        // Resize audio buffer if needed
        if (_continuousAudioBuffer.size() <= curIndex * 2)
        {
            _continuousAudioBuffer.resize(curIndex * 4);
        }

        // Detect rising edge
        if (left > _prevLeftValue)
        {
            curIndex = curIndex;
        }

        // Fill all samples between updateDAC calls
        for (size_t i = prevIndex; i < curIndex; i++)
        {
            _continuousAudioBuffer[(i << 1)] = _prevLeftValue;
            _continuousAudioBuffer[(i << 1) + 1] = _prevRightValue;
        }

        _continuousAudioBuffer[curIndex << 1] = left;
        _continuousAudioBuffer[(curIndex << 1) + 1] = right;
         */
    }
    /// endregion /Debug functionality>

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
    _prevFrane = _context->emulatorState.frame_counter;
}

/// endregion </Methods>

/// region <Debug functionality>
void SoundManager::saveContinuousWaveFile(const std::vector<int16_t>& audioBuffer)
{
    const std::string filePath = "unreal-continuous.wav";
    TinyWav tw;

    //AudioHelper::filterDCRejectionStereoInterleaved((int16_t*)audioBuffer.data(), audioBuffer.size());

    tinywav_open_write(
            &tw,
            AUDIO_CHANNELS,
            AUDIO_SAMPLING_RATE,
            TW_INT16,
            TW_INTERLEAVED,
            filePath.c_str());

    // Save using method with Int16 samples input
    size_t lengthInSamples = audioBuffer.size() / 2;
    tinywav_write_i(&tw, (void*)audioBuffer.data(), lengthInSamples);

    tinywav_close_write(&tw);
}
/// endregion </Debug functionality>

/// region <Emulation events>
void SoundManager::handleFrameStart()
{
    _audioBufferWrites = 0;

    // Initialize render buffer with values corresponding to 0-bits from #FE (i.e. INT16_MIN)
    if (false)
    {
        for (size_t i = 0; i < _renderAudioFrameDescriptor.durationInSamples * AUDIO_CHANNELS; i++)
        {
            _renderAudioBuffer[i] = INT16_MIN;
        }
    }
    else
    {
        memset(_renderAudioBuffer, 0x00, _renderAudioFrameDescriptor.memoryBufferSize);
    }
}

void SoundManager::handleFrameEnd()
{
    if (false)
    {
        if (_audioBufferWrites > 0)
        {
            std::string dump = DumpHelper::HexDumpBuffer(_renderAudioFrameDescriptor.memoryBuffer, _renderAudioFrameDescriptor.memoryBufferSize);
        }

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
    AudioHelper::filterDCRejectionStereoInterleaved(_audioBuffer, SAMPLES_PER_FRAME * AUDIO_CHANNELS);

    /// region <Debug functionality>

    // Replace generated audio stream with pcm file
    if (true)
    {
        int read = fread(_audioBuffer, 2, SAMPLES_PER_FRAME * AUDIO_CHANNELS, _pcmFile);
        read = read;
    }

    writeToWaveFile((uint8_t*)_audioBuffer, _renderAudioFrameDescriptor.memoryBufferSize);
    /// endregion </Debug functionality>

    // Enqueue generated sound data via previously registered application callback
    _context->pAudioCallback(_context->pAudioManagerObj, _audioBuffer, SAMPLES_PER_FRAME * AUDIO_CHANNELS);
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

    //_ay8910->attachToPorts(_context->pPortDecoder);

    return result;
}

bool SoundManager::detachAY8910FromPorts()
{
    bool result = false;

    //_ay8910->detachFromPorts();

    return result;
}

/// endregion </Port interconnection>