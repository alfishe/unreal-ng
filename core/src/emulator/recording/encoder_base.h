#pragma once

#include <memory>
#include <string>

/// Forward declarations
struct FramebufferDescriptor;
struct EncoderConfig;

/// @brief Abstract base class for all recording encoders
///
/// Encoders receive video frames and audio samples and encode them to a specific format.
/// Each encoder decides which media types it supports:
/// - Video-only encoders (GIF, PNG sequence) ignore audio
/// - Audio-only encoders (FLAC, WAV) ignore video
/// - Full encoders (H.264+AAC, VP9+Opus) handle both
///
/// Usage:
/// 1. Create encoder instance
/// 2. Call Start() with filename and config
/// 3. Feed frames via OnVideoFrame() / OnAudioSamples()
/// 4. Call Stop() to finalize
class EncoderBase
{
public:
    virtual ~EncoderBase() = default;

    /// region <Lifecycle>

    /// @brief Start encoding to file
    /// @param filename Output filename (encoder determines extension handling)
    /// @param config Encoding configuration
    /// @return true if encoder started successfully
    virtual bool Start(const std::string& filename, const EncoderConfig& config) = 0;

    /// @brief Stop encoding and finalize output file
    virtual void Stop() = 0;

    /// endregion </Lifecycle>

    /// region <Frame Input>

    /// @brief Called for each video frame
    /// @param framebuffer Video frame data
    /// @param timestampSec Presentation timestamp in seconds (emulated time)
    /// @note Default implementation does nothing (for audio-only encoders)
    virtual void OnVideoFrame(const FramebufferDescriptor& framebuffer, double timestampSec)
    {
        (void)framebuffer;
        (void)timestampSec;
    }

    /// @brief Called for each audio buffer
    /// @param samples Interleaved stereo samples (int16_t)
    /// @param sampleCount Total sample count (not per channel)
    /// @param timestampSec Presentation timestamp in seconds (emulated time)
    /// @note Default implementation does nothing (for video-only encoders)
    virtual void OnAudioSamples(const int16_t* samples, size_t sampleCount, double timestampSec)
    {
        (void)samples;
        (void)sampleCount;
        (void)timestampSec;
    }

    /// endregion </Frame Input>

    /// region <State>

    /// @brief Check if encoder is currently recording
    virtual bool IsRecording() const = 0;

    /// @brief Get encoder type identifier
    /// @return Type string (e.g., "gif", "h264", "flac", "wav")
    virtual std::string GetType() const = 0;

    /// @brief Get encoder display name
    /// @return Human-readable name (e.g., "GIF Animation", "H.264 Video")
    virtual std::string GetDisplayName() const = 0;

    /// @brief Check if encoder supports video
    virtual bool SupportsVideo() const = 0;

    /// @brief Check if encoder supports audio
    virtual bool SupportsAudio() const = 0;

    /// endregion </State>

    /// region <Statistics>

    /// @brief Get number of frames encoded
    virtual uint64_t GetFramesEncoded() const
    {
        return 0;
    }

    /// @brief Get number of audio samples encoded
    virtual uint64_t GetAudioSamplesEncoded() const
    {
        return 0;
    }

    /// @brief Get output file size in bytes
    virtual uint64_t GetOutputFileSize() const
    {
        return 0;
    }

    /// endregion </Statistics>
};

/// @brief Unique pointer type for encoder instances
using EncoderPtr = std::unique_ptr<EncoderBase>;
