#include "recordingmanager.h"

#include "3rdparty/message-center/messagecenter.h"
#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"
#include "encoders/gif_encoder.h"
#include "encoders/ffmpeg_pipe_encoder.h"
#include "platform_encoder.h"
#include "ffmpeg_probe.h"
#include "realtime_estimator.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>

/// region <Helper functions>

static const char* GetRecordingModeString(RecordingMode mode)
{
    switch (mode)
    {
        case RecordingMode::SingleTrack:
            return "SingleTrack";
        case RecordingMode::MultiTrack:
            return "MultiTrack";
        case RecordingMode::ChannelSplit:
            return "ChannelSplit";
        case RecordingMode::AudioOnly:
            return "AudioOnly";
        default:
            return "Unknown";
    }
}

static const char* GetAudioSourceName(AudioSourceType source)
{
    switch (source)
    {
        case AudioSourceType::MasterMix:
            return "MasterMix";
        case AudioSourceType::Beeper:
            return "Beeper";
        case AudioSourceType::AY1_All:
            return "AY1_All";
        case AudioSourceType::AY2_All:
            return "AY2_All";
        case AudioSourceType::AY3_All:
            return "AY3_All";
        case AudioSourceType::COVOX:
            return "COVOX";
        case AudioSourceType::GeneralSound:
            return "GeneralSound";
        case AudioSourceType::Moonsound:
            return "Moonsound";
        case AudioSourceType::AY1_ChannelA:
            return "AY1_ChannelA";
        case AudioSourceType::AY1_ChannelB:
            return "AY1_ChannelB";
        case AudioSourceType::AY1_ChannelC:
            return "AY1_ChannelC";
        case AudioSourceType::AY2_ChannelA:
            return "AY2_ChannelA";
        case AudioSourceType::AY2_ChannelB:
            return "AY2_ChannelB";
        case AudioSourceType::AY2_ChannelC:
            return "AY2_ChannelC";
        case AudioSourceType::AY3_ChannelA:
            return "AY3_ChannelA";
        case AudioSourceType::AY3_ChannelB:
            return "AY3_ChannelB";
        case AudioSourceType::AY3_ChannelC:
            return "AY3_ChannelC";
        case AudioSourceType::Custom:
            return "Custom";
        default:
            return "Unknown";
    }
}

/// @brief Expand a leading '~' and make sure the parent directory exists.
/// Recording always creates a NEW file — a missing directory must not fail
/// the recording with a confusing "no such file" error.
/// @return Normalized path, or empty string (with errorOut set) on failure
static std::string PrepareOutputPath(const std::string& path, std::string& errorOut)
{
    std::string result = path;

    // Expand "~/..." (fopen/AVFoundation do not expand tilde)
    // On Windows, also check for backslash after tilde
    if (!result.empty() && result[0] == '~' && (result.size() == 1 || result[1] == '/' || result[1] == '\\'))
    {
        const char* home = std::getenv("HOME");
#ifdef _WIN32
        // Windows uses USERPROFILE or HOMEPATH instead of HOME
        if (!home)
            home = std::getenv("USERPROFILE");
#endif
        if (home)
            result = std::string(home) + result.substr(1);
    }

    // Create parent directories recursively
    std::error_code ec;
    std::filesystem::path parent = std::filesystem::path(result).parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, ec);
        if (ec)
        {
            errorOut = "Cannot create output directory '" + parent.string() + "': " + ec.message();
            return {};
        }
    }

    return result;
}

/// endregion </Helper functions>

RecordingManager::RecordingManager(EmulatorContext* context) : _context(context)
{
    // Initialize logger from context
    _logger = context->pModuleLogger;

    MLOGINFO("RecordingManager::RecordingManager - Instance created");
}

RecordingManager::~RecordingManager()
{
    // Unsubscribe from message center
    if (_isSubscribed)
    {
        MessageCenter& mc = MessageCenter::DefaultMessageCenter();
        Observer* obs = static_cast<Observer*>(this);
        ObserverCallbackMethod callback = static_cast<ObserverCallbackMethod>(&RecordingManager::onEmulatorStateChange);
        mc.RemoveObserver(NC_EMULATOR_STATE_CHANGE, obs, callback);
        _isSubscribed = false;
    }

    // Stop recording if still active
    if (_isRecording)
    {
        StopRecording();
    }

    MLOGINFO("RecordingManager::~RecordingManager - Instance destroyed");
}

/// region <Initialization>

void RecordingManager::Init()
{
    MLOGINFO("RecordingManager::Init - Initializing recording manager");

    // Subscribe to emulator state changes to auto-stop on shutdown
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    Observer* obs = static_cast<Observer*>(this);
    ObserverCallbackMethod callback = static_cast<ObserverCallbackMethod>(&RecordingManager::onEmulatorStateChange);
    mc.AddObserver(NC_EMULATOR_STATE_CHANGE, obs, callback);
    _isSubscribed = true;

    // Reset state
    Reset();
}

