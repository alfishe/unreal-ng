# Videowall Performance - Discovery Document

This document captures the gaps discovered during performance analysis of the `unreal-videowall` application with 48 emulator instances on a 4K display.

---

## Gap 1: Debug GIF Recording Active in Production

### Discovery
The profiler shows significant CPU usage by GIF-related functions:
- `GifWriteLzwImage` (3.7%)
- `GifPickChangedPixels` (3.6%)
- `GifPartition`, `GifSwapPixels` (additional)
- **Total: ~10% CPU**

### Root Cause
Debug code was left in [`mainloop.cpp`](core/src/emulator/mainloop.cpp):

**Lines 79-84 (Run method - Start recording for EVERY instance):**
```cpp
/// region <Debug>

// Initialize animation
[[maybe_unused]] FramebufferDescriptor& framebuffer = _screen->GetFramebufferDescriptor();
gifAnimationHelper.StartAnimation("unreal.gif", framebuffer.width, framebuffer.height, 20);  // <-- Always runs!

/// endregion </Debug>
```

**Lines 235-243 (RunFrame method - Write frame EVERY tick):**
```cpp
// Save frame to GIF animation
if (_context && _context->pScreen)
{
    Screen& screen = *_context->pScreen;
    uint32_t* buffer;
    size_t size;
    screen.GetFramebufferData(&buffer, &size);
    gifAnimationHelper.WriteFrame(buffer, size);  // <-- Every frame for all 48 instances!
}
```

### Impact
- 48 instances × 50 frames/second = **2400 GIF frame writes/second**
- LZW compression (CPU-intensive) runs for every frame
- All instances try to write to the same `unreal.gif` file (race condition)
- ~10% total CPU wasted on debug functionality

### Required Action
> [!IMPORTANT]
> **Do NOT simply remove** the GIF writing logic. The `GIFAnimationHelper` is working code that should be **migrated** into `RecordingManager` as a proper encoder format (like "gif" codec alongside future H.264/AAC).

**Migration approach:**
1. Add GIF as a supported output format in `RecordingManager`
2. Move `GIFAnimationHelper` logic into `RecordingManager::InitializeEncoder()` / `EncodeVideoFrame()` for GIF format
3. Guard with feature flag (`recording` feature, OFF by default)
4. Remove hardcoded debug calls from `mainloop.cpp`

### Gap Classification
| Property | Value |
|----------|-------|
| **Severity** | Critical - Direct performance impact |
| **Type** | Debug code needs migration to proper architecture |
| **Fix Difficulty** | Medium - Migrate to RecordingManager, don't just remove |

---

## Gap 2: No Sound Disable on Fullscreen for Existing Tiles

### Discovery
User stated: "Sound disabled when we go to fullscreen state + from WebAPI when we're loading snapshots."

However, searching [`VideoWallWindow.cpp`](unreal-videowall/src/videowall/VideoWallWindow.cpp) for:
- `setFeature.*sound` — **No results**
- `disableSound` — **No results**
- `sound.*fullscreen` — **No results**

The `toggleFullscreenMacOS()` function (lines 263-330) handles geometry and tile management, but **does not disable sound features**.

### Root Cause
Sound disabling is only performed:
1. **Via WebAPI** - When Python test scripts call `client.disable_sound(emu_id)`
2. **For new instances** - Possibly during creation (not verified)

**Not performed:**
- On fullscreen transition for existing tiles
- Automatically for all instances in the grid

### Current Sound Disable Flow (WebAPI)
```
Python script → WebAPI PUT /feature/sound → FeatureManager::setFeature() → onFeatureChanged() → SoundManager::UpdateFeatureCache()
```

### Missing Flow (Fullscreen)
```
toggleFullscreenMode() → [NOTHING happens to sound features]
```

### Gap Classification
| Property | Value |
|----------|-------|
| **Severity** | High - Significant performance impact (~17% CPU) |
| **Type** | Missing feature propagation logic |
| **Fix Difficulty** | Medium - Add sound disable loop in fullscreen handler |

---

## Gap 3: No FeatureManager Integration for Recording

### Discovery
A proper `RecordingManager` exists ([see docs](docs/emulator/design/recording/recording-system.md)) with:
- `StartRecording()` / `StopRecording()` lifecycle
- `IsRecording()` state check  
- Full codec/bitrate configuration API

