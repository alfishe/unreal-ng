#pragma once
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

#include "3rdparty/message-center/eventqueue.h"
#include "emulator/platform.h"
#include "emulator/sound/soundmanager.h"  // AudioSourceType lives here now
#include "encoder_base.h"
#include "encoder_config.h"
#include "stdafx.h"

/// region <Forward declarations>
class EmulatorContext;
class ModuleLogger;
struct FramebufferDescriptor;
/// endregion </Forward declarations>

/// region <Recording types>

/// Recording mode determines how audio sources are handled
enum class RecordingMode
{
    SingleTrack,   // Record final mixed output only (default)
    MultiTrack,    // Record multiple audio sources to separate tracks
    ChannelSplit,  // Record individual AY channels separately
    AudioOnly      // Record audio without video
};

// AudioSourceType is now defined in soundmanager.h (shared with device registry)

/// Audio track configuration for multi-track recordings
struct AudioTrackConfig
{
    std::string name;             // Track name (for metadata)
    AudioSourceType source;       // Audio source type
    bool enabled = true;          // Enable/disable track
    float volume = 1.0f;          // Volume multiplier (0.0 - 1.0)
    int pan = 0;                  // Panning (-100 = left, 0 = center, +100 = right)
    std::string codec = "aac";    // Audio codec for this track
    uint32_t bitrate = 192;       // Bitrate in kbps
    uint32_t sampleRate = 44100;  // Sample rate (Hz)
};

/// endregion </Recording types>

/// Encoder backend selection
enum class EncoderBackend
{
    Auto,    ///< Try native first, fall back to FFmpeg
    Native,  ///< Force native encoder (VideoToolbox on macOS). Fail if unavailable.
    FFmpeg,  ///< Force FFmpeg pipe encoder. Fail if unavailable.
    Custom   ///< Use externally provided encoder (via StartRecordingWithEncoder)
};

/// @brief Video/Audio Recording Manager - Captures emulated output for video encoding
///
/// This manager handles recording of video frames and audio samples based on EMULATED time,
/// not wall-clock time. This ensures that recordings made in turbo mode play back at normal
/// speed with correct timing.
///
/// Key features:
/// - Frame capture at emulated 50 Hz (ZX Spectrum native rate)
/// - Audio capture at emulated 44.1 kHz
/// - Works correctly in turbo mode (captures all frames, not real-time frames)
/// - Timestamping based on emulated frame count, not system time
///
/// Recording lifecycle:
/// 1. Configure output format, codec, filename
/// 2. StartRecording() - Initialize encoder
/// 3. CaptureFrame() / CaptureAudio() - Called every emulated frame/audio callback
/// 4. StopRecording() - Finalize and write output file
///
/// Future codec implementations will fill in the encoder stubs.
class RecordingManager : public Observer
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    ModuleLogger* _logger = nullptr;
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_RECORDING;
    const uint16_t _SUBMODULE = PlatformRecordingSubmodulesEnum::SUBMODULE_RECORDING_MANAGER;
    /// endregion </ModuleLogger definitions for Module/Submodule>

public:
    RecordingManager() = delete;
    RecordingManager(EmulatorContext* context);
    virtual ~RecordingManager();

    /// region <Initialization>
public:
    void Init();
    void Reset();

    /// @brief Update cached feature flag state (called by FeatureManager)
    void UpdateFeatureCache();
    /// endregion </Initialization>

    /// region <Recording control>
public:
    /// Start recording to file (simple single-track mode)
    /// @param filename Output filename (extension determines format: .mp4, .avi, .mkv, .wav, .flac, etc.)
    /// @param videoCodec Video codec name (e.g., "h264", "h265", "vp9", "rawvideo") - empty for audio-only
    /// @param audioCodec Audio codec name (e.g., "aac", "mp3", "pcm_s16le", "vorbis", "flac")
    /// @param videoBitrate Video bitrate in kbps (0 = auto/default)
    /// @param audioBitrate Audio bitrate in kbps (0 = auto/default)
    /// @return true if recording started successfully
    bool StartRecording(const std::string& filename, const std::string& videoCodec = "h264",
                        const std::string& audioCodec = "aac", uint32_t videoBitrate = 0, uint32_t audioBitrate = 0);

    /// Start recording with full configuration (uses current mode and track settings)
    /// @param filename Output filename
    /// @return true if recording started successfully
    bool StartRecordingEx(const std::string& filename);

    /// Start recording with a custom encoder instance
    /// @param filename Output filename
    /// @param encoder Pre-configured encoder (ownership transferred)
    /// @return true if recording started successfully
    bool StartRecordingWithEncoder(const std::string& filename, std::unique_ptr<EncoderBase> encoder);

    /// Stop current recording and finalize output file
    void StopRecording();

    /// Pause recording (frames/audio will be skipped but timestamps preserved)
    void PauseRecording();

    /// Resume paused recording
    void ResumeRecording();

    /// Check if currently recording
    bool IsRecording() const
    {
        return _isRecording && !_isPaused;
    }

    /// Check if recording is paused
    bool IsPaused() const
    {
        return _isPaused;
    }

    /// Check if encoder can keep up with realtime
    bool IsRealtimeCapable() const
    {
        return _realtimeCapable;
    }

    /// Set realtime capability flag (called after encoder init)
    void SetRealtimeCapable(bool capable)
    {
        _realtimeCapable = capable;
    }

    /// Get the detailed error message from the last failed recording attempt
    /// @return Error string with specifics (encoder name, failure reason, path, codec)
    std::string GetLastRecordingError() const
    {
        return _lastRecordingError;
    }
    /// endregion </Recording control>

    /// region <Frame/Audio capture>
