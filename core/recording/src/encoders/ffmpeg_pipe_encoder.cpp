#include "ffmpeg_pipe_encoder.h"
#include "ffmpeg_probe.h"
#include "emulator/video/screen.h"
#include "common/threadhelper.h"
#include "common/framescaler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
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
        DWORD err = ::GetLastError();
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
    _config = config;
    _segmented = false;
    _segmentWidth = _segmentHeight = 0;
    _vaapiDevice.clear();
    _segmentFiles.clear();
    {
        const size_t frameBytes = static_cast<size_t>(_width) * _height * 4;
        MAX_QUEUE_FRAMES = frameBytes == 0 ? kMaxQueueFrames
                                           : std::clamp<size_t>(kQueueBudgetBytes / frameBytes, 4, kMaxQueueFrames);
    }
    _fps = config.frameRate;
    _hasVideo = !config.videoCodec.empty() && config.videoWidth > 0 && config.videoHeight > 0;
    _hasAudio = (config.audioChannels > 0);

    // Must have at least one stream
    if (!_hasVideo && !_hasAudio)
    {
        _lastError = "No video or audio streams configured";
        return false;
    }

    // Verify this ffmpeg build can encode the requested codec before
    // creating pipes/spawning — fail with a clear message instead of an
    // "Unknown encoder" process exit
    if (_hasVideo)
    {
        std::string ext;
        size_t dotPos = filename.rfind('.');
        if (dotPos != std::string::npos)
        {
            ext = filename.substr(dotPos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        }

        _videoEncoderName = resolveVideoEncoder(config, ext);
        if (_videoEncoderName.empty())
        {
            _lastError = "This ffmpeg build has no encoder for codec '" + config.videoCodec +
                         "' (container '" + ext + "'). Install a full ffmpeg build.";
            return false;
        }
        _container = ext;
        // A fixed frame (H.264 / H.265 only, see RecordingManager) is built by ffmpeg from the source picture
        _segmented = config.fixedOutput;
        if (_segmented)
            MAX_QUEUE_FRAMES = kMaxSegmentQueueFrames;  // source pictures, not 33 MB frames
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
    // Windows named pipes use \\.\pipe\name format
    // Use unique names based on pointer to allow multiple instances
    char uniqueSuffix[32];
    snprintf(uniqueSuffix, sizeof(uniqueSuffix), "_%p", this);
    _videoPipePath = std::string("unreal_video") + uniqueSuffix;
    _audioPipePath = std::string("unreal_audio") + uniqueSuffix;
#endif

    if (_segmented)
    {
        // The sound into one file for the whole recording; the video pipe and ffmpeg come with the first picture
        if (_hasAudio)
        {
#ifndef _WIN32
            _pcmPath = _tempDir + "/audio.pcm";
#else
            _pcmPath = _tempDir + "\\audio.pcm";
#endif
            _pcmFile = std::fopen(_pcmPath.c_str(), "wb");
            if (!_pcmFile)
            {
                _lastError = "Failed to create the sound file " + _pcmPath;
                cleanup();
                return false;
            }
        }
        _videoQueue.clear();
        _framePool.clear();
        _lentFrame = std::vector<uint8_t>();
        _audioQueue.clear();
        _cancelRequested = false;
        _drainRequested = false;
        _workerFailed = false;
        _diagnostics.clear();
        _videoThread = std::thread([this]() {
            ThreadHelper::setThreadName("ffmpeg-video");
            videoWriterMain();
        });
        if (_hasAudio)
            _audioThread = std::thread([this]() {
                ThreadHelper::setThreadName("ffmpeg-audio");
                audioWriterMain();
            });
        _isRecording = true;
        _framesEncoded = 0;
        _audioSamplesEncoded = 0;
        return true;
    }

    // Create named pipes (only for streams we have)
    if (_hasVideo)
    {
        if (!NamedPipe::create(_videoPipePath))
        {
            _lastError = "Failed to create video pipe";
            cleanup();
            return false;
        }
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
    _framePool.clear();
    _lentFrame = std::vector<uint8_t>();
    _audioQueue.clear();
    _cancelRequested = false;
    _drainRequested = false;
    _workerFailed = false;

    // Drain stderr for the lifetime of the process (returns at EOF)
    _diagnostics.clear();
    _stderrThread = std::thread([this]() {
        ThreadHelper::setThreadName("ffmpeg-stderr");
        _diagnostics = _ffmpegProcess.readAllStderr();
    });

    // Spawn one writer thread per pipe (see header note — required to avoid
    // deadlocking against ffmpeg's stream interleaving)
    if (_hasVideo)
        _videoThread = std::thread([this]() {
            ThreadHelper::setThreadName("ffmpeg-video");
            videoWriterMain();
        });
    if (_hasAudio)
        _audioThread = std::thread([this]() {
            ThreadHelper::setThreadName("ffmpeg-audio");
            audioWriterMain();
        });

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

    // Deadline: a stop with nothing queued completes in ~1 second (a slower graceful drain is not worth hanging the
    // UI). A stop with a BACKLOG - a software 4K encoder runs behind the emulation - waits while the queue keeps
    // shrinking (each step of progress buys 2 more seconds, 2 minutes at most): cutting the drain kills ffmpeg before
    // it finalizes and leaves a file without its index (an unplayable mkv / mp4)
    constexpr int STOP_TIMEOUT_MS = 1000;
    constexpr int PROGRESS_GRACE_MS = 2000;
    constexpr int DRAIN_CAP_MS = 120000;
    constexpr int FINALIZE_MS = 20000;  // ffmpeg flushes its encoder (x265 lookahead) after the pipe closes
    const auto stopStart = std::chrono::steady_clock::now();
    auto deadline = stopStart + std::chrono::milliseconds(STOP_TIMEOUT_MS);
    size_t lastQueued = 0;
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        lastQueued = _videoQueue.size();
    }

    auto msRemaining = [&]() {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        return left > 0 ? static_cast<int>(left) : 0;
    };

    // 1. Signal writers to drain. If they don't finish promptly, hard-cancel.
    _drainRequested = true;
    _queueCond.notify_all();

    // A thread that misses the deadline is cancelled and then joined for good: never detached. The member std::thread
    // stays joinable while a detached waiter is still inside join(), so the destructor of the encoder (or of a
    // restarted one) hit std::terminate, and the waiter kept references to this frame. killFfmpeg: the writers can sit
    // in a blocking pipe write the cancel flag does not reach; ffmpeg's death ends it with EPIPE. Only valid before
    // ffmpeg is reaped (a reaped pid may be reused).
    auto joinWithTimeout = [&](std::thread& t, bool killFfmpeg) {
        if (!t.joinable())
            return true;
        std::atomic<bool> done{false};
        std::thread waiter([&]() { t.join(); done = true; });
        while (!done && msRemaining() > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            size_t queued = 0;
            {
                std::lock_guard<std::mutex> lock(_queueMutex);
                queued = _videoQueue.size();
            }
            const auto now = std::chrono::steady_clock::now();
            if (queued < lastQueued && now - stopStart < std::chrono::milliseconds(DRAIN_CAP_MS))
                deadline = std::max(deadline, now + std::chrono::milliseconds(PROGRESS_GRACE_MS));
            lastQueued = queued;
        }
        if (done) {
            waiter.join();
            return true;
        }
        // Timed out — escalate: cancel, unblock a pending pipe write, wait for the thread to leave
        _cancelRequested = true;
        _queueCond.notify_all();
        if (killFfmpeg)
            signalFFmpeg(true);
        waiter.join();
        return false;
    };

    joinWithTimeout(_videoThread, true);
    joinWithTimeout(_audioThread, true);

    _isRecording = false;

    if (_segmented)
    {
        // The last segment ends, the sound file closes, and the segments are joined with the sound into the output
        const bool segmentDone = finishSegment(std::max(msRemaining(), FINALIZE_MS));
        if (_pcmFile)
        {
            std::fclose(_pcmFile);
            _pcmFile = nullptr;
        }
        // Joined even after a failure: the segments that finished are the recording
        const std::string segmentError = segmentDone ? std::string() : _lastError;
        muxSegments();
        if (!segmentError.empty())
            _lastError = segmentError;
        {
            std::lock_guard<std::mutex> lock(_processMutex);
            for (const std::string& file : _segmentFiles)
                std::remove(file.c_str());
            _segmentFiles.clear();
        }
        cleanup();
        return;
    }

    // 2. Close write ends → EOF to ffmpeg
    if (_videoPipe) _videoPipe->close();
    if (_audioPipe) _audioPipe->close();
    for (auto& p : _multiTrackAudioPipes) if (p) p->close();
    _multiTrackAudioPipes.clear();
    _ffmpegProcess.closeStdin();

    // 3. Wait for ffmpeg to finalize (remaining time budget)
    int exitCode = _ffmpegProcess.waitForFinished(std::max(msRemaining(), FINALIZE_MS));
    if (exitCode == -1)
    {
        _ffmpegProcess.kill();
        _ffmpegProcess.waitForFinished(100);
    }

    // 4. Close stderr to unblock the drain thread, then join briefly
    _ffmpegProcess.closeStderr();
    joinWithTimeout(_stderrThread, false);

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
    if (!_isRecording || !_hasVideo || _workerFailed)
        return;

    // Segments take the source picture at its own size; otherwise every frame is the file's size
    uint32_t expectedWidth = _width > 0 && !_segmented ? _width : framebuffer.width;
    uint32_t expectedHeight = _height > 0 && !_segmented ? _height : framebuffer.height;
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
    data.data = takeFrameBuffer(expectedSize);
    data.width = expectedWidth;
    data.height = expectedHeight;
    std::memcpy(data.data.data(), framebuffer.memoryBuffer, expectedSize);
    _videoQueue.push_back(std::move(data));

    _queueCond.notify_one();
}

std::vector<uint8_t> FFmpegPipeEncoder::takeFrameBuffer(size_t size)
{
    std::vector<uint8_t> buffer;
    if (!_framePool.empty())
    {
        buffer = std::move(_framePool.back());
        _framePool.pop_back();
    }
    if (buffer.size() != size)
        buffer.resize(size);
    return buffer;
}

void FFmpegPipeEncoder::recycleFrameBuffer(std::vector<uint8_t>&& buffer)
{
    if (_framePool.size() < kMaxQueueFrames + 2)
        _framePool.push_back(std::move(buffer));
}

FrameTargetResult FFmpegPipeEncoder::AcquireFrameTarget(uint32_t width, uint32_t height, FrameTarget& target)
{
    if (!_isRecording || !_hasVideo || _workerFailed)
        return FrameTargetResult::Dropped;
    if (_segmented || width != _width || height != _height || width == 0 || height == 0)
        return FrameTargetResult::Unsupported;

    std::unique_lock<std::mutex> lock(_queueMutex);
    if (_blocking)
    {
        // Non-realtime mode: the emulation waits for the encoder, so every frame is recorded
        _queueCond.wait(lock, [this]() {
            return _videoQueue.size() < MAX_QUEUE_FRAMES || _cancelRequested || _drainRequested || _workerFailed;
        });
    }
    if (_videoQueue.size() >= MAX_QUEUE_FRAMES || _cancelRequested || _drainRequested || _workerFailed)
        return FrameTargetResult::Dropped;

    _lentFrame = takeFrameBuffer(static_cast<size_t>(width) * height * 4);
    target.data = _lentFrame.data();
    target.stride = static_cast<size_t>(width) * 4;
    target.width = width;
    target.height = height;
    target.swapRedBlue = false;  // ffmpeg reads rgba: the emulator's own byte order
    target.handle = &_lentFrame;
    return FrameTargetResult::Ready;
}

void FFmpegPipeEncoder::SubmitFrameTarget(FrameTarget& target, double timestampSec)
{
    (void)timestampSec;
    if (target.handle != &_lentFrame)
        return;
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        FrameData data;
        data.data = std::move(_lentFrame);
        data.width = _width;
        data.height = _height;
        _videoQueue.push_back(std::move(data));
    }
    _queueCond.notify_one();
    target = FrameTarget();
}