void RecordingManager::onEmulatorStateChange(int /*id*/, Message* message)
{
    if (auto* payload = dynamic_cast<SimpleNumberPayload*>(message->obj))
    {
        // Only auto-stop recording when the emulator is being DESTROYED, not on regular stop/reset
        if (payload->_payloadNumber == StateStopped && _isRecording)
        {
            // NC_EMULATOR_STATE_CHANGE is a global broadcast — every emulator
            // instance posts it and every RecordingManager receives it.
            // The payload carries only the state number, NOT the emulator ID.
            // We must verify this is OUR emulator being destroyed before stopping.
            bool ourDestroying = false;
            if (_context && _context->pEmulator)
            {
                // Only stop if emulator is being destroyed, not on regular stop/reset
                ourDestroying = (_context->pEmulator->GetState() == StateDestroying ||
                                 _context->pEmulator->IsDestroying());
            }
            else
            {
                // Context already gone — emulator was destroyed
                ourDestroying = true;
            }

            if (ourDestroying)
            {
                MLOGINFO("RecordingManager: Emulator destroyed, auto-stopping recording");
                StopRecording();
            }
        }
    }
}

void RecordingManager::Reset()
{
    MLOGDEBUG("RecordingManager::Reset - Resetting recording manager");

    // Do NOT stop recording on reset - user may want to record across resets
    // Recording only auto-stops when the emulator is destroyed

    // Reset counters (these track total frames/samples recorded, not per-reset)
    // Note: We could optionally preserve these across resets if needed
    _emulatedFrameCount = 0;
    _emulatedAudioSampleCount = 0;

    // Reset statistics
    _stats = RecordingStats();
}

void RecordingManager::UpdateFeatureCache()
{
    if (_context && _context->pFeatureManager)
    {
        _featureEnabled = _context->pFeatureManager->isEnabled(Features::kRecording);
        MLOGDEBUG("RecordingManager::UpdateFeatureCache - recording feature = %s", _featureEnabled ? "ON" : "OFF");
    }
}

/// endregion </Initialization>

/// region <Recording control>

bool RecordingManager::StartRecording(const std::string& filename, const std::string& videoCodec,
                                      const std::string& audioCodec, uint32_t videoBitrate, uint32_t audioBitrate)
{
    // Feature guard - early exit if recording disabled
    if (!_featureEnabled)
    {
        MLOGWARNING("RecordingManager::StartRecording - Recording disabled (feature 'recording' = off)");
        _lastRecordingError = "Recording feature is disabled. Enable the 'recording' feature first.";
        return false;
    }

    if (_isRecording)
    {
        MLOGWARNING("RecordingManager::StartRecording - Already recording, stop current recording first");
        _lastRecordingError = "Already recording. Stop the current recording first.";
        return false;
    }

    MLOGINFO("RecordingManager::StartRecording - Starting recording to '%s'", filename.c_str());

    // Determine if video-only or audio-only based on codec parameters
    _videoEnabled = !videoCodec.empty();
    _videoCodec = videoCodec;
    _audioCodec = audioCodec;
    _videoBitrate = videoBitrate;
    _audioBitrate = audioBitrate;

    // Disable audio channels when no audio codec is specified
    if (audioCodec.empty())
    {
        _audioChannels = 0;
        _audioTracks.clear();
    }
    else if (_audioChannels == 0)
    {
        _audioChannels = 2; // Restore default if it was cleared from a previous run
    }

    MLOGINFO("  Mode: %s", _videoEnabled ? "Video+Audio" : "Audio-Only");
    if (_videoEnabled)
    {
        MLOGINFO("  Video: codec=%s, bitrate=%u kbps", videoCodec.c_str(), videoBitrate);
    }
    MLOGINFO("  Audio: codec=%s, bitrate=%u kbps", audioCodec.c_str(), audioBitrate);

    // Store configuration (normalized; ensures the destination directory exists)
    _outputFilename = PrepareOutputPath(filename, _lastRecordingError);
    if (_outputFilename.empty())
    {
        MLOGERROR("RecordingManager::StartRecording - %s", _lastRecordingError.c_str());
        return false;
    }

    // Use capture-region dimensions if not explicitly set
    if (_videoEnabled && (_videoWidth == 0 || _videoHeight == 0))
    {
        FramebufferDescriptor fb = _context->pScreen->GetFramebufferDescriptor();
        _videoWidth = fb.width;
        _videoHeight = fb.height;

        if (_captureRegion == VideoCaptureRegion::MainScreen)
        {
            const RasterDescriptor& rd = _context->pScreen->rasterDescriptors[fb.videoMode];
            if (rd.screenWidth > 0 && rd.screenHeight > 0)
            {
                _videoWidth = rd.screenWidth;
                _videoHeight = rd.screenHeight;
            }
        }
    }

    if (_videoEnabled)
    {
        MLOGINFO("  Resolution: %ux%u @ %.2f fps (%s, scale %ux)", _videoWidth, _videoHeight, _videoFrameRate,
                 _captureRegion == VideoCaptureRegion::MainScreen ? "screen only" : "full frame", _scaleFactor);
    }
    MLOGINFO("  Audio: %u Hz, %u channels", _audioSampleRate, _audioChannels);

    // Setup default single-track configuration if audio is enabled and no tracks configured
    if (!audioCodec.empty() && _audioTracks.empty())
    {
        AudioTrackConfig defaultTrack;
        defaultTrack.name = "Master Audio";
        defaultTrack.source = _selectedSource;
        defaultTrack.codec = audioCodec;
        defaultTrack.bitrate = audioBitrate > 0 ? audioBitrate : 192;
        _audioTracks.push_back(defaultTrack);
    }

    MLOGINFO("  Audio tracks: %zu", _audioTracks.size());

    // Serialize encoder (re)creation against the capture path
    std::lock_guard<std::mutex> lock(_captureMutex);

    // Initialize encoder
    if (!InitializeEncoder())
    {
        MLOGERROR("RecordingManager::StartRecording - Failed to initialize encoder: %s", _lastRecordingError.c_str());
        return false;
    }

    // Reset counters
    _emulatedFrameCount = 0;
    _emulatedAudioSampleCount = 0;
    _stats = RecordingStats();
    _frameTimestamps.clear();
    _frameTimestampIndex = 0;
    _wallClockStart = Clock::now();
    _wallClockPausedTotal = Clock::duration::zero();

    // Start recording
    _isRecording = true;
    _isPaused = false;

    // Track which emulator instance we're recording on, so the global
    // NC_EMULATOR_STATE_CHANGE handler can ignore stops from other instances
    if (_context && _context->pEmulator)
        _recordingEmulatorId = _context->pEmulator->GetId();

    MLOGINFO("RecordingManager::StartRecording - Recording started successfully");
    return true;
}

