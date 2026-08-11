#pragma once

#include <cstdint>
#include <cstddef>

/// DSD sample rates
enum class DSDRate
{
    DSD64  = 64,    // 2.8224 MHz (64 × 44.1 kHz)
    DSD128 = 128,   // 5.6448 MHz
    DSD256 = 256,   // 11.2896 MHz
    DSD512 = 512    // 22.5792 MHz
};

/// Get actual sample rate in Hz for a DSD rate
inline uint32_t GetDSDSampleRate(DSDRate rate)
{
    return static_cast<uint32_t>(rate) * 44100;
}

/// DSD container formats
enum class DSDFormat
{
    DSF,    // Sony format (.dsf) - most common, good metadata support
    DFF     // Philips DSDIFF (.dff) - simpler, widely supported
};

/// Delta-sigma modulator order (noise shaping)
enum class ModulatorOrder
{
    First  = 1,    // Simple, lowest latency
    Second = 2,    // Better noise shaping
    Third  = 3,    // Excellent noise floor, standard for DSD
    Fifth  = 5     // Professional grade, pushes noise above 100kHz
};

/// Channel layout for DSD output
struct DSDChannelConfig
{
    uint8_t channelCount = 2;   // Stereo default

    // Channel IDs (DSDIFF standard)
    static constexpr uint32_t FRONT_LEFT   = 0x00000001;
    static constexpr uint32_t FRONT_RIGHT  = 0x00000002;
    static constexpr uint32_t CENTER       = 0x00000004;
    static constexpr uint32_t LFE          = 0x00000008;
    static constexpr uint32_t BACK_LEFT    = 0x00000010;
    static constexpr uint32_t BACK_RIGHT   = 0x00000020;

    uint32_t channelMask = FRONT_LEFT | FRONT_RIGHT;  // Stereo
};

/// Statistics from DSD encoding
struct DSDStats
{
    uint64_t samplesEncoded = 0;     // DSD bits written
    uint64_t pcmSamplesIn = 0;       // PCM samples processed
    double peakLevel = 0.0;          // Peak input level (0.0-1.0)
    double modulatorLoad = 0.0;      // Modulator utilization (overload indicator)
};