void FFmpegPipeEncoder::ReleaseFrameTarget(FrameTarget& target)
{
    if (target.handle != &_lentFrame)
        return;
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        recycleFrameBuffer(std::move(_lentFrame));
    }
    target = FrameTarget();
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
        signalFFmpeg(false);
    }
    // Wake the other writer and any producer blocked on backpressure
    _queueCond.notify_all();
}

void FFmpegPipeEncoder::videoWriterMain()
{
    // Open video pipe (completes when ffmpeg connects its read end). Segments open theirs with their first picture
    _videoPipe = _segmented ? nullptr : std::make_unique<NamedPipe>();
    if (_videoPipe && !_videoPipe->openForWriteTimeout(_videoPipePath, 15000, &_cancelRequested))
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

        // A picture of another size (or the first one): the segment for that size
        if (_segmented && (frame.width != _segmentWidth || frame.height != _segmentHeight))
        {
            retireSegment();
            if (!startSegment(frame.width, frame.height))
            {
                failWorker(_lastError);
                return;
            }
        }

        if (!_videoPipe->write(frame.data.data(), frame.data.size(), &_cancelRequested))
        {
            failWorker("Failed to write video frame: " + _videoPipe->getLastError());
            return;
        }
        _framesEncoded++;

        // Space freed — the buffer goes back to the pool, producers blocked on backpressure wake
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            recycleFrameBuffer(std::move(frame.data));
        }
        _queueCond.notify_all();
    }
}

