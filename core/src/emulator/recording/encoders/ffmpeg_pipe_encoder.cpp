#include "ffmpeg_pipe_encoder.h"
#include "../ffmpeg_probe.h"
#include "../../video/screen.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

// ============================================================================
// Constructor / Destructor
// ============================================================================

FFmpegPipeEncoder::FFmpegPipeEncoder()
{
    // Try to auto-detect ffmpeg path
    _ffmpegPath = FFmpegProbe::findFFmpeg();
}

FFmpegPipeEncoder::~FFmpegPipeEncoder()
{
    Stop();
}

// ============================================================================
// Temp directory management
// ============================================================================

bool FFmpegPipeEncoder::createTempDir()
{
#ifndef _WIN32
    // Unix: create temp dir with mkdtemp
    std::string templatePath = "/tmp/unreal-rec-XXXXXX";
    std::vector<char> tmpl(templatePath.begin(), templatePath.end());
    tmpl.push_back('\0');

    char* result = mkdtemp(tmpl.data());
    if (result == nullptr)
    {
        _lastError = "Failed to create temp directory";
        return false;
    }
    _tempDir = result;
#else
    // Windows: create temp dir under %TEMP%
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);

    // Generate unique directory name
    _tempDir = std::string(tempPath) + "unreal-rec-";
    char uniqueName[16];
    snprintf(uniqueName, sizeof(uniqueName), "%p", this);
    _tempDir += uniqueName;

    if (!CreateDirectoryA(_tempDir.c_str(), nullptr))
    {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS)
        {
            _lastError = "Failed to create temp directory, error: " + std::to_string(err);
            return false;
        }
    }
#endif
    return true;
}

// ============================================================================
// Start
// ============================================================================

