# Encoder Architecture

## Overview

The Encoder Architecture provides a flexible, extensible system for recording emulator output to various formats. All encoders implement a common interface (`EncoderBase`) and are managed by `RecordingManager`.

---

## Architecture Diagram

```mermaid
graph TB
    subgraph EmulatorCore["Emulator Core"]
        ML[MainLoop] -->|OnFrameEnd| RM[RecordingManager]
        FM[FeatureManager] -->|UpdateFeatureCache| RM
    end
    
    subgraph RecordingManager
        RM -->|OnVideoFrame| ER[Encoder Registry]
        RM -->|OnAudioSamples| ER
    end
    
    subgraph Encoders["Active Encoders"]
        ER --> GIF[GIF Encoder]
        ER --> H264[H.264 Encoder]
        ER --> FLAC[FLAC Encoder]
        ER --> WAV[WAV Encoder]
    end
    
    subgraph Output["Output Files"]
        GIF --> O1[animation.gif]
        H264 --> O2[gameplay.mp4]
        FLAC --> O3[soundtrack.flac]
        WAV --> O4[audio.wav]
    end
```

---

## Feature Flag Guard

Recording is controlled by the `recording` feature in FeatureManager:

```mermaid
sequenceDiagram
    participant User
    participant FM as FeatureManager
    participant RM as RecordingManager
    participant E as GIFEncoder
    
    User->>FM: feature recording on
    FM->>RM: UpdateFeatureCache()
    RM->>RM: _featureEnabled = true
    
    Note over RM: Recording now available
    
    User->>RM: SetVideoCodec("gif")
    User->>RM: SetVideoEnabled(true)
    
    Note over RM: Select encoder based on format
    
    RM->>E: new GIFEncoder()
    RM->>RM: _activeEncoders.push_back(encoder)
    
    User->>RM: StartRecordingEx("demo.gif")
    RM->>E: Start("demo.gif", config)
    
    loop Every Frame (OnFrameEnd)
        RM->>E: OnVideoFrame(framebuffer, ts)
        RM->>E: OnAudioSamples(audio, count, ts)
    end
    
    User->>RM: StopRecording()
    RM->>E: Stop()
```

---

## EncoderBase Interface

All encoders implement this abstract interface:

```cpp
class EncoderBase {
public:
    // Lifecycle
    virtual bool Start(const std::string& filename, const EncoderConfig& config) = 0;
    virtual void Stop() = 0;
    
    // Frame input (encoders implement what they need)
    virtual void OnVideoFrame(const FramebufferDescriptor& fb, double timestamp) {}
    virtual void OnAudioSamples(const int16_t* samples, size_t count, double timestamp) {}
    
    // State queries
    virtual bool IsRecording() const = 0;
    virtual std::string GetType() const = 0;
    virtual bool SupportsVideo() const = 0;
    virtual bool SupportsAudio() const = 0;
};
```

---

## Encoder Registry

RecordingManager maintains a flat list of active encoders:

```cpp
std::vector<EncoderPtr> _activeEncoders;
```

**OnFrameEnd dispatch:**
```cpp
void RecordingManager::OnFrameEnd(framebuffer, audio, audioCount) {
    if (!_featureEnabled) return;
    
    for (auto& encoder : _activeEncoders) {
        encoder->OnVideoFrame(framebuffer, timestamp);
        encoder->OnAudioSamples(audio, audioCount, timestamp);
    }
}
```

---

## Encoder Categories

| Category | Video | Audio | Examples |
|----------|-------|-------|----------|
| **Video+Audio** | ✅ | ✅ | H.264+AAC, VP9+Opus, AVI |
| **Video-only** | ✅ | ❌ | GIF, PNG sequence, WebP |
| **Audio-only** | ❌ | ✅ | FLAC, WAV, MP3, AAC |

---

## Video Capture Region

Recording can capture different regions of the emulator framebuffer:

### Capture Modes

| Mode | Dimensions | Description |
|------|------------|-------------|
| **MainScreen** | 256×192 | ZX Spectrum main display area only (no border) |
| **FullFrame** | Varies by model | Full framebuffer including border |

### Model-Specific Full Frame Sizes