void FFmpegPipeEncoder::audioWriterMain()
{
    _audioPipe = _pcmFile ? nullptr : std::make_unique<NamedPipe>();
    if (_audioPipe && !_audioPipe->openForWriteTimeout(_audioPipePath, 15000, &_cancelRequested))
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

        if (_pcmFile ? std::fwrite(chunk.data.data(), 1, chunk.data.size(), _pcmFile) != chunk.data.size()
                     : !_audioPipe->write(chunk.data.data(), chunk.data.size(), &_cancelRequested))
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

namespace
{
uint64_t FileSize(const std::string& path)
{
#ifndef _WIN32
    struct stat st;
    if (stat(path.c_str(), &st) == 0)
        return static_cast<uint64_t>(st.st_size);
#else
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &attrs))
        return (static_cast<uint64_t>(attrs.nFileSizeHigh) << 32) | attrs.nFileSizeLow;
#endif
    return 0;
}
}  // namespace

uint64_t FFmpegPipeEncoder::GetOutputFileSize() const
{
    if (_outputFilename.empty())
        return 0;
    if (_segmented && _isRecording)
    {
        // While recording the video is in the segments (the output is written at Stop)
        std::lock_guard<std::mutex> lock(_processMutex);
        uint64_t bytes = 0;
        for (const std::string& file : _segmentFiles)
            bytes += FileSize(file);
        return bytes;
    }

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

namespace
{
#ifdef __linux__
/// True when this ffmpeg runs these arguments to the end (a trial encode into the null muxer)
bool FFmpegRuns(const std::string& ffmpeg, const std::vector<std::string>& args)
{
    Subprocess trial;
    if (!trial.spawn(ffmpeg, args))
        return false;
    trial.closeStdin();
    trial.readAllStderr();  // EOF when ffmpeg exits; -v error keeps it short
    const int exitCode = trial.waitForFinished(15000);
    if (exitCode == -1)
        trial.kill();
    return exitCode == 0;
}

/// The render node VA-API encodes on with this encoder, empty when none: every build lists h264_vaapi / hevc_vaapi,
/// whatever the card, and a laptop has an Intel and an NVIDIA node with the driver on one. A trial of a few frames
/// per node; cached per ffmpeg and encoder
std::string VaapiRenderNode(const std::string& ffmpeg, const std::string& encoder)
{
    static std::mutex cacheMutex;
    static std::map<std::string, std::string> cache;
    const std::string key = ffmpeg + "|" + encoder;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        const auto found = cache.find(key);
        if (found != cache.end())
            return found->second;
    }
    std::vector<std::string> nodes;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/dev/dri", ec))
    {
        const std::string name = entry.path().filename().string();
        if (name.rfind("renderD", 0) == 0)
            nodes.push_back(entry.path().string());
    }
    std::sort(nodes.begin(), nodes.end());
    std::string chosen;
    for (const std::string& node : nodes)
    {
        if (FFmpegRuns(ffmpeg, {"-v", "error", "-nostdin", "-vaapi_device", node, "-f", "lavfi", "-i",
                                "color=size=256x256:rate=10", "-frames:v", "3", "-vf", "format=nv12,hwupload", "-c:v",
                                encoder, "-f", "null", "-"}))
        {
            chosen = node;
            break;
        }
    }
    std::lock_guard<std::mutex> lock(cacheMutex);
    cache[key] = chosen;
    return chosen;
}
#endif

/// Whether this ffmpeg has the filter (pad_vaapi: ffmpeg 7.0 and later). Cached per ffmpeg and filter
bool FilterAvailable(const std::string& ffmpeg, const std::string& filter)
{
    static std::mutex cacheMutex;
    static std::map<std::string, bool> cache;
    const std::string key = ffmpeg + "|" + filter;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        const auto found = cache.find(key);
        if (found != cache.end())
            return found->second;
    }
    // The filter list on stdout: " ... pad_vaapi         V->V       Pad the input video."
    bool available = false;
    Subprocess list;
    if (list.spawn(ffmpeg, {"-hide_banner", "-filters"}))
    {
        list.closeStdin();
        available = list.readAllStdout().find(" " + filter + " ") != std::string::npos;
        if (list.waitForFinished(5000) == -1)
            list.kill();
    }
    std::lock_guard<std::mutex> lock(cacheMutex);
    cache[key] = available;
    return available;
}
}  // namespace

