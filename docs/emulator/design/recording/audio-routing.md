# Audio Routing Architecture

## Overview

The audio routing system manages how audio samples flow from individual sound devices (beeper, AY chips, COVOX, etc.) through mixing and output stages. It supports flexible source selection for recording individual devices or channels.

## Audio Device Architecture

### Device Hierarchy

```
┌─────────────────────────────────────────────────────────────────┐
│                        SoundManager                             │
│  - Master audio mixer                                           │
│  - Output buffer management                                     │
│  - Audio callback coordination                                  │
└──────────┬──────────────────────────────────────────────────────┘
           │
           ├─► TurboSound (AY chips manager)
           │   ├─► AY-3-8910 Chip #1
           │   │   ├─► Channel A (Tone + Noise)
           │   │   ├─► Channel B (Tone + Noise)
           │   │   └─► Channel C (Tone + Noise)
           │   ├─► AY-3-8910 Chip #2
           │   │   ├─► Channel A
           │   │   ├─► Channel B
           │   │   └─► Channel C
           │   └─► AY-3-8910 Chip #3
           │       ├─► Channel A
           │       ├─► Channel B
           │       └─► Channel C
           │
           ├─► Beeper
           │   └─► 1-bit digital output → DAC simulation
           │
           ├─► COVOX / DAC
           │   └─► 8-bit digital output
           │
           ├─► General Sound (GS)
           │   └─► Multi-channel Z80-based audio-card
           │
           └─► Moonsound / OPL4
               └─► FM synthesis + PCM samples
```

## Audio Data Flow

### 1. Sample Generation Phase

Each device generates samples independently during emulation:

```cpp
// In MainLoop::OnFrameEnd()
void MainLoop::OnFrameEnd()
{
    // Each device generates audio for current frame
    _context->pBeeper->GenerateSamples();           // 882 samples @ 44.1 kHz
    _context->pTurboSound->GenerateSamples();       // 882 samples per chip
    _context->pCOVOX->GenerateSamples();            // If present
    _context->pGeneralSound->GenerateSamples();     // If present
    _context->pMoonsound->GenerateSamples();        // If present
    
    // Mix and output
    _context->pSoundManager->handleFrameEnd();
}
```

**Sample count per frame:**
- Frame rate: 50 Hz (20ms per frame)
- Sample rate: 44,100 Hz
- Samples per frame: 44,100 / 50 = **882 samples**
- Stereo: 882 × 2 channels = **1,764 values**
- Buffer size: 1,764 × 2 bytes (int16_t) = **3,528 bytes**

### 2. Mixing Phase

`SoundManager::handleFrameEnd()` mixes all audio sources:

```cpp
void SoundManager::handleFrameEnd()
{
    // Get buffers from each device
    uint16_t* ayBuffer = _turboSound->getAudioBuffer();      // All 3 chips mixed
    int16_t* beeperBuffer = _beeper->getAudioBuffer();       // Beeper output
    
    // Mix all channels to master output buffer
    AudioUtils::MixAudio(
        (const int16_t*)ayBuffer, 
        beeperBuffer, 
        _outBuffer,              // Master output buffer
        AUDIO_BUFFER_SAMPLES_PER_FRAME
    );
    
    // Add other devices (COVOX, GS, Moonsound) if present
    if (_covox)
    {
        AudioUtils::MixAudio(_outBuffer, _covox->getAudioBuffer(), _outBuffer, SAMPLES_PER_FRAME);
    }
    
    // Apply mute if needed (for turbo mode)
    if (_mute)
    {
        // Send silence to audio callback
        std::vector<int16_t> silenceBuffer(SAMPLES_PER_FRAME * AUDIO_CHANNELS, 0);
        _context->pAudioCallback(_context->pAudioManagerObj, silenceBuffer.data(), SAMPLES_PER_FRAME * AUDIO_CHANNELS);
    }
    else
    {
        // Send mixed audio to output
        _context->pAudioCallback(_context->pAudioManagerObj, _outBuffer, SAMPLES_PER_FRAME * AUDIO_CHANNELS);
    }
}
```

### 3. Recording Capture Phase

`RecordingManager` can capture audio at multiple points:

```cpp
void MainLoop::OnFrameEnd()
{
    // ... device sample generation ...
    
    // Capture for recording BEFORE mixing (for multi-track)
    if (_recordingManager->IsRecording())
    {
        // Capture individual sources
        if (_recordingManager->GetRecordingMode() == RecordingMode::MultiTrack ||
            _recordingManager->GetRecordingMode() == RecordingMode::ChannelSplit)
        {
            // Capture each device separately
            _recordingManager->CaptureAudioSource(
                AudioSourceType::Beeper, 
                _beeper->getAudioBuffer(), 
                SAMPLES_PER_FRAME
            );
            
            _recordingManager->CaptureAudioSource(
                AudioSourceType::AY1_All,
                _turboSound->getChipBuffer(0),
                SAMPLES_PER_FRAME
            );
            
            // For channel-split mode, capture each AY channel
            if (_recordingManager->GetRecordingMode() == RecordingMode::ChannelSplit)
            {
                _recordingManager->CaptureAudioSource(
                    AudioSourceType::AY1_ChannelA,
                    _turboSound->getChannelBuffer(0, 0),  // Chip 0, Channel A
                    SAMPLES_PER_FRAME
                );
                // ... channels B, C ...
            }
        }
    }
    
    // Mix to master output
    _soundManager->handleFrameEnd();
    
    // Capture master mix (for single-track)
    if (_recordingManager->IsRecording() && 
        _recordingManager->GetRecordingMode() == RecordingMode::SingleTrack)
    {
        _recordingManager->CaptureAudio(
            _soundManager->getMasterBuffer(),
            SAMPLES_PER_FRAME
        );
    }
}
```

## Audio Source Access

### Interface Requirements

To support flexible recording, each audio device must provide:

1. **Master output buffer** - Mixed output from device
2. **Individual channel buffers** (for AY chips)

### Required API Changes

#### TurboSound Enhancement

```cpp
class TurboSound
{
public:
    // Existing method - returns mixed output from all chips
    uint16_t* getAudioBuffer();
    
    // NEW: Get buffer for specific chip
    uint16_t* getChipBuffer(uint8_t chipIndex);  // chipIndex: 0-2
    
    // NEW: Get buffer for specific chip channel
    int16_t* getChannelBuffer(uint8_t chipIndex, uint8_t channelIndex);  // channel: 0-2 (A,B,C)
    
    // NEW: Get number of active chips
    uint8_t getActiveChipCount() const;
};
```

#### AY-3-8910 Enhancement

```cpp
class SoundChip_AY8910
{
public:
    // Existing method - returns mixed output
    int16_t* getAudioBuffer();
    
    // NEW: Get buffer for specific channel
    int16_t* getChannelBuffer(uint8_t channelIndex);  // 0=A, 1=B, 2=C
    
    // NEW: Get channel names for UI/logging
    static const char* getChannelName(uint8_t channelIndex);
};
```

### Buffer Management Strategy

**Option 1: Separate buffers (memory overhead)**
- Each device maintains separate buffers for each channel
- Clean separation, easy to capture
- Memory cost: ~3.5 KB per channel × 9 AY channels = ~31.5 KB

**Option 2: Rendering on demand (CPU overhead)**
- Devices only render requested channels when needed
- Lower memory footprint
- CPU cost: Re-rendering channels for recording

**Recommendation:** Use **Option 1** (separate buffers) because:
- Memory cost is negligible (~32 KB)
- CPU cost of re-rendering is significant
- Recording can capture without affecting playback performance

## Recording Routing Examples

### Example 1: Single-Track Master Mix

```
Beeper ────┐
           │
AY Chip 1 ─┤
           ├─► SoundManager::Mix() ─► Master Buffer ─► RecordingManager
AY Chip 2 ─┤                                                  │
           │                                                  ▼
COVOX ─────┘                                            output.mp4
```

**Configuration:**
```cpp
recordingManager->SetRecordingMode(RecordingMode::SingleTrack);
recordingManager->SelectAudioSource(AudioSourceType::MasterMix);
recordingManager->StartRecording("gameplay.mp4");
```

**Result:** Standard video+audio recording with all sound mixed together.

### Example 2: Multi-Track Device Recording

```
Beeper ────────────────────────────────► Track 1 ──┐
                                                    │
AY Chip 1 ─────────────────────────────► Track 2 ──┤
                                                    ├─► output.mkv
AY Chip 2 ─────────────────────────────► Track 3 ──┤
                                                    │
COVOX ─────────────────────────────────► Track 4 ──┘

Master Mix (for playback) ─────────────► Audio output (not recorded)
```

**Configuration:**
```cpp
recordingManager->SetRecordingMode(RecordingMode::MultiTrack);
recordingManager->SetVideoEnabled(true);

// Add each device as a separate track
AudioTrackConfig track1;
track1.name = "Beeper";
track1.source = AudioSourceType::Beeper;
track1.codec = "flac";  // Lossless
recordingManager->AddAudioTrack(track1);

AudioTrackConfig track2;
track2.name = "AY Chip 1";
track2.source = AudioSourceType::AY1_All;
track2.codec = "flac";
recordingManager->AddAudioTrack(track2);

// ... add more tracks ...

recordingManager->StartRecordingEx("music.mkv");
```

**Result:** MKV file with video + 4 separate audio tracks (one per device).

### Example 3: Channel-Split AY Recording

