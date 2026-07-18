#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <deque>

#include "../encoder_base.h"
#include "../encoder_config.h"
#include "../../../common/subprocess.h"
#include "../../../common/named_pipe.h"

/// @brief FFmpeg Pipe Encoder — Universal fallback encoder using external ffmpeg binary
///
/// Feeds raw video (BGRA) and audio (PCM s16le) data to an external ffmpeg process
/// via named pipes. ffmpeg handles all compression and muxing.
///
/// Key features:
/// - Works on all platforms (macOS, Linux, Windows) where ffmpeg is installed
/// - Automatic hardware encoder detection (VideoToolbox, NVENC, QuickSync, VA-API)
/// - Multi-track audio support (MKV container)
/// - Natural backpressure via pipe buffer fullness
/// - Perfect 50Hz output (PTS based on emulated frame count, not wall-clock)
///
/// Frame flow:
///   Framebuffer (BGRA) -> video pipe -> ffmpeg process -> output file
///   Audio (PCM s16le)  -> audio pipe -> ffmpeg -------^
class FFmpegPipeEncoder : public EncoderBase
{
public:
    FFmpegPipeEncoder();
    ~FFmpegPipeEncoder() override;

    // Non-copyable
    FFmpegPipeEncoder(const FFmpegPipeEncoder&) = delete;
    FFmpegPipeEncoder& operator=(const FFmpegPipeEncoder&) = delete;

    /// region <Lifecycle>

    /// @brief Start encoding to file
    /// @param filename Output file path
    /// @param config Encoder configuration
    /// @return true if encoder started successfully
    bool Start(const std::string& filename, const EncoderConfig& config) override;

    /// @brief Stop encoding, close pipes, wait for ffmpeg to finalize
    void Stop() override;

    /// endregion </Lifecycle>

    /// region <Frame Input>

    /// @brief Write a video frame to ffmpeg via pipe
    /// @param framebuffer Frame data (BGRA format)
    /// @param timestampSec Presentation timestamp (emulated time)
    void OnVideoFrame(const FramebufferDescriptor& framebuffer, double timestampSec) override;

    /// @brief Write audio samples to ffmpeg via pipe
    /// @param samples Interleaved stereo samples (int16_t)
    /// @param sampleCount Total sample count
    /// @param timestampSec Presentation timestamp (emulated time)
    void OnAudioSamples(const int16_t* samples, size_t sampleCount, double timestampSec) override;

    /// endregion </Frame Input>

    /// region <State>

    bool IsRecording() const override { return _isRecording; }
    std::string GetType() const override { return "ffmpeg"; }
    std::string GetDisplayName() const override { return "FFmpeg Encoder"; }
    bool SupportsVideo() const override { return true; }
    bool SupportsAudio() const override { return true; }

    /// endregion </State>

    /// region <Statistics>

    uint64_t GetFramesEncoded() const override { return _framesEncoded; }
    uint64_t GetAudioSamplesEncoded() const override { return _audioSamplesEncoded; }

    uint64_t GetOutputFileSize() const override;

    /// endregion </Statistics>

    /// region <FFmpeg-specific>

    /// @brief Set explicit ffmpeg binary path
    /// @param path Path to ffmpeg executable
    void setFFmpegPath(const std::string& path) { _ffmpegPath = path; }

    /// @brief Blocking backpressure mode for non-realtime recording.
    /// When enabled, OnVideoFrame/OnAudioSamples block the caller until queue
    /// space is available instead of dropping data. The emulation thread is
    /// throttled to the encoder's pace, guaranteeing every frame is encoded.
    void setBlocking(bool blocking) { _blocking = blocking; }

    /// @brief Get last error message
    std::string GetLastError() const override { return _lastError; }

    /// @brief Get ffmpeg stderr diagnostics (available after Stop)
    std::string getDiagnostics() const { return _diagnostics; }

    /// endregion </FFmpeg-specific>

private:
    /// Build ffmpeg command-line arguments
    std::vector<std::string> buildFFmpegArgs(const std::string& filename, const EncoderConfig& config);

    /// Resolve the video encoder name based on config, platform, and target container
    std::string resolveVideoEncoder(const EncoderConfig& config, const std::string& container);

    /// Resolve the audio encoder name based on config and target container
    std::string resolveAudioEncoder(const EncoderConfig& config, const std::string& container);

    /// Get the ffmpeg-compatible pipe path (adds \\.\pipe\ prefix on Windows)
    std::string getFFmpegPipePath(const std::string& pipeName) const;

    /// Create temp directory for named pipes
    bool createTempDir();

    /// Clean up temp directory and pipes
    void cleanup();

    // FFmpeg process
    Subprocess _ffmpegProcess;

    // Named pipes for feeding data to ffmpeg
    std::unique_ptr<NamedPipe> _videoPipe;
    std::unique_ptr<NamedPipe> _audioPipe;
    std::vector<std::unique_ptr<NamedPipe>> _multiTrackAudioPipes;

    // Paths
    std::string _tempDir;
    std::string _videoPipePath;
    std::string _audioPipePath;
    std::string _ffmpegPath;
    std::string _lastError;
    std::string _diagnostics;

    // Video configuration
    uint32_t _width = 0;
    uint32_t _height = 0;
    float _fps = 50.0f;
    bool _hasVideo = true;
    bool _hasAudio = true;
    bool _isRecording = false;

    // Statistics
    std::atomic<uint64_t> _framesEncoded{0};
    std::atomic<uint64_t> _audioSamplesEncoded{0};

    // Background writers — ONE THREAD PER PIPE. This is load-bearing:
    // ffmpeg's muxer interleaves streams, so it stops reading video whenever
    // it needs audio (and vice versa). A single thread writing both pipes
    // sequentially deadlocks the moment one write blocks; independent writer
    // threads let ffmpeg's own backpressure drive both streams.
    void videoWriterMain();
    void audioWriterMain();
    void failWorker(const std::string& error);
    std::thread _videoThread;
    std::thread _audioThread;

    // Continuously drains ffmpeg's stderr into _diagnostics. Without a reader
    // the stderr pipe fills on long recordings and ffmpeg blocks mid-encode.
    std::thread _stderrThread;
    std::mutex _queueMutex;
    std::condition_variable _queueCond;

    struct FrameData {
        std::vector<uint8_t> data;
    };
    std::deque<FrameData> _videoQueue;
    std::deque<FrameData> _audioQueue;

    std::atomic<bool> _cancelRequested{false};   // Hard abort: in-flight writes give up
    std::atomic<bool> _drainRequested{false};    // Graceful stop: flush queues, then exit
    std::atomic<bool> _workerFailed{false};
    std::atomic<bool> _blocking{false};          // Blocking backpressure (non-realtime mode)

    // Limits
    const size_t MAX_QUEUE_FRAMES = 60;

    // Output file
    std::string _outputFilename;
};
