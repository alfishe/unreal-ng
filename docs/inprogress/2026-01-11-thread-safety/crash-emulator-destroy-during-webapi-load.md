# Emulator Destruction During WebAPI Operations

## Problem Summary

Race condition crash occurs when the main thread destroys emulators (during grid resizing) while the WebAPI thread is simultaneously loading snapshots.

## Crash Analysis

**Exception**: `SIGSEGV` at address `0x30` (null pointer + offset)

### Thread 0 (Main/UI Thread)
```
VideoWallWindow::resizeGridIntelligently()
  → EmulatorManager::RemoveEmulator()
    → Emulator::Release()
      → Core::Release()
        → Memory::~Memory()
```

### Thread 6 (WebAPI Thread) - CRASHED
```
EmulatorAPI::loadSnapshot()
  → Emulator::LoadSnapshot()
    → LoaderSNA::applySnapshotFromStaging()
      → Core::Reset()
        → Keyboard::Reset()  ← CRASH: accessing destroyed object
```

## Root Cause

The `Emulator` object is being destroyed on the main thread while the WebAPI thread still holds a reference and is performing operations. There's no synchronization or validity check.

## Trigger Scenario

1. VideoWall enters fullscreen mode
2. `toggleFullscreenMacOS()` is called
3. `resizeGridIntelligently()` removes excess emulators
4. Simultaneously, WebAPI receives snapshot load request
5. WebAPI thread accesses emulator that's being destroyed → CRASH

## Affected Components

| Component | File | Issue |
|-----------|------|-------|
| EmulatorManager | [core/src/emulator/emulatormanager.cpp](../../core/src/emulator/emulatormanager.cpp) | `RemoveEmulator()` doesn't wait for pending operations |
| EmulatorAPI | [core/automation/webapi/src/api/snapshot_api.cpp](../../core/automation/webapi/src/api/snapshot_api.cpp) | `loadSnapshot()` doesn't check if emulator is being destroyed |
| Emulator | [core/src/emulator/emulator.cpp](../../core/src/emulator/emulator.cpp) | No lifecycle state tracking |

## Proposed Fixes

### 1. Immediate Workaround (VideoWall)
Skip grid resizing when entering fullscreen - keep existing tile count.

### 2. Short-term Fix (Core Emulator)
Add `StateDestroying` state and `IsDestroying()` check in core operations.

### 3. Long-term Fix (All Automation Modules)
Implement proper lifecycle management affecting CLI, WebAPI, Lua, and Python.

---

See also: [Proposed Fix Details](./fix-emulator-lifecycle-guards.md)
