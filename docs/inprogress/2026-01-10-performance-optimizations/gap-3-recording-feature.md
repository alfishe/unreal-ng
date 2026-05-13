# Gap 3: Recording Subsystem Architecture

## Status: 📋 DESIGN REVISION

---

## Key Insight

> "Not everything needs to be a feature"

The recording subsystem should have:
1. **Master switch** (FeatureManager) - Enable/disable the entire recording subsystem
2. **API commands** (RecordingManager) - What/how/when to record

---

## Architecture Separation

Recording is a **core emulator feature** (lives in `core` library), not specific to any UI application.

```
┌─────────────────────────────────────────────────────────────┐
│        Emulator (core library - per instance)                │
├─────────────────────────────────────────────────────────────┤
│  FeatureManager                                              │
│    `recording` = ON/OFF (master switch)                      │
│    - OFF = Recording disabled, zero overhead                 │
│    - ON = RecordingManager available for use                 │
├─────────────────────────────────────────────────────────────┤
│  RecordingManager (guarded by feature)                       │
│    - StartRecording(filename, config)  ← Runtime API         │
│    - StopRecording()                                         │
│    - SetVideoCodec("h264" | "gif")                           │
│    - SetAudioSources([MasterMix, AY1, Beeper])               │
└─────────────────────────────────────────────────────────────┘
                            │
            ┌───────────────┼───────────────┐
            ▼               ▼               ▼
      unreal-qt      unreal-videowall   testclient
      (uses API)     (doesn't use it)   (CLI only)
```

---

## What's a Feature vs. What's an API Command?

| Concept | Type | When Set | Example |
|---------|------|----------|---------|
| **recording** | Feature | Startup / Config | `feature recording on` (enable subsystem) |
| **debugmode** | Feature | Startup / Config | `feature debugmode on` (debugging) |
| **sound** | Feature | Startup / Config | `feature sound off` (performance) |
| Start H.264 recording | API | Runtime | `record start gameplay.mp4 --codec h264` |
| Record audio to FLAC | API | Runtime | `record start music.flac --audio-only` |
| Broadcast to OBS | API | Runtime | `record broadcast obs` |

---

## Use Cases

### Use Case 1: Videowall (Recording OFF)
```cpp
// At startup - master switch OFF
featureManager->setFeature("recording", false);
// Result: RecordingManager early-exits, zero overhead
```

### Use Case 2: Regular User (Recording available, not active)
```cpp
// Default state - recording available but idle
featureManager->setFeature("recording", true);  // or default ON

// User decides to record
recordingManager->SetVideoCodec("h264");
recordingManager->SetAudioEnabled(true);
recordingManager->StartRecording("gameplay.mp4");
```

### Use Case 3: Record Audio Only
```cpp
// User wants lossless audio capture
recordingManager->SetRecordingMode(RecordingMode::AudioOnly);
recordingManager->SetAudioCodec("flac");
recordingManager->StartRecording("soundtrack.flac");
```

### Use Case 4: Broadcast to OBS
```cpp
// User wants to stream via OBS
recordingManager->SetVideoEnabled(true);
recordingManager->SetAudioEnabled(true);
recordingManager->StartBroadcast(BroadcastTarget::OBS_VirtualCamera);
```

---

## Implementation Plan

### Step 1: Add Master Switch Feature

```cpp
// featuremanager.h
constexpr const char* const kRecording = "recording";
constexpr const char* const kRecordingAlias = "rec";
constexpr const char* const kRecordingDesc = "Enable recording subsystem";
```

```cpp
// featuremanager.cpp - setDefaults()
registerFeature({Features::kRecording,
                 Features::kRecordingAlias,
                 Features::kRecordingDesc,
                 false,  // OFF by default - heavy functionality
                 "",
                 {Features::kStateOff, Features::kStateOn},
                 Features::kCategoryPerformance});
```

### Step 2: Guard RecordingManager with Feature

```cpp
// recordingmanager.cpp
bool RecordingManager::StartRecording(const std::string& filename)
{
    // Check master switch first
    if (!_feature_recording_enabled)
    {
        MLOGWARNING("Recording subsystem is disabled (feature 'recording' = off)");
        return false;
    }
    
    // Proceed with recording initialization...
}
```

### Step 3: Guard Debug GIF Code

```cpp
// mainloop.cpp - Remove unconditional debug code
// Guard with: _context->pFeatureManager->isEnabled(Features::kRecording)
```

---

## Default State

> **Decision:** `recording = OFF` **by default for ALL applications**
> 
> Recording is heavy functionality. Users enable it explicitly when needed.

---

## Files to Modify

| File | Change |
|------|--------|
| `core/src/base/featuremanager.h` | Add `kRecording` constants |
| `core/src/base/featuremanager.cpp` | Register feature (**OFF by default**) |
| `core/src/emulator/recording/recordingmanager.cpp` | Guard with feature check |
| `core/src/emulator/mainloop.cpp` | Guard/remove debug GIF code |

---

## Decisions Made

| Question | Decision |
|----------|----------|
| Videowall-level recording | Separate entity, reuses low-level components (out of scope for now) |
| Virtual camera | Keep both options (NDI, OBS) - decide later |
| Default for `recording` | **OFF for all applications** - heavy functionality |