bool FFmpegPipeEncoder::Start(const std::string& filename, const EncoderConfig& config)
{
    if (_isRecording)
    {
        _lastError = "Already recording";
        return false;
    }

    // Validate ffmpeg path (explicit config path wins over auto-detection)
    if (!config.ffmpegPath.empty())
    {
        _ffmpegPath = config.ffmpegPath;
    }
    if (_ffmpegPath.empty())
    {
        _ffmpegPath = FFmpegProbe::findFFmpeg();
    }
    if (_ffmpegPath.empty())
    {
        _lastError = "FFmpeg binary not found. Install ffmpeg or set path manually.";
        return false;
    }

    _outputFilename = filename;
    _width = config.videoWidth;
    _height = config.videoHeight;
    _fps = config.frameRate;
    _hasAudio = (config.audioChannels > 0);

    // Verify this ffmpeg build can encode the requested codec before
    // creating pipes/spawning — fail with a clear message instead of an
    // "Unknown encoder" process exit
    {
        std::string ext;
        size_t dotPos = filename.rfind('.');
        if (dotPos != std::string::npos)
        {
            ext = filename.substr(dotPos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        }

        if (resolveVideoEncoder(config, ext).empty())
        {
            _lastError = "This ffmpeg build has no encoder for codec '" + config.videoCodec +
                         "' (container '" + ext + "'). Install a full ffmpeg build.";
            return false;
        }
    }

    // Create temp directory for named pipes
    if (!createTempDir())
    {
        return false;
    }

    // Build pipe paths
#ifndef _WIN32
    _videoPipePath = _tempDir + "/video.fifo";
    _audioPipePath = _tempDir + "/audio.fifo";
#else
    _videoPipePath = "video_pipe";
    _audioPipePath = "audio_pipe";
#endif

    // Create named pipes
    if (!NamedPipe::create(_videoPipePath))
    {
        _lastError = "Failed to create video pipe";
        cleanup();
        return false;
    }

    if (_hasAudio)
    {
        if (!NamedPipe::create(_audioPipePath))
        {
            _lastError = "Failed to create audio pipe";
            cleanup();
            return false;
        }
    }

    // Build ffmpeg arguments
    auto args = buildFFmpegArgs(filename, config);

    // Spawn ffmpeg process
    if (!_ffmpegProcess.spawn(_ffmpegPath, args))
    {
        _lastError = "Failed to spawn ffmpeg: " + _ffmpegProcess.getLastError();
        cleanup();
        return false;
    }

    // Prepare queues
    _videoQueue.clear();
    _audioQueue.clear();
    _cancelRequested = false;
    _drainRequested = false;
    _workerFailed = false;

    // Drain stderr for the lifetime of the process (returns at EOF)
    _diagnostics.clear();
    _stderrThread = std::thread([this]() {
        _diagnostics = _ffmpegProcess.readAllStderr();
    });

    // Spawn one writer thread per pipe (see header note — required to avoid
    // deadlocking against ffmpeg's stream interleaving)
    _videoThread = std::thread(&FFmpegPipeEncoder::videoWriterMain, this);
    if (_hasAudio)
        _audioThread = std::thread(&FFmpegPipeEncoder::audioWriterMain, this);

    _isRecording = true;
    _framesEncoded = 0;
    _audioSamplesEncoded = 0;

    return true;
}

// ============================================================================
// Stop
// ============================================================================

void FFmpegPipeEncoder::Stop()
{
    if (!_isRecording)
        return;

    // Hard deadline: entire stop must complete in ~1 second. A slower graceful
    // drain is not worth hanging the UI — the file is already on disk and
    // playable; at worst we lose the last fraction of a second.
    constexpr int STOP_TIMEOUT_MS = 1000;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(STOP_TIMEOUT_MS);

    auto msRemaining = [&]() {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        return left > 0 ? static_cast<int>(left) : 0;
    };

    // 1. Signal writers to drain. If they don't finish promptly, hard-cancel.
    _drainRequested = true;
    _queueCond.notify_all();

    auto joinWithTimeout = [&](std::thread& t, const char* name) {
        if (!t.joinable())
            return true;
        std::atomic<bool> done{false};
        std::thread waiter([&]() { t.join(); done = true; });
        while (!done && msRemaining() > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (done) {
            waiter.join();
            return true;
        }
        // Timed out — escalate
        _cancelRequested = true;
        _queueCond.notify_all();
        waiter.detach();
        (void)name;
        return false;
    };

    joinWithTimeout(_videoThread, "video");
    joinWithTimeout(_audioThread, "audio");

    _isRecording = false;

    // 2. Close write ends → EOF to ffmpeg
    if (_videoPipe) _videoPipe->close();
    if (_audioPipe) _audioPipe->close();
    for (auto& p : _multiTrackAudioPipes) if (p) p->close();
    _multiTrackAudioPipes.clear();
    _ffmpegProcess.closeStdin();

    // 3. Wait for ffmpeg to finalize (remaining time budget)
    int exitCode = _ffmpegProcess.waitForFinished(msRemaining());
    if (exitCode == -1)
    {
        _ffmpegProcess.kill();
        _ffmpegProcess.waitForFinished(100);
    }

    // 4. Close stderr to unblock the drain thread, then join briefly
    _ffmpegProcess.closeStderr();
    joinWithTimeout(_stderrThread, "stderr");

    if (exitCode != 0 && exitCode != -1)
        _lastError = "FFmpeg exited with code " + std::to_string(exitCode);

    cleanup();
}

// ============================================================================
// Frame input
// ============================================================================

void FFmpegPipeEncoder::OnVideoFrame(const FramebufferDescriptor& framebuffer, double timestampSec)
{
    (void)timestampSec;
    if (!_isRecording || _workerFailed)
        return;

    uint32_t expectedWidth = _width > 0 ? _width : framebuffer.width;
    uint32_t expectedHeight = _height > 0 ? _height : framebuffer.height;
    size_t expectedSize = static_cast<size_t>(expectedWidth) * expectedHeight * 4;

    if (framebuffer.memoryBuffer == nullptr || framebuffer.memoryBufferSize < expectedSize)
        return;

    std::unique_lock<std::mutex> lock(_queueMutex);

    if (_blocking)
    {
        // Non-realtime mode: block the emulation thread until the encoder
        // frees queue space. This throttles emulation to the encoder's pace
        // so every frame is recorded (no drops).
        _queueCond.wait(lock, [this]() {
            return _videoQueue.size() < MAX_QUEUE_FRAMES || _cancelRequested || _drainRequested || _workerFailed;
        });
    }

    if (_videoQueue.size() >= MAX_QUEUE_FRAMES || _cancelRequested || _drainRequested || _workerFailed)
    {
        return; // Drop frame (realtime mode) or bail out (shutdown/failure)
    }

    FrameData data;
    data.data.assign((const uint8_t*)framebuffer.memoryBuffer, (const uint8_t*)framebuffer.memoryBuffer + expectedSize);
    _videoQueue.push_back(std::move(data));

    _queueCond.notify_one();
}

void FFmpegPipeEncoder::OnAudioSamples(const int16_t* samples, size_t sampleCount, double timestampSec)
{
    (void)timestampSec;
    if (!_isRecording || !_hasAudio || _workerFailed)
        return;

    if (samples == nullptr || sampleCount == 0)
        return;

    size_t byteCount = sampleCount * sizeof(int16_t);

    std::unique_lock<std::mutex> lock(_queueMutex);

    if (_blocking)
    {
        // Non-realtime mode: block until space frees up (see OnVideoFrame)
        _queueCond.wait(lock, [this]() {
            return _audioQueue.size() < MAX_QUEUE_FRAMES * 10 || _cancelRequested || _drainRequested || _workerFailed;
        });
    }

    // Audio is smaller, we can allow more items
    if (_audioQueue.size() >= MAX_QUEUE_FRAMES * 10 || _cancelRequested || _drainRequested || _workerFailed)
    {
        return; // Drop audio (realtime mode) or bail out (shutdown/failure)
    }

    FrameData data;
    data.data.assign((const uint8_t*)samples, ((const uint8_t*)samples) + byteCount);
    _audioQueue.push_back(std::move(data));

    _queueCond.notify_one();
}

// ============================================================================
// Worker Thread
// ============================================================================

void FFmpegPipeEncoder::failWorker(const std::string& error)
{
    if (!_workerFailed.exchange(true))
    {
        _lastError = error;
        _ffmpegProcess.terminate();
    }
    // Wake the other writer and any producer blocked on backpressure
    _queueCond.notify_all();
}

void FFmpegPipeEncoder::videoWriterMain()
{
    // Open video pipe (completes when ffmpeg connects its read end)
    _videoPipe = std::make_unique<NamedPipe>();
    if (!_videoPipe->openForWriteTimeout(_videoPipePath, 15000, &_cancelRequested))
    {
        failWorker("Failed to open video pipe: " + _videoPipe->getLastError());
        return;
    }

    while (true)
    {
        FrameData frame;
        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            _queueCond.wait(lock, [this]() {
                return !_videoQueue.empty() || _cancelRequested || _drainRequested || _workerFailed;
            });

            if (_cancelRequested || _workerFailed)
                return;

            if (_videoQueue.empty())
            {
                if (_drainRequested)
                    return;  // Graceful drain complete
                continue;
            }

            frame = std::move(_videoQueue.front());
            _videoQueue.pop_front();
        }

        if (!_videoPipe->write(frame.data.data(), frame.data.size(), &_cancelRequested))
        {
            failWorker("Failed to write video frame: " + _videoPipe->getLastError());
            return;
        }
        _framesEncoded++;

        // Space freed — wake producers blocked on backpressure
        _queueCond.notify_all();
    }
}

void FFmpegPipeEncoder::audioWriterMain()
{
    _audioPipe = std::make_unique<NamedPipe>();
    if (!_audioPipe->openForWriteTimeout(_audioPipePath, 15000, &_cancelRequested))
    {
        failWorker("Failed to open audio pipe: " + _audioPipe->getLastError());
        return;
    }

    while (true)
    {
        FrameData chunk;
        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            _queueCond.wait(lock, [this]() {
                return !_audioQueue.empty() || _cancelRequested || _drainRequested || _workerFailed;
            });

            if (_cancelRequested || _workerFailed)
                return;

            if (_audioQueue.empty())
            {
                if (_drainRequested)
                    return;  // Graceful drain complete
                continue;
            }

            chunk = std::move(_audioQueue.front());
            _audioQueue.pop_front();
        }

        if (!_audioPipe->write(chunk.data.data(), chunk.data.size(), &_cancelRequested))
        {
            failWorker("Failed to write audio samples: " + _audioPipe->getLastError());
            return;
        }
        _audioSamplesEncoded += (chunk.data.size() / sizeof(int16_t));

        _queueCond.notify_all();
    }
}

