#pragma once

#include <stdafx.h>

static constexpr const int FRAMES_PER_SECOND = 50;
static constexpr const size_t AUDIO_SAMPLING_RATE = 44100;
static constexpr const size_t AUDIO_CHANNELS = 2;
static constexpr const size_t CPU_CLOCK_RATE = 3.5 * 1'000'000;
static constexpr const size_t PSG_CLOCK_RATE = CPU_CLOCK_RATE / 2;
static constexpr const size_t PSG_CLOCKS_PER_AUDIO_SAMPLE = PSG_CLOCK_RATE / AUDIO_SAMPLING_RATE;
static constexpr const size_t AUDIO_SAMPLES_PER_VIDEO_FRAME = AUDIO_SAMPLING_RATE / FRAMES_PER_SECOND;
static constexpr const double TSTATES_PER_AUDIO_SAMPLE = (double)CPU_CLOCK_RATE / (double)AUDIO_SAMPLING_RATE;
static constexpr const double AUDIO_SAMPLE_TSTATE_INCREMENT = (double)AUDIO_SAMPLING_RATE / (double)CPU_CLOCK_RATE;

/// Real-time duration of one emulated frame in microseconds, derived from the
/// machine's t-states per frame at the base Z80 clock (Pentagon: 71680 t-states
/// = 20480 us = 48.83 FPS; ZX48/128/Scorpion: 69888 t-states = 19968 us).
/// Rounded UP: pacing emulation even 1 us faster than the audio produced per
/// frame makes the playback ring buffer fill up, delaying audio behind video.
/// Falls back to a 50 Hz frame when t-states per frame is not configured yet.
static constexpr uint32_t CalculateFrameDurationUs(uint32_t frameTStates)
{
    return frameTStates == 0
        ? 1'000'000u / FRAMES_PER_SECOND
        : static_cast<uint32_t>((static_cast<uint64_t>(frameTStates) * 1'000'000ULL + CPU_CLOCK_RATE - 1) / CPU_CLOCK_RATE);
}

static constexpr const int AUDIO_BUFFER_DURATION_MILLISEC = 1000 / FRAMES_PER_SECOND;
static constexpr const int SAMPLES_PER_FRAME = AUDIO_SAMPLING_RATE / FRAMES_PER_SECOND;   // 882 audio samples per frame @44100
static constexpr const int MAX_SAMPLES_PER_FRAME = 2048;
static constexpr const int AUDIO_BUFFER_SAMPLES_PER_FRAME = MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS;
static constexpr const int AUDIO_BUFFER_SIZE_PER_FRAME = MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS * sizeof(int16_t);

/// Holds memory buffer capable to store 20ms of stereo PCM samples at selected sampling rate
/// The rest is just meta-information about that buffer
struct AudioFrameDescriptor
{
    static constexpr const uint32_t samplingRate = AUDIO_SAMPLING_RATE;
    static constexpr const uint8_t channels = AUDIO_CHANNELS;
    static constexpr const size_t durationInMs = AUDIO_BUFFER_DURATION_MILLISEC;
    static constexpr const size_t durationInSamples = SAMPLES_PER_FRAME;
    static constexpr const size_t memoryBufferSizeInBytes = AUDIO_BUFFER_SIZE_PER_FRAME;

    alignas(alignof(int16_t))
    uint8_t memoryBuffer[memoryBufferSizeInBytes] = {};
};