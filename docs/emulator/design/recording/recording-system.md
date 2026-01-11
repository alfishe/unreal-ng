# Recording System Architecture

## Overview

The Recording System provides flexible video and audio capture capabilities for the emulator. It supports:
- **Video+Audio recording** - Standard gameplay recordings
- **Audio-only recording** - For music playback, demos, sound effects
- **Multi-track recording** - Capture individual audio sources separately
- **Flexible source selection** - Record any combination of audio devices/channels

All recordings are based on **emulated time**, ensuring that turbo mode recordings play back at normal speed with correct timing.

## Master Switch Feature

The recording subsystem is guarded by the `recording` feature in `FeatureManager`:

| Feature | ID | Alias | Default | Description |
|---------|-----|-------|---------|-------------|
| Recording | `recording` | `rec` | **OFF** | Enable recording subsystem (video, audio, GIF capture) |

**Behavior:**
- **OFF (default)**: RecordingManager API calls early-exit with zero CPU overhead. Heavy functionality disabled by default.
- **ON**: RecordingManager is active and ready for recording commands.

```cpp
// Enable recording subsystem
featureManager->setFeature("recording", true);

// Now RecordingManager API is available
recordingManager->StartRecording("gameplay.mp4");
```

**CLI/WebAPI:**
```bash
feature recording on   # Enable recording subsystem
feature recording off  # Disable (default)
```

> **Note:** The `recording` feature is a **master switch only**. Individual recording actions (start, stop, codec selection) are API commands, not separate features.


## Architecture

### Core Components

```
┌─────────────────────────────────────────────────────────────┐
│                   RecordingManager                          │
│  - Lifecycle management (Start/Stop/Pause/Resume)           │
│  - Configuration (codecs, bitrate, formats)                 │
│  - Multi-track coordination                                 │
└──────────────┬────────────────────────────┬─────────────────┘
               │                            │
       ┌───────▼────────┐          ┌────────▼────────┐
       │  VideoCapture  │          │  AudioCapture   │
       │  - Frame queue │          │  - Multi-track  │
       │  - Timestamps  │          │  - Source mixer │
       └───────┬────────┘          └────────┬────────┘
               │                            │
       ┌───────▼────────────────────────────▼─────────┐
       │           EncoderInterface                   │
       │  - FFmpeg/libav integration (future)         │
       │  - Video codec (H.264, H.265, VP9, etc.)     │
       │  - Audio codec (AAC, MP3, FLAC, WAV, etc.)   │
       │  - Container muxing (MP4, MKV, AVI, etc.)    │
       └──────────────────────────────────────────────┘
```

### Audio Sources Hierarchy

The ZX Spectrum emulator supports multiple audio devices and channels:

```
Audio Output Tree:
├─ Final Mixed Output (Master)        [Stereo, 16-bit, 44.1 kHz]
│
├─ Beeper                              [Mono → Stereo]
│  └─ Digital 1-bit output
│
├─ AY-3-8910 Chip #1                  [3 channels → Stereo]
│  ├─ Channel A (Tone/Noise)
│  ├─ Channel B (Tone/Noise)
│  └─ Channel C (Tone/Noise)
│
├─ AY-3-8910 Chip #2 (TurboSound)     [3 channels → Stereo]
│  ├─ Channel A
│  ├─ Channel B
│  └─ Channel C
│
├─ AY-3-8910 Chip #3 (TurboSound)     [3 channels → Stereo]
│  ├─ Channel A
│  ├─ Channel B
│  └─ Channel C
│
├─ COVOX / DAC                        [Mono → Stereo]
│  └─ 8-bit digital audio
│
├─ General Sound (GS)                 [Multi-channel, depends on config]
│  └─ Wavetable synthesis
│
└─ Moonsound / OPL4                   [Stereo]
   └─ FM synthesis + PCM samples
```

