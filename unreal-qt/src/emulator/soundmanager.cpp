// miniaudio.h includes <windows.h> on Windows. winsock2.h MUST be included before
// windows.h to prevent type conflicts (see core's stdafx.h for the same pattern).
// mmdeviceapi.h must also come before project headers to avoid UUID collision
// with unreal::UUID (rpcdce.h defines a global UUID typedef).
// INITGUID makes this TU the one that DEFINES PKEY_AudioEngine_DeviceFormat
// (otherwise it is only declared and the link fails).
#ifdef _WIN32
    #define INITGUID
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <mmdeviceapi.h>
    #include <functiondiscoverykeys_devpkey.h>
    #undef INITGUID
#endif

#define MA_LOG_LEVEL 4
#define MA_NO_DECODING
#define MINIAUDIO_IMPLEMENTATION
#include <3rdparty/miniaudio/miniaudio.h>

#include "soundmanager.h"

#ifdef __APPLE__
#include <CoreAudio/CoreAudio.h>
#endif
#include <QDebug>

#include <common/threadhelper.h>
#include <emulator/sound/soundmanager.h>
#include <common/timehelper.h>
#include <cstring>
#include <string>


/// region <Constructors / destructors>
AppSoundManager::AppSoundManager()
{
#ifdef _WIN32
    _formatChangeTimer.setSingleShot(true);
    _formatChangeTimer.setInterval(300);
    connect(&_formatChangeTimer, &QTimer::timeout, this, &AppSoundManager::handleDeviceRerouted);
#endif
}

AppSoundManager::~AppSoundManager()
{
    this->stop();
    this->deinit();
}
/// endregion </Constructors / destructors>

/// region <Methods>


#ifdef __APPLE__
/// Total output latency the OS/driver adds BELOW our buffers: CoreAudio
/// device latency + safety offset (output scope). Virtual loopback devices
/// (Background Music, BlackHole) report their internal buffering here.
static uint32_t queryCoreAudioOutputLatencyFrames(uint32_t deviceID)
{
    if (deviceID == 0)
        return 0;

    uint32_t total = 0;
    UInt32 value = 0;
    UInt32 size = sizeof(value);

    AudioObjectPropertyAddress addr = {kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput,
                                       kAudioObjectPropertyElementMain};
    if (AudioObjectGetPropertyData(deviceID, &addr, 0, nullptr, &size, &value) == noErr)
        total += value;

    addr.mSelector = kAudioDevicePropertySafetyOffset;
    value = 0;
    size = sizeof(value);
    if (AudioObjectGetPropertyData(deviceID, &addr, 0, nullptr, &size, &value) == noErr)
        total += value;

    return total;
}
#endif  // __APPLE__

bool AppSoundManager::init()
{
    bool result = true;

    _shuttingDown.store(false, std::memory_order_release);

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
    // Device reroute detection (default-output change / hotplug): re-init at
    // the new output's native rate instead of letting miniaudio resample
    config.notificationCallback = AppSoundManager::deviceNotificationCallback;

    if (ma_device_init(NULL, &config, &_audioDevice) != MA_SUCCESS)
    {
        result = false;  // Failed to initialize the device.
    }

    // Native rate actually granted by the device (config.sampleRate = 0).
    // Fill the monitoring descriptor: device parameters change only here and
    // on reroute re-init, while the device is stopped.
    const uint32_t grantedRate = _audioDevice.sampleRate ? _audioDevice.sampleRate : AUDIO_SAMPLING_RATE;
    _deviceDescriptor.sampleRate.store(grantedRate, std::memory_order_release);
    _deviceDescriptor.channels.store(_audioDevice.playback.channels, std::memory_order_release);
    _deviceDescriptor.capacityFrames.store(static_cast<uint32_t>(_ringBuffer.capacityStereoFrames()),
                                           std::memory_order_release);
    _deviceDescriptor.deviceBufferFrames.store(
        _audioDevice.playback.internalPeriodSizeInFrames * _audioDevice.playback.internalPeriods,
        std::memory_order_release);
#ifdef __APPLE__
    _deviceDescriptor.deviceOutputLatencyFrames.store(
        queryCoreAudioOutputLatencyFrames(_audioDevice.coreaudio.deviceObjectIDPlayback),
        std::memory_order_release);
#endif
    _deviceDescriptor.setDeviceName(_audioDevice.playback.name);

    // Publish for CoreRate=auto resolution: emulators are created AFTER audio
    // init but BEFORE the frontend binds/publishes the rate to them, so the
    // core-side resolver needs this process-wide default to match the device
    // family (otherwise auto always resolved to 44100)
    if (result)
    {
        SoundManager::PublishDefaultDeviceSampleRate(grantedRate);

#ifdef __APPLE__
        // Watch for nominal-rate changes on THIS device (same-device rate
        // switch fires no miniaudio reroute notification)
        startNominalRateWatch();
#endif
#ifdef _WIN32
        // Watch for format changes on the device (sample rate change in Windows Sound settings)
        startFormatWatch();
#endif
    }

    return result;
}