std::string FFmpegPipeEncoder::resolveVideoEncoder(const EncoderConfig& config, const std::string& container)
{
    std::string codec = config.videoCodec;
    _vaapiDevice.clear();

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
    if (!config.useHardwareAccel)
    {
        // Software only (libx264 / libx265): the same file without the GPU
        hwInfo.videoToolbox = false;
        hwInfo.nvenc = false;
        hwInfo.qsv = false;
        hwInfo.vaapi = false;
        hwInfo.amf = false;
    }

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
        {
            _vaapiDevice = VaapiRenderNode(_ffmpegPath, "h264_vaapi");
            if (!_vaapiDevice.empty())
                return "h264_vaapi";
        }
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
        {
            _vaapiDevice = VaapiRenderNode(_ffmpegPath, "hevc_vaapi");
            if (!_vaapiDevice.empty())
                return "hevc_vaapi";
        }
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

std::string FFmpegPipeEncoder::getFFmpegPipePath(const std::string& pipeName) const
{
#ifdef _WIN32
    // Windows: ffmpeg needs full \\.\pipe\name path
    return "\\\\.\\pipe\\" + pipeName;
#else
    // Unix: pipe path is already the full filesystem path
    return pipeName;
#endif
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

    // Let ffmpeg use all cores for filter graph processing (pixel format
    // conversion, scaling). The actual video encoder thread count is set
    // per-codec below — most single-threaded encoders (apng, gif) ignore
    // this, but the filter pipeline benefits.
    args.push_back("-threads");
    args.push_back("0");

    int inputIndex = 0;
    int videoInputIndex = -1;
    int audioInputIndex = -1;

    // --- Video input (skip for audio-only) ---
    if (_hasVideo)
    {
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
        args.push_back(getFFmpegPipePath(_videoPipePath));
        videoInputIndex = inputIndex++;
    }

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
        args.push_back(getFFmpegPipePath(_audioPipePath));
        audioInputIndex = inputIndex++;
    }

    // --- Stream mapping ---
    if (_hasVideo)
    {
        args.push_back("-map");
        args.push_back(std::to_string(videoInputIndex) + ":v");
    }
    if (_hasAudio)
    {
        args.push_back("-map");
        args.push_back(std::to_string(audioInputIndex) + ":a");
    }

    // --- Video encoder (skip for audio-only) ---
    if (_hasVideo)
    {
        std::string videoEncoder = resolveVideoEncoder(config, ext);
        if (!_vaapiDevice.empty())
        {
            args.push_back("-vaapi_device");  // a global option: before the output it applies to
            args.push_back(_vaapiDevice);
        }
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
            vf += "scale=in_range=full:out_range=limited:out_color_matrix=bt709,";
            // VA-API encodes GPU surfaces: the 4:2:0 frame uploaded as NV12 to the render node the trial found
            vf += _vaapiDevice.empty() ? "format=yuv420p" : "format=nv12,hwupload";
        }

        if (!vf.empty())
        {
            args.push_back("-vf");
            args.push_back(vf);
        }
        appendVideoCodecArgs(args, config, videoEncoder, ext);
    }

    // --- Audio encoder ---
    if (_hasAudio)
        appendAudioCodecArgs(args, config, ext);

    appendOutputArgs(args, ext, filename);
    return args;
}

void FFmpegPipeEncoder::appendVideoCodecArgs(std::vector<std::string>& args, const EncoderConfig& config,
                                             const std::string& videoEncoder, const std::string& ext) const
{
    {
        const bool rgbFormat = (videoEncoder == "gif" || videoEncoder == "apng" ||
                                videoEncoder.rfind("libwebp", 0) == 0);
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

        // APNG-specific options
        if (videoEncoder == "apng")
        {
            args.push_back("-plays");
            args.push_back("0");  // Loop forever

            std::string apngPred;
            if (config.qualityPreset >= 7)
                apngPred = "mixed";
            else if (config.qualityPreset >= 4)
                apngPred = "up";
            else
                apngPred = "none";

            args.push_back("-pred");
            args.push_back(apngPred);
        }

        // WebP-specific options
        if (videoEncoder == "libwebp_anim")
        {
            args.push_back("-loop");
            args.push_back("0");      // Loop forever
            args.push_back("-lossless");
            args.push_back("1");      // Pixel-perfect for ZX content
        }
    }
}