**Total potential channels:**
- Beeper: 1 channel
- AY chips: 3 chips × 3 channels = 9 channels
- COVOX: 1 channel
- General Sound: N channels (configurable)
- Moonsound: 2 channels (stereo)

**Maximum:** ~15+ individual audio sources

## Audio Source Selection

### Recording Modes

#### 1. **Single-Track Mode** (Default)
Records final mixed output only.

```cpp
// Example: Record mixed audio
recordingManager->SetRecordingMode(RecordingMode::SingleTrack);
recordingManager->SelectAudioSource(AudioSource::MasterMix);
recordingManager->StartRecording("gameplay.mp4");
```

**Use cases:**
- Standard gameplay recordings
- YouTube videos
- Quick captures

#### 2. **Multi-Track Mode**
Records multiple audio sources to separate tracks in output file.

```cpp
// Example: Record AY chip separately from rest
recordingManager->SetRecordingMode(RecordingMode::MultiTrack);
recordingManager->AddAudioTrack("Master Mix", AudioSource::MasterMix);
recordingManager->AddAudioTrack("AY Chip 1", AudioSource::AY1_All);
recordingManager->AddAudioTrack("Beeper", AudioSource::Beeper);
recordingManager->StartRecording("demo.mkv");  // MKV supports multiple audio tracks
```

**Use cases:**
- Music production (remix/remaster)
- Sound effect extraction
- Audio analysis
- Post-processing flexibility

#### 3. **Channel-Split Mode**
Records individual AY channels separately.

```cpp
// Example: Record each AY channel separately
recordingManager->SetRecordingMode(RecordingMode::ChannelSplit);
recordingManager->AddAudioTrack("AY1 Channel A", AudioSource::AY1_ChannelA);
recordingManager->AddAudioTrack("AY1 Channel B", AudioSource::AY1_ChannelB);
recordingManager->AddAudioTrack("AY1 Channel C", AudioSource::AY1_ChannelC);
recordingManager->StartRecording("ay_music.flac");
```

**Use cases:**
- Music transcription
- Channel-by-channel analysis
- Remixing individual voices

#### 4. **Audio-Only Mode**
Records audio without video.

```cpp
// Example: Record audio to WAV file
recordingManager->SetRecordingMode(RecordingMode::AudioOnly);
recordingManager->SelectAudioSource(AudioSource::MasterMix);
recordingManager->StartRecording("music.wav", "", "pcm_s16le");
```

**Use cases:**
- Music archives
- Sound effect libraries
- Audio-only demos

### Audio Source Enumeration

```cpp
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
```

### Audio Track Configuration

```cpp
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
```

## Recording Lifecycle

### 1. Configuration Phase

```cpp
RecordingManager* recording = context->pRecordingManager;

// Configure output format
recording->SetVideoEnabled(true);              // Enable/disable video
recording->SetRecordingMode(RecordingMode::MultiTrack);

// Configure video (if enabled)
recording->SetVideoResolution(640, 480);       // Upscale from native 256x192
recording->SetVideoFrameRate(50.0f);           // ZX Spectrum native rate
recording->SetVideoCodec("h264");
recording->SetVideoBitrate(5000);              // 5 Mbps

// Add audio tracks
AudioTrackConfig masterTrack;
masterTrack.name = "Master Audio";
masterTrack.source = AudioSourceType::MasterMix;
masterTrack.codec = "aac";
masterTrack.bitrate = 256;
recording->AddAudioTrack(masterTrack);

AudioTrackConfig ayTrack;
ayTrack.name = "AY Music";
ayTrack.source = AudioSourceType::AY1_All;
ayTrack.codec = "flac";                        // Lossless for music
recording->AddAudioTrack(ayTrack);
```

### 2. Recording Phase

```cpp
// Start recording
bool success = recording->StartRecording("output.mkv");
if (success)
{
    // Enable turbo mode for fast recording
    emulator->EnableTurboMode(true);  // with audio generation
    
    // Run emulation...
    // Frames and audio are automatically captured
    
    // Recording can be paused/resumed
    recording->PauseRecording();
    // ... user pauses emulation ...
    recording->ResumeRecording();
}
```