bool RecordingManager::StartRecordingEx(const std::string& filename)
{
    if (_isRecording)
    {
        MLOGWARNING("RecordingManager::StartRecordingEx - Already recording, stop current recording first");
        return false;
    }

    MLOGINFO("RecordingManager::StartRecordingEx - Starting recording to '%s'", filename.c_str());
    MLOGINFO("  Mode: %s", GetRecordingModeString(_recordingMode));
    MLOGINFO("  Video: %s", _videoEnabled ? "ENABLED" : "DISABLED");
    MLOGINFO("  Audio tracks: %zu", _audioTracks.size());

    // Store configuration (normalized; ensures the destination directory exists)
    _outputFilename = PrepareOutputPath(filename, _lastRecordingError);
    if (_outputFilename.empty())
    {
        MLOGERROR("RecordingManager::StartRecordingEx - %s", _lastRecordingError.c_str());
        return false;
    }

    // Use native framebuffer dimensions if not explicitly set
    if (_videoEnabled && (_videoWidth == 0 || _videoHeight == 0))
    {
        FramebufferDescriptor fb = _context->pScreen->GetFramebufferDescriptor();
        _videoWidth = fb.width;
        _videoHeight = fb.height;
    }

    if (_videoEnabled)
    {
        MLOGINFO("  Video: %s, %ux%u @ %.2f fps, %u kbps", _videoCodec.c_str(), _videoWidth, _videoHeight,
                 _videoFrameRate, _videoBitrate);
    }

    // Log audio track configuration
    for (size_t i = 0; i < _audioTracks.size(); i++)
    {
        const AudioTrackConfig& track = _audioTracks[i];
        MLOGINFO("  Track %zu: %s [%s, %u kbps]", i, track.name.c_str(), track.codec.c_str(), track.bitrate);
    }

    // Serialize encoder (re)creation against the capture path
    std::lock_guard<std::mutex> lock(_captureMutex);

    // Initialize encoder
    if (!InitializeEncoder())
    {
        MLOGERROR("RecordingManager::StartRecordingEx - Failed to initialize encoder");
        return false;
    }

    // Reset counters
    _emulatedFrameCount = 0;
    _emulatedAudioSampleCount = 0;
    _stats = RecordingStats();
    _wallClockStart = Clock::now();
    _wallClockPausedTotal = Clock::duration::zero();

    // Start recording
    _isRecording = true;
    _isPaused = false;

    // Track which emulator instance we're recording on
    if (_context && _context->pEmulator)
        _recordingEmulatorId = _context->pEmulator->GetId();

    MLOGINFO("RecordingManager::StartRecordingEx - Recording started successfully");
    return true;
}

