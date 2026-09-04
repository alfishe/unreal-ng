#pragma once

#include "stdafx.h"

#include "3rdparty/tinywav/tinywav.h"
#include "encoder_base.h"
#include "encoder_config.h"

#include <string>

/// region <Documentation>

/// Mono int16 WAV sink over tinywav, shaped as an EncoderBase so the tape
/// renderer treats native WAV and ffmpeg FLAC identically (tape-audio-bridge
/// design §5.3). tinywav is the in-tree, dependency-free writer — the WAV
/// path is always available and is the fallback when ffmpeg is absent.

/// endregion </Documentation>

class TapeAudioWavEncoder : public EncoderBase
{
public:
    TapeAudioWavEncoder() = default;
    ~TapeAudioWavEncoder() override;

    TapeAudioWavEncoder(const TapeAudioWavEncoder&) = delete;
    TapeAudioWavEncoder& operator=(const TapeAudioWavEncoder&) = delete;

    bool Start(const std::string& filename, const EncoderConfig& config) override;
    void Stop() override;

    void OnAudioSamples(const int16_t* samples, size_t sampleCount, double timestampSec) override;

    bool IsRecording() const override { return _started; }
    std::string GetType() const override { return "tinywav"; }
    std::string GetDisplayName() const override { return "WAV (tinywav)"; }
    bool SupportsVideo() const override { return false; }
    bool SupportsAudio() const override { return true; }

    uint64_t GetAudioSamplesEncoded() const override { return _samplesWritten; }
    uint64_t GetOutputFileSize() const override;
    std::string GetLastError() const override { return _lastError; }

private:
    TinyWav _wav{};
    bool _started = false;
    uint64_t _samplesWritten = 0;
    std::string _outputPath;
    std::string _lastError;
};