void FFmpegPipeEncoder::appendAudioCodecArgs(std::vector<std::string>& args, const EncoderConfig& config,
                                             const std::string& ext)
{
    {
        std::string audioEncoder = resolveAudioEncoder(config, ext);
        args.push_back("-c:a");
        args.push_back(audioEncoder);

        if (config.audioBitrate > 0 && audioEncoder != "pcm_s16le" && audioEncoder != "flac")
        {
            args.push_back("-b:a");
            args.push_back(std::to_string(config.audioBitrate) + "k");
        }

        // Output audio rate policy (encoder_config.h): audio-only output
        // stays at the core's native rate (bit-exact, no resampling); video
        // containers always carry 48 kHz - the delivery-standard rate.
        // Opus additionally REQUIRES 48/24/16/12/8 kHz regardless of policy.
        uint32_t outputAudioRate = config.audioOutputSampleRate;
        if (outputAudioRate == 0 && _hasVideo)
            outputAudioRate = 48000;
        if (audioEncoder == "libopus" || audioEncoder == "opus")
            outputAudioRate = 48000;

        if (outputAudioRate != 0 && outputAudioRate != config.audioSampleRate)
        {
            // High-quality resample: SoX (soxr, 28-bit precision) when this
            // ffmpeg build has it; swresample otherwise (always built in,
            // transparent quality). Both run realtime-easily at these rates.
            if (FFmpegProbe::isSoxrAvailable(_ffmpegPath))
            {
                args.push_back("-af");
                args.push_back("aresample=resampler=soxr:precision=28:osr=" +
                               std::to_string(outputAudioRate));
            }
            else
            {
                args.push_back("-ar:a");
                args.push_back(std::to_string(outputAudioRate));
            }
        }

        // ffmpeg's built-in opus/vorbis encoders are experimental and refuse
        // to run without explicit opt-in (used only when lib* are absent)
        if (audioEncoder == "opus" || audioEncoder == "vorbis")
        {
            args.push_back("-strict");
            args.push_back("-2");
        }
    }

}

void FFmpegPipeEncoder::appendOutputArgs(std::vector<std::string>& args, const std::string& ext,
                                         const std::string& filename)
{
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

    // --- Output file ---
    args.push_back(filename);
}

// ============================================================================
// Cleanup
// ============================================================================

void FFmpegPipeEncoder::cleanup()
{
    if (_pcmFile)
    {
        std::fclose(_pcmFile);
        _pcmFile = nullptr;
    }
    if (!_pcmPath.empty())
    {
        std::remove(_pcmPath.c_str());
        _pcmPath.clear();
    }

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

// ============================================================================
// Segments: the fixed frame built by ffmpeg from the source picture
// ============================================================================

namespace
{
std::string TempFile(const std::string& dir, const std::string& name)
{
#ifndef _WIN32
    return dir + "/" + name;
#else
    return dir + "\\" + name;
#endif
}

/// A remap table of the picture, gray16le: per pixel the source column (xAxis) or row of FrameScaler's nearest mapping
/// (output pixel i of the picture takes source pixel i * src / picture). The bars are pad's, outside the table
bool WriteMap(const std::string& path, const FrameScaler::Layout& layout, uint32_t source, bool xAxis)
{
    const uint32_t picture = xAxis ? layout.width : layout.height;
    std::vector<uint16_t> axis(picture);
    for (uint32_t i = 0; i < picture; ++i)
        axis[i] = static_cast<uint16_t>(static_cast<uint64_t>(i) * source / picture);

    std::vector<uint8_t> row(static_cast<size_t>(layout.width) * 2);
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file)
        return false;
    bool ok = true;
    for (uint32_t y = 0; y < layout.height && ok; ++y)
    {
        for (uint32_t x = 0; x < layout.width; ++x)
        {
            const uint16_t value = xAxis ? axis[x] : axis[y];
            row[2 * static_cast<size_t>(x)] = static_cast<uint8_t>(value & 0xFF);  // little-endian
            row[2 * static_cast<size_t>(x) + 1] = static_cast<uint8_t>(value >> 8);
        }
        ok = std::fwrite(row.data(), 1, row.size(), file) == row.size();
    }
    return std::fclose(file) == 0 && ok;
}

bool WriteFile(const std::string& path, const std::vector<uint8_t>& bytes)
{
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file)
        return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    return std::fclose(file) == 0 && ok;
}

std::vector<uint8_t> ReadFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

}  // namespace

