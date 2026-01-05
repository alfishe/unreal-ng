#pragma once
#include "stdafx.h"

#include "emulator/platform.h"

/// region <Forward declarations>
class EmulatorContext;
class ModuleLogger;
struct FramebufferDescriptor;
/// endregion </Forward declarations>

/// region <Recording types>

/// Recording mode determines how audio sources are handled
enum class RecordingMode
{
    SingleTrack,    // Record final mixed output only (default)
    MultiTrack,     // Record multiple audio sources to separate tracks
    ChannelSplit,   // Record individual AY channels separately
    AudioOnly       // Record audio without video
};

/// Audio source types for individual device/channel selection
enum class AudioSourceType
{
    // Master output
    MasterMix,              // Final mixed output (all devices)
    
    // Individual devices
    Beeper,                 // Beeper output
    AY1_All,                // AY chip #1 (all channels mixed)
    AY2_All,                // AY chip #2 (all channels mixed)
    AY3_All,                // AY chip #3 (all channels mixed)
    COVOX,                  // COVOX/DAC output
    GeneralSound,           // General Sound output
    Moonsound,              // Moonsound/OPL4 output
    
    // Individual AY channels (chip 1)
    AY1_ChannelA,           // AY chip #1, channel A
    AY1_ChannelB,           // AY chip #1, channel B
    AY1_ChannelC,           // AY chip #1, channel C
    
    // Individual AY channels (chip 2)
    AY2_ChannelA,           // AY chip #2, channel A
    AY2_ChannelB,           // AY chip #2, channel B
    AY2_ChannelC,           // AY chip #2, channel C
    
    // Individual AY channels (chip 3)
    AY3_ChannelA,           // AY chip #3, channel A
    AY3_ChannelB,           // AY chip #3, channel B
    AY3_ChannelC,           // AY chip #3, channel C
    
    // Custom source (for future extensions)
    Custom
};

/// Audio track configuration for multi-track recordings
struct AudioTrackConfig
{
    std::string name;                    // Track name (for metadata)
    AudioSourceType source;              // Audio source type
    bool enabled = true;                 // Enable/disable track
    float volume = 1.0f;                 // Volume multiplier (0.0 - 1.0)
    int pan = 0;                         // Panning (-100 = left, 0 = center, +100 = right)
    std::string codec = "aac";          // Audio codec for this track
    uint32_t bitrate = 192;             // Bitrate in kbps
    uint32_t sampleRate = 44100;        // Sample rate (Hz)
};

/// endregion </Recording types>

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
class RecordingManager
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
    bool StartRecording(
        const std::string& filename,
        const std::string& videoCodec = "h264",
        const std::string& audioCodec = "aac",
        uint32_t videoBitrate = 0,
        uint32_t audioBitrate = 0
    );
    
    /// Start recording with full configuration (uses current mode and track settings)
    /// @param filename Output filename
    /// @return true if recording started successfully
    bool StartRecordingEx(const std::string& filename);

    /// Stop current recording and finalize output file
    void StopRecording();

    /// Pause recording (frames/audio will be skipped but timestamps preserved)
    void PauseRecording();

    /// Resume paused recording
    void ResumeRecording();

    /// Check if currently recording
    bool IsRecording() const { return _isRecording && !_isPaused; }

    /// Check if recording is paused
    bool IsPaused() const { return _isPaused; }
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
        double recordedDuration = 0.0;      // Duration in seconds (emulated time)
        uint64_t outputFileSize = 0;        // Output file size in bytes
        double averageFrameTime = 0.0;      // Average time per frame encode (ms)
    };

    RecordingStats GetStats() const { return _stats; }
    /// endregion </Statistics>

    /// region <Configuration>
public:
    // Recording mode
    void SetRecordingMode(RecordingMode mode);
    RecordingMode GetRecordingMode() const { return _recordingMode; }
    
    // Video configuration
    void SetVideoEnabled(bool enabled);
    bool IsVideoEnabled() const { return _videoEnabled; }
    void SetVideoResolution(uint32_t width, uint32_t height);
    void SetVideoFrameRate(float fps);
    void SetVideoCodec(const std::string& codec);
    void SetVideoBitrate(uint32_t kbps);
    
    // Audio configuration (global)
    void SetAudioSampleRate(uint32_t sampleRate);
    
    // Audio track management (for multi-track mode)
    void AddAudioTrack(const AudioTrackConfig& config);
    void RemoveAudioTrack(size_t index);
    void ClearAudioTracks();
    size_t GetAudioTrackCount() const { return _audioTracks.size(); }
    const AudioTrackConfig& GetAudioTrack(size_t index) const { return _audioTracks[index]; }
    
    // Simple audio source selection (for single-track mode)
    void SelectAudioSource(AudioSourceType source);
    
    /// endregion </Configuration>

protected:
    /// region <Internal state>
    EmulatorContext* _context = nullptr;

    // Recording state
    bool _isRecording = false;
    bool _isPaused = false;
    
    // Recording mode
    RecordingMode _recordingMode = RecordingMode::SingleTrack;

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
    
    // Audio tracks (for multi-track mode)
    std::vector<AudioTrackConfig> _audioTracks;
    
    // Selected audio source (for single-track mode)
    AudioSourceType _selectedSource = AudioSourceType::MasterMix;

    // Emulated time tracking
    uint64_t _emulatedFrameCount = 0;
    uint64_t _emulatedAudioSampleCount = 0;

    // Statistics
    RecordingStats _stats;
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

