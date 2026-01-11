# Optimization Roadmap: Screen Rendering Performance

**Date**: 2026-01-11  
**Priority**: P0 (Critical Path)

## Executive Summary

Screen rendering now dominates CPU usage (~50% of total). This document outlines cross-platform optimization strategies for:
1. **ScreenZX::Draw** - SIMD vectorization with fallback
2. **TransformTstate* functions** - LUT-based coordinate lookups  
3. **Qt blending** - Pre-compositing and overdraw reduction

---

## 1. ScreenZX::Draw Optimization

### Current Implementation Analysis

**Location**: `screenzx.cpp:622-662` (LUT-optimized version)

**Hot Path** (called ~69,888 times per frame):
```cpp
// First pixel
framebufferARGB[framebufferOffset] = ((pixels << lut.pixelXBit) & 0x80) ? colorInk : colorPaper;
// Second pixel
framebufferARGB[framebufferOffset + 1] = ((pixels << (lut.pixelXBit + 1)) & 0x80) ? colorInk : colorPaper;
```

**Identified Bottleneck**:
- Conditional branching (`? :`) for each pixel causes pipeline stalls

### ⚠️ Demo Compatibility Constraint

> **IMPORTANT**: Many ZX-Spectrum demos use "racing the beam" techniques where the Z80 modifies
> attribute memory while the ULA is actively rendering a line. This allows up to **8 different
> colors per 8x8 character cell** instead of just 2.
>
> This means we **cannot** batch 8 pixels together - we MUST read attributes at each 2-pixel
> t-state granularity to support multicolor demo effects correctly.

### Proposed Optimization: Branch-Free Color Selection

Replace conditional branching with arithmetic mask operations:

```cpp
// Current (branching - causes pipeline stalls)
color = ((pixels << bit) & 0x80) ? ink : paper;

// Branch-free (predictable, no pipeline stalls)
uint32_t pixelSet = (pixels << bit) & 0x80;        // 0x80 or 0x00
uint32_t mask = (pixelSet >> 7) * 0xFFFFFFFF;      // 0xFFFFFFFF or 0x00000000
color = (ink & mask) | (paper & ~mask);            // Select without branching
```

### Optimized 2-Pixel Draw Implementation

```cpp
void ScreenZX::Draw(uint32_t tstate)
{
    // ... LUT lookup and bounds checking (unchanged) ...

    if (lut.renderType == RT_SCREEN)
    {
        uint8_t* zxScreen = _activeScreenMemoryOffset;
        uint8_t pixels = *(zxScreen + lut.screenOffset + lut.symbolX);
        uint8_t attributes = *(zxScreen + lut.attrOffset + lut.symbolX);
        uint32_t colorInk = _rgbaColors[attributes];
        uint32_t colorPaper = _rgbaFlashColors[attributes];

        // Branch-free first pixel
        uint32_t bit0 = (pixels << lut.pixelXBit) & 0x80;
        uint32_t mask0 = static_cast<uint32_t>(-(static_cast<int32_t>(bit0) >> 7));
        framebufferARGB[framebufferOffset] = (colorInk & mask0) | (colorPaper & ~mask0);

        // Branch-free second pixel
        uint32_t bit1 = (pixels << (lut.pixelXBit + 1)) & 0x80;
        uint32_t mask1 = static_cast<uint32_t>(-(static_cast<int32_t>(bit1) >> 7));
        framebufferARGB[framebufferOffset + 1] = (colorInk & mask1) | (colorPaper & ~mask1);
    }
    else
    {
        // Border (unchanged)
        uint32_t borderColor = _rgbaColors[_borderColor];
        framebufferARGB[framebufferOffset] = borderColor;
        framebufferARGB[framebufferOffset + 1] = borderColor;
    }
}
```

### Why Branch-Free is Better

| Aspect | Ternary Operator | Branch-Free |
|:---|:---|:---|
| Pipeline | May stall on misprediction | No branches to predict |
| Predictor pressure | Consumes branch predictor entries | None |
| Instruction count | ~3-4 with branch | ~5-6 arithmetic |
| Consistency | Variable timing | Constant timing |

**Expected improvement**: Small but measurable (~5-10% in Draw function)

---

## 2. LUT-Based Coordinate Lookups

### Current Implementation Analysis

**TransformTstateToFramebufferCoords** (`screenzx.cpp:334-364`):
- Called ~69,888 times per frame
- Contains division and modulo operations per call
- Multiple conditional checks

**TransformTstateToZXCoords** (`screenzx.cpp:366-401`):
- Called ~69,888 times per frame (often redundantly)
- Contains division, modulo, and multiple conditionals

### Proposed LUT Structure

Pre-compute ALL coordinate transformations in a single lookup table:

```cpp
struct TstateCoordLUT {
    uint16_t framebufferX;     // Framebuffer X coordinate (or UINT16_MAX if invisible)
    uint16_t framebufferY;     // Framebuffer Y coordinate
    uint16_t zxX;              // ZX screen X (255 if border/invisible)
    uint8_t  zxY;              // ZX screen Y (192 if border/invisible)
    uint8_t  symbolX;          // Pre-computed x / 8
    uint8_t  pixelXBit;        // Pre-computed x % 8
    uint8_t  renderType;       // RT_BLANK, RT_BORDER, or RT_SCREEN
    uint16_t attrOffset;       // Pre-computed _attrLineOffsets[y]
    uint16_t screenOffset;     // Pre-computed _screenLineOffsets[y]
};

// Allocate for max frame size (69888 entries * 16 bytes = ~1.1 MB)
TstateCoordLUT _tstateLUT[MAX_FRAME_TSTATES];
```

### Initialization

```cpp
void ScreenZX::CreateTstateLUT() {
    const RasterDescriptor& rd = rasterDescriptors[_mode];
    
    for (uint32_t t = 0; t < _rasterState.maxFrameTiming; t++) {
        TstateCoordLUT& entry = _tstateLUT[t];
        
        // Calculate framebuffer coords
        uint16_t fbX, fbY;
        if (TransformTstateToFramebufferCoords_Internal(t, &fbX, &fbY)) {
            entry.framebufferX = fbX;
            entry.framebufferY = fbY;
            
            // Calculate ZX coords
            uint16_t zxX, zxY;
            if (TransformTstateToZXCoords_Internal(t, &zxX, &zxY)) {
                entry.zxX = zxX;
                entry.zxY = zxY;
                entry.symbolX = zxX / 8;
                entry.pixelXBit = zxX % 8;
                entry.renderType = RT_SCREEN;
                entry.attrOffset = _attrLineOffsets[zxY];
                entry.screenOffset = _screenLineOffsets[zxY];
            } else {
                entry.zxX = 255;
                entry.zxY = 192;
                entry.renderType = RT_BORDER;
            }
        } else {
            entry.framebufferX = UINT16_MAX;
            entry.renderType = RT_BLANK;
        }
    }
}
```

### Optimized Draw Using LUT

```cpp
void ScreenZX::Draw_LUT(uint32_t tstate) {
    const TstateCoordLUT& lut = _tstateLUT[tstate];
    
    if (lut.renderType == RT_BLANK) return;
    
    uint32_t* fb = (uint32_t*)_framebuffer.memoryBuffer;
    uint32_t offset = lut.framebufferY * _framebuffer.width + lut.framebufferX;
    
    if (lut.renderType == RT_SCREEN) {
        uint8_t* screen = _activeScreenMemoryOffset;
        uint8_t pixels = screen[lut.screenOffset + lut.symbolX];
        uint8_t attrs = screen[lut.attrOffset + lut.symbolX];
        uint32_t ink = _rgbaColors[attrs];
        uint32_t paper = _rgbaFlashColors[attrs];
        
        // Two pixels per t-state
        fb[offset]     = ((pixels << lut.pixelXBit) & 0x80) ? ink : paper;
        fb[offset + 1] = ((pixels << (lut.pixelXBit + 1)) & 0x80) ? ink : paper;
    } else {
        // Border
        uint32_t borderColor = _rgbaColors[_borderColor];
        fb[offset] = fb[offset + 1] = borderColor;
    }
}
```

**Expected Improvement**: Eliminates all division/modulo/conditionals → **~3-5x faster**

---

## 3. Qt Blending Optimization

### Current Analysis

**`qt_blend_argb32_on_argb32_neon`** (~12% of CPU):
- Called during `QPainter::drawImage()` or compositing
- Indicates alpha blending on final framebuffer

**Root Cause**: Qt is performing blending even though the framebuffer is opaque.

### Proposed Optimizations

#### Option A: Use Opaque Format

```cpp
// Instead of ARGB32, use RGB32 (no alpha channel)
QImage framebufferImage(
    _framebuffer.memoryBuffer,
    _framebuffer.width,
    _framebuffer.height,
    QImage::Format_RGB32  // Not Format_ARGB32
);
```

#### Option B: Disable Composition

```cpp
// When rendering the framebuffer to screen
painter.setCompositionMode(QPainter::CompositionMode_Source);
painter.drawImage(destRect, framebufferImage);
```

#### Option C: Direct Pixel Copy

```cpp
// Use memcpy for opaque content instead of QPainter
void EmulatorTile::convertFramebuffer() {
    if (sourceFormat == destFormat && sourcePixelSize == destPixelSize) {
        memcpy(dest, src, size);  // No alpha blending needed
    } else {
        // Fallback to QPainter
    }
}
```

#### Option D: Native Rendering Bypass

For maximum performance, bypass Qt entirely for framebuffer display:

**macOS (Metal)**:
```cpp
#ifdef Q_OS_MACOS
id<MTLTexture> texture = /* create from framebuffer */;
[metalView.currentDrawable.texture setBytes:_framebuffer.memoryBuffer ...];
#endif
```

