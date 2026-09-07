# Bug Fix: CF_TRDOS Flag Not Set on Snapshot Load

## Problem Description

When loading a 128K .sna snapshot with TR-DOS active (`is_TRDOS=1`), the snapshot loader activates the TR-DOS ROM but **fails to set the `CF_TRDOS` flag** in the emulator state.

This causes TR-DOS to be immediately deactivated when `UpdateZ80Banks()` is called, breaking any software that depends on TR-DOS ROM routines.

## Root Cause

### File: `core/src/loaders/snapshot/loader_sna.cpp`

Lines 501-505 (BEFORE fix):
```cpp
// Step 5: Activate TR-DOS ROM if needed
if (_ext128Header.is_TRDOS)
{
    _context->pMemory->SetROMDOS();
}
```

**Problem:** Only calls `SetROMDOS()` without setting the `CF_TRDOS` flag.

### How Other Code Handles This

#### Z80 Execution (z80.cpp lines 155-160):
```cpp
if (!(state.flags & CF_TRDOS) && (cpu.pch == 0x3D))
{
    state.flags |= CF_TRDOS;  // ← Sets the flag!
    memory.UpdateZ80Banks();
}
```

#### Memory Banking (memory.cpp lines 689-699):
```cpp
if (state.flags & CF_TRDOS)  // ← Checks the flag!
{
    if (state.p7FFD & 0x10)
    {
        SetROMDOS();
    }
    else
    {
        SetROMSystem();
    }
}
```

**Without the CF_TRDOS flag set:**
- `UpdateZ80Banks()` thinks TR-DOS is NOT active
- Switches to wrong ROM (48K or 128K BASIC ROM)
- TRDOS calls at 0x3D00-0x3DFF fail

## The Fix

### File: `core/src/loaders/snapshot/loader_sna.cpp`

Lines 501-508 (AFTER fix):
```cpp
// Step 5: Activate TR-DOS ROM if needed
if (_ext128Header.is_TRDOS)
{
    // Set CF_TRDOS flag to indicate TR-DOS is active
    _context->emulatorState.flags |= CF_TRDOS;
    
    // Activate TR-DOS ROM
    _context->pMemory->SetROMDOS();
}
```

## Impact

### Before Fix:
- "Insult" demo loader fails with infinite loop
- Any snapshot with TR-DOS active would fail
- TRDOS ROM calls return garbage data

### After Fix:
- TR-DOS state properly restored from snapshot
- Loader can successfully chain sectors
- TRDOS calls work correctly

## Testing

### Reproduce Bug:
1. Load `insult-1-4.sna` (has `is_TRDOS=1`)
2. Emulator pauses at breakpoint 0x3D2F
3. Resume execution
4. Loader loops forever reading same sector

### Verify Fix:
1. Apply fix and recompile
2. Load `insult-1-4.sna`
3. Resume execution
4. Loader should successfully load all 21 sectors
5. Demo should start at 0x8700

## Related Code

- **CF_TRDOS definition:** `core/src/emulator/platform.h` line 851
  ```cpp
  #define CF_TRDOS  0x02    // DOSEN trigger
  ```

- **CF_TRDOS usage:** 
  - Z80 execution: `core/src/emulator/cpu/z80.cpp` lines 155-160
  - Memory banking: `core/src/emulator/memory/memory.cpp` lines 689-699
  - Port decoder: `core/src/emulator/ports/ports.cpp`

## Conclusion

This was a classic case of **incomplete state restoration**. The snapshot format includes the `is_TRDOS` byte to indicate TR-DOS was active, but the loader only restored the ROM page without setting the corresponding flag in the emulator's state machine.

**The fix is a one-line addition:** `_context->emulatorState.flags |= CF_TRDOS;`

---

*Date: 2026-01-30*
*Found by: User observation that snapshot loading might not restore TR-DOS activation state*
*Fixed by: Adding CF_TRDOS flag update in snapshot loader*