void AppSoundManager::deinit()
{
    _shuttingDown.store(true, std::memory_order_release);

#ifdef __APPLE__
    stopNominalRateWatch();
#endif
#ifdef _WIN32
    stopFormatWatch();
#endif

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
        qDebug() << "AppSoundManager::start() - Audio device started successfully:"
                 << QString::fromUtf8(_audioDevice.playback.name)
                 << "@" << _audioDevice.sampleRate << "Hz," << _audioDevice.playback.channels << "channels";
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
    thread_local bool s_threadNamed = false;
    if (!s_threadNamed)
    {
        s_threadNamed = true;
        ThreadHelper::setThreadName("miniaudio");

#if defined(__linux__)
        // Linux is the one platform where the audio callback thread does not
        // arrive real-time: miniaudio does not raise its priority itself.
        // Elevate it here (SCHED_FIFO, best effort - a no-op without
        // CAP_SYS_NICE, which is the desktop default).
        //   macOS:   this callback runs on CoreAudio's own device thread, which
        //            already carries time-constraint real-time scheduling -
        //            applying our own policy would only overwrite CoreAudio's
        //            tighter constraints with looser ones
        //   Windows: the WASAPI thread is MMCSS-driven ("Audio" role)
        ThreadHelper::setRealtimePriority();
#endif
    }

    AppSoundManager* obj = (AppSoundManager*)pDevice->pUserData;

    if (!obj)
    {
        // Safety: zero output if callback fires before/after manager is ready
        std::memset(pOutput, 0, frameCount * 2 * sizeof(int16_t));
        return;
    }

    const size_t samplesRequested = frameCount * 2;
    const size_t samplesDequeued = obj->_ringBuffer.dequeue((int16_t*)pOutput, samplesRequested);

    // Zero any unfilled portion to prevent audio glitches on underrun
    if (samplesDequeued < samplesRequested)
    {
        int16_t* outSamples = static_cast<int16_t*>(pOutput);
        std::memset(outSamples + samplesDequeued, 0, (samplesRequested - samplesDequeued) * sizeof(int16_t));
    }

    // Hard resync (consumer thread, SPSC-safe): occupancy far beyond the
    // target is unrecoverable by the DRC trim - discard down to the
    // target in one step and let tracking continue from there
    {
        const uint32_t rate = obj->_deviceDescriptor.sampleRate.load(std::memory_order_relaxed);
        const size_t occFrames = obj->_ringBuffer.getOccupancyStereoFrames();
        if (rate != 0 && occFrames * 1000.0 > SoundManager::HARD_RESYNC_MS * rate)
        {
            const size_t targetFrames = static_cast<size_t>(SoundManager::DRC_TARGET_MS * rate / 1000.0);
            const size_t dropped = obj->_ringBuffer.discard((occFrames - targetFrames) * 2) / 2;
            qWarning("AppSoundManager: hard resync - dropped %zu frames (%.0f ms) of overfilled audio",
                     dropped, dropped * 1000.0 / rate);
        }
    }

    // Publish ring occupancy for the DRC rate controller (audio-sync
    // design, Fix 2): SoundManager::updateDrcControl reads this cell once
    // per emulated frame and trims the resample ratio continuously.
    // Replaces the former NC_AUDIO_BUFFER_HALF_FULL watermark posts
    // (level trigger + async queue latency -> rubber-banding).
    obj->_deviceDescriptor.occupancyFrames.store(
        static_cast<uint32_t>(obj->_ringBuffer.getOccupancyStereoFrames()), std::memory_order_relaxed);
    obj->_deviceDescriptor.framesDequeued.fetch_add(frameCount, std::memory_order_relaxed);
    obj->_deviceDescriptor.dequeueErrors.store(obj->_ringBuffer.getDequeueErrorCount(),
                                               std::memory_order_relaxed);

    (void)pInput; // Not used during playback
}