bool RecordingManager::StartRecordingWithEncoder(const std::string& filename, std::unique_ptr<EncoderBase> encoder)
{
    if (_isRecording)
    {
        MLOGWARNING("RecordingManager::StartRecordingWithEncoder - Already recording, stop current recording first");
        return false;
    }

    if (!encoder)
    {
        _lastRecordingError = "No encoder provided";
        MLOGERROR("RecordingManager::StartRecordingWithEncoder - %s", _lastRecordingError.c_str());
        return false;
    }

    MLOGINFO("RecordingManager::StartRecordingWithEncoder - Starting recording to '%s' with %s",
             filename.c_str(), encoder->GetDisplayName().c_str());

    // Prepare output path
    _outputFilename = PrepareOutputPath(filename, _lastRecordingError);
    if (_outputFilename.empty())
    {
        MLOGERROR("RecordingManager::StartRecordingWithEncoder - %s", _lastRecordingError.c_str());
        return false;
    }

    // Serialize encoder (re)creation against the capture path
    std::lock_guard<std::mutex> lock(_captureMutex);

    // Build encoder config
    EncoderConfig config;
    config.videoWidth = _videoWidth;
    config.videoHeight = _videoHeight;
    config.frameRate = _videoFrameRate;
    config.audioSampleRate = _audioSampleRate;
    config.audioChannels = _audioChannels;

    // Start the custom encoder
    if (!encoder->Start(_outputFilename, config))
    {
        _lastRecordingError = encoder->GetLastError();
        if (_lastRecordingError.empty())
            _lastRecordingError = "Encoder failed to start";
        MLOGERROR("RecordingManager::StartRecordingWithEncoder - %s", _lastRecordingError.c_str());
        return false;
    }

    // Add to active encoders
    _activeEncoders.clear();
    _activeEncoders.push_back(std::move(encoder));

    // Reset counters
    _emulatedFrameCount = 0;
    _emulatedAudioSampleCount = 0;
    _stats = RecordingStats();
    _wallClockStart = Clock::now();
    _wallClockPausedTotal = Clock::duration::zero();

    // Start recording
    _isRecording = true;
    _isPaused = false;

    // Track which emulator instance we're recording on
    if (_context && _context->pEmulator)
        _recordingEmulatorId = _context->pEmulator->GetId();

    MLOGINFO("RecordingManager::StartRecordingWithEncoder - Recording started successfully");
    return true;
}

void RecordingManager::StopRecording()
{
    if (!_isRecording)
    {
        MLOGWARNING("RecordingManager::StopRecording - Not currently recording");
        return;
    }

    MLOGINFO("RecordingManager::StopRecording - Stopping recording");

    // Wait for any in-flight CaptureFrame/CaptureAudio to finish, then stop
    // accepting new frames BEFORE finalizing (closing) the encoders
    std::lock_guard<std::mutex> lock(_captureMutex);

    _isRecording = false;
    _isPaused = false;
    _recordingEmulatorId.clear();

    // Compute final wall clock duration
    auto wallNow = Clock::now();
    auto wallElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        wallNow - _wallClockStart - _wallClockPausedTotal).count();
    _stats.recordedDuration = static_cast<double>(wallElapsed) / 1000.0;

    MLOGINFO("  Frames recorded: %llu", _stats.framesRecorded);
    MLOGINFO("  Audio samples: %llu", _stats.audioSamplesRecorded);
    MLOGINFO("  Duration: %.2f seconds (wall clock)", _stats.recordedDuration);

    // Finalize encoder and close output file
    FinalizeEncoder();

    MLOGINFO("RecordingManager::StopRecording - Recording stopped, output saved to '%s'", _outputFilename.c_str());
}

void RecordingManager::PauseRecording()
{
    if (!_isRecording)
    {
        MLOGWARNING("RecordingManager::PauseRecording - Not currently recording");
        return;
    }

    if (_isPaused)
    {
        MLOGWARNING("RecordingManager::PauseRecording - Already paused");
        return;
    }

    MLOGINFO("RecordingManager::PauseRecording - Pausing recording");
    _isPaused = true;
    _wallClockPauseStart = Clock::now();
}

void RecordingManager::ResumeRecording()
{
    if (!_isRecording)
    {
        MLOGWARNING("RecordingManager::ResumeRecording - Not currently recording");
        return;
    }

    if (!_isPaused)
    {
        MLOGWARNING("RecordingManager::ResumeRecording - Not paused");
        return;
    }

    MLOGINFO("RecordingManager::ResumeRecording - Resuming recording");
    _isPaused = false;
    _wallClockPausedTotal += Clock::now() - _wallClockPauseStart;
}

/// endregion </Recording control>

/// region <Frame/Audio capture>