public:
    /// Capture video frame for recording
    /// Called every emulated frame (50 Hz in ZX Spectrum time)
    /// @param framebuffer Framebuffer containing current screen state
    void CaptureFrame(const FramebufferDescriptor& framebuffer);

    /// Capture audio samples for recording
    /// Called every emulated frame after audio generation
    /// @param samples Audio sample buffer (interleaved stereo, int16_t)
    /// @param sampleCount Number of samples (total, not per channel)
    void CaptureAudio(const int16_t* samples, size_t sampleCount);

    /// endregion </Frame/Audio capture>

    /// region <Statistics>
public:
    /// Get current recording statistics
    struct RecordingStats
    {
        uint64_t framesRecorded = 0;        // Total video frames captured
        uint64_t audioSamplesRecorded = 0;  // Total audio samples captured
        double recordedDuration = 0.0;      // Duration in seconds (wall clock — real elapsed time)
        double emulatedDuration = 0.0;      // Duration in seconds (emulated time — may differ in turbo mode)
        uint64_t outputFileSize = 0;        // Output file size in bytes
        double averageFrameTime = 0.0;      // Average time per frame encode (ms)
        double recentFps = 0.0;             // FPS over the last N seconds (rolling window)
    };

    /// Set the rolling FPS window duration in seconds (default: 5.0)
    void SetFpsWindowSeconds(double seconds) { _fpsWindowSeconds = seconds; }

    RecordingStats GetStats() const
    {
        return _stats;
    }
    /// endregion </Statistics>

    /// region <Configuration>
public:
    // Encoder backend selection
    void SetEncoderBackend(EncoderBackend backend) { _encoderBackend = backend; }
    EncoderBackend GetEncoderBackend() const { return _encoderBackend; }

    // Recording mode
    void SetRecordingMode(RecordingMode mode);
    RecordingMode GetRecordingMode() const
    {
        return _recordingMode;
    }

    // Video configuration
    void SetVideoEnabled(bool enabled);
    bool IsVideoEnabled() const
    {
        return _videoEnabled;
    }
    void SetVideoResolution(uint32_t width, uint32_t height);
    void SetVideoFrameRate(float fps);
    void SetVideoCodec(const std::string& codec);
    void SetVideoBitrate(uint32_t kbps);

    /// Quality preset (0-10, 0 = fastest/lowest quality, 10 = slowest/best)
    void SetQualityPreset(int preset)
    {
        _qualityPreset = preset < 0 ? 0 : (preset > 10 ? 10 : preset);
    }

    /// Capture region: full frame with border, or main screen only (256×192)
    void SetCaptureRegion(VideoCaptureRegion region)
    {
        if (!_isRecording)
            _captureRegion = region;
    }
    VideoCaptureRegion GetCaptureRegion() const
    {
        return _captureRegion;
    }

    /// Integer nearest-neighbor output upscale (1-4). 2x+ preserves per-pixel
    /// color detail through YUV 4:2:0 chroma subsampling.
    void SetScaleFactor(uint32_t factor)
    {
        if (!_isRecording)
            _scaleFactor = factor < 1 ? 1 : (factor > 4 ? 4 : factor);
    }

    /// Explicit ffmpeg binary path (empty = auto-detect)
    void SetFFmpegPath(const std::string& path)
    {
        _ffmpegPath = path;
    }

    // Audio configuration (global)
    void SetAudioSampleRate(uint32_t sampleRate);

    // Audio track management (for multi-track mode)
    void AddAudioTrack(const AudioTrackConfig& config);
    void RemoveAudioTrack(size_t index);
    void ClearAudioTracks();
    size_t GetAudioTrackCount() const
    {
        return _audioTracks.size();
    }
    const AudioTrackConfig& GetAudioTrack(size_t index) const
    {
        return _audioTracks[index];
    }

    // Simple audio source selection (for single-track mode)
    void SelectAudioSource(AudioSourceType source);

    /// endregion </Configuration>

