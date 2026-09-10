# 11 — INT Timing Jitter Fix

**Date:** 2026-08-09
**Scope:** Fix border effect jitter caused by INT timing issues

---

## Problem

Border stripes in demos (e.g., Edge) show inconsistent positioning frame-to-frame,
appearing to "jitter" by 1-4 pixels (2-8 T-states).

## Root Causes Identified

### 1. Incorrect INT Position (4T error)

**File**: `data/configs/pentagon128k/unreal.ini`

| Parameter | Was | Should Be | Error |
|-----------|-----|-----------|-------|
| intstart | 71623 | 71619 | +4T |

**Calculation**:
```
Pentagon INT trigger: vc=239, hc=326 (from MiSTer HDL ula.sv)
emulatorLine = (239 + 80) mod 320 = 319
intstart = 319 × 224 + (326 / 2) = 319 × 224 + 163 = 71619
```

### 2. Z80Step Executed After INT Handling

**File**: `core/src/emulator/cpu/z80.cpp`

The frame loop was:
```cpp
while (cpu.t < frameLimit)
{
    ProcessInterrupts(...);  // May call HandleINT()
    Z80Step();               // Executes FIRST ISR instruction!
    OnCPUStep();
}
```

When `HandleINT()` was called, the loop continued to `Z80Step()`, which executed
the first instruction of the ISR in the same iteration. This caused INT entry
plus first instruction to occur in a single loop pass — not cycle-accurate.

### 3. INT Duration Subtracted M1 Time Incorrectly

```cpp
case 2:
    interruptDuration = 19 - 3;  // Assumed M1 already counted
```

This assumed `Z80Step()` would add M1 time, but with the fix that skips Z80Step,
the full 19T must be added.

---

## Fixes Applied

### Fix 1: Correct INT Position

```ini
; data/configs/pentagon128k/unreal.ini
; data/configs/pentagon512k/unreal.ini
intstart=71619 ; was 71623
```

### Fix 2: Skip Z80Step After INT

**z80.h**:
```cpp
bool ProcessInterrupts(bool int_occured, unsigned int_start, unsigned int_end);
// Returns true if INT was handled (skip Z80Step)
```

**z80.cpp**:
```cpp
bool Z80::ProcessInterrupts(...)
{
    // ...
    if (cpu.int_pending && cpu.iff1 && cpu.t != cpu.eipos)
    {
        HandleINT();
        return true;  // Signal: skip Z80Step
    }
    return false;
}

// Frame loop:
while (cpu.t < frameLimit)
{
    bool intHandled = ProcessInterrupts(int_occurred, int_start, int_end);
    if (!intHandled)
        Z80Step();
    OnCPUStep();
}
```

### Fix 3: Full INT Duration

```cpp
switch (cpu.im)
{
    case 0:
    case 1:
        interruptDuration = 13;  // Full timing (was 13-3)
        break;
    case 2:
        interruptDuration = 19;  // Full timing (was 19-3)
        break;
}
```

---

## Verification Against Other Emulators

### MiSTer HDL (ula.sv)
```verilog
if(!mZX && (vc_next == 239) && (hc_next == 326)) INT <= 1;  // Pentagon
```
Confirms intstart=71619 calculation.

### ZXMAK2 (C#)
Returns bool from INT handling, skips instruction execution:
```csharp
if (INT && (!BINT) && IFF1)
{
    // INT handling adds 13T or 19T
    return true;
}
return false;
```

### UnrealSpeccy Original (C++)
```cpp
cpu->t += (cpu->im < 2) ? 13 : 19;  // Full timing in handle_int()
```

---

## Files Changed

| File | Change |
|------|--------|
| `data/configs/pentagon128k/unreal.ini` | intstart=71619 |
| `data/configs/pentagon512k/unreal.ini` | intstart=71619 |
| `core/src/emulator/cpu/z80.h` | ProcessInterrupts returns bool |
| `core/src/emulator/cpu/z80.cpp` | Skip Z80Step after INT, full INT duration |

---

## Test Plan

1. Load Edge demo on Pentagon 128K
2. Verify border stripes are stable (no frame-to-frame jitter)
3. Set breakpoint at OUT (FE),A — verify T-state is consistent across frames
