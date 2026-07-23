#pragma once

#include "emulator/recording/encoder_base.h"
#include "emulator/recording/encoder_config.h"

#include <memory>
#include <string>
#include <vector>
#include <atomic>

#ifdef _WIN32

/// @brief Native NVIDIA NVENC hardware encoder
/// Uses NVENC SDK directly via runtime DLL loading - no FFmpeg dependency.
/// Requires NVIDIA GPU with Kepler architecture or newer (GTX 600+).
class NvencEncoder : public EncoderBase
{
public:
    NvencEncoder();
    ~NvencEncoder() override;

    // Non-copyable
    NvencEncoder(const NvencEncoder&) = delete;
    NvencEncoder& operator=(const NvencEncoder&) = delete;

    // EncoderBase interface
    bool Start(const std::string& filename, const EncoderConfig& config) override;
    void Stop() override;
    void OnVideoFrame(const FramebufferDescriptor& framebuffer, double timestampSec) override;
    void OnAudioSamples(const int16_t* samples, size_t sampleCount, double timestampSec) override;

    bool IsRecording() const override { return _isRecording; }
    std::string GetType() const override { return "nvenc"; }
    std::string GetDisplayName() const override { return "NVIDIA NVENC"; }
    bool SupportsVideo() const override { return true; }
    bool SupportsAudio() const override { return true; }

    uint64_t GetFramesEncoded() const override { return _framesEncoded; }
    uint64_t GetAudioSamplesEncoded() const override { return 0; }
    uint64_t GetOutputFileSize() const override;
    std::string GetLastError() const override { return _lastError; }

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;

    std::string _outputFilename;
    std::string _lastError;
    std::atomic<bool> _isRecording{false};
    std::atomic<uint64_t> _framesEncoded{0};
};

#endif // _WIN32