void RecordingManager::CaptureFrame(const FramebufferDescriptor& framebuffer)
{
    // Cheap unlocked pre-check (the mainloop calls this every frame)
    if (!IsRecording())
    {
        return;
    }

    // Serialize against StopRecording closing the encoders mid-frame
    std::lock_guard<std::mutex> lock(_captureMutex);
    if (!IsRecording())
    {
        return;
    }

    // Calculate presentation timestamp based on emulated frame count
    double timestamp = static_cast<double>(_emulatedFrameCount) / _videoFrameRate;

    // Crop to the main screen area (no border) when requested
    const FramebufferDescriptor* toEncode = &framebuffer;
    FramebufferDescriptor cropped;
    if (_captureRegion == VideoCaptureRegion::MainScreen && _context->pScreen && framebuffer.memoryBuffer)
    {
        const RasterDescriptor& rd = _context->pScreen->rasterDescriptors[framebuffer.videoMode];
        if (rd.screenWidth > 0 && rd.screenHeight > 0 &&
            rd.screenOffsetLeft + rd.screenWidth <= framebuffer.width &&
            rd.screenOffsetTop + rd.screenHeight <= framebuffer.height)
        {
            size_t rowBytes = static_cast<size_t>(rd.screenWidth) * 4;
            _cropBuffer.resize(rowBytes * rd.screenHeight);

            const uint8_t* src = framebuffer.memoryBuffer +
                (static_cast<size_t>(rd.screenOffsetTop) * framebuffer.width + rd.screenOffsetLeft) * 4;
            for (uint16_t y = 0; y < rd.screenHeight; y++)
            {
                memcpy(_cropBuffer.data() + y * rowBytes, src, rowBytes);
                src += static_cast<size_t>(framebuffer.width) * 4;
            }

            cropped.videoMode = framebuffer.videoMode;
            cropped.width = rd.screenWidth;
            cropped.height = rd.screenHeight;
            cropped.memoryBuffer = _cropBuffer.data();
            cropped.memoryBufferSize = _cropBuffer.size();
            toEncode = &cropped;
        }
    }

    // Encode video frame
    EncodeVideoFrame(*toEncode, timestamp);

    // Update statistics
    _stats.framesRecorded++;

    // Wall clock duration (real elapsed time, excluding pauses)
    auto wallNow = Clock::now();
    auto wallElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        wallNow - _wallClockStart - _wallClockPausedTotal).count();
    _stats.recordedDuration = static_cast<double>(wallElapsed) / 1000.0;
    _stats.emulatedDuration = timestamp;

    // Rolling FPS: track frame timestamps in a ring buffer
    size_t maxFrames = static_cast<size_t>(_fpsWindowSeconds * 60.0); // ~60fps max
    if (_frameTimestamps.size() < maxFrames)
    {
        _frameTimestamps.push_back(wallNow);
    }
    else
    {
        _frameTimestamps[_frameTimestampIndex] = wallNow;
        _frameTimestampIndex = (_frameTimestampIndex + 1) % maxFrames;
    }

    // Calculate FPS over the window
    if (_frameTimestamps.size() >= 2)
    {
        auto windowStart = wallNow - std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(_fpsWindowSeconds));
        size_t framesInWindow = 0;
        Clock::time_point oldest = wallNow;
        for (const auto& ts : _frameTimestamps)
        {
            if (ts >= windowStart)
            {
                framesInWindow++;
                if (ts < oldest)
                    oldest = ts;
            }
        }
        if (framesInWindow > 1)
        {
            double windowDuration = std::chrono::duration<double>(wallNow - oldest).count();
            if (windowDuration > 0.01)
                _stats.recentFps = static_cast<double>(framesInWindow - 1) / windowDuration;
        }
    }

    // Update file size from encoder (throttled — every 25 frames / 0.5s to avoid stat() overhead)
    if (_stats.framesRecorded % 25 == 0)
    {
        for (auto& encoder : _activeEncoders)
        {
            if (encoder)
            {
                _stats.outputFileSize = encoder->GetOutputFileSize();
                break; // Single encoder for now
            }
        }
    }

    _emulatedFrameCount++;
}

void RecordingManager::CaptureAudio(const int16_t* samples, size_t sampleCount)
{
    // Cheap unlocked pre-check
    if (!IsRecording() || _audioTracks.empty())
    {
        return;
    }

    // Serialize against StopRecording closing the encoders mid-write
    std::lock_guard<std::mutex> lock(_captureMutex);
    if (!IsRecording() || _audioTracks.empty())
    {
        return;
    }

    // Calculate presentation timestamp based on emulated sample count
    double timestamp = static_cast<double>(_emulatedAudioSampleCount) / _audioSampleRate;

    // Encode audio samples
    EncodeAudioSamples(samples, sampleCount, timestamp);

    // Update statistics
    _stats.audioSamplesRecorded += sampleCount;
    _emulatedAudioSampleCount += sampleCount;
}

/// endregion </Frame/Audio capture>

/// region <Configuration>

