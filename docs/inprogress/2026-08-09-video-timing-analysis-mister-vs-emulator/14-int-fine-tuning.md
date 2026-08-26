# 14 — INT Signal Fine-Tuning and Synchronization Reference

**Date:** 2026-08-10  
**Scope:** Cycle-Accurate INT Signal Timing, Model Calibration, and Demarche Demo Synchronization  
**Reference HDL:** `ZX-Spectrum_MISTer/rtl/ula.sv`  
**Reference Core:** `core/src/emulator/cpu/z80.cpp`, `core/src/emulator/config.cpp`  

---

## 1. Overview & Objectives

Interrupt (INT) signal timing is the single most critical timing parameter in ZX Spectrum hardware emulation. Because software-driven raster effects ("racing the beam", multicolor, border stripes, and floating bus reads) rely on exact CPU cycle counts following the INT signal acknowledgement, even a 1 T-state deviation causes visual corruption or frame-to-frame jitter.

This document serves as the master reference for:
1. **Per-Model INT Parameters**: Exact hardware trigger points, durations, and derived `intstart` values.
2. **Mathematical Derivation Model**: Mapping MiSTer HDL (`vc`, `hc`) coordinates to emulator frame t-states.
3. **Frame Loop & Jitter Corrections**: Eliminating instruction-execution desynchronization during INT entry.
4. **Demo Alignment Calibration**: Empirical verification against Demarche's *"Across The Edge"* timing test harness.

---

## 2. Master Model INT Parameters

| Machine Model | Frame T-States | Lines | Line Length (T) | HDL Trigger (`vc`, `hc`) | Emulator Line | `intstart` (T) | `intlen` (T) |
|---|---|---|---|---|---|---|---|
| **Pentagon 128K / 512K** | 71680 | 320 | 224 | (`239`, `326`) | 319 | **71635** *(see doc 18)* | **32** |
| **ZX-Spectrum 48K** | 69888 | 312 | 224 | (`248`, `4`) | 8 | **1794** | **32** |
| **ZX-Spectrum 128K** | 70908 | 311 | 228 | (`248`, `8`) | 9 | **2056** | **36** |
| **ZX-Spectrum +3 / +2A** | 70908 | 311 | 228 | (`248`, `8`) | 9 | **2056** | **36** |

> [!NOTE]
> **Pentagon `intstart` Note** (superseded by doc 18): the MiSTer line-mapping formula gives 71619, but that assumed our raster window matches MiSTer's line layout. Our renderer places paper 24T into the line (`CreateTstateLUT`: `x = T_in_line*2`, paper x∈[48,304)), so the hardware INT-to-paper distance of 17989T requires `intstart = 17944 − 17989 + 71680 = 71635`. Verified empirically with *Across the Edge* `fix_0` (≤2 px residual).

---

## 3. Mathematical Derivation Formula

To convert MiSTer HDL vertical counter (`vc`) and horizontal counter (`hc`) coordinates to the emulator's single-frame T-state offset (`intstart`):

```
vSyncLines       = RasterDescriptor.vSyncLines
vBlankLines      = RasterDescriptor.vBlankLines
screenOffsetTop  = RasterDescriptor.screenOffsetTop (in lines)
tstatesPerLine   = config.t_line
totalLines       = config.frame / config.t_line

paperStartLine   = vSyncLines + vBlankLines + screenOffsetTop
emulatorLine     = (vc_trigger + paperStartLine) mod totalLines

intstart         = emulatorLine * tstatesPerLine + (hc_trigger / 2)
```

### 3.1 Pentagon Derivation Example
- `paperStartLine` = $16 + 16 + 48 = 80$ lines
- `emulatorLine` = $(239 + 80) \bmod 320 = 319$
- `intstart` = $319 \times 224 + \lfloor 326 / 2 \rfloor = 71456 + 163 = \mathbf{71619 \text{ T}}$