| Platform | Full Frame Size | Notes |
|----------|-----------------|-------|
| **ZX Spectrum 48K/128K** | 320×240 | Standard PAL timing |
| **Pentagon 128** | 320×240 | Slightly different border timing |
| **Scorpion ZS 256** | 320×240 | Standard size |
| **TSConf** | 360×288 (configurable) | Extended border modes available |
| **ATM Turbo 2+** | 320×240 | Standard |

### Configuration

```cpp
enum class VideoCaptureRegion
{
    MainScreen,     // 256×192 - main display only
    FullFrame       // Full framebuffer with border
};

// RecordingManager API
recordingManager->SetCaptureRegion(VideoCaptureRegion::MainScreen);  // Smaller files
recordingManager->SetCaptureRegion(VideoCaptureRegion::FullFrame);   // Includes border effects
```

### Use Cases

| Region | Use Case |
|--------|----------|
| **MainScreen** | Smaller files, game content only, consistent size across models |
| **FullFrame** | Border effects, loading stripes, demos with border tricks |

> **Note:** Hi-res modes (Timex, TSConf multicolor) typically use FullFrame capture.

## Recording Scenarios

### H.264 Video + AAC Audio Recording

Full gameplay recording with video and audio to MP4 container:

```mermaid
sequenceDiagram
    participant User
    participant RM as RecordingManager
    participant H264 as H264Encoder
    participant FFmpeg
    
    User->>RM: SetVideoCodec("h264")
    User->>RM: SetAudioCodec("aac")
    User->>RM: SetVideoBitrate(5000)
    User->>RM: SetAudioSampleRate(44100)
    
    Note over RM: Create H.264 encoder
    
    RM->>H264: new H264Encoder()
    
    User->>RM: StartRecordingEx("gameplay.mp4")
    RM->>H264: Start("gameplay.mp4", config)
    H264->>FFmpeg: avformat_alloc_output_context2()
    H264->>FFmpeg: avcodec_open2(h264)
    H264->>FFmpeg: avcodec_open2(aac)
    
    loop Every Frame (OnFrameEnd)
        RM->>H264: OnVideoFrame(framebuffer, ts)
        H264->>FFmpeg: encode_video_frame()
        
        RM->>H264: OnAudioSamples(audio, count, ts)
        H264->>FFmpeg: encode_audio_samples()
    end
    
    User->>RM: StopRecording()
    RM->>H264: Stop()
    H264->>FFmpeg: av_write_trailer()
```

**Data Flow - Video + Audio:**

```mermaid
graph LR
    subgraph Emulator
        FB[Framebuffer<br/>320x240 RGBA] 
        AB[Audio Buffer<br/>882 samples/frame]
    end
    
    subgraph H264Encoder
        VE[Video Encoder<br/>RGB→YUV→H.264]
        AE[Audio Encoder<br/>PCM→AAC]
        MUX[MP4 Muxer]
    end
    
    FB -->|OnVideoFrame| VE
    AB -->|OnAudioSamples| AE
    VE --> MUX
    AE --> MUX
    MUX --> OUT[gameplay.mp4]
```

---

### Audio-Only Recording (FLAC)

High-quality lossless audio capture for music preservation:

```mermaid
sequenceDiagram
    participant User
    participant RM as RecordingManager
    participant FLAC as FLACEncoder
    
    User->>RM: SetVideoEnabled(false)
    User->>RM: SetAudioCodec("flac")
    User->>RM: SetAudioSampleRate(44100)
    User->>RM: SelectAudioSource(AY1_All)
    
    Note over RM: Create FLAC encoder (audio-only)
    
    RM->>FLAC: new FLACEncoder()
    
    User->>RM: StartRecordingEx("soundtrack.flac")
    RM->>FLAC: Start("soundtrack.flac", config)
    
    loop Every Frame (OnFrameEnd)
        RM->>FLAC: OnVideoFrame(framebuffer, ts)
        Note over FLAC: Ignored (audio-only)
        
        RM->>FLAC: OnAudioSamples(audio, count, ts)
        FLAC->>FLAC: Compress & write samples
    end
    
    User->>RM: StopRecording()
    RM->>FLAC: Stop()
```

**Data Flow - Audio Only:**