**Windows (Direct2D)**:
```cpp
#ifdef Q_OS_WIN
ID2D1Bitmap* bitmap = /* create from framebuffer */;
renderTarget->DrawBitmap(bitmap, ...);
#endif
```

---

---

## 4. WD1793 FDD Sleep Mode Optimization

### Current Analysis

**WD1793 processing** (~8% of CPU):
- `handleStep()` called on EVERY CPU step (lines 1958-1963)
- `process()` called unconditionally, even when FDD is idle
- Functions like `processFDDIndexStrobe` (2.26%) and `processClockTimings` (1.91%) run continuously

**Root Cause**: The FDD controller runs its full state machine on every CPU cycle even when:
- No disk is inserted
- Motor is off
- No active commands (IDLE state)

### Proposed: Sleep Mode with Lazy Wake-on-Access

```cpp
class WD1793 {
private:
    bool _sleeping = true;          // Start in sleep mode
    uint64_t _wakeTimestamp = 0;    // T-state when last accessed
    static constexpr uint64_t SLEEP_AFTER_IDLE_TSTATES = Z80_FREQUENCY * 2;  // 2 seconds of inactivity
    
public:
    void handleStep() {
        // Skip processing if sleeping
        if (_sleeping) return;
        
        // Check if we should go to sleep (idle for too long)
        if (_state == S_IDLE && !_motorTimeoutTStates) {
            uint64_t currentTime = _context->pCore->GetZ80()->t;
            if (currentTime - _wakeTimestamp > SLEEP_AFTER_IDLE_TSTATES) {
                enterSleepMode();
                return;
            }
        }
        
        process();
    }
    
    void enterSleepMode() {
        _sleeping = true;
        MLOGDEBUG("WD1793 entering sleep mode");
    }
    
    void wakeUp() {
        if (_sleeping) {
            _sleeping = false;
            _wakeTimestamp = _context->pCore->GetZ80()->t;
            MLOGDEBUG("WD1793 waking up");
        }
    }
    
    // Called on ANY port access (IN or OUT)
    uint8_t portDeviceInMethod(uint16_t port) {
        wakeUp();  // <-- Wake on access
        // ... existing code ...
    }
    
    void portDeviceOutMethod(uint16_t port, uint8_t value) {
        wakeUp();  // <-- Wake on access
        // ... existing code ...
    }
};
```

### Sleep Conditions

Enter sleep mode when ALL conditions are met:
1. `_state == S_IDLE` - No active command
2. `_motorTimeoutTStates == 0` - Motor is off
3. Idle duration > 2 seconds (configurable)

### Wake Conditions

Wake immediately on:
1. Any port IN (read) to FDC ports (#1F, #3F, #5F, #7F, #FF)
2. Any port OUT (write) to FDC ports
3. Disk insert/eject event

### Expected Improvement

- **Idle FDD**: 8% → ~0% CPU
- **Active FDD**: No change (full processing when needed)

---

## Implementation Priority

| Phase | Target | Expected Gain | Effort | Status |
|:---:|:---|:---:|:---:|:---:|
| **1** | WD1793 FDD sleep mode | **8% → ~0%** | Low | ✅ Done |
| **2** | LUT-based coordinates | **~2.4x faster Draw** | Medium | ✅ Done |
| **3** | Branch-free color selection | **~4% in Draw** | Low | ✅ Done |
| **4-5** | Batch 8-pixel + SIMD | **~25x faster screen render** | Medium | ✅ Done (ScreenHQ=OFF) |
| **Qt** | SmoothPixmapTransform=false | **~90% Qt scaling reduction** | Low | ✅ Done |
| **6** | GPU rendering (QRhiWidget) | **Near-zero CPU scaling** | Medium | Approved |

> **Note**: Phases 4-5 are integrated via the `ScreenHQ` feature flag. When ScreenHQ=OFF, 
> batch 8-pixel rendering provides **25x speedup** for screen-only rendering. Videowall 
> automatically disables ScreenHQ for all instances.
>
> **Qt Optimization**: Setting `SmoothPixmapTransform=false` reduces Qt scaling overhead by ~90%.
> See [benchmarking-results.md](benchmarking-results.md) for full profiling data.
>
> **Phase 6**: GPU-accelerated rendering proposal documented in [phase-6-gpu-rendering-proposal.md](phase-6-gpu-rendering-proposal.md).

## Files to Modify

- `core/src/emulator/io/fdc/wd1793.h` - Add sleep mode state
- `core/src/emulator/io/fdc/wd1793.cpp` - Implement sleep/wake logic
- `core/src/emulator/video/zx/screenzx.h` - Add LUT and SIMD declarations
- `core/src/emulator/video/zx/screenzx.cpp` - Implement draw optimizations
- `core/src/emulator/video/screen.h` - Base class updates
- `unreal-videowall/emulatortile.cpp` - Qt rendering optimizations
