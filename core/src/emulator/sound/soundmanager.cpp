#include "stdafx.h"
#include "soundmanager.h"

#include "3rdparty/audiofile/AudioFile.h"
#include "emulator/emulatorcontext.h"

/// region <Constructors / Destructors>

SoundManager::SoundManager(EmulatorContext *context)
{
    _context = context;
    _logger = context->pModuleLogger;

    // Reserve buffer capacity for 1 second of stereo PCM samples @44100
    _outBuffer[0].resize(SAMPLING_RATE * sizeof(uint16_t));
    _outBuffer[1].resize(SAMPLING_RATE * sizeof(uint16_t));
}

SoundManager::~SoundManager()
{

}

/// endregion </Constructors / Destructors>

/// region <Methods>

void SoundManager::reset()
{
    // Reset all chips state
    _ay8910.reset();

    // Reset sound rendering state
}

void SoundManager::mute()
{

}

void SoundManager::unmute()
{

}

/// endregion </Methods>

/// region <Wave file export>
bool SoundManager::openWaveFile(std::string& path)
{
    AudioFile<float> audioFile;
    audioFile.setNumChannels(2);
    audioFile.setSampleRate(44100);

    audioFile.samples.resize(2 * 44100);
}

void SoundManager::closeWaveFile()
{

}

/// endregion </Wave file export>

/// region <Port interconnection>

bool SoundManager::attachAY8910ToPorts()
{
    bool result = false;

    _ay8910.attachToPorts(_context->pPortDecoder);

    return result;
}

bool SoundManager::detachAY810FromPorts()
{
    bool result = false;

    _ay8910.detachFromPorts();

    return result;
}

/// endregion </Port interconnection>