# Performance Optimizations Plan - 2026-01-10

## Objective
Improve unreal-videowall performance from 20-30 FPS to stable 50 FPS with 48 emulator instances on 4K display.

---

## Phase Summary

| Phase | Gaps | Status | Expected CPU Savings |
|-------|------|--------|---------------------|
| **Phase 1** | Gap 4 + Gap 2 | ✅ COMPLETED | ~17% (sound) |
| **Phase 2** | Gap 3 + Gap 1 | 📋 Planned | ~10% (recording) |
| **Phase 3** | Gap 5 | 📐 Needs Design | ~18% (video mode) |

---

## Phase 1: Bug Fixes (COMPLETED)

### Gap 4: Feature Flag Edge Case
**File:** [featuremanager.cpp](core/src/base/featuremanager.cpp)

**Problem:** `onFeatureChanged()` was only called when feature value actually changed, not when `setFeature()` was called with the same value. This caused `UpdateFeatureCache()` to not be invoked, leaving cached flags potentially out of sync.

**Fix:** Always call `onFeatureChanged()` when `setFeature()` finds a valid feature, regardless of whether the value changed. Only mark `_dirty` if value actually changed (for file persistence).

**Status:** ✅ IMPLEMENTED

---

### Gap 2: No Sound Disable on Fullscreen
**File:** [VideoWallWindow.cpp](unreal-videowall/src/videowall/VideoWallWindow.cpp)

**Problem:** Sound was only disabled via WebAPI calls from test scripts. Entering fullscreen mode did not automatically disable sound for existing tiles.

**Fix:** 
- Added `setSoundForAllTiles(bool enabled)` helper method
- Calls `setFeature(Features::kSoundGeneration, false)` and `setFeature(Features::kSoundHQ, false)` for all tiles when entering fullscreen
- Sound stays disabled when exiting fullscreen (per user requirement)

**Status:** ✅ IMPLEMENTED

---

## Phase 2: Architecture Improvements (Planned)

### Gap 3: No Recording Feature in FeatureManager
**Design needed:** See [gap-3-recording-feature.md](gap-3-recording-feature.md)

**Summary:**
- Add `recording` feature to FeatureManager (OFF by default)
- Integrate with RecordingManager lifecycle
- Guard both RecordingManager and GIFAnimationHelper

---

### Gap 1: GIF Recording Migration
**Design needed:** See [gap-1-gif-migration.md](gap-1-gif-migration.md)

**Summary:**
- Migrate `GIFAnimationHelper` logic into `RecordingManager` as a GIF encoder
- Remove hardcoded debug code from `mainloop.cpp`
- Guard with `recording` feature flag

---

## Phase 3: Future Design Work

### Gap 5: Simplified Video Rendering
**Design needed:** Separate planning cycle per [feature-management.md](docs/emulator/design/core/feature-management.md)

**Summary:**
- Add `video_mode` feature with `accurate` / `fast` / `off` modes
- Implement simplified per-frame renderer for high-density scenarios
- Expected savings: ~18% CPU

---

## Files Modified

### Phase 1 Changes:
1. `core/src/base/featuremanager.cpp` - Gap 4 fix
2. `unreal-videowall/src/videowall/VideoWallWindow.h` - Added `setSoundForAllTiles()` declaration
3. `unreal-videowall/src/videowall/VideoWallWindow.cpp` - Gap 2 implementation

---

## Next Steps

1. ✅ Build and verify Phase 1 changes compile
2. 📋 Create design documents for Phase 2 gaps
3. 📋 Update architectural documentation after design approval
4. 📋 Implement Phase 2 after design review
