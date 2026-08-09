# 05 — Frame Timing Configuration Fixes

**Date:** 2026-08-09
**Files:** `data/configs/spectrum48/unreal.ini`, `data/configs/spectrum128/unreal.ini`, `core/src/emulator/video/screen.h`

---

## 1. ZX-48K frame/line mismatch

### 1.1 Current (wrong) configuration

File: `data/configs/spectrum48/unreal.ini`

```ini
[ULA]
Frame=70908  ; t-states in frame
Line=228     ; t-states in line
```

### 1.2 Correct values

The genuine ZX Spectrum 48K uses:
- **224 t-states per line** (448 HC at 7MHz pixel clock / 2 = 224)
- **312 lines per frame** (non-interlaced PAL)
- **69,888 t-states per frame** (224 × 312 = 69,888)

```ini
[ULA]
Frame=69888  ; t-states in frame
Line=224     ; t-states in line
```

### 1.3 MiSTer verification

From `ula.sv` line 100-102:
```verilog
if (hc==((mZX && m128) ? 455 : 447)) begin  // hc wraps at 448 for mZX=1, m128=0
    hc_next = 0;
    if (vc == (!mZX ? 319 : m128 ? 310 : 311)) begin  // vc wraps at 312 for mZX=1
```

For ZX-48K (`mZX=1, m128=0`):
- HC per line = 448 → T-states/line = 224
- Lines = 312
- Frame = 224 × 312 = 69,888

### 1.4 Impact of the error

Using `Frame=70908, Line=228` makes the ZX-48K model run with ZX-128K timing.
This means:
- The frame is 1,020 t-states too long
- Each scanline is 4 t-states too wide
- INT fires at the wrong position relative to the raster
- Any timing-precise code will behave differently from real ZX-48K hardware

## 2. ZX-128K raster descriptor issue

### 2.1 Current raster descriptor

File: `core/src/emulator/video/screen.h` line 314

```cpp
{352, 288, 256, 192, 48, 48, 456, 64, 32, 8, 16},   // M_ZX128 - Not ready!
```

Fields: `{fullFrameWidth=352, fullFrameHeight=288, screenWidth=256,
screenHeight=192, screenOffsetLeft=48, screenOffsetTop=48,
pixelsPerLine=456, hSyncPixels=64, hBlankPixels=32, vSyncLines=8,
vBlankLines=16}`

### 2.2 The line count issue

With the current values:
- `vSyncLines + vBlankLines + fullFrameHeight = 8 + 16 + 288 = 312`

But MiSTer ZX-128K has **311 lines** (`vc wraps at 311`):
```verilog
if (vc == (!mZX ? 319 : m128 ? 310 : 311))
// For ZX-128K (mZX=1, m128=1): vc wraps at 310 → 311 total lines (0..310)
```

### 2.3 Fix

Adjust the raster descriptor so the total line count is 311:

```cpp
// Option A: reduce vBlankLines to 15
{352, 287, 256, 192, 48, 48, 456, 64, 32, 8, 15},   // M_ZX128
// Total = 8 + 15 + 287 = 310... need 311

// Option B: adjust both vBlankLines and fullFrameHeight
{352, 288, 256, 192, 48, 48, 456, 64, 32, 8, 15},   // M_ZX128
// Total = 8 + 15 + 288 = 311 ✅
```

The exact split between vBlank and visible area depends on the physical VBlank
positioning of the 128K ULA. The MiSTer HDL uses `vc_next >= 236` for VBlank on,
which is within the non-visible area, so the count is 311 total lines.

## 3. ZX-128K INT duration

### 3.1 Current

```ini
intlen=32     ; int length in t-states
```

### 3.2 Correct for ZX-128K

From MiSTer `ula.sv` line 172:
```verilog
if(INT) INTCnt <= ((m128 && INTCnt == 71) || (~m128 && INTCnt == 63)) ? 7'd0 : (INTCnt + 1'd1);
```

For ZX-128K: INT lasts 72 HC = **36 t-states**.
For ZX-48K and Pentagon: INT lasts 64 HC = **32 t-states**.

### 3.3 Fix

The ZX-128K INI should use `intlen=36`. Alternatively, make this a model-derived
default rather than relying on per-INI configuration.

## 4. Pentagon INT start position

### 4.1 Current

```ini
intstart=13   ; t-states before int
```

### 4.2 MiSTer reference

From `ula.sv` line 170:
```verilog
if(!mZX && (vc_next == 239) && (hc_next == 326)) INT <= 1;
```

The Pentagon INT fires at vc=239, hc=326. Converting to emulator coordinates:
- Emulator line = vc + paperStartLine = 239 + 80 = 319
- Emulator t-state = 319 × 224 + 326/2 = 71,456 + 163 = **71,619**

But the emulator uses `intstart=13`, meaning INT fires at t-state 13. This is
a **critical bug**, not a design difference.

### 4.3 Analysis — CRITICAL BUG

The `intstart` value directly represents the t-state within the frame at which
INT is asserted. The emulator's frame loop checks `cpu.t >= int_start` to fire
INT. With `intstart=13`, INT fires 13 t-states into VSync — at the very
beginning of the frame.

On real Pentagon hardware, INT fires at t-state 71,619 (99.9% through the
frame), wraps across the frame boundary, and the ISR continues into the next
frame. This mid-line, frame-wrapping INT is the defining characteristic of
Pentagon timing.

See [07-int-signal-timings.md](07-int-signal-timings.md) and
[08-int-correction-implementation.md](08-int-correction-implementation.md) for
the complete analysis and fix.

## 5. Summary of configuration changes

| File | Setting | Current | Correct | Notes |
|---|---|---|---|---|
| `spectrum48/unreal.ini` | `Frame` | 70908 | **69888** | 224×312 |
| `spectrum48/unreal.ini` | `Line` | 228 | **224** | ZX-48K line |
| `spectrum48/unreal.ini` | `intstart` | 13 | **1794** | INT at vc=248, hc=4 |
| `pentagon128k/unreal.ini` | `intstart` | 13 | **71619** | INT at vc=239, hc=326 |
| `spectrum128/unreal.ini` | `intstart` | 13 | **2056** | INT at vc=248, hc=8 |
| `spectrum128/unreal.ini` | `intlen` | 32 | **36** | ZX-128K INT |
| `screen.h` M_ZX128 | `vBlankLines` | 16 | **15** | 311 total lines |

## 6. Risk assessment

- **ZX-48K frame fix**: Low risk, high correctness improvement. The wrong frame
  size affects all timing on this model.

- **ZX-128K raster fix**: Low risk. The 311 vs 312 line difference is subtle
  but matters for frame-precise effects and VBlank timing.

- **ZX-128K INT length**: Low risk. The 36 vs 32 t-state difference affects
  interrupt acknowledgment timing marginally but should be correct for accuracy.
