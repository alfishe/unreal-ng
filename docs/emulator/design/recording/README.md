# Emulator Design Documentation

This directory contains architectural documentation for the ZX Spectrum emulator core.

## Recording System

Comprehensive documentation for the video and audio recording subsystem:

### Core Documents

1. **[Recording System Architecture](recording-system.md)** 📹🎵
   - Overview of recording capabilities
   - Recording modes (single-track, multi-track, channel-split, audio-only)
   - Audio source enumeration (master mix, individual devices, AY channels)
   - API reference and usage examples
   - Performance considerations
   - **Start here for an overview**

2. **[Audio Routing](audio-routing.md)** 🎵
   - Audio device hierarchy and data flow
   - Sample generation, mixing, and capture pipeline
   - Buffer management and access patterns
   - Multi-track routing examples
   - Implementation phases and testing strategy
   - **Read this for audio architecture details**

3. **[Video & Audio Encoding](video-audio-encoding.md)** 🎬
   - FFmpeg/libav integration details
   - Video encoder initialization and frame encoding
   - Audio encoder setup (single-track and multi-track)
   - Container muxing (MP4, MKV, AVI, etc.)
   - Codec selection guide
   - Performance optimization strategies
   - **Read this for encoder implementation**

4. **[Encoder Architecture](encoders/encoder-architecture.md)** 🔧
   - EncoderBase interface abstraction
   - Encoder registry and dispatch flow
   - Feature flag integration
   - Envisioned encoders catalog
   - **Read this for encoder plugin design**

5. **[Encoder Development Guideline](encoders/encoder-development-guideline.md)** 📖
   - Required interface implementation
   - Error handling and validation patterns
   - RAII resource management
   - Unit testing requirements
   - **Read this when implementing new encoders**

### Encoder Documentation

Individual encoder implementations are documented in the `encoders/` subfolder:

- **[GIF Encoder](encoders/gif-encoder.md)** - Animated GIF output (video-only)
- WAV Encoder (planned) - Lossless audio
- FLAC Encoder (planned) - Compressed lossless audio
- H.264 Encoder (planned) - Video with audio

### Quick Reference

#### Recording Modes

| Mode | Description | Use Case |
|------|-------------|----------|
| **SingleTrack** | Final mixed output only | Standard gameplay recordings |
| **MultiTrack** | Multiple audio sources to separate tracks | Music production, remixing |
| **ChannelSplit** | Individual AY channels separately | Channel-by-channel analysis |
| **AudioOnly** | Audio without video | Music archives, sound libraries |

#### Audio Sources

- **MasterMix** - Final mixed output (all devices)
- **Beeper** - 1-bit digital audio
- **AY1/2/3_All** - AY chip (all channels mixed)
- **AY1/2/3_ChannelA/B/C** - Individual AY channels
- **COVOX** - 8-bit DAC
- **GeneralSound** - Wavetable synthesis
- **Moonsound** - FM synthesis + PCM

#### Supported Formats

**Containers:** MP4, MKV, AVI, WebM, WAV, FLAC, MP3  
**Video Codecs:** H.264, H.265, VP9, AV1, MJPEG, FFV1  
**Animated Images:** GIF, WebP, AVIF, APNG  
**Audio Codecs:** AAC, MP3, FLAC, Vorbis, Opus, PCM, ALAC

### Usage Examples

#### Standard Video Recording
```cpp
recordingManager->SetRecordingMode(RecordingMode::SingleTrack);
recordingManager->SelectAudioSource(AudioSourceType::MasterMix);
recordingManager->StartRecording("gameplay.mp4", "h264", "aac");
```

#### Multi-Track Music Recording
```cpp
recordingManager->SetRecordingMode(RecordingMode::MultiTrack);
recordingManager->SetVideoEnabled(false);

AudioTrackConfig track;
track.name = "AY Chip 1";
track.source = AudioSourceType::AY1_All;
track.codec = "flac";
recordingManager->AddAudioTrack(track);

recordingManager->StartRecordingEx("music.flac");
```

#### Audio-Only Capture
```cpp
recordingManager->SetRecordingMode(RecordingMode::AudioOnly);
recordingManager->SelectAudioSource(AudioSourceType::Beeper);
recordingManager->StartRecording("sfx.wav", "", "pcm_s16le");
```

#### Web Sharing (Animated GIF/WebP)
```cpp
recordingManager->SetVideoFrameRate(15.0f);     // Lower FPS for smaller files
recordingManager->SetAudioEnabled(false);       // No audio in animated images
recordingManager->StartRecording("clip.gif", "gif", "");  // Or "clip.webp", "libwebp"
```

### Implementation Status

#### Phase 1: Infrastructure ✅ Complete
- [x] Audio source type enumeration
- [x] Recording mode definitions
- [x] Audio track configuration
- [x] RecordingManager interface update
- [x] Documentation

#### Phase 2: Audio Device API 🚧 In Progress
- [ ] TurboSound multi-chip buffer access
- [ ] AY-3-8910 channel buffer access
- [ ] COVOX/GS/Moonsound buffer access
- [ ] SoundManager device buffer exposure

#### Phase 3: Recording Integration ⏳ Planned
- [ ] Multi-track capture in MainLoop
- [ ] Audio routing by mode
- [ ] Per-track timestamp synchronization
- [ ] Buffer management and pooling

#### Phase 4: FFmpeg Encoding ⏳ Planned
- [ ] FFmpeg/libav integration
- [ ] Video encoder implementation
- [ ] Multi-track audio muxing
- [ ] Codec selection and optimization

## Speed Control

Documentation for emulation speed control and turbo mode:

- **[Speed Control](speed-control.md)** ⚡
  - CPU frequency multiplication
  - Turbo/max speed mode
  - Audio/video synchronization
  - Recording in turbo mode

## Snapshot System

Documentation for snapshot loading/saving:

- **[Snapshots](snapshots/)** 💾
  - SNA format
  - Z80 format
  - SZX format (planned)

## User Interface

Documentation for UI architecture:

- **[UI Design](ui/)** 🖥️
  - Main window layout
  - Menu system
  - Debugger interface
  - Keyboard mapping

## Control Interfaces

Documentation for controlling the emulator:

- **[Control Interfaces](control-interfaces/)** 🎮
  - REST API (async CLI)
  - Qt GUI integration
  - Script automation

---

## Contributing

When adding new design documentation:

1. Create a new `.md` file in the appropriate directory
2. Add a link to this README
3. Follow the existing structure and formatting
4. Include code examples where applicable
5. Add diagrams using ASCII art or Mermaid

## Document Conventions

- Use clear, descriptive headings
- Include code examples with syntax highlighting
- Provide visual diagrams where helpful
- Reference related documents
- Keep implementation status up to date
- Use emojis for quick visual scanning (optional)

## Contact

For questions about design decisions or documentation:
- Open an issue in the project repository
- Tag @architecture or @documentation