### 3. Finalization Phase

```cpp
// Stop recording
recording->StopRecording();

// Get recording statistics
RecordingStats stats = recording->GetStats();
std::cout << "Recorded " << stats.framesRecorded << " frames\n";
std::cout << "Duration: " << stats.recordedDuration << " seconds\n";
std::cout << "File size: " << stats.outputFileSize << " bytes\n";
```

## Implementation Details

### Audio Capture Pipeline

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│  Sound       │     │  Track       │     │  Audio       │
│  Devices     │────▶│  Router      │────▶│  Encoder     │
│  (Sources)   │     │  (Mixer)     │     │  (Codec)     │
└──────────────┘     └──────────────┘     └──────────────┘
       │                    │                     │
       │                    │                     │
   Beeper              Volume/Pan            FFmpeg/libav
   AY chips            Selection             AAC/MP3/FLAC
   COVOX               Buffering             WAV/OGG/etc.
   etc.
```

**Audio routing:**
1. **Source Generation** - Each audio device generates samples
2. **Track Router** - Routes samples to configured tracks
3. **Track Processing** - Apply volume, pan, resampling
4. **Encoding** - Compress audio (or store raw)
5. **Muxing** - Interleave with video (if present)

### Video Capture Pipeline

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│  Screen      │     │  Color       │     │  Video       │
│  Renderer    │────▶│  Converter   │────▶│  Encoder     │
│  (RGB32)     │     │  (YUV420)    │     │  (H.264)     │
└──────────────┘     └──────────────┘     └──────────────┘
```

**Video routing:**
1. **Frame Capture** - Grab rendered framebuffer (RGB/RGBA)
2. **Color Conversion** - Convert to YUV420 (for most codecs)
3. **Scaling** - Upscale from native resolution (optional)
4. **Encoding** - Compress video frame
5. **Muxing** - Interleave with audio

### Timestamp Synchronization

**Critical requirement:** Audio and video timestamps must be based on **emulated time**, not wall-clock time.

```cpp
// In MainLoop::OnFrameEnd()
void MainLoop::OnFrameEnd()
{
    // ... existing code ...
    
    // Calculate emulated timestamp
    double emulatedTime = _context->emulatorState.frame_counter / 50.0;  // 50 Hz
    
    // Capture video frame with emulated timestamp
    if (_recordingManager->IsRecording() && _recordingManager->IsVideoEnabled())
    {
        _recordingManager->CaptureFrame(framebuffer, emulatedTime);
    }
    
    // Capture audio with emulated timestamp
    if (_recordingManager->IsRecording())
    {
        double audioTimestamp = _context->emulatorState.t_states / 3500000.0;  // 3.5 MHz
        _recordingManager->CaptureAudio(audioBuffer, sampleCount, audioTimestamp);
    }
}
```

**Why emulated time matters:**
- Turbo mode runs 10x faster (wall-clock)
- But emulates 10 seconds in 1 second (wall-clock)
- Recording must capture all 10 seconds worth of frames/audio
- Output file plays back at normal speed (10 seconds duration)

## Supported Output Formats

### Video+Audio Formats

| Container | Video Codecs | Audio Codecs | Multi-Track Audio |
|-----------|-------------|--------------|-------------------|
| **MP4**   | H.264, H.265, VP9 | AAC, MP3 | Limited (1-2 tracks) |
| **MKV**   | H.264, H.265, VP9, AV1 | AAC, MP3, FLAC, Vorbis, Opus | **Yes** (unlimited) |
| **AVI**   | H.264, MJPEG, FFV1 | PCM, MP3, AAC | Limited (1-2 tracks) |
| **WebM**  | VP8, VP9, AV1 | Vorbis, Opus | Yes (multiple) |

**Recommendation:** Use **MKV** for multi-track recordings (most flexible).

### Animated Image Formats (Video-Only)

