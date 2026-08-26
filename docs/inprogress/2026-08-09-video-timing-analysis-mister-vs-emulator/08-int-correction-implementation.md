# 08 — INT Correction Implementation Guide

**Date:** 2026-08-09
**Related:** [07-int-signal-timings.md](07-int-signal-timings.md)

---

## 1. Where the problem lives

The INT timing pattern is **copy-pasted 10 times** across two files:

| File | Method | Line |
|---|---|---|
| `z80.cpp` | `Z80FrameCycle()` | 379-380 |
| `emulator.cpp` | `RunSingleCPUCycle()` | 1235-1236 |
| `emulator.cpp` | `RunNCPUCycles()` | 1283-1284 |
| `emulator.cpp` | `RunFrame()` | 1343-1344 |
| `emulator.cpp` | `RunNFrames()` | 1440-1441 |
| `emulator.cpp` | `RunTStates()` | 1500-1501 |
| `emulator.cpp` | `RunUntilScanline()` | 1555-1556 |
| `emulator.cpp` | `RunNScanlines()` | 1617-1618 |
| `emulator.cpp` | `RunUntilNextScreenPixel()` | 1704-1705 |
| `emulator.cpp` | `RunUntilInterrupt()` | 1782-1783 |
| `emulator.cpp` | `RunUntilCondition()` | 1859-1860 |

Each location does the same thing:

```cpp
unsigned int_start = config.intstart * state.current_z80_frequency_multiplier;
unsigned int_end = (config.intstart + config.intlen) * state.current_z80_frequency_multiplier;
```

The values flow from INI → config → these 10 call sites → `ProcessInterrupts()`.

## 2. Two approaches

### Approach A: Config-only fix (fastest, config-driven)

Just change the INI values. No code changes. Works because the emulator's INT
mechanism already supports mid-frame and frame-wrapping INT via:

```cpp
if (int_end >= frameLimit) {
    int_end -= frameLimit;
    cpu.int_pending = true;
    int_occurred = true;
}
```

**Files to change:**

| File | Setting | Current | New value |
|---|---|---|---|
| `data/configs/pentagon128k/unreal.ini` | `intstart` | 13 | **71619** |
| `data/configs/pentagon512k/unreal.ini` | `intstart` | 13 | **71619** |
| `data/configs/spectrum48/unreal.ini` | `intstart` | 13 | **1794** |
| `data/configs/spectrum128/unreal.ini` | `intstart` | 13 | **2056** |
| `data/configs/spectrum128/unreal.ini` | `intlen` | 32 | **36** |
| `data/configs/spectrum3/unreal.ini` | `intstart` | 13 | **2056** |
| `data/configs/spectrum3/unreal.ini` | `intlen` | 32 | **36** |
| `data/configs/zx-diagnostics/unreal.ini` | `intstart` | 13 | **71619** (if Pentagon) |

**Pros:** Zero risk, immediate effect
**Cons:** Config-driven only — if someone creates a new INI, they'll get the
wrong default (0). The default in config.cpp is also 0.

### Approach B: Model-aware defaults in config.cpp (recommended)

Add model-derived INT defaults in `config.cpp` after model detection, so the
correct values are set even if the INI has wrong/missing values.

**Step 1:** Add a new method to apply timing defaults per model, called after
`DetermineModel()` in `config.cpp`.

**File:** `core/src/emulator/config.cpp`, after line 313 (`DetermineModel` call):

```cpp
// After DetermineModel sets config.mem_model:
ApplyModelTimingDefaults(config);
```

**Step 2:** Add the method:

```cpp
void Config::ApplyModelTimingDefaults(CONFIG& config)
{
    // Save user overrides (if set in INI to non-zero)
    unsigned userIntstart = config.intstart;
    unsigned userIntlen   = config.intlen;

    // Apply hardware-accurate defaults per model
    switch (config.mem_model)
    {
        case MM_PENTAGON:
            // Pentagon: INT at vc=239, hc=326
            // emulator line = 239 + 80 (vSync+vBlank+topBorder) = 319
            // intstart = 319 * 224 + 163 = 71619
            config.intstart = 71619;
            config.intlen   = 32;
            break;

        case MM_SPECTRUM48:
            // ZX-48K: INT at vc=248, hc=4
            // emulator line = 248 + 72 = 320, wraps mod 312 = 8
            // intstart = 8 * 224 + 2 = 1794
            config.intstart = 1794;
            config.intlen   = 32;
            break;

        case MM_SPECTRUM128:
        case MM_PLUS3:
            // ZX-128K/+3: INT at vc=248, hc=8
            // emulator line = 248 + 72 = 320, wraps mod 311 = 9
            // intstart = 9 * 228 + 4 = 2056
            config.intstart = 2056;
            config.intlen   = 36;  // 128K ULA has 36T INT, not 32T
            break;

        default:
            // Leave existing values for TSConf, ATM, etc.
            break;
    }

    // Allow INI override if user explicitly set non-zero values
    if (userIntstart != 0 && userIntstart != 13)
        config.intstart = userIntstart;
    if (userIntlen != 0 && userIntlen != 32)
        config.intlen = userIntlen;
}
```

