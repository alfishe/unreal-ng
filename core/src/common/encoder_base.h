#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

/// Forward declarations
struct FramebufferDescriptor;
struct EncoderConfig;

/// @brief A buffer an encoder lends to the producer of a frame, so the finished picture is written INTO the encoder's
/// own memory (the pixel buffer a hardware encoder reads, the queue slot an ffmpeg pipe writes) and never copied
/// again. 4 bytes per pixel, rows `stride` bytes apart.
struct FrameTarget
{
    uint8_t* data = nullptr;
    size_t stride = 0;        ///< Bytes between row starts (>= width * 4)
    uint32_t width = 0;
    uint32_t height = 0;
    bool swapRedBlue = false; ///< The encoder wants B,G,R,A byte order (the emulator frame is R,G,B,A)
    void* handle = nullptr;   ///< The encoder's own bookkeeping; the producer hands it back untouched
};

/// Outcome of EncoderBase::AcquireFrameTarget
enum class FrameTargetResult
{
    Ready,        ///< `target` is filled: write the picture, then SubmitFrameTarget (or ReleaseFrameTarget)
    Dropped,      ///< The encoder takes no frame now (writer stuck, queue full in real-time mode): skip this frame
    Unsupported   ///< No lending (the default): hand the frame over with OnVideoFrame
};

/// @brief Abstract base class for all recording encoders
///
/// Lives in core, not in the optional recording library: core's tape-audio
/// renderer writes through it too (native WAV always, ffmpeg FLAC when the
/// recording library is built), so the contract must exist in every build.
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

    /// @brief Zero-copy input: lend the producer a buffer of exactly width x height pixels to write the frame into.
    /// The producer calls SubmitFrameTarget (with the frame's timestamp) or ReleaseFrameTarget, on the same thread,
    /// before the next frame. Only an encoder whose memory the producer can write directly overrides this
    virtual FrameTargetResult AcquireFrameTarget(uint32_t width, uint32_t height, FrameTarget& target)
    {
        (void)width;
        (void)height;
        (void)target;
        return FrameTargetResult::Unsupported;
    }

    /// @brief The lent buffer holds the finished frame: encode it (takes it back)
    virtual void SubmitFrameTarget(FrameTarget& target, double timestampSec)
    {
        (void)target;
        (void)timestampSec;
    }

    /// @brief Take a lent buffer back without encoding it
    virtual void ReleaseFrameTarget(FrameTarget& target) { (void)target; }

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

    /// @brief Get last error message (empty string if no error)
    virtual std::string GetLastError() const
    {
        return {};
    }

    /// endregion </Statistics>
};

/// @brief Unique pointer type for encoder instances
using EncoderPtr = std::unique_ptr<EncoderBase>;