| Format | Quality | File Size | Compatibility | Transparency | Best For |
|--------|---------|-----------|---------------|--------------|----------|
| **GIF**   | Low (256 colors) | Small | Universal | Yes | Web sharing, memes, forums |
| **WebP**  | Good (24-bit) | Small | Modern browsers | Yes | Better than GIF, smaller files |
| **AVIF**  | Excellent | Very Small | Chrome 85+, Firefox 93+ | Yes | High quality, smallest size |
| **APNG**  | Perfect (lossless) | Medium | Good | Yes | PNG-quality animations |

**Note:** Animated formats do not support audio. Use for short clips (5-30 seconds) and web embedding.

**Recommendations:**
- **GIF**: Maximum compatibility, simple content
- **WebP**: Modern web, better quality than GIF
- **AVIF**: Smallest file size, high quality (slow encoding)
- **APNG**: Lossless quality with transparency

### Audio-Only Formats

| Container | Audio Codecs | Multi-Track | Lossless |
|-----------|-------------|-------------|----------|
| **WAV**   | PCM (16/24/32-bit) | Yes | **Yes** |
| **FLAC**  | FLAC | No | **Yes** |
| **MP3**   | MP3 | No | No |
| **OGG**   | Vorbis, Opus | No | No |
| **M4A**   | AAC, ALAC | No | ALAC only |

**Recommendations:**
- **WAV**: Multi-track, uncompressed (large files)
- **FLAC**: Single-track, compressed lossless (best quality/size ratio)
- **MP3**: Single-track, lossy compression (small files)

## Use Cases

### Use Case 1: Standard Gameplay Recording

```cpp
// Simple video+audio recording
recordingManager->SetRecordingMode(RecordingMode::SingleTrack);
recordingManager->SelectAudioSource(AudioSourceType::MasterMix);
recordingManager->StartRecording("gameplay.mp4", "h264", "aac");
```

**Output:** Standard MP4 file with gameplay video and audio.

### Use Case 2: Music Demo Recording (Multi-Track)

```cpp
// Capture music with separate tracks for remixing
recordingManager->SetRecordingMode(RecordingMode::MultiTrack);
recordingManager->SetVideoEnabled(false);  // Audio only

// Add individual AY channels
for (int i = 0; i < 3; i++)
{
    AudioTrackConfig track;
    track.name = "AY1 Channel " + std::string(1, 'A' + i);
    track.source = AudioSourceType::AY1_ChannelA + i;
    track.codec = "flac";  // Lossless
    recordingManager->AddAudioTrack(track);
}

recordingManager->StartRecording("music.flac");
```

**Output:** FLAC file with 3 separate audio tracks (one per AY channel).

### Use Case 3: Sound Effect Extraction

```cpp
// Record specific sound device
recordingManager->SetRecordingMode(RecordingMode::AudioOnly);
recordingManager->SelectAudioSource(AudioSourceType::Beeper);
recordingManager->StartRecording("beeper_sfx.wav", "", "pcm_s16le");
```

**Output:** WAV file containing only beeper sound effects.

### Use Case 4: Complete Audio Archive

```cpp
// Record all audio devices to separate tracks
recordingManager->SetRecordingMode(RecordingMode::MultiTrack);
recordingManager->SetVideoEnabled(true);

// Add all sources
AudioSourceType sources[] = {
    AudioSourceType::Beeper,
    AudioSourceType::AY1_All,
    AudioSourceType::AY2_All,
    AudioSourceType::COVOX
};

for (auto source : sources)
{
    AudioTrackConfig track;
    track.name = GetSourceName(source);
    track.source = source;
    track.codec = "flac";
    recordingManager->AddAudioTrack(track);
}

recordingManager->StartRecording("complete.mkv");
```

**Output:** MKV file with video + 4 audio tracks (all devices).

### Use Case 5: Web Sharing (Animated GIF/WebP)