void AppSoundManager::audioCallback(void* obj, int16_t* samples, size_t numSamples)
{
    AppSoundManager* appSoundManager = (AppSoundManager*)obj;
    if (appSoundManager)
    {
        appSoundManager->_ringBuffer.enqueue(samples, numSamples);
        AudioDeviceDescriptor& desc = appSoundManager->_deviceDescriptor;
        desc.occupancyFrames.store(
            static_cast<uint32_t>(appSoundManager->_ringBuffer.getOccupancyStereoFrames()),
            std::memory_order_relaxed);
        desc.framesEnqueued.fetch_add(numSamples / 2, std::memory_order_relaxed);
        desc.enqueueErrors.store(appSoundManager->_ringBuffer.getEnqueueErrorCount(),
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

/// Miniaudio device notification (arrives on a backend/OS thread). Reroutes
/// happen when the OS default output changes (headphones plugged/unplugged,
/// device removed). Only hop to the GUI thread here - a device cannot be
/// re-initialized from its own callback context.
void AppSoundManager::deviceNotificationCallback(const ma_device_notification* pNotification)
{
    if (!pNotification || !pNotification->pDevice)
        return;

    AppSoundManager* obj = (AppSoundManager*)pNotification->pDevice->pUserData;
    if (!obj || obj->_shuttingDown.load(std::memory_order_acquire))
        return;

    if (pNotification->type == ma_device_notification_type_rerouted)
    {
        QMetaObject::invokeMethod(obj, &AppSoundManager::handleDeviceRerouted, Qt::QueuedConnection);
    }
}

void AppSoundManager::handleDeviceRerouted()
{
    if (_shuttingDown.load(std::memory_order_acquire))
        return;

    const uint32_t oldRate = _deviceDescriptor.sampleRate.load(std::memory_order_acquire);

    qDebug() << "AppSoundManager::handleDeviceRerouted() - Audio device rerouted, re-establishing at native rate";

    // Miniaudio keeps streaming after a reroute but resamples internally to
    // the ORIGINALLY negotiated rate. Re-init at the new output's native rate
    // so the core's DRC resampler stays the only conversion in the chain
    // (audio-sync design Fix 3).
#ifdef __APPLE__
    // The device object (and possibly its ID) is about to be torn down;
    // init() re-arms the watch on the re-established device
    stopNominalRateWatch();
#endif
#ifdef _WIN32
    // The watch must not stay registered while the device is torn down
    // (deadlocks against miniaudio's own notification unregistration);
    // init() re-arms it on the re-established device
    stopFormatWatch();
#endif

    ma_device_uninit(&_audioDevice);

    if (!init())
    {
        qWarning() << "AppSoundManager::handleDeviceRerouted() - Failed to re-initialize audio device after reroute";
        return;
    }

    // Frames queued in the ring were resampled for the previous device rate
    _ringBuffer.clear();
    _deviceDescriptor.reinitCount.fetch_add(1, std::memory_order_relaxed);

    start();  // Logs the re-established device the same way as the initial start

    const uint32_t newRate = _deviceDescriptor.sampleRate.load(std::memory_order_acquire);
    if (newRate != oldRate)
    {
        qDebug() << "AppSoundManager::handleDeviceRerouted() - Device sample rate changed"
                 << oldRate << "->" << newRate << "Hz; republishing for DRC re-base";
        emit deviceReinitialized(newRate);

#ifdef _WIN32
        // The engine may still have been switching (an upshift passes through
        // an intermediate rate) and its final notification is lost while the
        // watch is down. Re-check after the settle interval for as long as the
        // rate keeps changing; a re-open yielding the same rate ends the chain.
        _formatChangeTimer.start();
#endif
    }
}

#ifdef __APPLE__
#include <CoreAudio/CoreAudio.h>

/// CoreAudio property listener: the device's nominal sample rate changed
/// underneath us (same device, new rate - e.g. Audio MIDI Setup or a virtual
/// device like Background Music). Runs on a CoreAudio thread: compare and
/// hop to the GUI thread for the full re-negotiation.
static OSStatus nominalRateListenerProc(AudioObjectID objectID, UInt32 numAddresses,
                                        const AudioObjectPropertyAddress* addresses, void* clientData)
{
    (void)objectID;
    (void)numAddresses;
    (void)addresses;

    AppSoundManager* obj = static_cast<AppSoundManager*>(clientData);
    if (!obj)
        return noErr;

    QMetaObject::invokeMethod(obj, "handleDeviceRerouted", Qt::QueuedConnection);
    return noErr;
}

void AppSoundManager::startNominalRateWatch()
{
    const uint32_t deviceID = _audioDevice.coreaudio.deviceObjectIDPlayback;
    if (deviceID == 0)
        return;

    AudioObjectPropertyAddress addr = {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    if (AudioObjectAddPropertyListener(deviceID, &addr, nominalRateListenerProc, this) == noErr)
    {
        _watchedDeviceObjectID = deviceID;
    }
}

void AppSoundManager::stopNominalRateWatch()
{
    if (_watchedDeviceObjectID == 0)
        return;

    AudioObjectPropertyAddress addr = {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    AudioObjectRemovePropertyListener(_watchedDeviceObjectID, &addr, nominalRateListenerProc, this);
    _watchedDeviceObjectID = 0;
}
#endif  // __APPLE__

#ifdef _WIN32
/// IMMNotificationClient watching PKEY_AudioEngine_DeviceFormat on ONE endpoint
/// (the Windows counterpart of the CoreAudio nominal-rate listener above).
/// Callbacks arrive on a COM thread: nothing is decided or touched there - the
/// event is only forwarded to the GUI thread.
///
/// Lifetime: create() hands the caller one reference. Registration makes
/// Windows hold a second one, released again by shutdown(); the caller must
/// therefore call shutdown() before its final Release().
class AppSoundManager::FormatChangeNotifier final : public IMMNotificationClient
{
public:
    /// Returns nullptr (after logging why) if the watch could not be established.
    static FormatChangeNotifier* create(AppSoundManager* owner, const wchar_t* deviceId)
    {
        IMMDeviceEnumerator* enumerator = nullptr;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
        if (FAILED(hr))
        {
            qWarning() << "AppSoundManager::FormatChangeNotifier - CoCreateInstance(MMDeviceEnumerator) failed, hr="
                       << Qt::hex << hr;
            return nullptr;
        }

        FormatChangeNotifier* notifier = new FormatChangeNotifier(owner, deviceId, enumerator);
        hr = enumerator->RegisterEndpointNotificationCallback(notifier);
        if (FAILED(hr))
        {
            qWarning() << "AppSoundManager::FormatChangeNotifier - RegisterEndpointNotificationCallback failed, hr="
                       << Qt::hex << hr;
            notifier->Release();
            return nullptr;
        }

        return notifier;
    }

    /// Unregisters from Windows. Synchronous: once it returns no callback is
    /// running or will be delivered, so _owner may be dropped safely.
    void shutdown()
    {
        if (!_enumerator)
            return;

        _enumerator->UnregisterEndpointNotificationCallback(this);
        _enumerator->Release();
        _enumerator = nullptr;
        _owner = nullptr;
    }

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return InterlockedIncrement(&_refCount);
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG count = InterlockedDecrement(&_refCount);
        if (count == 0)
            delete this;
        return count;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;

        if (riid == IID_IUnknown || riid == __uuidof(IMMNotificationClient))
        {
            *ppv = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }

        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    // IMMNotificationClient. Device add/remove/default changes are miniaudio's
    // business (it reports them as 'rerouted'); only the format matters here.
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR deviceId, const PROPERTYKEY key) override
    {
        // IsEqualPropertyKey: PROPERTYKEY operator!= is MSVC-only, MinGW lacks it
        if (!IsEqualPropertyKey(key, PKEY_AudioEngine_DeviceFormat) || !deviceId || _deviceId != deviceId)
            return S_OK;

        // The queued call is dropped by Qt if the owner is destroyed first
        if (AppSoundManager* owner = _owner)
            QMetaObject::invokeMethod(owner, &AppSoundManager::onDeviceFormatChanged, Qt::QueuedConnection);

        return S_OK;
    }

private:
    FormatChangeNotifier(AppSoundManager* owner, const wchar_t* deviceId, IMMDeviceEnumerator* enumerator)
        : _owner(owner), _enumerator(enumerator), _deviceId(deviceId)
    {
    }

    ~FormatChangeNotifier()
    {
        // Only reached without shutdown() when registration failed in create()
        if (_enumerator)
            _enumerator->Release();
    }

    AppSoundManager* _owner;
    IMMDeviceEnumerator* _enumerator;
    std::wstring _deviceId;
    LONG _refCount = 1;
};

void AppSoundManager::startFormatWatch()
{
    // miniaudio always records the id of the endpoint it actually opened
    // (also when the default device was requested) - its own rerouting relies on it
    const wchar_t* deviceId = _audioDevice.playback.id.wasapi;
    if (!deviceId[0])
    {
        qWarning() << "AppSoundManager::startFormatWatch() - No WASAPI endpoint id; sample rate changes will not be detected";
        return;
    }

    if (_formatNotifier)
        return;

    _formatNotifier = FormatChangeNotifier::create(this, deviceId);
    if (!_formatNotifier)
    {
        qWarning() << "AppSoundManager::startFormatWatch() - Format watch unavailable; sample rate changes will not be detected";
        return;
    }

    qDebug() << "AppSoundManager::startFormatWatch() - Watching for Windows device format changes";
}

void AppSoundManager::stopFormatWatch()
{
    _formatChangeTimer.stop();

    if (!_formatNotifier)
        return;

    _formatNotifier->shutdown();
    _formatNotifier->Release();
    _formatNotifier = nullptr;
}

void AppSoundManager::onDeviceFormatChanged()
{
    if (_shuttingDown.load(std::memory_order_acquire))
        return;

    // Restarting the single-shot timer coalesces the burst of notifications
    // Windows emits per format switch and lets the engine settle before the
    // device is re-opened; the interval is set in the constructor.
    _formatChangeTimer.start();
}
#endif  // _WIN32