// ============================================================================
// Output file size
// ============================================================================

uint64_t FFmpegPipeEncoder::GetOutputFileSize() const
{
    if (_outputFilename.empty())
        return 0;

#ifndef _WIN32
    struct stat st;
    if (stat(_outputFilename.c_str(), &st) == 0)
        return static_cast<uint64_t>(st.st_size);
#else
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    if (GetFileAttributesExA(_outputFilename.c_str(), GetFileExInfoStandard, &attrs))
    {
        return (static_cast<uint64_t>(attrs.nFileSizeHigh) << 32) | attrs.nFileSizeLow;
    }
#endif
    return 0;
}

// ============================================================================
// FFmpeg argument construction
// ============================================================================

std::string FFmpegPipeEncoder::resolveVideoEncoder(const EncoderConfig& config, const std::string& container)
{
    std::string codec = config.videoCodec;

    // Normalize codec name
    std::transform(codec.begin(), codec.end(), codec.begin(), ::tolower);

    // Animation formats (lossless, ideal for 16-color ZX pixel art)
    if (codec == "gif")
        return "gif";
    if (codec == "apng" || container == "apng")
        return "apng";
    if (codec == "webp" || container == "webp")
    {
        if (FFmpegProbe::isEncoderAvailable("libwebp_anim", _ffmpegPath))
            return "libwebp_anim";
        if (FFmpegProbe::isEncoderAvailable("libwebp", _ffmpegPath))
            return "libwebp";
        return {};  // This ffmpeg build lacks WebP support
    }

    // WebM only holds VP8/VP9/AV1 — coerce incompatible codec requests to VP9
    if (container == "webm" && codec != "vp8" && codec != "vp9" && codec != "av1")
        codec = "vp9";

    // Detect hardware acceleration support
    FFmpegProbe::HWAccelInfo hwInfo = FFmpegProbe::detectHWAccel(_ffmpegPath);

    // Platform-specific encoder selection
    if (codec == "h264" || codec == "h.264" || codec == "avc")
    {
#ifdef __APPLE__
        if (hwInfo.videoToolbox && FFmpegProbe::isEncoderAvailable("h264_videotoolbox", _ffmpegPath))
            return "h264_videotoolbox";
#elif defined(_WIN32)
        if (hwInfo.nvenc && FFmpegProbe::isEncoderAvailable("h264_nvenc", _ffmpegPath))
            return "h264_nvenc";
        if (hwInfo.qsv && FFmpegProbe::isEncoderAvailable("h264_qsv", _ffmpegPath))
            return "h264_qsv";
#elif defined(__linux__)
        if (hwInfo.vaapi && FFmpegProbe::isEncoderAvailable("h264_vaapi", _ffmpegPath))
            return "h264_vaapi";
        if (hwInfo.nvenc && FFmpegProbe::isEncoderAvailable("h264_nvenc", _ffmpegPath))
            return "h264_nvenc";
#endif
        // Fallback to software
        if (FFmpegProbe::isEncoderAvailable("libx264", _ffmpegPath))
            return "libx264";
        return "libx264"; // Most common default
    }

    if (codec == "h265" || codec == "h.265" || codec == "hevc")
    {
#ifdef __APPLE__
        if (hwInfo.videoToolbox && FFmpegProbe::isEncoderAvailable("hevc_videotoolbox", _ffmpegPath))
            return "hevc_videotoolbox";
#elif defined(_WIN32)
        if (hwInfo.nvenc && FFmpegProbe::isEncoderAvailable("hevc_nvenc", _ffmpegPath))
            return "hevc_nvenc";
        if (hwInfo.qsv && FFmpegProbe::isEncoderAvailable("hevc_qsv", _ffmpegPath))
            return "hevc_qsv";
#elif defined(__linux__)
        if (hwInfo.vaapi && FFmpegProbe::isEncoderAvailable("hevc_vaapi", _ffmpegPath))
            return "hevc_vaapi";
        if (hwInfo.nvenc && FFmpegProbe::isEncoderAvailable("hevc_nvenc", _ffmpegPath))
            return "hevc_nvenc";
#endif
        if (FFmpegProbe::isEncoderAvailable("libx265", _ffmpegPath))
            return "libx265";
        return "libx264"; // Fallback to H.264
    }

    if (codec == "vp9")
    {
        if (FFmpegProbe::isEncoderAvailable("libvpx-vp9", _ffmpegPath))
            return "libvpx-vp9";
        return "libvpx"; // VP8 fallback
    }

    if (codec == "vp8")
    {
        return "libvpx";
    }

    if (codec == "av1")
    {
        if (FFmpegProbe::isEncoderAvailable("libaom-av1", _ffmpegPath))
            return "libaom-av1";
        if (FFmpegProbe::isEncoderAvailable("libsvtav1", _ffmpegPath))
            return "libsvtav1";
        return "libx264"; // Fallback
    }

    // Default
    return "libx264";
}

