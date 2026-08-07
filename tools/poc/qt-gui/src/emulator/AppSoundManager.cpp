// miniaudio.h includes <windows.h> on Windows. winsock2.h MUST be included before
// windows.h to prevent type conflicts.
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#endif

#ifdef HAS_EMULATOR_CORE
#define MA_LOG_LEVEL 4
#define MA_NO_DECODING
#define MINIAUDIO_IMPLEMENTATION
#include <3rdparty/miniaudio/miniaudio.h>
#endif

#include "AppSoundManager.h"
#include <QDebug>

#ifdef HAS_EMULATOR_CORE
#include <3rdparty/message-center/messagecenter.h>
#include <emulator/notifications.h>
#endif

AppSoundManager::~AppSoundManager()
{
    stop();
    deinit();
}

bool AppSoundManager::init()
{
#ifdef HAS_EMULATOR_CORE
    if (_initialized)
        return true;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_s16;
    config.playback.channels = AUDIO_CHANNELS;
    config.sampleRate        = AUDIO_SAMPLING_RATE;
    config.dataCallback      = AppSoundManager::audioDataCallback;
    config.pUserData         = this;

    if (ma_device_init(NULL, &config, &_audioDevice) != MA_SUCCESS)
    {
        qWarning() << "AppSoundManager: Failed to initialize audio device";
        return false;
    }

    _initialized = true;
    qDebug() << "AppSoundManager: Audio device initialized";
    return true;
#else
    return false;
#endif
}

void AppSoundManager::deinit()
{
#ifdef HAS_EMULATOR_CORE
    if (!_initialized)
        return;

    stop();
    ma_device_uninit(&_audioDevice);
    _initialized = false;
    qDebug() << "AppSoundManager: Audio device deinitialized";
#endif
}

void AppSoundManager::start()
{
#ifdef HAS_EMULATOR_CORE
    ma_result result = ma_device_start(&_audioDevice);

    if (result != MA_SUCCESS)
    {
        qWarning() << "AppSoundManager: Failed to start audio device. Error:" << result;
    }
    else
    {
        qDebug() << "AppSoundManager: Audio started";
    }
#endif
}

void AppSoundManager::stop()
{
#ifdef HAS_EMULATOR_CORE
    ma_device_stop(&_audioDevice);
    _ringBuffer.clear();
    qDebug() << "AppSoundManager: Audio stopped";
#endif
}

#ifdef HAS_EMULATOR_CORE

void AppSoundManager::audioDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    (void)pInput;

    AppSoundManager* obj = static_cast<AppSoundManager*>(pDevice->pUserData);
    if (obj)
    {
        obj->_ringBuffer.dequeue(static_cast<int16_t*>(pOutput), frameCount * 2);

        if (!obj->_ringBuffer.isHalfFull())
        {
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            messageCenter.Post(NC_AUDIO_BUFFER_HALF_FULL);
        }
    }
}

void AppSoundManager::audioCallback(void* obj, int16_t* samples, size_t numSamples)
{
    AppSoundManager* appSoundManager = static_cast<AppSoundManager*>(obj);
    if (appSoundManager)
    {
        appSoundManager->_ringBuffer.enqueue(samples, numSamples);
    }
}

#endif
