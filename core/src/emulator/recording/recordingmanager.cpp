#include "recordingmanager.h"

#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"

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

/// endregion </Helper functions>

RecordingManager::RecordingManager(EmulatorContext* context) : _context(context)
{
    // Initialize logger from context
    _logger = context->pModuleLogger;

    MLOGINFO("RecordingManager::RecordingManager - Instance created");
}

RecordingManager::~RecordingManager()
{
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

    // Reset state
    Reset();

    // TODO: Initialize encoder libraries (FFmpeg, libav, etc.)
    // TODO: Query available codecs and formats
}

void RecordingManager::Reset()
{
    MLOGDEBUG("RecordingManager::Reset - Resetting recording manager");

    // Stop recording if active
    if (_isRecording)
    {
        StopRecording();
    }

    // Reset counters
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
        return false;
    }

    if (_isRecording)
    {
        MLOGWARNING("RecordingManager::StartRecording - Already recording, stop current recording first");
        return false;
    }

    MLOGINFO("RecordingManager::StartRecording - Starting recording to '%s'", filename.c_str());

    // Determine if video-only or audio-only based on codec parameters
    _videoEnabled = !videoCodec.empty();
    _videoCodec = videoCodec;
    _audioCodec = audioCodec;
    _videoBitrate = videoBitrate;
    _audioBitrate = audioBitrate;

    MLOGINFO("  Mode: %s", _videoEnabled ? "Video+Audio" : "Audio-Only");
    if (_videoEnabled)
    {
        MLOGINFO("  Video: codec=%s, bitrate=%u kbps", videoCodec.c_str(), videoBitrate);
    }
    MLOGINFO("  Audio: codec=%s, bitrate=%u kbps", audioCodec.c_str(), audioBitrate);

    // Store configuration
    _outputFilename = filename;

    // Use native framebuffer dimensions if not explicitly set
    if (_videoEnabled && (_videoWidth == 0 || _videoHeight == 0))
    {
        FramebufferDescriptor fb = _context->pScreen->GetFramebufferDescriptor();
        _videoWidth = fb.width;
        _videoHeight = fb.height;
    }

    if (_videoEnabled)
    {
        MLOGINFO("  Resolution: %ux%u @ %.2f fps", _videoWidth, _videoHeight, _videoFrameRate);
    }
    MLOGINFO("  Audio: %u Hz, %u channels", _audioSampleRate, _audioChannels);

    // Setup default single-track configuration if no tracks configured
    if (_audioTracks.empty())
    {
        AudioTrackConfig defaultTrack;
        defaultTrack.name = "Master Audio";
        defaultTrack.source = _selectedSource;
        defaultTrack.codec = audioCodec;
        defaultTrack.bitrate = audioBitrate > 0 ? audioBitrate : 192;
        _audioTracks.push_back(defaultTrack);
    }

    MLOGINFO("  Audio tracks: %zu", _audioTracks.size());

    // Initialize encoder
    if (!InitializeEncoder())
    {
        MLOGERROR("RecordingManager::StartRecording - Failed to initialize encoder");
        return false;
    }

    // Reset counters
    _emulatedFrameCount = 0;
    _emulatedAudioSampleCount = 0;
    _stats = RecordingStats();

    // Start recording
    _isRecording = true;
    _isPaused = false;

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

    // Store configuration
    _outputFilename = filename;

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

    // Start recording
    _isRecording = true;
    _isPaused = false;

    MLOGINFO("RecordingManager::StartRecordingEx - Recording started successfully");
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
    MLOGINFO("  Frames recorded: %llu", _stats.framesRecorded);
    MLOGINFO("  Audio samples: %llu", _stats.audioSamplesRecorded);
    MLOGINFO("  Duration: %.2f seconds (emulated time)", _stats.recordedDuration);

    // Finalize encoder and close output file
    FinalizeEncoder();

    // Stop recording
    _isRecording = false;
    _isPaused = false;

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
}

/// endregion </Recording control>

/// region <Frame/Audio capture>

void RecordingManager::CaptureFrame(const FramebufferDescriptor& framebuffer)
{
    // Skip if not recording or paused
    if (!IsRecording())
    {
        return;
    }

    // Calculate presentation timestamp based on emulated frame count
    double timestamp = static_cast<double>(_emulatedFrameCount) / _videoFrameRate;

    // Encode video frame
    EncodeVideoFrame(framebuffer, timestamp);

    // Update statistics
    _stats.framesRecorded++;
    _stats.recordedDuration = timestamp;
    _emulatedFrameCount++;
}

