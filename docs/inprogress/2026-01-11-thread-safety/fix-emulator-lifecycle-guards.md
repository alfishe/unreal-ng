# Proposed Fix: Emulator Lifecycle Guards

## Overview

Add lifecycle state tracking and guards to prevent operations on emulators that are being destroyed. The fix protects ALL automation modules (CLI, WebAPI, Lua, Python) through a central guard.

## Implementation Status

| Phase | Description | Status |
|-------|-------------|--------|
| Phase 1 | Add `StateDestroying` to enum | ✅ Complete |
| Phase 2 | Add `IsDestroying()` method | ✅ Complete |
| Phase 3 | Guard core operations | ✅ Complete |
| Phase 4 | Add WebAPI-level checks | ✅ Complete |

## Files Modified

### Core Library

#### [core/src/emulator/emulator.h](../../core/src/emulator/emulator.h)
- Added `StateDestroying` to `EmulatorStateEnum`
- Added `IsDestroying()` method declaration

#### [core/src/emulator/emulator.cpp](../../core/src/emulator/emulator.cpp)
- Set `StateDestroying` state in `Release()` before cleanup
- Added guard in `LoadSnapshot()` to reject operations during destruction
- Implemented `IsDestroying()` method

### WebAPI Module

#### [core/automation/webapi/src/api/snapshot_api.cpp](../../core/automation/webapi/src/api/snapshot_api.cpp)
- Added `IsDestroying()` check with HTTP 503 response

## Protection Architecture

```
┌────────────────────────────────────────────────────────────┐
│                    Automation Modules                      │
├──────────┬──────────┬──────────┬──────────┬────────────────┤
│   CLI    │  WebAPI  │   Lua    │  Python  │     Other      │
├──────────┴──────────┴──────────┴──────────┴────────────────┤
│                         ↓ calls                            │
├────────────────────────────────────────────────────────────┤
│           Emulator::LoadSnapshot() [GUARDED]               │
│                                                            │
│   if (_state == StateDestroying || _isReleased)            │
│       return false;  // Rejects ALL callers                │
├────────────────────────────────────────────────────────────┤
│                    Emulator::Release()                     │
│                                                            │
│   _isReleased = true;                                      │
│   SetState(StateDestroying);  // Prevents new operations   │
│   ReleaseNoGuard();           // Safe cleanup              │
└────────────────────────────────────────────────────────────┘
```

## Testing Plan

1. **Unit Test**: Call `LoadSnapshot` during `Release()` - should return false
2. **Integration Test**: Toggle fullscreen while loading snapshots via WebAPI
3. **Stress Test**: Rapid fullscreen toggle with continuous WebAPI requests

## Rollout

- [x] Implement `StateDestroying` enum value
- [x] Implement `IsDestroying()` method
- [x] Guard `Emulator::LoadSnapshot()` 
- [x] Guard `Emulator::Release()` to set destroying state
- [x] Add WebAPI-level check for proper HTTP response
- [ ] Add similar guards to `LoadTape()`, `LoadDisk()` (future)
- [ ] Run stress tests before deploying to production