std::string FFmpegPipeEncoder::resolveAudioEncoder(const EncoderConfig& config, const std::string& container)
{
    std::string codec = config.audioCodec;
    std::transform(codec.begin(), codec.end(), codec.begin(), ::tolower);

    // WebM only holds Opus/Vorbis — coerce incompatible audio codec requests to Opus
    if (container == "webm" && codec != "opus" && codec != "vorbis")
        codec = "opus";

    if (codec == "aac")
    {
        // Prefer the higher-quality fdk encoder when this build has it;
        // the native aac encoder is compiled into every ffmpeg build
        if (FFmpegProbe::isEncoderAvailable("libfdk_aac", _ffmpegPath))
            return "libfdk_aac";
        return "aac";
    }

    if (codec == "mp3")
    {
        if (FFmpegProbe::isEncoderAvailable("libmp3lame", _ffmpegPath))
            return "libmp3lame";
        return "mp3";
    }

    if (codec == "flac")
        return "flac";

    if (codec == "pcm" || codec == "pcm_s16le" || codec == "wav")
        return "pcm_s16le";

    if (codec == "vorbis")
    {
        if (FFmpegProbe::isEncoderAvailable("libvorbis", _ffmpegPath))
            return "libvorbis";
        return "vorbis";
    }

    if (codec == "opus")
    {
        if (FFmpegProbe::isEncoderAvailable("libopus", _ffmpegPath))
            return "libopus";
        return "opus";
    }

    // Default
    return "aac";
}

