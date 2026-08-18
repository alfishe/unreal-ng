// miniaudio.h includes <windows.h> on Windows. winsock2.h MUST be included before
// windows.h to prevent type conflicts (see core's stdafx.h for the same pattern).
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#endif

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

    // Set audio output parameters for low latency
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_s16;       // Set to ma_format_unknown to use the device's native format.
    config.playback.channels = AUDIO_CHANNELS;      // Set to 0 to use the device's native channel count.
    // Use the device's NATIVE rate (audio-sync design Fix 3): eliminates the
    // OS mixer's hidden resampler (PipeWire/CoreAudio) - the core's DRC
    // resampler does the core->device conversion under our quality control.
    config.sampleRate        = 0;
    config.performanceProfile = ma_performance_profile_low_latency;
    config.periodSizeInFrames = 256;                 // ~5.8ms period @ 44.1kHz
    config.periods            = 2;                   // 2 periods = 512 frames (~11.6ms hardware buffer @ 44.1kHz)
    config.dataCallback      = AppSoundManager::audioDataCallback; // This function will be called when miniaudio needs more data.
    config.pUserData         = (void*)this;         // Can be accessed from the device object (device.pUserData).

    if (ma_device_init(NULL, &config, &_audioDevice) != MA_SUCCESS)
    {
        result = false;  // Failed to initialize the device.
    }

    // Native rate actually granted by the device (config.sampleRate = 0)
    _deviceSampleRate = _audioDevice.sampleRate ? _audioDevice.sampleRate : AUDIO_SAMPLING_RATE;

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
    AppSoundManager* obj = (AppSoundManager*)pDevice->pUserData;

    if (obj)
    {
        obj->_ringBuffer.dequeue((int16_t*)pOutput, frameCount * 2);

        // Publish ring occupancy for the DRC rate controller (audio-sync
        // design, Fix 2): SoundManager::updateDrcControl reads this cell once
        // per emulated frame and trims the resample ratio continuously.
        // Replaces the former NC_AUDIO_BUFFER_HALF_FULL watermark posts
        // (level trigger + async queue latency -> rubber-banding).
        obj->_occupancyFrames.store(static_cast<uint32_t>(obj->_ringBuffer.getOccupancyStereoFrames()),
                                    std::memory_order_relaxed);
    }

    (void)pInput; // Not used during playback
}

void AppSoundManager::audioCallback(void* obj, int16_t* samples, size_t numSamples)
{
    AppSoundManager* appSoundManager = (AppSoundManager*)obj;
    if (appSoundManager)
    {
        appSoundManager->_ringBuffer.enqueue(samples, numSamples);
        appSoundManager->_occupancyFrames.store(
            static_cast<uint32_t>(appSoundManager->_ringBuffer.getOccupancyStereoFrames()),
            std::memory_order_relaxed);

        // Observability (audio-sync design 5.4): under correct rate control
        // both error counters stay at zero permanently. Log transitions
        // rate-limited (once per ~256 frames = ~5s) so a non-converging
        // controller or a stalled device is visible instead of silent.
        if ((appSoundManager->_errorLogCounter++ % 256) == 0)
        {
            size_t enq = appSoundManager->_ringBuffer.getEnqueueErrorCount();
            size_t deq = appSoundManager->_ringBuffer.getDequeueErrorCount();
            if (enq != appSoundManager->_lastEnqueueErrors || deq != appSoundManager->_lastDequeueErrors)
            {
                qWarning("AppSoundManager: ring errors enqueue=%zu dequeue=%zu (occupancy %zu frames)", enq,
                         deq, appSoundManager->_ringBuffer.getOccupancyStereoFrames());
                appSoundManager->_lastEnqueueErrors = enq;
                appSoundManager->_lastDequeueErrors = deq;
            }
        }
    }
}