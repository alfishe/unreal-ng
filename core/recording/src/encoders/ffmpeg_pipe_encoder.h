#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdio>
#include <deque>

#include "common/encoder_base.h"
#include "common/encoderconfig.h"
#include "common/subprocess.h"
#include "common/named_pipe.h"

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
    /// Zero-copy input: the producer writes the frame straight into a queue slot (R,G,B,A, tightly packed).
    /// Blocks for queue space in non-realtime mode, drops (Dropped) when the queue is full in realtime mode
    FrameTargetResult AcquireFrameTarget(uint32_t width, uint32_t height, FrameTarget& target) override;
    void SubmitFrameTarget(FrameTarget& target, double timestampSec) override;
    void ReleaseFrameTarget(FrameTarget& target) override;
    /// A fixed output frame (1080p / 4K) is built by ffmpeg from the source picture (see Segments below)
    bool TakesSourcePicture() const override { return _segmented && _isRecording; }

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

    /// Whether this ffmpeg's nearest scaling (`flags=neighbor`) of a width x height source into a picture of
    /// pictureWidth x pictureHeight takes FrameScaler's source pixel on both axes (then the fixed frame needs no
    /// remap tables). Asked from ffmpeg once per sizes (scratch files in dir), cached
    static bool NeighborIsFrameScaler(const std::string& ffmpeg, const std::string& dir, uint32_t width,
                                      uint32_t height, uint32_t pictureWidth, uint32_t pictureHeight);

    /// endregion </FFmpeg-specific>

private:
    /// Build ffmpeg command-line arguments
    std::vector<std::string> buildFFmpegArgs(const std::string& filename, const EncoderConfig& config);

    /// The output side of the video stream after its filter: codec tags, quality, per-codec options (no -c:v)
    void appendVideoCodecArgs(std::vector<std::string>& args, const EncoderConfig& config,
                              const std::string& videoEncoder, const std::string& ext) const;
    /// The audio encoder, its bitrate and resampling
    void appendAudioCodecArgs(std::vector<std::string>& args, const EncoderConfig& config, const std::string& ext);
    /// Packet flushing, container flags and the output file
    void appendOutputArgs(std::vector<std::string>& args, const std::string& ext, const std::string& filename);

    /// region <Segments>
    /// A fixed output frame (EncoderConfig::fixedOutput) is built by ffmpeg: the pipe carries the source picture
    /// (352x288 RGBA = 0.4 MB, against 33 MB of a 4K frame) and ffmpeg converts it at that size, scales it (nearest,
    /// FrameScaler's mapping) and pads it into the frame. Measured end to end (POC 054, 4K libx264): 2.2-5.5x the frames
    /// a second on macOS, 1.8-2x on Linux. A raw video input has one size, so the video goes into segments, one ffmpeg
    /// each, a new one when the picture changes size (TS-Conf, Profi hi-res, overscan); the sound goes into one PCM
    /// file, and Stop joins the segments (stream copy) with the sound encoded into the output
    /// One segment's ffmpeg: its pipe (the writer's _videoPipe while it is the current one), scaling tables, stderr
    struct Segment
    {
        std::unique_ptr<Subprocess> process;
        std::unique_ptr<NamedPipe> pipe;
        std::string pipePath;
        std::string xmapPath;
        std::string ymapPath;
        bool neighbor = false;  ///< swscale's nearest scaling, no remap tables (NeighborIsFrameScaler)
        std::thread stderrThread;
        int exitCode = 0;
    };
    bool startSegment(uint32_t width, uint32_t height);
    /// The current segment gets EOF and finishes on a thread of its own: the next segment starts at once and no
    /// frame waits for the old ffmpeg's flush (x264 lookahead: 1-2 s at 4K, the queue would overflow meanwhile)
    void retireSegment();
    /// Stop: the current segment ends, then every retired one is waited for. False when any of them failed
    bool finishSegment(int timeoutMs);
    static int closeSegment(Segment& segment, int timeoutMs);
    std::vector<std::string> buildSegmentArgs(uint32_t width, uint32_t height, const std::string& segmentFile,
                                              const Segment& segment);
    bool muxSegments();
    void signalFFmpeg(bool kill);  ///< terminate (or kill) the running ffmpeg, either mode

    bool _segmented = false;
    EncoderConfig _config;
    std::string _container;
    std::string _videoEncoderName;
    std::string _vaapiDevice;  ///< VA-API: the render node the trial encoded on (resolveVideoEncoder); empty otherwise
    uint32_t _segmentWidth = 0;
    uint32_t _segmentHeight = 0;
    std::vector<std::string> _segmentFiles;  ///< finished and current, in order
    std::unique_ptr<Segment> _segment;                 ///< the current one (its pipe is in _videoPipe)
    std::vector<std::unique_ptr<Segment>> _retired;   ///< finishing or finished, until Stop
    std::vector<std::thread> _retireThreads;          ///< the writer thread starts them, Stop joins them
    mutable std::mutex _processMutex;  ///< guards _segment, _retired and _segmentFiles against Stop / failWorker / GetOutputFileSize
    std::mutex _diagnosticsMutex;
    std::string _pcmPath;
    std::FILE* _pcmFile = nullptr;
    /// endregion </Segments>

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
        uint32_t width = 0;   ///< the picture's size (segments: a new size starts a new segment)
        uint32_t height = 0;
    };
    std::deque<FrameData> _videoQueue;
    // Frame buffers the writer is done with, reused by the next frames: a 4K frame is 33 MB, allocating (and page
    // faulting) one per frame costs more than the pipe write. Guarded by _queueMutex
    std::vector<std::vector<uint8_t>> _framePool;
    std::vector<uint8_t> _lentFrame;  ///< The slot AcquireFrameTarget lent (one at a time)
    std::vector<uint8_t> takeFrameBuffer(size_t size);   // call with _queueMutex held
    void recycleFrameBuffer(std::vector<uint8_t>&& buffer);  // call with _queueMutex held
    std::deque<FrameData> _audioQueue;

    std::atomic<bool> _cancelRequested{false};   // Hard abort: in-flight writes give up
    std::atomic<bool> _drainRequested{false};    // Graceful stop: flush queues, then exit
    std::atomic<bool> _workerFailed{false};
    std::atomic<bool> _blocking{false};          // Blocking backpressure (non-realtime mode)

    // Limits
    // The video queue holds at most 60 frames, and at most ~512 MiB of them: a 4K frame is 33 MB, 60 of them would be
    // 2 GB of queue (set from the frame size at Start)
    static constexpr size_t kMaxQueueFrames = 60;
    /// Segments queue source pictures (0.4-1.6 MB): 5 s at 50 fps carry the frames over a segment change, the new
    /// ffmpeg starting while the old one flushes (about 1 s each at 4K); within kQueueBudgetBytes for any picture
    static constexpr size_t kMaxSegmentQueueFrames = 250;
    static constexpr size_t kQueueBudgetBytes = static_cast<size_t>(512) * 1024 * 1024;
    size_t MAX_QUEUE_FRAMES = kMaxQueueFrames;

    // Output file
    std::string _outputFilename;
};