**Step 3:** Update the INI files too (so they match and don't rely on the override):

All Pentagon INIs: `intstart=71619`, `intlen=32`
All ZX-48K INIs: `intstart=1794`, `intlen=32`
All ZX-128K/+3 INIs: `intstart=2056`, `intlen=36`

## 3. Why the frame-wrap logic already works

The emulator already handles INT that wraps across the frame boundary. In
`Z80::Z80FrameCycle()` (z80.cpp lines 384-390):

```cpp
// INT interrupt handling lasts for more than 1 frame
if (int_end >= frameLimit)
{
    int_end -= frameLimit;
    cpu.int_pending = true;
    int_occurred = true;
}
```

For Pentagon with `intstart=71619, intlen=32`:
- `int_start = 71619`
- `int_end = 71619 + 32 = 71651`
- `frameLimit = 71680`
- `int_end (71651) < frameLimit (71680)` → no wrap needed

But in `ProcessInterrupts()` (z80.cpp lines 559-566):

```cpp
if (!int_occurred && cpu.t >= int_start) {
    int_occurred = true;
    cpu.int_pending = true;
}
if (cpu.int_pending && (cpu.t >= int_end))
    cpu.int_pending = false;
```

When `cpu.t` reaches 71619, INT is asserted. It stays asserted until `cpu.t`
reaches 71651 (32 t-states later). Then the frame loop ends at `cpu.t = 71680`,
`AdjustFrameCounters` resets `cpu.t -= 71680`, and the next frame starts.

This means the INT fires at the **correct position near the end of the frame**,
the ISR executes, and the frame wraps naturally. The existing infrastructure
handles it correctly — we just need the right `intstart` value.

## 4. INT calculation formula reference

For future models, the `intstart` value can be derived:

```
Given MiSTer coordinates (vc_trigger, hc_trigger):

    vSyncLines    = raster descriptor vSyncLines
    vBlankLines   = raster descriptor vBlankLines
    screenOffsetTop = raster descriptor screenOffsetTop (in lines)
    tstatesPerLine = config.t_line
    totalLines    = config.frame / config.t_line

    paperStartLine = vSyncLines + vBlankLines + screenOffsetTop

    emulatorLine = (vc_trigger + paperStartLine) mod totalLines

    intstart = emulatorLine * tstatesPerLine + (hc_trigger / 2)
```

Verification for Pentagon:
```
paperStartLine = 16 + 16 + 48 = 80
emulatorLine   = (239 + 80) mod 320 = 319 mod 320 = 319
intstart       = 319 * 224 + (326 / 2) = 71456 + 163 = 71619 ✅
```

## 5. Testing the fix

### Quick test: frame counter behavior

Before the fix, `cpu.t` at INT = 13 (start of frame).
After the fix, `cpu.t` at INT = 71619 (near end of frame).

Add a debug log in `ProcessInterrupts`:

```cpp
if (!int_occurred && cpu.t >= int_start) {
    MLOGDEBUG("INT asserted at cpu.t=%u (frame=%u, %.1f%% through frame)",
              cpu.t, frameLimit, (float)cpu.t / frameLimit * 100.0f);
}
```

Expected output:
- Pentagon: `INT asserted at cpu.t=71619 (frame=71680, 99.9% through frame)`
- ZX-48K: `INT asserted at cpu.t=1794 (frame=69888, 2.6% through frame)`

### Integration test

Load a Pentagon demo that does cycle-counted border effects (e.g., a raster
bar demo). With `intstart=13`, the border bars appear at the wrong position.
With `intstart=71619`, they should align correctly with screen content.