std::vector<std::string> FFmpegPipeEncoder::buildFFmpegArgs(const std::string& filename, const EncoderConfig& config)
{
    std::vector<std::string> args;

    // Determine container from filename extension (drives codec compatibility)
    std::string ext;
    size_t dotPos = filename.rfind('.');
    if (dotPos != std::string::npos)
    {
        ext = filename.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }

    // Overwrite output
    args.push_back("-y");

    // No periodic status line, warnings+errors only. The status spam fills
    // the 64KB stderr pipe on long recordings; once full, ffmpeg blocks
    // mid-encode and the whole pipeline stalls (stderr is also drained by a
    // reader thread — this keeps what remains meaningful for diagnostics).
    args.push_back("-nostats");
    args.push_back("-loglevel");
    args.push_back("warning");

    // --- Video input ---
    // Minimal probing: all raw stream parameters are given explicitly, so
    // ffmpeg must not buffer input data during avformat_find_stream_info —
    // that would delay opening the audio pipe.
    args.push_back("-probesize");
    args.push_back("32");
    args.push_back("-analyzeduration");
    args.push_back("0");
    args.push_back("-f");
    args.push_back("rawvideo");
    args.push_back("-pix_fmt");
    args.push_back("rgba");  // Emulator framebuffer byte order is R,G,B,A
    args.push_back("-s");
    args.push_back(std::to_string(_width) + "x" + std::to_string(_height));
    args.push_back("-r");
    // Use integer fps to avoid floating point PTS issues
    args.push_back(std::to_string(static_cast<int>(std::round(_fps))));
    args.push_back("-i");
    args.push_back(_videoPipePath);

    // --- Audio input ---
    if (_hasAudio)
    {
        args.push_back("-probesize");
        args.push_back("32");
        args.push_back("-analyzeduration");
        args.push_back("0");
        args.push_back("-f");
        args.push_back("s16le");
        args.push_back("-ar");
        args.push_back(std::to_string(config.audioSampleRate));
        args.push_back("-ac");
        args.push_back(std::to_string(config.audioChannels));
        args.push_back("-i");
        args.push_back(_audioPipePath);
    }

    // --- Stream mapping ---
    args.push_back("-map");
    args.push_back("0:v");
    if (_hasAudio)
    {
        args.push_back("-map");
        args.push_back("1:a");
    }

    // --- Video encoder ---
    std::string videoEncoder = resolveVideoEncoder(config, ext);
    args.push_back("-c:v");
    args.push_back(videoEncoder);

    // --- Video filters + color handling ---
    // RGB-native animation formats (gif/apng/webp) keep exact colors; YUV
    // codecs need an explicit full→limited BT.709 conversion AND tagging —
    // untagged output makes players guess and render pale/shifted colors.
    bool rgbFormat = (videoEncoder == "gif" || videoEncoder == "apng" ||
                      videoEncoder.rfind("libwebp", 0) == 0);

    std::string vf;
    if (config.scaleFactor > 1)
    {
        // Nearest-neighbor upscale: keeps ZX pixels crisp and preserves
        // per-pixel color detail through 4:2:0 chroma subsampling
        std::string n = std::to_string(config.scaleFactor);
        vf = "scale=iw*" + n + ":ih*" + n + ":flags=neighbor";
    }

    if (!rgbFormat)
    {
        if (!vf.empty())
            vf += ",";
        vf += "scale=in_range=full:out_range=limited:out_color_matrix=bt709,format=yuv420p";
    }

    if (!vf.empty())
    {
        args.push_back("-vf");
        args.push_back(vf);
    }

    if (!rgbFormat)
    {
        args.push_back("-colorspace");
        args.push_back("bt709");
        args.push_back("-color_primaries");
        args.push_back("bt709");
        args.push_back("-color_trc");
        args.push_back("bt709");
        args.push_back("-color_range");
        args.push_back("tv");
    }

    // HEVC in MP4/MOV needs the hvc1 tag for QuickTime/Safari playback
    bool isHEVC = (videoEncoder.find("265") != std::string::npos ||
                   videoEncoder.find("hevc") != std::string::npos);
    if (isHEVC && (ext == "mp4" || ext == "mov"))
    {
        args.push_back("-tag:v");
        args.push_back("hvc1");
    }

    // Video bitrate
    if (config.videoBitrate > 0)
    {
        args.push_back("-b:v");
        args.push_back(std::to_string(config.videoBitrate) + "k");
    }

    // Quality settings for libx264/libx265
    if (videoEncoder == "libx264" || videoEncoder == "libx265")
    {
        // CRF quality (0-51, lower = better)
        int crf = 23;
        if (config.qualityPreset >= 8) crf = 18;
        else if (config.qualityPreset >= 6) crf = 20;
        else if (config.qualityPreset >= 4) crf = 23;
        else crf = 28;

        args.push_back("-crf");
        args.push_back(std::to_string(crf));

        // Preset
        std::string preset = "ultrafast"; // Fast encoding for realtime
        if (config.qualityPreset >= 8) preset = "medium";
        else if (config.qualityPreset >= 6) preset = "fast";
        args.push_back("-preset");
        args.push_back(preset);
    }

    // VP9/VP8 realtime encoding settings — libvpx is CPU-intensive; without
    // these flags it cannot sustain 50fps on most machines
    if (videoEncoder == "libvpx-vp9" || videoEncoder == "libvpx")
    {
        args.push_back("-deadline");
        args.push_back("realtime");
        args.push_back("-cpu-used");
        args.push_back("8");       // Max speed (0-8, higher = faster)
        args.push_back("-row-mt");
        args.push_back("1");       // Row-based multithreading

        // Constant-quality mode when no explicit bitrate is set
        // (libvpx defaults to 256 kbps otherwise, which looks terrible)
        if (config.videoBitrate == 0)
        {
            int crf = 30;
            if (config.qualityPreset >= 8) crf = 24;
            else if (config.qualityPreset >= 6) crf = 28;
            else if (config.qualityPreset < 4) crf = 34;

            args.push_back("-crf");
            args.push_back(std::to_string(crf));
            args.push_back("-b:v");
            args.push_back("0");
        }
    }

    // Hardware encoder bitrate (required for some HW encoders)
    if (videoEncoder.find("videotoolbox") != std::string::npos ||
        videoEncoder.find("nvenc") != std::string::npos ||
        videoEncoder.find("qsv") != std::string::npos ||
        videoEncoder.find("vaapi") != std::string::npos)
    {
        if (config.videoBitrate > 0)
        {
            // Already added -b:v above
        }
        else
        {
            // Default bitrate for HW encoders
            args.push_back("-b:v");
            args.push_back("5000k");
        }
    }

    // --- Audio encoder ---
    if (_hasAudio)
    {
        std::string audioEncoder = resolveAudioEncoder(config, ext);
        args.push_back("-c:a");
        args.push_back(audioEncoder);

        if (config.audioBitrate > 0 && audioEncoder != "pcm_s16le" && audioEncoder != "flac")
        {
            args.push_back("-b:a");
            args.push_back(std::to_string(config.audioBitrate) + "k");
        }

        // Opus only supports 48/24/16/12/8 kHz — resample the 44.1 kHz
        // emulator audio to 48 kHz on the output side
        if (audioEncoder == "libopus" || audioEncoder == "opus")
        {
            args.push_back("-ar:a");
            args.push_back("48000");
        }

        // ffmpeg's built-in opus/vorbis encoders are experimental and refuse
        // to run without explicit opt-in (used only when lib* are absent)
        if (audioEncoder == "opus" || audioEncoder == "vorbis")
        {
            args.push_back("-strict");
            args.push_back("-2");
        }
    }

    // Flush the muxer's output buffer after every packet. Without this the
    // 32KB avio buffer holds minutes of low-bitrate ZX output and the file
    // size appears frozen at 0 during recording.
    args.push_back("-flush_packets");
    args.push_back("1");

    // --- Container-specific flags ---
    if (ext == "mp4" || ext == "mov")
    {
        // Enable faststart for web playback
        args.push_back("-movflags");
        args.push_back("+faststart");
    }

    if (videoEncoder == "apng")
    {
        args.push_back("-plays");
        args.push_back("0");  // Loop forever
    }

    if (videoEncoder == "libwebp_anim")
    {
        args.push_back("-loop");
        args.push_back("0");      // Loop forever
        args.push_back("-lossless");
        args.push_back("1");      // Pixel-perfect for ZX content
    }

    // --- Output file ---
    args.push_back(filename);

    return args;
}

// ============================================================================
// Cleanup
// ============================================================================

void FFmpegPipeEncoder::cleanup()
{
    // Close any remaining pipes
    if (_videoPipe)
    {
        _videoPipe->close();
        _videoPipe.reset();
    }

    if (_audioPipe)
    {
        _audioPipe->close();
        _audioPipe.reset();
    }

    // Remove pipe files
    if (!_videoPipePath.empty())
        NamedPipe::remove(_videoPipePath);
    if (!_audioPipePath.empty())
        NamedPipe::remove(_audioPipePath);

    // Remove temp directory
    if (!_tempDir.empty())
    {
#ifndef _WIN32
        rmdir(_tempDir.c_str());
#else
        RemoveDirectoryA(_tempDir.c_str());
#endif
        _tempDir.clear();
    }

    _videoPipePath.clear();
    _audioPipePath.clear();
}