### 3.2 ZX-48K Derivation Example
- `paperStartLine` = $16 + 12 + 44 = 72$ lines
- `emulatorLine` = $(248 + 72) \bmod 312 = 320 \bmod 312 = 8$
- `intstart` = $8 \times 224 + \lfloor 4 / 2 \rfloor = 1792 + 2 = \mathbf{1794 \text{ T}}$

### 3.3 ZX-128K Derivation Example
- `paperStartLine` = $16 + 12 + 44 = 72$ lines
- `emulatorLine` = $(248 + 72) \bmod 311 = 320 \bmod 311 = 9$
- `intstart` = $9 \times 228 + \lfloor 8 / 2 \rfloor = 2052 + 4 = \mathbf{2056 \text{ T}}$

---

## 4. Summary of Root Cause Fixes

### 4.1 Fix 1: Model-Aware Defaults in `config.cpp`
Previously, `intstart` defaulted to `13` across all configuration files. The configuration loader now applies accurate defaults based on `config.mem_model`:

```cpp
void Config::ApplyModelTimingDefaults(CONFIG& config)
{
    switch (config.mem_model)
    {
        case MM_PENTAGON:
            config.intstart = 71619;
            config.intlen   = 32;
            break;
        case MM_SPECTRUM48:
            config.intstart = 1794;
            config.intlen   = 32;
            break;
        case MM_SPECTRUM128:
        case MM_PLUS3:
            config.intstart = 2056;
            config.intlen   = 36;
            break;
        default:
            break;
    }
}
```

### 4.2 Fix 2: Frame Loop Jitter Elimination
Executing `Z80Step()` in the same loop pass after handling an INT caused the first instruction of the ISR to be executed in the wrong cycle phase.

**Resolution in `z80.cpp`**:
```cpp
bool Z80::ProcessInterrupts(...)
{
    if (cpu.int_pending && cpu.iff1 && cpu.t != cpu.eipos)
    {
        HandleINT();
        return true;  // Skip Z80Step() for this pass
    }
    return false;
}

// Frame loop iteration:
bool intHandled = ProcessInterrupts(int_occurred, int_start, int_end);
if (!intHandled)
{
    Z80Step();
}
OnCPUStep();
```

### 4.3 Fix 3: Full Cycle INT Acknowledgement Duration
Subtracting 3T (M1 fetch pre-calculation) when skipping `Z80Step()` resulted in an artificially shortened INT cycle. The full duration is now applied during `HandleINT()`:
- **IM 0 / IM 1**: 13 T-states
- **IM 2**: 19 T-states

---

## 5. Empirical Calibration via *"Across The Edge"*

Demarche's *"Across The Edge"* demo includes 4 TRD variants (`fix_0.trd` through `fix_3.trd`) specifically engineered to test Pentagon clone timing:

| Demo Variant | Shift Technique | Pixel Shift | T-State Shift | Target `intstart` Baseline |
|---|---|---|---|---|
| `fix_0.trd` | Code shift base (CALL 0x6806) | 0 px | 0 T | **71623** |
| `fix_1.trd` | Code shift +1 (CALL 0x6807) | +4 px | +2 T | **71625** |
| `fix_2.trd` | Code shift +2 (CALL 0x6808) | +8 px | +4 T | **71627** |
| `fix_3.trd` | Code shift +3 (CALL 0x6809) | +12 px | +6 T | **71629** |

Each byte shift alters instruction fetch alignment during memory contention windows, shifting border raster effects by exactly **2 T-states (4 pixels)** per increment.

---

## 6. Verification Checklist

- [x] **Config Verification**: All model `.ini` files updated with correct `intstart` and `intlen` values.
- [x] **Unit Tests**: `int_timing_test.cpp` passes across Pentagon, ZX-48K, and ZX-128K presets.
- [x] **Jitter Verification**: Border raster bar T-state position stays strictly invariant across 10,000 consecutive frames.
- [x] **Demo Compatibility**: *"Across The Edge"* (`fix_0.trd`) renders clean, jitter-free border alignment.
