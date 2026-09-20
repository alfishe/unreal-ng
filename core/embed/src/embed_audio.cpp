#include "embed_audio.h"
#include "emulator/emulator.h"
#include "common/logger.h"

#define MINIAUDIO_IMPLEMENTATION
#include "3rdparty/miniaudio/miniaudio.h"

EmbedAudio::EmbedAudio()
{
    _deviceDescriptor.sampleRate.store(44100, std::memory_order_relaxed);
    _deviceDescriptor.occupancyFrames.store(0, std::memory_order_relaxed);
}

EmbedAudio::~EmbedAudio()
{
    Close();
}

app_result EmbedAudio::OpenDevice(Emulator* emu)
{
    if (!emu)
        return APP_ERR_ARG;

    Close();
    _attachedEmulator = emu;

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = ma_format_s16;
    deviceConfig.playback.channels = 2;
    deviceConfig.sampleRate        = 0; // Native hardware rate
    deviceConfig.dataCallback      = MiniaudioDeviceCallback;
    deviceConfig.pUserData         = this;

    if (ma_device_init(NULL, &deviceConfig, &_audioDevice) != MA_SUCCESS)
    {
        LOGERROR("EmbedAudio::OpenDevice - Failed to initialize miniaudio playback device");
        return APP_ERR_INTERNAL;
    }

    _deviceInitialized = true;
    uint32_t actualRate = _audioDevice.sampleRate;
    _deviceDescriptor.sampleRate.store(actualRate, std::memory_order_release);

    emu->SetAudioCallback(this, AudioProducerCallback, &_deviceDescriptor.occupancyFrames, &_deviceDescriptor);
    emu->SetAudioDeviceSampleRate(actualRate);

    if (ma_device_start(&_audioDevice) != MA_SUCCESS)
    {
        LOGERROR("EmbedAudio::OpenDevice - Failed to start miniaudio device");
        return APP_ERR_INTERNAL;
    }

    return APP_OK;
}

app_result EmbedAudio::AttachPull(Emulator* emu, uint32_t deviceRate)
{
    if (!emu || deviceRate == 0)
        return APP_ERR_ARG;

    Close();
    _attachedEmulator = emu;
    _deviceDescriptor.sampleRate.store(deviceRate, std::memory_order_release);

    emu->SetAudioCallback(this, AudioProducerCallback, &_deviceDescriptor.occupancyFrames, &_deviceDescriptor);
    emu->SetAudioDeviceSampleRate(deviceRate);

    return APP_OK;
}

size_t EmbedAudio::PullF32(float* interleaved, size_t frames)
{
    if (!interleaved || frames == 0)
        return 0;

    size_t samplesNeeded = frames * 2;
    std::vector<int16_t> temp(samplesNeeded);
    size_t dequeued = _ringBuffer.dequeue(temp.data(), samplesNeeded);

    _deviceDescriptor.occupancyFrames.store(static_cast<uint32_t>(_ringBuffer.getOccupancyStereoFrames()), std::memory_order_release);

    for (size_t i = 0; i < dequeued; ++i)
    {
        interleaved[i] = temp[i] / 32768.0f;
    }
    for (size_t i = dequeued; i < samplesNeeded; ++i)
    {
        interleaved[i] = 0.0f;
    }

    return dequeued / 2;
}

void EmbedAudio::SetActive(int active)
{
    const bool wantActive = (active != 0);
    const bool wasActive = _active.exchange(wantActive, std::memory_order_acq_rel);
    if (wantActive == wasActive)
        return;

    if (wantActive)
    {
        // Re-arm the producer so the emulator regains its ring-occupancy
        // cell (the MainLoop pacing/DRC input) at the previous device rate
        if (_attachedEmulator)
        {
            _attachedEmulator->SetAudioCallback(this, AudioProducerCallback,
                                                GetOccupancyFrames(), &GetDeviceDescriptor());
            _attachedEmulator->SetAudioDeviceSampleRate(GetSampleRate());
        }
        return;
    }

    // Deactivating must fully detach the producer: a muted-but-attached ring
    // stays pinned at zero occupancy and the MainLoop emergency refill then
    // reads "starved" on every frame - skipping frame pacing entirely and
    // free-running the emulator (observed as unintended turbo mode)
    if (_attachedEmulator)
    {
        _attachedEmulator->SetAudioCallback(nullptr, nullptr, nullptr, nullptr);
    }
    _ringBuffer.clear();
    _deviceDescriptor.occupancyFrames.store(0, std::memory_order_release);
}

void EmbedAudio::Close()
{
    if (_attachedEmulator)
    {
        _attachedEmulator->SetAudioCallback(nullptr, nullptr, nullptr, nullptr);
        _attachedEmulator = nullptr;
    }

    if (_deviceInitialized)
    {
        ma_device_uninit(&_audioDevice);
        _deviceInitialized = false;
    }
    _ringBuffer.clear();
    _deviceDescriptor.occupancyFrames.store(0, std::memory_order_release);
}

void EmbedAudio::AudioProducerCallback(void* obj, int16_t* samples, size_t numSamples)
{
    auto* self = static_cast<EmbedAudio*>(obj);
    if (!self || !self->_active.load(std::memory_order_acquire))
        return;

    // Apply DC filter (left & right interleaved)
    for (size_t i = 0; i < numSamples; i += 2)
    {
        samples[i]     = static_cast<int16_t>(self->_filterDCLeft.filter(samples[i]));
        samples[i + 1] = static_cast<int16_t>(self->_filterDCRight.filter(samples[i + 1]));
    }

    self->_ringBuffer.enqueue(samples, numSamples);
    self->_deviceDescriptor.occupancyFrames.store(static_cast<uint32_t>(self->_ringBuffer.getOccupancyStereoFrames()), std::memory_order_release);
}

void EmbedAudio::MiniaudioDeviceCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    (void)pInput;
    auto* self = static_cast<EmbedAudio*>(pDevice->pUserData);
    if (!self || !pOutput)
        return;

    auto* dst = static_cast<int16_t*>(pOutput);
    size_t samplesRequested = frameCount * 2;

    if (!self->_active.load(std::memory_order_acquire))
    {
        std::memset(dst, 0, samplesRequested * sizeof(int16_t));
        return;
    }

    const size_t targetFrames = static_cast<size_t>(self->_deviceDescriptor.sampleRate.load(std::memory_order_relaxed) * 0.040f);
    const size_t maxFrames    = static_cast<size_t>(self->_deviceDescriptor.sampleRate.load(std::memory_order_relaxed) * 0.160f);

    size_t currentOccupancy = self->_ringBuffer.getOccupancyStereoFrames();
    if (currentOccupancy > maxFrames)
    {
        size_t excessFrames = currentOccupancy - targetFrames;
        self->_ringBuffer.discard(excessFrames * 2);
    }

    size_t samplesRead = self->_ringBuffer.dequeue(dst, samplesRequested);
    if (samplesRead < samplesRequested)
    {
        std::memset(dst + samplesRead, 0, (samplesRequested - samplesRead) * sizeof(int16_t));
    }

    self->_deviceDescriptor.occupancyFrames.store(static_cast<uint32_t>(self->_ringBuffer.getOccupancyStereoFrames()), std::memory_order_release);
}
