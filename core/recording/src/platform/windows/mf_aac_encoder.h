#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <functional>

#ifdef _WIN32

/// Media Foundation AAC audio encoder
/// Encodes PCM audio to AAC using Windows Media Foundation
class MfAacEncoder
{
public:
    MfAacEncoder();
    ~MfAacEncoder();

    // Non-copyable
    MfAacEncoder(const MfAacEncoder&) = delete;
    MfAacEncoder& operator=(const MfAacEncoder&) = delete;

    /// Initialize encoder
    /// @param sampleRate Audio sample rate (e.g., 44100, 48000)
    /// @param channels Number of channels (1 = mono, 2 = stereo)
    /// @param bitrate Target bitrate in bps (e.g., 128000 for 128kbps)
    bool init(uint32_t sampleRate, uint32_t channels, uint32_t bitrate);

    /// Encode PCM samples
    /// @param samples Interleaved 16-bit PCM samples
    /// @param sampleCount Total sample count (not per channel)
    /// @param pts Presentation timestamp in encoder timescale
    /// @param onOutput Callback for each encoded AAC frame
    /// @return true on success
    bool encode(const int16_t* samples, size_t sampleCount, int64_t pts,
                std::function<void(const uint8_t* data, size_t size, int64_t pts)> onOutput);

    /// Flush encoder and get remaining frames
    bool flush(std::function<void(const uint8_t* data, size_t size, int64_t pts)> onOutput);

    /// Get AudioSpecificConfig for MP4 esds box
    const std::vector<uint8_t>& getAudioSpecificConfig() const { return _audioSpecificConfig; }

    uint32_t getSampleRate() const { return _sampleRate; }
    uint32_t getChannels() const { return _channels; }

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;

    std::vector<uint8_t> _audioSpecificConfig;
    uint32_t _sampleRate = 0;
    uint32_t _channels = 0;
    uint32_t _bitrate = 0;
};

#endif // _WIN32