void RecordingManager::SetRecordingMode(RecordingMode mode)
{
    if (_isRecording)
    {
        MLOGWARNING("RecordingManager::SetRecordingMode - Cannot change mode while recording");
        return;
    }

    _recordingMode = mode;
    MLOGINFO("RecordingManager::SetRecordingMode - Mode set to %s", GetRecordingModeString(mode));
}

void RecordingManager::SetVideoEnabled(bool enabled)
{
    if (_isRecording)
    {
        MLOGWARNING("RecordingManager::SetVideoEnabled - Cannot change while recording");
        return;
    }

    _videoEnabled = enabled;
    MLOGINFO("RecordingManager::SetVideoEnabled - Video %s", enabled ? "ENABLED" : "DISABLED");
}

void RecordingManager::SetVideoResolution(uint32_t width, uint32_t height)
{
    if (_isRecording)
    {
        MLOGWARNING("RecordingManager::SetVideoResolution - Cannot change resolution while recording");
        return;
    }

    _videoWidth = width;
    _videoHeight = height;

    MLOGINFO("RecordingManager::SetVideoResolution - Resolution set to %ux%u", width, height);
}

void RecordingManager::SetVideoFrameRate(float fps)
{
    if (_isRecording)
    {
        MLOGWARNING("RecordingManager::SetVideoFrameRate - Cannot change frame rate while recording");
        return;
    }

    _videoFrameRate = fps;

    MLOGINFO("RecordingManager::SetVideoFrameRate - Frame rate set to %.2f fps", fps);
}

void RecordingManager::SetVideoCodec(const std::string& codec)
{
    if (_isRecording)
    {
        MLOGWARNING("RecordingManager::SetVideoCodec - Cannot change codec while recording");
        return;
    }

    _videoCodec = codec;
    MLOGINFO("RecordingManager::SetVideoCodec - Video codec set to %s", codec.c_str());
}

void RecordingManager::SetVideoBitrate(uint32_t kbps)
{
    if (_isRecording)
    {
        MLOGWARNING("RecordingManager::SetVideoBitrate - Cannot change bitrate while recording");
        return;
    }

    _videoBitrate = kbps;
    MLOGINFO("RecordingManager::SetVideoBitrate - Video bitrate set to %u kbps", kbps);
}

void RecordingManager::SetAudioSampleRate(uint32_t sampleRate)
{
    if (_isRecording)
    {
        MLOGWARNING("RecordingManager::SetAudioSampleRate - Cannot change sample rate while recording");
        return;
    }

    _audioSampleRate = sampleRate;

    MLOGINFO("RecordingManager::SetAudioSampleRate - Sample rate set to %u Hz", sampleRate);
}

void RecordingManager::AddAudioTrack(const AudioTrackConfig& config)
{
    if (_isRecording)
    {
        MLOGWARNING("RecordingManager::AddAudioTrack - Cannot add tracks while recording");
        return;
    }

    _audioTracks.push_back(config);
    MLOGINFO("RecordingManager::AddAudioTrack - Added track '%s' (source: %s)", config.name.c_str(),
             GetAudioSourceName(config.source));
}

void RecordingManager::RemoveAudioTrack(size_t index)
{
    if (_isRecording)
    {
        MLOGWARNING("RecordingManager::RemoveAudioTrack - Cannot remove tracks while recording");
        return;
    }

    if (index >= _audioTracks.size())
    {
        MLOGWARNING("RecordingManager::RemoveAudioTrack - Invalid track index %zu", index);
        return;
    }

    MLOGINFO("RecordingManager::RemoveAudioTrack - Removed track '%s'", _audioTracks[index].name.c_str());
    _audioTracks.erase(_audioTracks.begin() + index);
}

void RecordingManager::ClearAudioTracks()
{
    if (_isRecording)
    {
        MLOGWARNING("RecordingManager::ClearAudioTracks - Cannot clear tracks while recording");
        return;
    }

    _audioTracks.clear();
    MLOGINFO("RecordingManager::ClearAudioTracks - All audio tracks cleared");
}

void RecordingManager::SelectAudioSource(AudioSourceType source)
{
    if (_isRecording)
    {
        MLOGWARNING("RecordingManager::SelectAudioSource - Cannot change source while recording");
        return;
    }

    _selectedSource = source;
    MLOGINFO("RecordingManager::SelectAudioSource - Selected source: %s", GetAudioSourceName(source));
}

/// endregion </Configuration>

/// region <Encoder interface>