protected:
    /// region <Internal state>
    EmulatorContext* _context = nullptr;

    // Feature flag (cached from FeatureManager)
    bool _featureEnabled = false;

    // Recording state
    bool _isRecording = false;
    bool _isPaused = false;

    // Recording mode
    RecordingMode _recordingMode = RecordingMode::SingleTrack;

    // Encoder backend (Auto / Native / FFmpeg)
    EncoderBackend _encoderBackend = EncoderBackend::Auto;

    // Configuration
    std::string _outputFilename;

    // Video configuration
    bool _videoEnabled = true;
    std::string _videoCodec = "h264";
    uint32_t _videoBitrate = 0;
    uint32_t _videoWidth = 0;
    uint32_t _videoHeight = 0;
    float _videoFrameRate = 50.0f;  // ZX Spectrum native rate

    // Audio configuration
    std::string _audioCodec = "aac";
    uint32_t _audioBitrate = 0;
    uint32_t _audioSampleRate = 44100;
    uint32_t _audioChannels = 2;  // Stereo

    // Quality preset (0-10) applied to encoder config
    int _qualityPreset = 5;

    // Capture region and output scaling
    VideoCaptureRegion _captureRegion = VideoCaptureRegion::FullFrame;
    uint32_t _scaleFactor = 1;
    std::vector<uint8_t> _cropBuffer;  // Reused per-frame when cropping to main screen

    // Explicit ffmpeg binary path (empty = auto-detect)
    std::string _ffmpegPath;

    // Audio tracks (for multi-track mode)
    std::vector<AudioTrackConfig> _audioTracks;

    // Selected audio source (for single-track mode)
    AudioSourceType _selectedSource = AudioSourceType::MasterMix;

    // Emulated time tracking
    uint64_t _emulatedFrameCount = 0;
    uint64_t _emulatedAudioSampleCount = 0;

    // Wall clock tracking for real elapsed recording time
    using Clock = std::chrono::steady_clock;
    Clock::time_point _wallClockStart;
    Clock::time_point _wallClockPauseStart;
    Clock::duration _wallClockPausedTotal = Clock::duration::zero();

    // Statistics
    RecordingStats _stats;

    // Rolling FPS: ring buffer of frame timestamps
    double _fpsWindowSeconds = 5.0;
    std::vector<Clock::time_point> _frameTimestamps;
    size_t _frameTimestampIndex = 0;

    // Active encoders registry
    std::vector<EncoderPtr> _activeEncoders;

    // Serializes encoder dispatch (emulation thread: CaptureFrame/CaptureAudio)
    // against start/stop/finalize (UI thread). Without it, StopRecording can
    // close an encoder's output file while a frame is still being written.
    std::mutex _captureMutex;

    // Realtime capability (false = non-realtime mode, emulation slows)
    bool _realtimeCapable = true;

    // Detailed error message from the last failed start attempt
    std::string _lastRecordingError;

    // Message center subscription tracking
    bool _isSubscribed = false;

    // Emulator instance ID we are currently recording on.
    // NC_EMULATOR_STATE_CHANGE is a global broadcast with no instance ID in the
    // payload, so we store our own ID and verify the emulator's actual state
    // before reacting to a stop notification.
    std::string _recordingEmulatorId;

    // MessageCenter callback (Observer interface)
    void onEmulatorStateChange(int id, Message* message);
    /// endregion </Internal state>

    /// region <Encoder interface (to be implemented)>
protected:
    /// Initialize video/audio encoder
    /// @return true if encoder initialized successfully
    bool InitializeEncoder();

    /// Finalize encoder and close output file
    void FinalizeEncoder();

    /// Encode single video frame
    /// @param framebuffer Framebuffer to encode
    /// @param timestamp Presentation timestamp (in seconds, emulated time)
    void EncodeVideoFrame(const FramebufferDescriptor& framebuffer, double timestamp);

    /// Encode audio samples
    /// @param samples Audio sample buffer
    /// @param sampleCount Number of samples
    /// @param timestamp Presentation timestamp (in seconds, emulated time)
    void EncodeAudioSamples(const int16_t* samples, size_t sampleCount, double timestamp);
    /// endregion </Encoder interface (to be implemented)>
};