/// swscale's nearest (`flags=neighbor`) against FrameScaler's i * src / picture. When they agree the frame needs no
/// remap tables: `remap` costs ~38 ms of CPU per 4K frame, `neighbor` next to nothing. swscale rounds its own way
/// (exact at 7.5x: 352x288 into 4K, 256x192 into 1440p; off by one pixel at 3.75x, 4.5x, 5.625x, 11.25x; ffmpeg 8.0
/// and 9.0), so it is asked rather than assumed: two thin pictures, every plane holding the low byte of its column
/// (row) index (a nearest mapping is monotonic, any wrong pixel is a neighbour with another low byte)
bool FFmpegPipeEncoder::NeighborIsFrameScaler(const std::string& ffmpeg, const std::string& dir, uint32_t width,
                                              uint32_t height, uint32_t pictureWidth, uint32_t pictureHeight)
{
    static std::mutex cacheMutex;
    static std::map<std::string, bool> cache;
    const std::string key = ffmpeg + "|" + std::to_string(width) + "x" + std::to_string(height) + "|" +
                            std::to_string(pictureWidth) + "x" + std::to_string(pictureHeight);
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        const auto found = cache.find(key);
        if (found != cache.end())
            return found->second;
    }

    // yuv444p, 3 planes: columns as a width x 2 picture, rows as a 2 x height one
    std::vector<uint8_t> columns(static_cast<size_t>(width) * 2 * 3);
    for (size_t i = 0; i < columns.size(); ++i)
        columns[i] = static_cast<uint8_t>(i % width);
    std::vector<uint8_t> rows(static_cast<size_t>(height) * 2 * 3);
    for (size_t i = 0; i < rows.size(); ++i)
        rows[i] = static_cast<uint8_t>((i / 2) % height);
    const std::string columnsIn = TempFile(dir, "probe-x.yuv");
    const std::string rowsIn = TempFile(dir, "probe-y.yuv");
    const std::string columnsOut = TempFile(dir, "probe-x-out.yuv");
    const std::string rowsOut = TempFile(dir, "probe-y-out.yuv");
    bool exact = false;
    if (WriteFile(columnsIn, columns) && WriteFile(rowsIn, rows))
    {
        const std::string pw = std::to_string(pictureWidth);
        const std::string ph = std::to_string(pictureHeight);
        Subprocess probe;
        const std::vector<std::string> args = {
            "-y", "-nostdin", "-v", "error",
            "-f", "rawvideo", "-pix_fmt", "yuv444p", "-s", std::to_string(width) + "x2", "-i", columnsIn,
            "-f", "rawvideo", "-pix_fmt", "yuv444p", "-s", "2x" + std::to_string(height), "-i", rowsIn,
            "-filter_complex", "[0:v]scale=" + pw + ":2:flags=neighbor[x];[1:v]scale=2:" + ph + ":flags=neighbor[y]",
            "-map", "[x]", "-f", "rawvideo", columnsOut, "-map", "[y]", "-f", "rawvideo", rowsOut};
        if (probe.spawn(ffmpeg, args))
        {
            probe.closeStdin();
            probe.readAllStderr();  // EOF when ffmpeg exits
            if (probe.waitForFinished(10000) == 0)
            {
                const std::vector<uint8_t> x = ReadFile(columnsOut);
                const std::vector<uint8_t> y = ReadFile(rowsOut);
                exact = x.size() == static_cast<size_t>(pictureWidth) * 2 * 3 &&
                        y.size() == static_cast<size_t>(pictureHeight) * 2 * 3;
                for (size_t i = 0; exact && i < x.size(); ++i)
                    exact = x[i] == static_cast<uint8_t>((i % pictureWidth) * width / pictureWidth);
                for (size_t i = 0; exact && i < y.size(); ++i)
                    exact = y[i] == static_cast<uint8_t>((i / 2 % pictureHeight) * height / pictureHeight);
            }
            else
                probe.kill();
        }
    }
    for (const std::string* file : {&columnsIn, &rowsIn, &columnsOut, &rowsOut})
        std::remove(file->c_str());

    std::lock_guard<std::mutex> lock(cacheMutex);
    cache[key] = exact;
    return exact;
}

std::vector<std::string> FFmpegPipeEncoder::buildSegmentArgs(uint32_t width, uint32_t height,
                                                             const std::string& segmentFile, const Segment& segment)
{
    const FrameScaler::Layout layout = FrameScaler::ComputeLayout(width, height, _width, _height);
    std::vector<std::string> args = {"-y", "-nostats", "-loglevel", "warning", "-threads", "0",
                                     "-probesize", "32", "-analyzeduration", "0", "-f", "rawvideo",
                                     "-pix_fmt", "rgba", "-s", std::to_string(width) + "x" + std::to_string(height),
                                     "-r", std::to_string(static_cast<int>(std::round(_fps))),
                                     "-i", getFFmpegPipePath(segment.pipePath)};

    // The colour first, at the source size: converting a pixel and repeating it is repeating the converted pixel, so
    // the 4:4:4 picture is byte for byte the conversion of FrameScaler's frame. 4:2:0 next: the area average of the
    // chroma, 0.06-0.10 dB above swscale's conversion of the RGB frame (POC 054). The bars last when the picture sits
    // on whole chroma blocks (even size and offsets): the same bytes as 4:2:0 over the padded frame, 17 % less CPU
    // (4K, Linux), a third fewer pixels in the 4:2:0 step and a 4:2:0 pad. VA-API: the frame uploaded as NV12, the
    // bars added on the GPU (pad_vaapi, ffmpeg 7.0 and later), 25 % less CPU than the 4K frame uploaded
    const std::string convert = "scale=in_range=full:out_range=limited:out_color_matrix=bt709,format=yuv444p";
    const std::string pad = "pad=" + std::to_string(_width) + ":" + std::to_string(_height) + ":" +
                            std::to_string(layout.offsetX) + ":" + std::to_string(layout.offsetY) + ":black";
    const bool vaapi = !_vaapiDevice.empty();
    const bool barsLast = layout.width % 2 == 0 && layout.height % 2 == 0 && layout.offsetX % 2 == 0 &&
                          layout.offsetY % 2 == 0;
    const std::string format420 = vaapi ? "format=nv12" : "format=yuv420p";
    const std::string upload = vaapi ? ",hwupload" : "";
    std::string tail;
    if (!barsLast)
    {
        tail = pad + ",scale=" + std::to_string(_width) + ":" + std::to_string(_height) + ":flags=area," + format420 +
               upload;
    }
    else
    {
        tail = "scale=" + std::to_string(layout.width) + ":" + std::to_string(layout.height) + ":flags=area,";
        if (vaapi && FilterAvailable(_ffmpegPath, "pad_vaapi"))
            tail += "format=nv12,hwupload,pad_vaapi=w=" + std::to_string(_width) + ":h=" + std::to_string(_height) +
                    ":x=" + std::to_string(layout.offsetX) + ":y=" + std::to_string(layout.offsetY);
        else
            tail += "format=yuv420p," + pad + (vaapi ? ",format=nv12,hwupload" : "");
    }
    if (vaapi)
        args.insert(args.begin(), {"-vaapi_device", _vaapiDevice});

    if (segment.neighbor)
    {
        // A whole factor, or swscale's nearest is FrameScaler's mapping (NeighborIsFrameScaler)
        args.push_back("-vf");
        args.push_back(convert + ",scale=" + std::to_string(layout.width) + ":" + std::to_string(layout.height) +
                       ":flags=neighbor," + tail);
    }
    else
    {
        // Any other factor: swscale's nearest puts the k / k+1 boundaries elsewhere; FrameScaler's own mapping goes in
        // as remap tables of the picture (startSegment writes them), read once and repeated
        const std::string pictureSize = std::to_string(layout.width) + "x" + std::to_string(layout.height);
        for (const std::string* map : {&segment.xmapPath, &segment.ymapPath})
        {
            for (const char* word : {"-f", "rawvideo", "-pix_fmt", "gray16le", "-s"})
                args.push_back(word);
            args.push_back(pictureSize);
            args.push_back("-i");
            args.push_back(*map);
        }
        args.push_back("-filter_complex");
        args.push_back("[0:v]" + convert + "[s];[1:v]loop=loop=-1:size=1[x];[2:v]loop=loop=-1:size=1[y];" +
                       "[s][x][y]remap," + tail + "[v]");
        args.push_back("-map");
        args.push_back("[v]");
    }
    args.push_back("-c:v");
    args.push_back(_videoEncoderName);
    appendVideoCodecArgs(args, _config, _videoEncoderName, "mkv");  // the hvc1 tag is the output's (muxSegments)
    for (const char* word : {"-an", "-f", "matroska"})
        args.push_back(word);
    args.push_back(segmentFile);
    return args;
}

