#pragma once

// miniaudio.h includes <windows.h> on Windows. winsock2.h MUST be included before
// windows.h to prevent type conflicts (see core's stdafx.h for the same pattern).
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#endif

#include <QObject>

#include <3rdparty/tinywav/tinywav.h>
#include <3rdparty/miniaudio/miniaudio.h>
#include <common/stringhelper.h>
#include <common/sound/audioringbuffer.h>
#include <common/sound/filters/filter_dc.h>
#include <emulator/sound/soundmanager.h>
#include <emulator/emulatorcontext.h>

#include <atomic>
#include <emulator/mainloop.h>

class AppSoundManager : public QObject
{
    Q_OBJECT

    /// region <Fields>
protected:
    FilterDC<int16_t> _filterDCLeft;
    FilterDC<int16_t> _filterDCRight;

    ma_device _audioDevice;
    AudioRingBuffer<int16_t, AUDIO_BUFFER_SAMPLES_PER_FRAME * 8> _ringBuffer;

    // Ring occupancy in stereo frames, published for the emulator-side DRC
    // controller (registered via Emulator::SetAudioCallback)
    std::atomic<uint32_t> _occupancyFrames{0};
    uint32_t _deviceSampleRate = AUDIO_SAMPLING_RATE;

    // Save to Wave file
    TinyWav _tinyWav;

    // Device reroute handling (OS default-output change / hotplug)
    std::atomic<bool> _shuttingDown{false};

    std::atomic<EmulatorContext*> _activeContext{nullptr};

    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    AppSoundManager() = default;
    virtual ~AppSoundManager();
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    bool init();
    const std::atomic<uint32_t>* occupancyCell() const { return &_occupancyFrames; }
    uint32_t deviceSampleRate() const { return _deviceSampleRate; }
    void deinit();

    void start();
    void stop();

    void setActiveContext(EmulatorContext* context) { _activeContext.store(context, std::memory_order_release); }
    EmulatorContext* getActiveContext() const { return _activeContext.load(std::memory_order_acquire); }

signals:
    /// Emitted after the audio device was re-established at a DIFFERENT native
    /// sample rate (device hotplug / OS default-output change). Consumers must
    /// republish the rate to the emulator (SetAudioDeviceSampleRate) so the
    /// DRC resampler re-bases its core->device ratio.
    void deviceReinitialized(uint32_t sampleRate);

protected slots:
    /// GUI-thread half of reroute handling: re-init the device at the new
    /// output's native rate. NEVER run on the notification thread - a device
    /// cannot be uninitialized from its own callback context.
    void handleDeviceRerouted();

public:
    static void audioDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
    static void audioCallback(void* obj, int16_t* samples, size_t numSamples);
    static void deviceNotificationCallback(const ma_device_notification* pNotification);
};