```mermaid
graph LR
    subgraph Emulator
        FB[Framebuffer] 
        AB[Audio Buffer<br/>44.1kHz stereo]
    end
    
    subgraph FLACEncoder
        AE[FLAC Encoder<br/>PCM→FLAC]
    end
    
    FB -.->|OnVideoFrame<br/>ignored| AE
    AB -->|OnAudioSamples| AE
    AE --> OUT[soundtrack.flac]
    
    style FB stroke-dasharray: 5 5
```

---

### Simultaneous Multiple Encoders

Record to multiple formats at once (e.g., MP4 + GIF preview):

```mermaid
graph TB
    subgraph RecordingManager
        RM[OnFrameEnd]
    end
    
    subgraph ActiveEncoders["Active Encoders (parallel)"]
        E1[H264Encoder]
        E2[GIFEncoder]
        E3[FLACEncoder]
    end
    
    RM --> E1
    RM --> E2
    RM --> E3
    
    E1 --> O1[gameplay.mp4<br/>Full quality]
    E2 --> O2[preview.gif<br/>15fps, 256 colors]
    E3 --> O3[soundtrack.flac<br/>Lossless audio]
```

## Envisioned Encoders

### Implemented

| Encoder | Type | Format | Status |
|---------|------|--------|--------|
| **GIF** | Video-only | .gif | ✅ Implemented |

### Planned - Video

| Encoder | Type | Format | Priority | Notes |
|---------|------|--------|----------|-------|
| **H.264** | Video+Audio | .mp4 | High | FFmpeg/libx264, HW accel available |
| **VP9** | Video+Audio | .webm | Medium | Royalty-free, FFmpeg |
| **PNG Sequence** | Video-only | .png | Low | Frame-by-frame, lossless |
| **WebP Animation** | Video-only | .webp | Low | Modern GIF alternative |

### Planned - Audio

| Encoder | Type | Format | Priority | Notes |
|---------|------|--------|----------|-------|
| **WAV** | Audio-only | .wav | High | Lossless, simple, no dependencies |
| **FLAC** | Audio-only | .flac | High | Lossless, compressed |
| **MP3** | Audio-only | .mp3 | Medium | Lossy, widely compatible |
| **Ogg Vorbis** | Audio-only | .ogg | Medium | Lossy, royalty-free |

---

## Hardware Acceleration

Video encoding can be HW accelerated for significantly improved performance. This is highly platform-specific:

| Platform | SDK | API | H.264 | HEVC | Notes |
|----------|-----|-----|-------|------|-------|
| **macOS** | VideoToolbox | `VTCompressionSession` | ✅ | ✅ | Apple Silicon optimized |
| **Windows** | Media Foundation | `IMFTransform` | ✅ | ✅ | Requires Windows 10+ |
| **Windows** | NVENC | NVIDIA SDK | ✅ | ✅ | NVIDIA GPUs only |
| **Windows** | AMF | AMD SDK | ✅ | ✅ | AMD GPUs only |
| **Linux** | VA-API | `libva` | ✅ | ✅ | Intel/AMD integrated |
| **Linux** | NVENC | NVIDIA SDK | ✅ | ✅ | NVIDIA GPUs only |

> **Note:** HW acceleration requires custom SDK integration and may not be available on all systems. Software fallback (libx264) should always be provided.

**Encoder Selection Priority:**
1. HW accelerated (if available and enabled)
2. Software fallback (libx264/libx265 via FFmpeg)

---

## Files Structure

```
core/src/emulator/recording/
├── encoder_base.h          # Abstract interface
├── encoder_config.h        # Configuration struct
├── recordingmanager.h      # Main manager
├── recordingmanager.cpp
└── encoders/               # Encoder implementations
    ├── gif_encoder.h
    ├── gif_encoder.cpp
    ├── wav_encoder.h       (planned)
    ├── flac_encoder.h      (planned)
    ├── mp3_encoder.h       (planned)
    ├── ogg_encoder.h       (planned)
    └── h264_encoder.h      (planned - SW + optional HW)
```

---

## See Also

- [Recording System](./recording-system.md) - High-level recording overview
- [Video+Audio Encoding](./video-audio-encoding.md) - Codec details
- [GIF Encoder](./encoders/gif-encoder.md) - GIF implementation details
