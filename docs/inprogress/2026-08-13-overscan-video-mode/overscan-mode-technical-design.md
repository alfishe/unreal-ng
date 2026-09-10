# Overscan Video Mode - Technical Design Document

## 1. Overview

This document specifies the implementation of an "Overscan Mode" for Unreal-NG, enabling visualization of the invisible border area that exists between VBLANK and the standard visible border region. This feature supports demo developers who need to see content rendered in overscan areas.

## 2. Background

### 2.1 Current Implementation

Unreal-NG renders a **352×288** frame with symmetric 48-pixel borders:

```
┌─────────────────────────────────────┐
│           VBLANK (not rendered)     │  16 lines
├─────────────────────────────────────┤
│         Top Border (48 lines)       │  ← Current visible top
├─────────────────────────────────────┤
│  Left │                     │ Right │
│  Brd  │   Screen 256×192    │  Brd  │
│ (48px)│                     │(48px) │
├─────────────────────────────────────┤
│        Bottom Border (48 lines)     │
├─────────────────────────────────────┤
│           VBLANK (not rendered)     │  16 lines
└─────────────────────────────────────┘
         Total: 352×288 visible
```

### 2.2 Pentagon Hardware Reality

Pentagon 128K actual frame structure (320 lines total, 448 dots/line):

| Region | Lines | Description |
|--------|-------|-------------|
| VBLANK | 16 | True vertical blank (retrace) |
| Overscan Top | 16 | Border after VBLANK, hidden by CRT bezel |
| Visible Top Border | 48 | Standard visible border |
| Screen | 192 | Paper area |
| Bottom Border | 48 | Standard visible border |
| VBLANK | (part of next frame) | - |

**Total visible (excluding VBLANK):** 16 + 48 + 192 + 48 = 304 lines

### 2.3 Xpeccy+ Approach

Xpeccy+ renders the full 304-line visible area including the 16-line overscan:

```cpp
// Pentagon geometry from Xpeccy+ config.cpp:
// rows: 16Vblk + (16 invis + 48 vis) top border + 192 screen + 48 bottom border = 320 rows
// cols: 64Hblk + 72 left border + 256 screen + 56 right border = 448 dots (224T)
vLayout vlay = {{448,320},{72,64},{64,16},{256,192},{0,0},64};
```

## 3. Requirements

### 3.1 Functional Requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-01 | Add overscan video mode showing 16 extra top border lines | Must |
| FR-02 | Add overscan video mode showing extra left border pixels | Must |
| FR-03 | Maintain backward compatibility with existing 352×288 mode | Must |
| FR-04 | Support runtime switching between standard and overscan modes | Should |
| FR-05 | Overscan mode for Pentagon only (ZX48/128 have no overscan) | Must |

### 3.2 Research Finding: Pentagon-Only Overscan

**ZX48k/ZX128k do NOT have overscan areas.** Analysis of frame timings:

| Machine | Total Lines | Frame T-states | Border Lines | Overscan |
|---------|-------------|----------------|--------------|----------|
| ZX48k | 312 | 69888 | 56 top + 56 bottom | None |
| ZX128k | 311 | 70908 | 56 top + 56 bottom | None |
| Pentagon | 320 | 71680 | 64 top + 48 bottom | 16 lines |

Pentagon has **8-9 more lines** than UK machines. These extra lines create the overscan area that exists between VBLANK and the standard visible border. ZX48/128 use all available lines for visible content—there's nothing hidden to expose.

**Xpeccy+ approach:** Uses Pentagon 320-line geometry for ALL ZX machines (technically incorrect for ZX48/128), which is why it shows the same tall frame regardless of emulated machine. This is a simplification, not accurate emulation.

### 3.2 Non-Functional Requirements

| ID | Requirement |
|----|-------------|
| NFR-01 | Minimal CPU overhead when overscan mode is disabled |
| NFR-02 | No memory allocation changes at runtime (pre-allocate for overscan) |
| NFR-03 | Compatible with existing recording, screenshot, and videowall features |

## 4. Technical Design

### 4.1 New Constants

Add to `core/src/emulator/platform.h`:

```cpp
// Overscan dimensions (Pentagon reference)
constexpr uint16_t OVERSCAN_TOP_LINES = 16;      // Lines after VBLANK, before visible border
constexpr uint16_t OVERSCAN_LEFT_PIXELS = 24;    // Extra pixels on left (72 - 48)
constexpr uint16_t OVERSCAN_RIGHT_PIXELS = 8;    // Extra pixels on right (56 - 48)
constexpr uint16_t OVERSCAN_BOTTOM_LINES = 0;    // No extra bottom (standard border is full)
```

### 4.2 Extended VideoModeEnum

Modify `core/src/emulator/video/screen.h`:

```cpp
enum VideoModeEnum : uint8_t
{
    M_NUL = 0,
    M_ZX48,              // Sinclair ZX-Spectrum 48k (352×288)
    M_ZX128,             // Sinclair ZX-Spectrum 128k (352×288)
    M_PENTAGON128K,      // Pentagon 128k (352×288)
    M_PMC,               // Pentagon multicolor

    // Overscan variant - Pentagon only (ZX48/128 have no overscan)
    M_PENTAGON128K_OVERSCAN, // Pentagon with overscan (384×304)

    // ... existing modes ...
    M_MAX
};
```

**Note:** Only Pentagon gets an overscan mode. ZX48/ZX128 frame structure has no hidden border lines to expose.

### 4.3 Two-Layer Architecture: Framebuffer vs Display

**Key design decision:** Render **asymmetric** borders to framebuffer (matching real hardware), but **crop/display** based on user settings.

```
┌─────────────────────────────────────────────────────┐
│                    FRAMEBUFFER                      │
│   (Asymmetric, matches hardware timing exactly)     │
│                                                     │
│  ┌───────────────────────────────────────────────┐  │
│  │ Left Border │    Screen 256×192    │ Right    │  │
│  │   72 px     │                      │  56 px   │  │
│  └───────────────────────────────────────────────┘  │
│                     384 × 304                       │
└─────────────────────────────────────────────────────┘
                         │
                         ▼
              ┌─────────────────────┐
              │   DISPLAY VIEWPORT  │
              │   (Configurable)    │
              │                     │
              │  • Symmetric crop   │
              │  • Full overscan    │
              │  • Screen only      │
              └─────────────────────┘
```

### 4.4 New RasterDescriptor Entries

Pentagon overscan descriptor in `rasterDescriptors[]`:

```cpp
const RasterDescriptor rasterDescriptors[M_MAX] = {
    // Standard modes (existing - symmetric display crops):
    // {fullW, fullH, scrW, scrH, offsetL, offsetT, pxPerLine, hSync, hBlank, vSync, vBlank}
    {352, 288, 256, 192, 48, 48, 448, 64, 32, 8, 16},   // M_ZX48k
    {352, 288, 256, 192, 48, 48, 456, 64, 32, 8, 15},   // M_ZX128
    {352, 288, 256, 192, 48, 48, 448, 64, 32, 16, 16},  // M_PENTAGON128K
    
    // Pentagon Overscan (asymmetric framebuffer):
    // Matches xpeccy+ Pentagon geometry: 72 left + 256 screen + 56 right
    {384, 304, 256, 192, 72, 64, 448, 64, 16, 16, 16},  // M_PENTAGON128K_OVERSCAN
};
```

**Pentagon Overscan Dimensions (asymmetric, matching real hardware):**
- `fullFrameWidth: 384` = 72 (left border) + 256 (screen) + 56 (right border)
- `fullFrameHeight: 304` = 64 (top border incl. overscan) + 192 (screen) + 48 (bottom border)
- `screenOffsetLeft: 72` = actual left border width (asymmetric)
- `screenOffsetTop: 64` = 16 (overscan) + 48 (visible border)

### 4.5 Display Viewport Configuration

Add viewport cropping structure:

```cpp
struct DisplayViewport
{
    uint16_t cropLeft;    // Pixels to crop from left
    uint16_t cropRight;   // Pixels to crop from right
    uint16_t cropTop;     // Lines to crop from top
    uint16_t cropBottom;  // Lines to crop from bottom
};

// Preset viewports (actual implementation)
const DisplayViewport FULL_OVERSCAN = {0, 0, 0, 0};           // 384×304 - all extra border visible
const DisplayViewport SYMMETRIC_HORIZONTAL = {0, 32, 0, 0};   // 352×304 - equal 48px L/R borders, full vertical
const DisplayViewport STANDARD = {0, 32, 16, 0};              // 352×288 - matches Pentagon standard
const DisplayViewport SCREEN_ONLY = {48, 80, 64, 48};         // 256×192 - paper area only
```

### 4.6 Recording Behavior

**Recording always uses native framebuffer resolution:**

```cpp
// In RecordingManager
void startRecording()
{
    // Get framebuffer dimensions (NOT display viewport)
    auto& fb = _emulator->GetScreen()->GetFramebufferDescriptor();
    
    // Record at native resolution: 384×304 for overscan mode
    _encoder->configure(fb.width, fb.height);
    
    // Optional: allow integer scaling (2x, 3x, etc.)
    if (_config.integerScale > 1)
    {
        _encoder->configure(fb.width * _config.integerScale, 
                           fb.height * _config.integerScale);
    }
}
```

This ensures:
1. Demo content in overscan areas is captured
2. Consistent video dimensions regardless of display viewport
3. Optional integer scaling for higher resolution output

### 4.7 Framebuffer Allocation

In `Screen::SetVideoMode()`, framebuffer allocates full asymmetric size:

```cpp
_framebuffer.width = rasterDescriptor.fullFrameWidth;   // 384 for Pentagon overscan
_framebuffer.height = rasterDescriptor.fullFrameHeight; // 304 for Pentagon overscan
_framebuffer.memoryBufferSize = _framebuffer.width * _framebuffer.height * RGBA_SIZE;
```

### 4.8 Display Pipeline

Add viewport application in the display path:

```cpp
// In Screen or display widget
void applyViewport(const DisplayViewport& vp, uint32_t* srcBuffer, uint32_t* dstBuffer)
{
    uint16_t srcW = _framebuffer.width;
    uint16_t srcH = _framebuffer.height;
    uint16_t dstW = srcW - vp.cropLeft - vp.cropRight;
    uint16_t dstH = srcH - vp.cropTop - vp.cropBottom;
    
    for (int y = 0; y < dstH; y++)
    {
        memcpy(&dstBuffer[y * dstW],
               &srcBuffer[(y + vp.cropTop) * srcW + vp.cropLeft],
               dstW * sizeof(uint32_t));
    }
}
```

### 4.9 Mode Switching API

```cpp
// In emulator.h
bool SetOverscanMode(bool enable);
bool IsOverscanMode() const;
void SetDisplayViewport(const DisplayViewport& viewport);
const DisplayViewport& GetDisplayViewport() const;

// In emulator.cpp
bool Emulator::SetOverscanMode(bool enable)
{
    VideoModeEnum currentMode = _screen->GetVideoMode();
    
    // Only Pentagon supports overscan
    if (currentMode != M_PENTAGON128K && currentMode != M_PENTAGON128K_OVERSCAN)
    {
        return false;  // ZX48/128 have no overscan
    }
    
    VideoModeEnum newMode = enable ? M_PENTAGON128K_OVERSCAN : M_PENTAGON128K;
    
    if (newMode != currentMode)
    {
        _screen->SetVideoMode(newMode);
        return true;
    }
    return false;
}
```

### 4.10 Feature Toggle Integration

```cpp
// In featuremanager.h
namespace Features {
    constexpr const char* kOverscanMode = "overscan";        // Enable overscan framebuffer
    constexpr const char* kOverscanDisplay = "overscan_display"; // Show overscan in UI
}
```

### 4.11 UI Integration

#### Menu Items (unreal-qt)

```
View
├── ...
├── Overscan Mode              Ctrl+Shift+O   (Pentagon only)
├── Display Viewport           ▸
│   ├── Full Overscan          (384×304) - all extra border visible
│   ├── Symmetric Horizontal   (352×304) - equal 48px L/R, full vertical
│   ├── Standard               (352×288) - matches Pentagon standard
│   └── Screen Only            (256×192) - paper area only
└── ...
```

