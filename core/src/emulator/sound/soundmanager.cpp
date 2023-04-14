#include "stdafx.h"
#include "soundmanager.h"

#include "common/sound/audiofilehelper.h"
#include "emulator/emulatorcontext.h"

/// region <Constructors / Destructors>

SoundManager::SoundManager(EmulatorContext *context)
{
    _context = context;
    _logger = context->pModuleLogger;

    _beeper = new Beeper(1500000, AUDIO_SAMPLING_RATE);
    _ay8910 = new SoundChip_AY8910();
    _ym2149 = new SoundChip_YM2149();

    // Reserve buffer capacity for 1 second of stereo PCM samples at selected sampling rate
    constexpr int bufferSize = AUDIO_SAMPLING_RATE * AUDIO_BUFFER_DURATION_MILLISEC / 1000;
    _outBufferLeft.resize(bufferSize);
    _outBufferRight.resize(bufferSize);
}

SoundManager::~SoundManager()
{
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
}

void SoundManager::mute()
{

}

void SoundManager::unmute()
{

}

void SoundManager::initFrame()
{
    //_beeper->frameStart();
}

/// endregion </Methods>

/// region <Wave file export>
bool SoundManager::openWaveFile(std::string& path)
{
    bool result = false;

    //AudioFile<float> audioFile;
    //audioFile.setNumChannels(2);
    //audioFile.setSampleRate(44100);

    //audioFile.samples.resize(2 * 44100);

    return result;
}

void SoundManager::closeWaveFile()
{

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