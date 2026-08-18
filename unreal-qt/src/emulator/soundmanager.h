#pragma once

#include <QObject>
#include <atomic>

// miniaudio.h includes <windows.h> on Windows. winsock2.h MUST be included before
// windows.h to prevent type conflicts (see core's stdafx.h for the same pattern).
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#endif

#include <3rdparty/tinywav/tinywav.h>
#include <3rdparty/miniaudio/miniaudio.h>
#include <common/stringhelper.h>
#include <common/sound/audioringbuffer.h>
#include <common/sound/filters/filter_dc.h>
#include <emulator/sound/soundmanager.h>
#include <emulator/emulatorcontext.h>

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
    // controller (registered via Emulator::SetAudioCallback). Owned here so
    // the cell outlives any emulator instance.
    std::atomic<uint32_t> _occupancyFrames{0};
    uint32_t _deviceSampleRate = AUDIO_SAMPLING_RATE;  // Read back from ma_device after init

    // Device reroute handling (OS default-output change / hotplug)
    std::atomic<bool> _shuttingDown{false};

    // Ring error observability (emulator thread, audioCallback only)
    uint32_t _errorLogCounter = 0;
    size_t _lastEnqueueErrors = 0;
    size_t _lastDequeueErrors = 0;


    // Save to Wave file
    TinyWav _tinyWav;

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
    /// Actual device sample rate after init (native rate; audio-sync Fix 3)
    uint32_t deviceSampleRate() const { return _deviceSampleRate; }
    void deinit();

    void start();
    void stop();

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