```
AY Chip 1:
  ├─ Channel A ────────────────────────► Track 1 ──┐
  ├─ Channel B ────────────────────────► Track 2 ──┤
  └─ Channel C ────────────────────────► Track 3 ──┼─► music.flac
                                                    │
AY Chip 2:                                         │
  ├─ Channel A ────────────────────────► Track 4 ──┤
  ├─ Channel B ────────────────────────► Track 5 ──┤
  └─ Channel C ────────────────────────► Track 6 ──┘

Master Mix (for playback) ─────────────► Audio output
```

**Configuration:**
```cpp
recordingManager->SetRecordingMode(RecordingMode::ChannelSplit);
recordingManager->SetVideoEnabled(false);  // Audio only

// Add each channel as a separate track
for (int chip = 0; chip < 2; chip++)
{
    for (int channel = 0; channel < 3; channel++)
    {
        AudioTrackConfig track;
        track.name = StringHelper::Format("AY%d Channel %c", chip + 1, 'A' + channel);
        track.source = (AudioSourceType)(AudioSourceType::AY1_ChannelA + (chip * 3) + channel);
        track.codec = "flac";
        recordingManager->AddAudioTrack(track);
    }
}

recordingManager->StartRecordingEx("ay_channels.flac");
```

**Result:** FLAC file with 6 separate audio tracks (3 channels × 2 chips).

## Performance Considerations

### Memory Usage

**Per-frame buffers:**
- Master mix: 3,528 bytes (882 samples × 2 channels × 2 bytes)
- Per device: 3,528 bytes
- Per AY channel: 1,764 bytes (mono)

**Total memory for all sources:**
- Master: 3.5 KB
- Beeper: 3.5 KB
- AY chips (3): 10.5 KB
- AY channels (9): 15.9 KB
- COVOX: 3.5 KB
- GS: variable
- Moonsound: 3.5 KB
- **Total: ~40-50 KB** (negligible)

### CPU Usage

**Mixing overhead:**
- 1 device mix: ~1-2 µs (simple addition loop)
- 4 device mix: ~5-10 µs
- **Total: <1% CPU at 50 Hz** (negligible)

**Recording overhead:**
- Buffer copy: ~5-10 µs per track
- Encoding (FFmpeg): variable, depends on codec
- Multi-track: linear scaling (~10 µs × track count)

**Recommendations:**
- Use hardware-accelerated encoding when available
- Batch recording operations (every N frames)
- Use separate encoding thread to avoid blocking emulation

## Implementation Phases

### Phase 1: Basic Infrastructure ✓
- [x] Define audio source types
- [x] Define recording modes
- [x] Define audio track configuration
- [x] Update RecordingManager interface

### Phase 2: Audio Device API (TODO)
- [ ] Add `getChipBuffer()` to TurboSound
- [ ] Add `getChannelBuffer()` to SoundChip_AY8910
- [ ] Add buffer access for COVOX, GS, Moonsound
- [ ] Update SoundManager to expose device buffers

### Phase 3: Recording Integration (TODO)
- [ ] Implement `CaptureAudioSource()` in RecordingManager
- [ ] Add multi-track capture logic to MainLoop
- [ ] Implement audio routing based on mode
- [ ] Add timestamp synchronization per track

### Phase 4: Encoding (TODO)
- [ ] Integrate FFmpeg/libav
- [ ] Implement multi-track muxing
- [ ] Add codec selection per track
- [ ] Optimize encoding performance

## Testing Strategy

### Unit Tests

1. **Buffer Access Tests**
   - Verify each device provides correct buffer size
   - Verify channel buffer isolation (no cross-contamination)
   - Verify buffer contents are valid (no NaN, inf, etc.)

2. **Routing Tests**
   - Verify correct source selection
   - Verify multi-track routing doesn't affect playback
   - Verify channel-split mode captures individual channels

3. **Performance Tests**
   - Measure memory overhead of multi-track recording
   - Measure CPU overhead of buffer copying
   - Verify no impact on emulation speed

### Integration Tests

1. **Playback Tests**
   - Play AY music, verify all channels audible in master mix
   - Play AY music, verify individual channels in multi-track recording
   - Verify beeper + AY mixing is correct

2. **Recording Tests**
   - Record gameplay, verify audio is in sync with video
   - Record multi-track, verify each track contains correct source
   - Record channel-split, verify individual channel isolation

## References

- [Recording System Architecture](recording-system.md)
- [Speed Control](speed-control.md) - For turbo mode audio handling
- SoundManager implementation (`core/src/emulator/sound/soundmanager.cpp`)
- TurboSound implementation (`core/src/emulator/sound/ay/turbosound.cpp`)
- AY-3-8910 implementation (`core/src/emulator/sound/chips/soundchip_ay8910.cpp`)

