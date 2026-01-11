# Gap 1: GIF Recording Migration to RecordingManager

## Status: 📋 DESIGN IN PROGRESS

---

## Objective

Migrate the working `GIFAnimationHelper` code from debug code in `mainloop.cpp` into the proper `RecordingManager` architecture as a supported encoder format.

---

## Current State

### Debug Code Location
**File:** [mainloop.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/mainloop.cpp)

**Start recording (line 83):**
```cpp
gifAnimationHelper.StartAnimation("unreal.gif", framebuffer.width, framebuffer.height, 20);
```

**Write frame (line 242):**
```cpp
gifAnimationHelper.WriteFrame(buffer, size);
```

### GIFAnimationHelper Implementation
**File:** [gifanimationhelper.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/common/image/gifanimationhelper.cpp)

Uses 3rd party `gif.h` library for LZW encoding.

---

## Design Decision

### Q1: Where to Put GIF Encoder?

**Option A: Keep `GIFAnimationHelper` separate, call from RecordingManager**
- Pros: Minimal code changes, reuses existing helper
- Cons: Adds dependency, less cohesive

**Option B: Integrate into RecordingManager as `InitializeEncoder()` implementation**
- Pros: Consistent architecture, single encoder interface
- Cons: More refactoring

**Recommendation:** **Option A** initially (guard with feature flag), then refactor to Option B later.

---

### Q2: Recording Trigger

1. **Manual API:** User explicitly starts recording
2. **Feature-guarded:** Recording only works when `recording` feature is ON

**Decision:** Both - Feature must be ON, then user starts recording via API or CLI.

---

## Proposed Implementation

### Phase 2a: Guard Existing Code (Quick Win)

Add feature flag guards to existing debug code:

```cpp
// mainloop.cpp - Run()
if (_context->pFeatureManager->isEnabled(Features::kRecording)) {
    gifAnimationHelper.StartAnimation(...);
}

// mainloop.cpp - RunFrame() 
if (_context->pFeatureManager->isEnabled(Features::kRecording)) {
    gifAnimationHelper.WriteFrame(...);
}
```

**Result:** ~10% CPU savings when recording is OFF (default)

### Phase 2b: Full Migration (Later)

1. Move `GIFAnimationHelper` calls into `RecordingManager::InitializeEncoder()` and `RecordingManager::EncodeVideoFrame()`
2. Add "gif" as a recognized video codec in `StartRecording()`
3. Remove debug code from `mainloop.cpp`
4. Frame capture already exists: `OnFrameEnd()` → `RecordingManager::CaptureFrame()`

```cpp
// RecordingManager::InitializeEncoder()
if (_videoCodec == "gif") {
    _gifHelper.StartAnimation(_outputFilename, _videoWidth, _videoHeight, 1000 / _videoFrameRate);
    return true;  // GIF encoder initialized
}

// RecordingManager::EncodeVideoFrame()
if (_videoCodec == "gif") {
    _gifHelper.WriteFrame(framebuffer.data, framebuffer.size);
}

// RecordingManager::FinalizeEncoder()
if (_videoCodec == "gif") {
    _gifHelper.StopAnimation();
}
```

---

## Files to Modify

### Phase 2a (Guard)
| File | Change |
|------|--------|
| `core/src/emulator/mainloop.cpp` | Guard GIF calls with feature check |

### Phase 2b (Full Migration)
| File | Change |
|------|--------|
| `core/src/emulator/recording/recordingmanager.h` | Add `GIFAnimationHelper` member |
| `core/src/emulator/recording/recordingmanager.cpp` | Implement GIF encoder path |
| `core/src/emulator/mainloop.cpp` | Remove debug GIF code |

---

## Dependencies

- **Gap 3 must be completed first** - Requires `recording` feature flag

---

## Verification

1. With `recording off` (default): No "unreal.gif" created, profiler shows no GIF functions
2. With `recording on` + manual start: GIF created correctly
3. Recording quality matches current debug output
