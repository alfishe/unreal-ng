# Gap 6: RecordingManager Enhancements

## Status: 📋 IMPLEMENTATION PLAN

---

## Goal

Enhance RecordingManager with feature flag guard, encoder abstraction, and GIF encoder migration.

---

## Overview

Current state:
- `RecordingManager` exists with full API (Start/Stop/CaptureFrame/CaptureAudio)
- Encoder methods are stubs (TODO)
- `GIFAnimationHelper` exists separately in `core/src/common/image/`
- No feature flag integration

---

## Proposed Changes

### 1. Feature Flag Guard

Add check in all public methods to early-exit when `recording` feature is OFF:

```cpp
// recordingmanager.h - Add cached feature flag
bool _featureEnabled = false;

// recordingmanager.cpp - Check at start of public methods
bool RecordingManager::StartRecording(...) {
    if (!_featureEnabled) {
        MLOGWARNING("Recording disabled (feature 'recording' = off)");
        return false;
    }
    // ... existing logic
}
```

Update cache in `FeatureManager::onFeatureChanged()`.

---

### 2. Encoder Interface

Create abstract base for encoders:

```cpp
// encoding/encoder_base.h
class EncoderBase {
public:
    virtual ~EncoderBase() = default;
    
    // Lifecycle
    virtual bool Start(const std::string& filename, const EncoderConfig& config) = 0;
    virtual void Stop() = 0;
    
    // Frame input (encoder decides which to use)
    virtual void OnVideoFrame(const FramebufferDescriptor& fb, double timestamp) {}
    virtual void OnAudioSamples(const int16_t* samples, size_t count, double timestamp) {}
    
    // State
    virtual bool IsRecording() const = 0;
    virtual std::string GetType() const = 0;  // "gif", "h264", "flac", etc.
};
```

---

### 3. Encoder Registry

Flat list approach (simpler, each encoder handles its own media types):

```cpp
// recordingmanager.h
std::vector<std::unique_ptr<EncoderBase>> _activeEncoders;

// OnFrameEnd: dispatch to all active encoders
void RecordingManager::OnFrameEnd(const FramebufferDescriptor& fb, 
                                   const int16_t* audio, size_t audioCount) {
    double ts = _emulatedFrameCount / _videoFrameRate;
    for (auto& encoder : _activeEncoders) {
        encoder->OnVideoFrame(fb, ts);
        encoder->OnAudioSamples(audio, audioCount, ts);
    }
}
```

---

### 4. GIF Encoder Implementation

Wrap existing `GIFAnimationHelper` as an encoder:

```cpp
// encoding/gif_encoder.h
class GIFEncoder : public EncoderBase {
    GIFAnimationHelper _gifHelper;
    bool _isRecording = false;
    
    bool Start(const std::string& filename, const EncoderConfig& config) override;
    void Stop() override;
    void OnVideoFrame(const FramebufferDescriptor& fb, double timestamp) override;
    // OnAudioSamples - no-op (GIF has no audio)
    
    std::string GetType() const override { return "gif"; }
};
```

---

## Files to Create/Modify

| File | Action |
|------|--------|
| `core/src/emulator/recording/encoder_base.h` | **NEW** - Abstract encoder interface |
| `core/src/emulator/recording/encoder_config.h` | **NEW** - Encoder configuration struct |
| `core/src/emulator/recording/gif_encoder.h` | **NEW** - GIF encoder wrapping helper |
| `core/src/emulator/recording/gif_encoder.cpp` | **NEW** - GIF encoder implementation |
| `core/src/emulator/recording/recordingmanager.h` | **MODIFY** - Add encoder list, feature flag |
| `core/src/emulator/recording/recordingmanager.cpp` | **MODIFY** - Feature guard, encoder dispatch |
| `core/src/emulator/mainloop.cpp` | **MODIFY** - Wire to OnFrameEnd |

---

## Verification

1. `feature recording on` → RecordingManager API works
2. `feature recording off` → All calls early-exit
3. GIF recording via new encoder interface works
4. Core library compiles

---

## Related Gaps

- **Gap 1**: GIF Migration → Addressed by GIF encoder implementation
- **Gap 3**: Recording Feature → Addressed by feature flag guard