#### VideoWall Support

Tiles display based on viewport setting, but recording uses framebuffer:

```cpp
QSize EmulatorTile::getDisplaySize() const
{
    auto& vp = _emulator->GetDisplayViewport();
    auto& fb = _emulator->GetScreen()->GetFramebufferDescriptor();
    return QSize(fb.width - vp.cropLeft - vp.cropRight,
                 fb.height - vp.cropTop - vp.cropBottom);
}
```

## 5. File Changes Summary

| File | Changes |
|------|---------|
| `core/src/emulator/platform.h` | Add overscan constants, `DisplayViewport` struct |
| `core/src/emulator/video/screen.h` | Add `M_PENTAGON128K_OVERSCAN` enum, extend `rasterDescriptors[]` |
| `core/src/emulator/video/screen.cpp` | Add viewport cropping in display path |
| `core/src/emulator/emulator.h` | Add `SetOverscanMode()`, `SetDisplayViewport()` |
| `core/src/emulator/emulator.cpp` | Implement Pentagon-only overscan switching |
| `core/src/base/featuremanager.h` | Add `kOverscanMode`, `kOverscanDisplay` constants |
| `unreal-qt/src/mainwindow.cpp` | Add menu items for overscan + viewport selection |
| `unreal-qt/src/emulator/emulatorwidget.cpp` | Apply viewport cropping before display |
| `unreal-videowall/src/videowall/EmulatorTile.cpp` | Use viewport for display, framebuffer for recording |
| `core/src/recording/recordingmanager.cpp` | Always use native framebuffer resolution |

## 6. Testing Plan

### 6.1 Unit Tests

| Test | Description |
|------|-------------|
| `RasterDescriptor_Overscan_Dimensions` | Verify 384×304 dimensions for overscan modes |
| `Framebuffer_Overscan_Allocation` | Verify correct buffer size allocation |
| `ModeSwitch_Standard_To_Overscan` | Verify runtime switching works |

### 6.2 Visual Tests

| Test | Description |
|------|-------------|
| Border demo (e.g., "Insult") | Verify overscan content is visible |
| Screenshot capture | Verify 384×304 PNG output in overscan mode |
| Recording | Verify video dimensions match overscan |

### 6.3 Compatibility Tests

| Test | Description |
|------|-------------|
| Xpeccy+ snapshot load | Verify content appears in same position |
| Standard demo | Verify no regression in standard mode |

## 7. Rollout Plan

1. **Phase 1**: Implement core changes (VideoModeEnum, RasterDescriptor)
2. **Phase 2**: Add mode switching API and feature toggle
3. **Phase 3**: UI integration (menu, settings persistence)
4. **Phase 4**: VideoWall support
5. **Phase 5**: Documentation and release

## 8. Resolved Design Decisions

| Question | Decision |
|----------|----------|
| Asymmetric borders? | **Yes** - render 72 left / 56 right to framebuffer (matches hardware). Display viewport crops to symmetric if needed. |
| Per-machine overscan? | **Pentagon only** - ZX48/128 have no overscan (312/311 lines vs 320). Not a timing difference, but different frame structure. |
| Recording dimensions? | **Always native framebuffer** - 384×304 for Pentagon overscan. Optional integer scaling (2x, 3x). |

## 9. Open Questions

1. **Viewport persistence?** Should display viewport be saved per-emulator or globally?
2. **Integer scaling options?** 2x only, or allow 3x, 4x for 4K displays?
3. **Viewport in screenshots?** Apply viewport crop, or save full framebuffer like recording?

## 9. References

- [Xpeccy+ video.c](https://github.com/xpeccy/xpeccy-plus/blob/master/src/libxpeccy/video/video.c)
- [Xpeccy+ config.cpp](https://github.com/xpeccy/xpeccy-plus/blob/master/src/xcore/config.cpp) (line 107)
- [MiSTer ZX Spectrum core](https://github.com/MiSTer-devel/ZXSpectrum_MiSTer) - ula.sv
- [docs/inprogress/2026-08-13-overscan-video-mode/xpeccy-vs-unreal-video-rendering.md](xpeccy-vs-unreal-video-rendering.md)
