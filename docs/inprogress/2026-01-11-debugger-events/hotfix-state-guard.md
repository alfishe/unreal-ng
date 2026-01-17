# Hotfix: State-Based Disassembly Guard

**Branch:** `disasm-table`  
**Date:** 2026-01-11

## Summary

Added emulator state check to mute all disassembly operations unless emulator is in `StateRun`, `StateResumed`, or `StatePaused`.

## Change

**File:** `unreal-qt/src/debugger/disassemblertablemodel.cpp`

**In `setEmulator()`:**
```cpp
// HOTFIX: Only trigger disassembly when emulator is in a ready state
EmulatorStateEnum state = m_emulator->GetState();
bool isReadyState = (state == StateRun || state == StateResumed || state == StatePaused);

if (!isReadyState) {
    qDebug() << "Emulator state is" << state << "- deferring disassembly";
    emit dataChanged(...);
    return;  // EXIT EARLY - no disassembly
}

// Only now trigger setVisibleRange() which does actual disassembly
```

## Why This Works

During startup sequence:
1. `setEmulator()` called with `StateInitialized` → **guard triggers, no disassembly**
2. Emulator starts running → state changes to `StateRun`
3. User pauses → state becomes `StatePaused`
4. `updateState()` calls `setEmulator()` again → **guard allows, disassembly proceeds**

## Additional Guards Added

Also added null checks in:
- `disassembleForward()` - checks context, pCore, pDebugManager, pMemory
- `disassembleBackward()` - same checks
- `disassembleAt()` - checks disassembler, memory, registers