void RecordingManager::CaptureAudio(const int16_t* samples, size_t sampleCount)
{
    // Skip if not recording, paused, or no audio tracks configured
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

void RecordingManager::OnFrameEnd(const FramebufferDescriptor& framebuffer, const int16_t* audioSamples,
                                  size_t audioSampleCount)
{
    // Feature guard - early exit if recording disabled
    if (!_featureEnabled)
    {
        return;
    }

    // Skip if no active encoders
    if (_activeEncoders.empty())
    {
        return;
    }

    // Calculate timestamp based on emulated frame count
    double timestamp = static_cast<double>(_emulatedFrameCount) / _videoFrameRate;

    // Dispatch to all active encoders
    for (auto& encoder : _activeEncoders)
    {
        if (encoder->IsRecording())
        {
            encoder->OnVideoFrame(framebuffer, timestamp);
            encoder->OnAudioSamples(audioSamples, audioSampleCount, timestamp);
        }
    }

    // Update frame count for timestamp calculation
    _emulatedFrameCount++;
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

/// region <Encoder interface (STUBS - to be implemented)>

bool RecordingManager::InitializeEncoder()
{
    MLOGINFO("RecordingManager::InitializeEncoder - TODO: Initialize encoder (FFmpeg, libav, etc.)");

    // TODO: Initialize video encoder
    //   - Open output file
    //   - Initialize video codec context
    //   - Set video parameters (resolution, frame rate, bitrate, codec)
    //   - Allocate video frame buffers

    // TODO: Initialize audio encoder (if audio enabled)
    //   - Initialize audio codec context
    //   - Set audio parameters (sample rate, channels, bitrate, codec)
    //   - Allocate audio frame buffers

    // TODO: Write container header
    //   - Initialize muxer (MP4, AVI, MKV, etc.)
    //   - Add video/audio streams
    //   - Write header

    // STUB: Return false to prevent actual recording attempts
    MLOGWARNING("RecordingManager::InitializeEncoder - Encoder not implemented, recording disabled");
    return false;
}

void RecordingManager::FinalizeEncoder()
{
    MLOGINFO("RecordingManager::FinalizeEncoder - TODO: Finalize encoder and close output file");

    // TODO: Flush encoder buffers
    //   - Flush remaining video frames
    //   - Flush remaining audio frames

    // TODO: Write container trailer
    //   - Write muxer trailer
    //   - Update file header with final statistics

    // TODO: Close output file
    //   - Close codec contexts
    //   - Free buffers
    //   - Close file handle

    // TODO: Update final statistics
    //   - File size
    //   - Final duration
    //   - Average encoding time
}

void RecordingManager::EncodeVideoFrame([[maybe_unused]] const FramebufferDescriptor& framebuffer,
                                        [[maybe_unused]] double timestamp)
{
    // TODO: Convert framebuffer to encoder format
    //   - RGB/RGBA → YUV420 (for most codecs)
    //   - Scale if resolution differs from native
    //   - Apply color space conversion

    // TODO: Encode frame
    //   - Pass frame to video encoder
    //   - Set presentation timestamp
    //   - Encode and get compressed packets

    // TODO: Write encoded packets to output
    //   - Mux video packets
    //   - Interleave with audio packets
    //   - Write to file

    // STUB: Log frame capture (disabled to avoid spam)
    // MLOGDEBUG("RecordingManager::EncodeVideoFrame - Frame %llu @ %.3f sec", _emulatedFrameCount, timestamp);
}

void RecordingManager::EncodeAudioSamples([[maybe_unused]] const int16_t* samples, [[maybe_unused]] size_t sampleCount,
                                          [[maybe_unused]] double timestamp)
{
    // TODO: Resample if needed
    //   - Convert sample rate if encoder rate differs
    //   - Convert from stereo to mono or vice versa if needed

    // TODO: Encode audio
    //   - Pass samples to audio encoder
    //   - Set presentation timestamp
    //   - Encode and get compressed packets

    // TODO: Write encoded packets to output
    //   - Mux audio packets
    //   - Interleave with video packets
    //   - Write to file

    // STUB: Log audio capture (disabled to avoid spam)
    // MLOGDEBUG("RecordingManager::EncodeAudioSamples - %zu samples @ %.3f sec", sampleCount, timestamp);
}

/// endregion </Encoder interface (STUBS - to be implemented)>