bool FFmpegPipeEncoder::startSegment(uint32_t width, uint32_t height)
{
    const FrameScaler::Layout layout = FrameScaler::ComputeLayout(width, height, _width, _height);
    if (layout.width == 0 || layout.height == 0)
    {
        _lastError = "No picture to record";
        return false;
    }

    const std::string index = std::to_string(_segmentFiles.size());
    auto segment = std::make_unique<Segment>();
    segment->neighbor =
        layout.scale > 0 || NeighborIsFrameScaler(_ffmpegPath, _tempDir, width, height, layout.width, layout.height);
    if (!segment->neighbor)
    {
        // Per segment: a retired segment's ffmpeg may still be reading its own tables
        segment->xmapPath = TempFile(_tempDir, "xmap-" + index + ".raw");
        segment->ymapPath = TempFile(_tempDir, "ymap-" + index + ".raw");
        if (!WriteMap(segment->xmapPath, layout, width, true) || !WriteMap(segment->ymapPath, layout, height, false))
        {
            _lastError = "Failed to write the scaling tables in " + _tempDir;
            std::remove(segment->xmapPath.c_str());
            std::remove(segment->ymapPath.c_str());
            return false;
        }
    }
#ifndef _WIN32
    segment->pipePath = TempFile(_tempDir, "video-" + index + ".fifo");
#else
    char suffix[48];
    snprintf(suffix, sizeof(suffix), "_%p_", static_cast<void*>(this));
    segment->pipePath = std::string("unreal_video") + suffix + index;
#endif
    _videoPipePath = segment->pipePath;
    const std::string segmentFile = TempFile(_tempDir, "segment-" + index + ".mkv");
    if (!NamedPipe::create(segment->pipePath))
    {
        _lastError = "Failed to create video pipe";
        return false;
    }

    segment->process = std::make_unique<Subprocess>();
    if (!segment->process->spawn(_ffmpegPath, buildSegmentArgs(width, height, segmentFile, *segment)))
    {
        _lastError = "Failed to spawn ffmpeg: " + segment->process->getLastError();
        NamedPipe::remove(segment->pipePath);
        std::remove(segment->xmapPath.c_str());
        std::remove(segment->ymapPath.c_str());
        return false;
    }
    // Without a reader the stderr pipe fills and ffmpeg blocks (see _stderrThread)
    Subprocess* running = segment->process.get();
    segment->stderrThread = std::thread([this, running]() {
        ThreadHelper::setThreadName("ffmpeg-stderr");
        const std::string text = running->readAllStderr();
        std::lock_guard<std::mutex> lock(_diagnosticsMutex);
        _diagnostics += text;
    });
    {
        std::lock_guard<std::mutex> lock(_processMutex);
        _segment = std::move(segment);
        _segmentFiles.push_back(segmentFile);
    }

    _videoPipe = std::make_unique<NamedPipe>();
    if (!_videoPipe->openForWriteTimeout(_videoPipePath, 15000, &_cancelRequested))
    {
        _lastError = "Failed to open video pipe: " + _videoPipe->getLastError();
        return false;
    }
    _segmentWidth = width;
    _segmentHeight = height;
    return true;
}

