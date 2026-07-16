#pragma once

#ifdef __APPLE__

#include <cstdint>
#include <string>

#include "../encoder_base.h"
#include "../encoder_config.h"

/// @brief macOS VideoToolbox Encoder — Native hardware-accelerated encoding
///
/// Uses AVFoundation for zero-dependency video encoding on macOS:
/// - AVAssetWriter compresses H.264/HEVC internally via VideoToolbox hardware
///   (video input carries outputSettings, so the writer owns compression —
///   raw BGRA pixel buffers go in through AVAssetWriterInputPixelBufferAdaptor)
/// - PCM s16le -> AAC conversion handled by the writer's audio input
///
/// Key advantages over FFmpegPipeEncoder:
/// - Zero external dependencies (no ffmpeg binary needed)
/// - Hardware GPU encode via VideoToolbox
/// - Native MP4/MOV muxing
/// - Always realtime for ZX-sized frames
class VideoToolboxEncoder : public EncoderBase
{
public:
    VideoToolboxEncoder();
    ~VideoToolboxEncoder() override;

    // Non-copyable
    VideoToolboxEncoder(const VideoToolboxEncoder&) = delete;
    VideoToolboxEncoder& operator=(const VideoToolboxEncoder&) = delete;

    /// region <Lifecycle>

    bool Start(const std::string& filename, const EncoderConfig& config) override;
    void Stop() override;

    /// endregion </Lifecycle>

    /// region <Frame Input>

    void OnVideoFrame(const FramebufferDescriptor& framebuffer, double timestampSec) override;
    void OnAudioSamples(const int16_t* samples, size_t sampleCount, double timestampSec) override;

    /// endregion </Frame Input>

    /// region <State>

    bool IsRecording() const override { return _isRecording; }
    std::string GetType() const override { return "videotoolbox"; }
    std::string GetDisplayName() const override { return "macOS VideoToolbox (H.264/HEVC)"; }
    bool SupportsVideo() const override { return true; }
    bool SupportsAudio() const override { return true; }

    /// endregion </State>

    /// region <Statistics>

    uint64_t GetFramesEncoded() const override { return _framesEncoded; }
    uint64_t GetAudioSamplesEncoded() const override { return _audioSamplesEncoded; }
    uint64_t GetOutputFileSize() const override;

    /// endregion </Statistics>

    /// @brief Get last error message
    std::string GetLastError() const override { return _lastError; }

private:
    // Internal initialization methods (implemented in .mm)
    bool initAudioConverter(const EncoderConfig& config);
    bool initAssetWriter(const std::string& filename, const EncoderConfig& config);

    // AVFoundation state (opaque pointers, actual types in .mm)
    void* _assetWriter = nullptr;          // AVAssetWriter*
    void* _videoInput = nullptr;           // AVAssetWriterInput*
    void* _audioInput = nullptr;           // AVAssetWriterInput*
    void* _pixelBufferAdaptor = nullptr;   // AVAssetWriterInputPixelBufferAdaptor*
    void* _audioFormatDesc = nullptr;      // CMAudioFormatDescriptionRef

    // Configuration
    uint32_t _width = 0;        // Output dimensions (source * scale)
    uint32_t _height = 0;
    uint32_t _srcWidth = 0;     // Incoming framebuffer dimensions
    uint32_t _srcHeight = 0;
    uint32_t _scale = 1;        // Integer nearest-neighbor upscale
    float _fps = 50.0f;
    bool _useHEVC = false;
    bool _hasAudio = true;
    bool _isRecording = false;
    uint32_t _audioChannels = 2;
    uint32_t _audioSampleRate = 44100;

    // Statistics
    uint64_t _framesEncoded = 0;
    uint64_t _audioSamplesEncoded = 0;

    // State
    std::string _filename;
    std::string _lastError;
};

#endif // __APPLE__