However, it's **guarded by an encoder stub that always returns false**:
```cpp
// recordingmanager.cpp line 515-516
MLOGWARNING("RecordingManager::InitializeEncoder - Encoder not implemented, recording disabled");
return false;
```

Meanwhile, the debug `GIFAnimationHelper` runs **unconditionally** with no feature flag control.

### Architecture Gaps

#### 3.1 Missing Feature Flag for Recording
`FeatureManager` has features for:
- `sound` / `soundhq` (performance)
- `debugmode` / `breakpoints` / `memorytracking` (debugging)
- `calltrace` (analysis)

**Missing:**
- `recording` or `video_recording` feature
- Should be **OFF by default**
- Should guard both `RecordingManager` and debug `GIFAnimationHelper`

#### 3.2 No Default-Off Policy for Recording
Per user requirement:
> "Recording features must be OFF by default"

The current state violates this:
- `GIFAnimationHelper` always runs
- `RecordingManager` only disabled because encoder stub returns false (not by design)

#### 3.3 Relationship to Existing RecordingManager
The `RecordingManager` is designed for:
- Video+Audio recordings (FFmpeg integration, future)
- Multi-track audio capture
- Emulated-time timestamps

The debug `GIFAnimationHelper` is:
- Hardcoded start/stop
- No integration with lifecycle
- No user control

**Correct architecture:** GIF recording should be a mode within `RecordingManager`, not a separate debug helper.

### Gap Classification
| Property | Value |
|----------|-------|
| **Severity** | High - Prevents proper feature management |
| **Type** | Missing architecture / integration |
| **Fix Difficulty** | Medium-High - Requires feature registration and code refactoring |

---

## Gap 4: Feature Flag Edge Case in FeatureManager

### Discovery
In [`featuremanager.cpp`](core/src/base/featuremanager.cpp) lines 53-72:

```cpp
bool FeatureManager::setFeature(const std::string& idOrAlias, bool enabled)
{
    auto* f = findFeature(idOrAlias);
    if (f && f->enabled != enabled)  // <-- Only triggers if value CHANGES
    {
        f->enabled = enabled;
        _dirty = true;
        onFeatureChanged();  // <-- Called only when value changes
        return true;
    }
    else if (f)
    {
        // Feature found but no change needed
        return true;  // <-- Does NOT call onFeatureChanged()!
    }
    // ...
}
```

If a feature is already in the desired state (e.g., sound already OFF), `onFeatureChanged()` is NOT called, so `UpdateFeatureCache()` is NOT invoked.

### Impact
If `UpdateFeatureCache()` was never called during initialization:
1. First `setFeature("sound", false)` call — does nothing if already false
2. `_feature_sound_enabled` in `SoundManager` remains uninitialized/true
3. Sound processing continues despite feature being "off"

### Potential Scenario
1. Emulator starts with `sound: true` (default)
2. User disables sound via WebAPI: `setFeature("sound", false)` → `onFeatureChanged()` → `UpdateFeatureCache()`
3. Emulator resets/reloads → `FeatureManager::setDefaults()` → `sound: true` again
4. User disables sound again → But value already changed to false in step 2? **Race condition possible**

### Gap Classification
| Property | Value |
|----------|-------|
| **Severity** | Medium - Could cause silent failures |
| **Type** | Logic edge case |
| **Fix Difficulty** | Low - Force `UpdateFeatureCache()` call on first access or always call on set |

---

## Gap 5: Simplified Video Rendering for High-Density Scenarios

### Discovery
The profiler shows significant CPU usage by video rendering:
- `ScreenZX::Draw` (10.1%)
- `TransformTstateToFramebufferCoords` (5.2%)
- `TransformTstateToZXCoords` (3.1%)
- **Total: ~18-20% CPU**

### Current Implementation
`ScreenZX::Draw()` renders **2 pixels per t-state** with cycle-accurate timing:
- Called ~70,000 times per frame
- Each call performs coordinate transformations (modulo/division)
- Each call accesses memory for pixel/attribute data
- This is the **correct** approach for accurate ULA emulation

### Potential Optimization
For high-density scenarios (48 instances on 4K), simplified rendering could be acceptable:
- Render once per frame (not per t-state)
- Skip border effects and mid-frame attribute changes
- Use pre-computed lookup tables
- Potential CPU savings: ~15-20%