```cpp
// Record short clip for social media
recordingManager->SetRecordingMode(RecordingMode::SingleTrack);
recordingManager->SetVideoFrameRate(15.0f);     // Lower FPS = smaller file
recordingManager->SetAudioEnabled(false);       // Animated images don't support audio

// Option A: GIF (maximum compatibility)
recordingManager->StartRecording("clip.gif", "gif", "");

// Option B: WebP (better quality, smaller size)
recordingManager->StartRecording("clip.webp", "libwebp", "");

// Option C: AVIF (best quality, smallest size, slow encoding)
recordingManager->StartRecording("clip.avif", "libaom-av1", "");

// ... run for 10 seconds ...

recordingManager->StopRecording();
```

**Output:** Animated image file (GIF/WebP/AVIF) for embedding in websites, forums, or social media.

## Performance Considerations

### CPU Usage

**Recording impact:**
- Video encoding: **High** (H.264/H.265 compression)
- Audio encoding: **Low** (AAC/MP3) to **Medium** (FLAC)
- Multi-track: **Linear** (scales with track count)

**Recommendations:**
- Use turbo mode for non-real-time recording (encode faster than playback)
- Use hardware acceleration (GPU encoding) when available
- Reduce video bitrate/resolution if CPU-limited

### Memory Usage

**Buffering requirements:**
- Video frames: ~300 KB per frame (256×192×4 bytes RGB32)
- Audio samples: ~4 KB per frame (882 samples × 2 channels × 2 bytes)
- Multi-track: Linear scaling (4 KB × track count)

**Buffer strategy:**
- Ring buffer with 1-2 seconds of data (~50-100 frames)
- Encoder runs in separate thread
- Drop frames if encoder can't keep up (with warning)

### Disk I/O

**Write patterns:**
- Interleaved video/audio packets
- Sequential writes (optimal for HDD/SSD)
- Batch writes to reduce syscall overhead

**Estimates:**
- Video: ~5 Mbps = 625 KB/s
- Audio: ~192 Kbps = 24 KB/s per track
- Total: ~650 KB/s (single track) to ~800 KB/s (4 tracks)

## Future Extensions

### 1. Live Streaming
- RTMP/RTSP output
- Network streaming to Twitch/YouTube
- Low-latency encoding

### 2. Screenshot Burst Mode
- Capture multiple frames rapidly
- Export as PNG/JPEG sequence
- Useful for animations/sprites

### 3. Audio Filters
- Real-time effects (reverb, EQ, etc.)
- Noise reduction
- Volume normalization

### 4. Format Presets
- "YouTube" preset (1080p H.264 AAC)
- "Archive" preset (lossless FLAC)
- "Streaming" preset (low latency)
- "Music" preset (multi-track FLAC)

## API Summary

```cpp
// Configuration
recordingManager->SetRecordingMode(RecordingMode mode);
recordingManager->SetVideoEnabled(bool enabled);
recordingManager->SetVideoResolution(uint32_t width, uint32_t height);
recordingManager->SetVideoFrameRate(float fps);
recordingManager->SetVideoCodec(const std::string& codec);
recordingManager->SetVideoBitrate(uint32_t kbps);

// Audio track management
recordingManager->AddAudioTrack(const AudioTrackConfig& config);
recordingManager->RemoveAudioTrack(size_t index);
recordingManager->ClearAudioTracks();
recordingManager->SelectAudioSource(AudioSourceType source);  // Single-track helper

// Recording control
recordingManager->StartRecording(const std::string& filename);
recordingManager->StopRecording();
recordingManager->PauseRecording();
recordingManager->ResumeRecording();

// Status/Statistics
bool isRecording = recordingManager->IsRecording();
RecordingStats stats = recordingManager->GetStats();
```

## References

- FFmpeg documentation: https://ffmpeg.org/documentation.html
- Container format specs (MP4, MKV, AVI)
- Audio codec specs (AAC, MP3, FLAC)
- Video codec specs (H.264, H.265, VP9)