int FFmpegPipeEncoder::closeSegment(Segment& segment, int timeoutMs)
{
    if (segment.pipe)
        segment.pipe->close();  // EOF: ffmpeg flushes the encoder and closes the segment
    segment.process->closeStdin();
    int exitCode = segment.process->waitForFinished(timeoutMs);
    if (exitCode == -1)
    {
        segment.process->kill();
        segment.process->waitForFinished(100);
    }
    segment.process->closeStderr();
    if (segment.stderrThread.joinable())
        segment.stderrThread.join();
    segment.pipe.reset();
    NamedPipe::remove(segment.pipePath);
    if (!segment.xmapPath.empty())
        std::remove(segment.xmapPath.c_str());
    if (!segment.ymapPath.empty())
        std::remove(segment.ymapPath.c_str());
    return exitCode;
}

void FFmpegPipeEncoder::retireSegment()
{
    Segment* retiring = nullptr;
    {
        std::lock_guard<std::mutex> lock(_processMutex);
        if (!_segment)
            return;
        _segment->pipe = std::move(_videoPipe);
        retiring = _segment.get();
        _retired.push_back(std::move(_segment));  // stays alive (and signalled by signalFFmpeg) until Stop
    }
    _videoPipePath.clear();
    _segmentWidth = _segmentHeight = 0;
    _retireThreads.emplace_back([retiring]() {
        ThreadHelper::setThreadName("ffmpeg-segment-end");
        retiring->exitCode = closeSegment(*retiring, 20000);
    });
}

bool FFmpegPipeEncoder::finishSegment(int timeoutMs)
{
    retireSegment();
    // The last one has the whole timeout; the earlier ones had theirs since they were retired
    for (std::thread& thread : _retireThreads)
        if (thread.joinable())
            thread.join();
    _retireThreads.clear();
    (void)timeoutMs;

    bool ok = true;
    std::lock_guard<std::mutex> lock(_processMutex);
    for (const std::unique_ptr<Segment>& segment : _retired)
    {
        if (segment->exitCode != 0 && ok)
        {
            ok = false;
            _lastError = segment->exitCode == -1 ? "ffmpeg did not finish a video segment in time"
                                                 : "FFmpeg exited with code " + std::to_string(segment->exitCode);
        }
    }
    _retired.clear();
    return ok;
}

bool FFmpegPipeEncoder::muxSegments()
{
    const bool sound = _hasAudio && !_pcmPath.empty() && FileSize(_pcmPath) > 0;
    if (_segmentFiles.empty() && !sound)
        return true;

    std::vector<std::string> args = {"-y", "-nostats", "-loglevel", "warning"};
    const std::string list = TempFile(_tempDir, "segments.ffconcat");
    if (!_segmentFiles.empty())
    {
        // The segments one after another: each one's times follow the previous one's end
        std::FILE* file = std::fopen(list.c_str(), "wb");
        if (!file)
        {
            _lastError = "Failed to write " + list;
            return false;
        }
        std::fputs("ffconcat version 1.0\n", file);
        for (const std::string& segment : _segmentFiles)
            std::fprintf(file, "file '%s'\n", segment.c_str());
        std::fclose(file);
        for (const char* word : {"-f", "concat", "-safe", "0", "-i"})
            args.push_back(word);
        args.push_back(list);
    }
    if (sound)
    {
        for (const char* word : {"-f", "s16le", "-ar"})
            args.push_back(word);
        args.push_back(std::to_string(_config.audioSampleRate));
        args.push_back("-ac");
        args.push_back(std::to_string(_config.audioChannels));
        args.push_back("-i");
        args.push_back(_pcmPath);
    }
    int input = 0;
    if (!_segmentFiles.empty())
    {
        for (const char* word : {"-map", "0:v", "-c:v", "copy"})
            args.push_back(word);
        const bool hevc = _videoEncoderName.find("265") != std::string::npos ||
                          _videoEncoderName.find("hevc") != std::string::npos;
        if (hevc && (_container == "mp4" || _container == "mov"))
        {
            args.push_back("-tag:v");
            args.push_back("hvc1");
        }
        ++input;
    }
    if (sound)
    {
        args.push_back("-map");
        args.push_back(std::to_string(input) + ":a");
        appendAudioCodecArgs(args, _config, _container);
    }
    appendOutputArgs(args, _container, _outputFilename);

    Subprocess mux;
    if (!mux.spawn(_ffmpegPath, args))
    {
        _lastError = "Failed to spawn ffmpeg: " + mux.getLastError();
        std::remove(list.c_str());
        return false;
    }
    std::string text;
    std::thread reader([&]() { text = mux.readAllStderr(); });
    mux.closeStdin();
    // Stream copy of the video, the sound encoded: seconds for a long recording
    constexpr int kMuxTimeoutMs = 10 * 60 * 1000;
    int exitCode = mux.waitForFinished(kMuxTimeoutMs);
    if (exitCode == -1)
    {
        mux.kill();
        mux.waitForFinished(100);
    }
    mux.closeStderr();
    reader.join();
    {
        std::lock_guard<std::mutex> lock(_diagnosticsMutex);
        _diagnostics += text;
    }
    std::remove(list.c_str());
    if (exitCode != 0)
    {
        _lastError = exitCode == -1 ? "ffmpeg did not join the segments in time"
                                    : "FFmpeg exited with code " + std::to_string(exitCode) + " joining the segments";
        return false;
    }
    return true;
}

void FFmpegPipeEncoder::signalFFmpeg(bool kill)
{
    if (!_segmented)
    {
        if (kill)
            _ffmpegProcess.kill();
        else
            _ffmpegProcess.terminate();
        return;
    }
    std::lock_guard<std::mutex> lock(_processMutex);
    auto signal = [kill](Segment& segment) {
        if (kill)
            segment.process->kill();
        else
            segment.process->terminate();
    };
    if (_segment)
        signal(*_segment);
    for (const std::unique_ptr<Segment>& segment : _retired)
        signal(*segment);
}