### Required Action
> [!NOTE]
> This optimization requires a **new feature flag** and a full design/planning cycle per [feature-management.md](docs/emulator/design/core/feature-management.md).

**Proposed feature:**
- Name: `video_accurate` or `video_mode`
- Values: `accurate` (default) / `fast` / `off`
- `accurate`: Current per-t-state rendering
- `fast`: Per-frame rendering with simplified attribute handling
- `off`: No video rendering (headless mode)

**Implementation pattern:**
```cpp
void ScreenZX::UpdateScreen() {
    if (!_feature_video_enabled)
        return;  // off mode
    
    if (_feature_video_fast) {
        RenderFrameSimplified();  // New fast path
    } else {
        DrawPeriod(lastTState, currentTState);  // Existing accurate path
    }
}
```

### Gap Classification
| Property | Value |
|----------|-------|
| **Severity** | Medium - Performance improvement, not a bug |
| **Type** | Missing feature flag + optimization opportunity |
| **Fix Difficulty** | High - Requires new renderer implementation + feature flag |

---

## Summary Table

| Gap | Description | CPU Impact | Severity | Fix Effort |
|-----|-------------|------------|----------|------------|
| **Gap 1** | Debug GIF recording in mainloop.cpp | ~10% | Critical | Medium (migration) |
| **Gap 2** | No sound disable on fullscreen | ~17% | High | Medium |
| **Gap 3** | No recording feature in FeatureManager | - | High | Medium-High |
| **Gap 4** | Feature flag edge case | ~17% (potential) | Medium | Low |
| **Gap 5** | No simplified video rendering mode | ~18% | Medium | High (design first) |

---

## Requirements Before Implementation

Before implementing fixes, the following requirements should be clarified:

### R1: Recording Feature Architecture
1. Should `recording` be a single feature flag, or split into `video_recording` and `gif_recording`?
2. Should GIF recording be moved into `RecordingManager`, or remain separate but feature-guarded?
3. What is the default state for recording features? (Assumption: OFF)

### R2: Fullscreen Sound Behavior
1. Should fullscreen transition disable sound for ALL tiles, or just the focused one?
2. Should exiting fullscreen restore previous sound state?
3. Should this be configurable via a setting?

### R3: FeatureManager Consistency
1. Should `UpdateFeatureCache()` be called unconditionally on `setFeature()`, even if value doesn't change?
2. Or should initialization explicitly call `UpdateFeatureCache()` for all components?

### R4: Video Rendering Mode (Future)
1. Should `video_mode` support `accurate` / `fast` / `off`, or just `on` / `off`?
2. What rendering shortcuts are acceptable in `fast` mode? (skip border, skip mid-line effects?)
3. Should this be a per-instance feature for mixed videowall/debug scenarios?

---

## Recommended Implementation Order

### Phase 1: Immediate Performance Fixes
1. **Gap 4** (Medium/Low effort) - Fix feature flag edge case → Ensures sound disable actually works
2. **Gap 2** (High/Medium effort) - Add fullscreen sound disable → Ensures ~17% CPU savings

### Phase 2: Architecture Improvements
3. **Gap 3** (High/Medium-High effort) - Add `recording` feature to FeatureManager → OFF by default
4. **Gap 1** (Critical/Medium effort) - Migrate GIF logic to RecordingManager → ~10% CPU savings when off, preserves working functionality

### Phase 3: Future Design Work (Requires Separate Planning)
5. **Gap 5** (Medium/High effort) - Design `video_mode` feature and simplified renderer → ~18% CPU savings potential

---

## References

- [RecordingManager implementation](core/src/emulator/recording/recordingmanager.cpp)
- [Recording System Architecture](docs/emulator/design/recording/recording-system.md)
- [Feature Management System](docs/emulator/design/core/feature-management.md)
- [FeatureManager implementation](core/src/base/featuremanager.cpp)
- [MainLoop (GIF debug code)](core/src/emulator/mainloop.cpp)
- [Videowall profiler analysis KI](file:///Users/dev/.gemini/antigravity/knowledge/unreal_ng_system_architecture/artifacts/verification/videowall_profiler_analysis_2026_01.md)