bool RecordingManager::InitializeEncoder()
{
    _lastRecordingError.clear();
    MLOGINFO("RecordingManager::InitializeEncoder - Selecting encoder for codec '%s'", _videoCodec.c_str());

    // Build EncoderConfig from current RecordingManager state
    EncoderConfig config;
    config.videoWidth = _videoWidth;
    config.videoHeight = _videoHeight;
    config.frameRate = _videoFrameRate;
    config.videoBitrate = _videoBitrate;
    config.videoCodec = _videoCodec;
    config.audioSampleRate = _audioSampleRate;
    config.audioChannels = _audioChannels;
    config.audioBitrate = _audioBitrate > 0 ? _audioBitrate : 192;
    config.audioCodec = _audioCodec;
    config.qualityPreset = _qualityPreset;
    config.ffmpegPath = _ffmpegPath;
    config.captureRegion = _captureRegion;
    config.scaleFactor = _scaleFactor;

    // Determine container from filename extension
    size_t dotPos = _outputFilename.rfind('.');
    if (dotPos != std::string::npos)
    {
        config.container = _outputFilename.substr(dotPos + 1);
        std::transform(config.container.begin(), config.container.end(), config.container.begin(), ::tolower);
    }

    // Clear any existing encoders
    _activeEncoders.clear();

    // Normalize codec for GIF check
    std::string codecLower = _videoCodec;
    std::transform(codecLower.begin(), codecLower.end(), codecLower.begin(), ::tolower);

    // ====================================================================
    // Dispatch based on selected backend
    // ====================================================================
    // GIF is always handled natively regardless of backend choice
    if (codecLower == "gif" || config.container == "gif")
    {
        MLOGINFO("  Trying GIFEncoder (native)");
        auto gifEncoder = std::make_unique<GIFEncoder>();
        if (gifEncoder->Start(_outputFilename, config))
        {
            _activeEncoders.push_back(std::move(gifEncoder));
            _realtimeCapable = true;
            MLOGINFO("  GIFEncoder started successfully");
            return true;
        }
        std::string gifErr = gifEncoder->GetLastError();
        MLOGERROR("  GIFEncoder failed: %s", gifErr.c_str());
        _lastRecordingError = "GIF encoder failed: " + (gifErr.empty() ? "unknown error" : gifErr)
            + " | Output: " + _outputFilename
            + " | Resolution: " + std::to_string(config.videoWidth) + "x" + std::to_string(config.videoHeight);
        return false;
    }

    // Determine which single backend to use (NO fallback allowed)
    EncoderBackend backend = _encoderBackend;

    // Auto: pick one based on availability — native first if available and compatible
    if (backend == EncoderBackend::Auto)
    {
        if (PlatformEncoderFactory::isNativeAvailable())
        {
            // Check if native supports this codec/container/audio combination
            std::string audioLower = config.audioCodec;
            std::transform(audioLower.begin(), audioLower.end(), audioLower.begin(), ::tolower);

            bool nativeCompatible = (config.container == "mp4" || config.container == "mov")
                && (codecLower == "h264" || codecLower == "h265" || codecLower == "hevc")
                && (config.audioChannels == 0 || audioLower.empty() || audioLower == "aac");
            backend = nativeCompatible ? EncoderBackend::Native : EncoderBackend::FFmpeg;
        }
        else
        {
            backend = EncoderBackend::FFmpeg;
        }
    }

    MLOGINFO("  Selected backend: %s",
             backend == EncoderBackend::Native ? "Native" : "FFmpeg");

    // ====================================================================
    // Native encoder — try once, fail hard
    // ====================================================================
    if (backend == EncoderBackend::Native)
    {
        if (!PlatformEncoderFactory::isNativeAvailable())
        {
            _lastRecordingError = "Native encoder not available on this platform.";
            return false;
        }

        MLOGINFO("  Trying native encoder");
        auto nativeEncoder = PlatformEncoderFactory::createNativeEncoder(config);
        if (!nativeEncoder)
        {
            std::string nativeName = PlatformEncoderFactory::getNativeDisplayName();
            _lastRecordingError = nativeName + " does not support codec '" + _videoCodec
                + "' in container '" + config.container + "'. "
                + nativeName + " supports H.264 and H.265 in MP4/MOV containers.";
            MLOGERROR("  %s", _lastRecordingError.c_str());
            return false;
        }

        if (!nativeEncoder->Start(_outputFilename, config))
        {
            std::string err = nativeEncoder->GetLastError();
            std::string nativeName = PlatformEncoderFactory::getNativeDisplayName();
            _lastRecordingError = nativeName + " encoder failed: "
                + (err.empty() ? "unknown error" : err)
                + " | Codec: " + _videoCodec
                + " | Container: " + config.container
                + " | Resolution: " + std::to_string(config.videoWidth) + "x" + std::to_string(config.videoHeight);
            MLOGERROR("  %s", _lastRecordingError.c_str());
            return false;
        }

        _activeEncoders.push_back(std::move(nativeEncoder));
        _realtimeCapable = true;
        MLOGINFO("  Native encoder started");
        return true;
    }

    // ====================================================================
    // FFmpeg encoder — try once, fail hard
    // ====================================================================
    MLOGINFO("  Trying FFmpegPipeEncoder");

    // Check if ffmpeg is available
    std::string ffmpegPath = FFmpegProbe::findFFmpeg();
    if (ffmpegPath.empty())
    {
        MLOGERROR("  FFmpeg binary not found. Recording cannot proceed.");
        MLOGERROR("  Install ffmpeg: macOS: brew install ffmpeg, Linux: sudo apt install ffmpeg");
        _lastRecordingError = "FFmpeg binary not found on system PATH. "
            + std::string("Install it: macOS 'brew install ffmpeg', Linux 'sudo apt install ffmpeg'. ")
            + "Codec: " + _videoCodec + ", Container: " + config.container;
        return false;
    }

    MLOGINFO("  FFmpeg found at: %s", ffmpegPath.c_str());

    auto ffmpegEncoder = std::make_unique<FFmpegPipeEncoder>();
    ffmpegEncoder->setFFmpegPath(ffmpegPath);

    if (ffmpegEncoder->Start(_outputFilename, config))
    {
        // Estimate realtime capability
        FFmpegProbe::HWAccelInfo hwInfo = FFmpegProbe::detectHWAccel(ffmpegPath);
        auto capability = RealtimeEstimator::estimate(config, false,
                                                       hwInfo.videoToolbox,
                                                       hwInfo.nvenc,
                                                       hwInfo.qsv,
                                                       hwInfo.vaapi);

        _realtimeCapable = (capability == RealtimeEstimator::RealtimeCapability::Realtime);

        // Blocking mode: emulation thread waits when the queue is full.
        // - Non-realtime encoders: MUST block, otherwise we'd drop most frames
        // - Realtime encoders: do NOT block — the 60-frame buffer absorbs
        //   frame-to-frame timing variance; blocking would cause micro-stalls
        //   on every slow frame and make playback choppy even though the
        //   encoder keeps up on average
        ffmpegEncoder->setBlocking(!_realtimeCapable);

        _activeEncoders.push_back(std::move(ffmpegEncoder));

        std::string reason = RealtimeEstimator::getReason(config, false);
        if (_realtimeCapable)
        {
            MLOGINFO("  FFmpeg encoder started. Realtime: YES. %s", reason.c_str());
        }
        else
        {
            MLOGWARNING("  FFmpeg encoder started. NON-REALTIME. Emulation will slow to guarantee 50Hz. %s", reason.c_str());
        }

        return true;
    }

    std::string ffmpegErr = ffmpegEncoder->GetLastError();
    MLOGERROR("  FFmpeg encoder failed: %s", ffmpegErr.c_str());
    _lastRecordingError = "FFmpeg encoder failed: " + (ffmpegErr.empty() ? "unknown error" : ffmpegErr)
        + " | FFmpeg path: " + ffmpegPath
        + " | Codec: " + _videoCodec + " (" + config.container + ")"
        + " | Resolution: " + std::to_string(config.videoWidth) + "x" + std::to_string(config.videoHeight)
        + " | Bitrate: " + std::to_string(config.videoBitrate) + "kbps";
    return false;
}

