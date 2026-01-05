#define MA_LOG_LEVEL 4
#define MA_NO_DECODING
#define MINIAUDIO_IMPLEMENTATION
#include <3rdparty/miniaudio/miniaudio.h>

#include "soundmanager.h"
#include <QDebug>

#include <emulator/sound/soundmanager.h>
#include <common/timehelper.h>


/// region <Constructors / destructors>
AppSoundManager::~AppSoundManager()
{
    this->stop();
    this->deinit();
}
/// endregion </Constructors / destructors>

/// region <Methods>

bool AppSoundManager::init()
{
    bool result = true;

    // Set audio output parameters
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_s16;       // Set to ma_format_unknown to use the device's native format.
    config.playback.channels = AUDIO_CHANNELS;      // Set to 0 to use the device's native channel count.
    config.sampleRate        = AUDIO_SAMPLING_RATE; // Set to 0 to use the device's native sample rate.
    config.dataCallback      = AppSoundManager::audioDataCallback; // This function will be called when miniaudio needs more data.
    config.pUserData         = (void*)this;         // Can be accessed from the device object (device.pUserData).


    if (ma_device_init(NULL, &config, &_audioDevice) != MA_SUCCESS)
    {
        result = false;  // Failed to initialize the device.
    }

    return result;
}

void AppSoundManager::deinit()
{
    this->stop();

    ma_device_uninit(&_audioDevice);
}

void AppSoundManager::start()
{
    ma_result result = ma_device_start(&_audioDevice);     // The device is sleeping by default, so you'll need to start it manually.

    if (result != MA_SUCCESS)
    {
        qDebug() << "AppSoundManager::start() - Failed to start audio device. Error code:" << result;
    }
    else
    {
        qDebug() << "AppSoundManager::start() - Audio device started successfully";
    }

    // New wave file
    /*
    if (_tinyWav.file)
        tinywav_close_write(&_tinyWav);
    std::string filePath = "unreal-qt.wav";
    int res = tinywav_open_write(
            &_tinyWav,
            AUDIO_CHANNELS,
            AUDIO_SAMPLING_RATE,
            TW_INT16,
            TW_INTERLEAVED,
            filePath.c_str());
    */
}

void AppSoundManager::stop()
{
    ma_device_stop(&_audioDevice);

    // Wipe ring buffer to prevent crackles during next emulation session
    _ringBuffer.clear();
}

/// Audio playback callback
/// @details Will be called by miniaudio library when more sample data required
/// @param pDevice
/// @param pOutput
/// @param pInput
/// @param frameCount The `frameCount` parameter tells you
///                   how many frames can be written to the output buffer and read from the input buffer. A "frame" is
///                   one sample for each channel. For example, in a stereo stream (2 channels), one frame is 2
///                   samples: one for the left, one for the right. The channel count is defined by the device config.
void AppSoundManager::audioDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    // In playback mode copy data to pOutput. In capture mode read data from pInput. In full-duplex mode, both
    // pOutput and pInput will be valid and you can move data from pInput into pOutput. Never process more than
    // frameCount frames
    AppSoundManager* obj = (AppSoundManager*)pDevice->pUserData;

    if (obj)
    {
        obj->_ringBuffer.dequeue((int16_t*)pOutput, frameCount * 2);

        if (!obj->_ringBuffer.isHalfFull())
        {
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            messageCenter.Post(NC_AUDIO_BUFFER_HALF_FULL);
        }
    }

    (void)pInput; // Not used during playback
}

void AppSoundManager::audioCallback(void* obj, int16_t* samples, size_t numSamples)
{
    static int callCount = 0;
    if (++callCount == 1)
    {
        qDebug() << "AppSoundManager::audioCallback() - First call received with" << numSamples << "samples";
    }

    AppSoundManager* appSoundManager = (AppSoundManager*)obj;
    if (appSoundManager)
    {
        appSoundManager->_ringBuffer.enqueue(samples, numSamples);
    }
}