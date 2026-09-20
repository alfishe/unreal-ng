#pragma once

#include "unrealng_embed.h"
#include <atomic>
#include <cstdint>
#include <cstddef>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#endif

#include "3rdparty/miniaudio/miniaudio.h"
#include "common/sound/audioringbuffer.h"
#include "common/sound/filters/filter_dc.h"
#include "emulator/sound/soundmanager.h"
#include "common/sound/audiodevicedescriptor.h"

class Emulator;

class EmbedAudio
{
private:
    FilterDC<int16_t> _filterDCLeft;
    FilterDC<int16_t> _filterDCRight;

    ma_device _audioDevice{};
    bool _deviceInitialized = false;

    AudioRingBuffer<int16_t, AUDIO_BUFFER_SAMPLES_PER_FRAME * 8> _ringBuffer;
    AudioDeviceDescriptor _deviceDescriptor;

    std::atomic<bool> _active{true};
    Emulator* _attachedEmulator = nullptr;

public:
    EmbedAudio();
    ~EmbedAudio();

    app_result OpenDevice(Emulator* emu);
    app_result AttachPull(Emulator* emu, uint32_t deviceRate);
    size_t PullF32(float* interleaved, size_t frames);
    void SetActive(int active);
    void Close();

private:
    static void AudioProducerCallback(void* obj, int16_t* samples, size_t numSamples);
    static void MiniaudioDeviceCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
};