void RecordingManager::FinalizeEncoder()
{
    MLOGINFO("RecordingManager::FinalizeEncoder - Finalizing %zu encoder(s)", _activeEncoders.size());

    for (auto& encoder : _activeEncoders)
    {
        if (encoder)
        {
            encoder->Stop();

            // Update final statistics
            _stats.framesRecorded = encoder->GetFramesEncoded();
            _stats.audioSamplesRecorded = encoder->GetAudioSamplesEncoded();
            _stats.outputFileSize = encoder->GetOutputFileSize();

            // For FFmpeg encoder, log diagnostics
            auto* ffmpegEncoder = dynamic_cast<FFmpegPipeEncoder*>(encoder.get());
            if (ffmpegEncoder)
            {
                std::string diag = ffmpegEncoder->getDiagnostics();
                if (!diag.empty())
                {
                    MLOGDEBUG("  FFmpeg diagnostics:\n%s", diag.c_str());
                }
            }
        }
    }

    _activeEncoders.clear();
}

void RecordingManager::EncodeVideoFrame(const FramebufferDescriptor& framebuffer, double timestamp)
{
    // Dispatch to all active encoders
    for (auto& encoder : _activeEncoders)
    {
        if (encoder && encoder->IsRecording())
        {
            encoder->OnVideoFrame(framebuffer, timestamp);
        }
    }
}

void RecordingManager::EncodeAudioSamples(const int16_t* samples, size_t sampleCount, double timestamp)
{
    // Dispatch to all active encoders
    for (auto& encoder : _activeEncoders)
    {
        if (encoder && encoder->IsRecording())
        {
            encoder->OnAudioSamples(samples, sampleCount, timestamp);
        }
    }
}

/// endregion </Encoder interface>
