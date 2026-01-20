# Phase 1 Execution Log: WD1793 FDD Sleep Mode

**Date**: 2026-01-11  
**Status**: ✅ Complete  
**Build**: Successful (74/74 targets)

## Overview

Implemented sleep mode for the WD1793 floppy disk controller to eliminate ~8% CPU overhead when the FDD is idle.

## Changes Made

### File: [wd1793.h](core/src/emulator/io/fdc/wd1793.h)

#### 1. Added Sleep Mode Constant
```diff
+    // Sleep mode: FDD enters sleep after 2 seconds of inactivity to save CPU
+    static constexpr const size_t SLEEP_AFTER_IDLE_TSTATES = Z80_FREQUENCY * 2;  // 2 seconds
```

#### 2. Added Sleep Mode State Fields
```diff
+    // Sleep mode state - reduces CPU overhead when FDD is idle
+    bool _sleeping = true;              // Start in sleep mode (wake on first port access)
+    uint64_t _wakeTimestamp = 0;        // T-state when last port access occurred
```

#### 3. Added Sleep Mode Method Declarations
```diff
+    // Sleep mode management - reduces CPU overhead when FDD is idle
+    void wakeUp();                      // Wake FDD from sleep mode
+    void enterSleepMode();              // Put FDD into sleep mode
+    bool isSleeping() const { return _sleeping; }
```

---

### File: [wd1793.cpp](core/src/emulator/io/fdc/wd1793.cpp)

#### 1. Initialize Sleep State in `internalReset()`
```diff
+    // Start in sleep mode - will wake on first port access
+    _sleeping = true;
+    _wakeTimestamp = 0;
```

#### 2. Added `wakeUp()` Method
```cpp
void WD1793::wakeUp()
{
    if (_sleeping)
    {
        _sleeping = false;
        updateTimeFromEmulatorState();
        _wakeTimestamp = _time;
        MLOGDEBUG("WD1793 waking up from sleep mode");
    }
}
```

#### 3. Added `enterSleepMode()` Method
```cpp
void WD1793::enterSleepMode()
{
    if (!_sleeping)
    {
        _sleeping = true;
        MLOGDEBUG("WD1793 entering sleep mode");
    }
}
```

#### 4. Modified `handleStep()` - Skip Processing When Sleeping
```diff
 void WD1793::handleStep()
 {
+    // Skip processing if sleeping - major CPU optimization when FDD is idle
+    if (_sleeping)
+    {
+        return;
+    }
+
+    // Check if we should enter sleep mode (idle for too long with motor off)
+    if (_state == S_IDLE && _motorTimeoutTStates == 0)
+    {
+        updateTimeFromEmulatorState();
+        if (_time - _wakeTimestamp > SLEEP_AFTER_IDLE_TSTATES)
+        {
+            enterSleepMode();
+            return;
+        }
+    }
+
     // We need better precision to read data from the disk...
     process();
 }
```

#### 5. Modified `handleFrameEnd()` - Skip Processing When Sleeping
```diff
 void WD1793::handleFrameEnd()
 {
+    // Skip processing if sleeping
+    if (_sleeping)
+    {
+        return;
+    }
+
     // Perform FSM update at least once per frame...
     process();
 }
```

#### 6. Modified `portDeviceInMethod()` - Wake on Port Read
```diff
     /// endregion </Debug print>
 
+    // Wake from sleep mode on port access
+    wakeUp();
+
     // Update FDC internal states
     process();
```

#### 7. Modified `portDeviceOutMethod()` - Wake on Port Write
```diff
     /// endregion </Debug print>
 
+    // Wake from sleep mode on port access
+    wakeUp();
+
     // Update FDC internal states
     process();
```

---

## Behavior Summary

| Condition | Action |
|:---|:---|
| FDD starts or resets | Enters sleep mode |
| Any port I/O to FDC ports (#1F, #3F, #5F, #7F, #FF) | Wakes immediately |
| Idle (S_IDLE) + motor off + 2 seconds elapsed | Enters sleep mode |
| Sleeping | `handleStep()` and `handleFrameEnd()` return immediately |

## Expected Performance Improvement

- **Idle FDD**: 8% CPU → ~0% CPU
- **Active FDD**: No change (full processing when disk access occurs)

## Verification

```
Build: 74/74 targets successful
Targets: core, testclient, core-tests, core-benchmarks
```

### Unit Tests

**10 sleep mode tests added** to `wd1793_test.cpp`:

| Test | Description |
|:---|:---|
| `SleepMode_StartsInSleepMode` | Verifies FDD starts in sleep mode |
| `SleepMode_HandleStepSkipsWhenSleeping` | Confirms handleStep returns immediately when sleeping |
| `SleepMode_WakeUp` | Tests wakeUp() transitions to awake state |
| `SleepMode_WakeUpIdempotent` | Confirms wakeUp() is idempotent |
| `SleepMode_EnterSleepMode` | Tests enterSleepMode() transitions to sleep |
| `SleepMode_AutoSleepAfterIdleTimeout` | Verifies auto-sleep after 2 seconds idle |
| `SleepMode_NoAutoSleepWhileMotorRunning` | Confirms no sleep while motor runs |
| `SleepMode_NoAutoSleepWhileFSMActive` | Confirms no sleep during active commands |
| `SleepMode_PortAccessWakesUp` | Tests port access wakes FDD |
| `SleepMode_CompleteLifecycle` | Full cycle: sleep → wake → work → sleep → wake |

**Test Results**: ✅ **10/10 PASSED**

```
[==========] Running 10 tests from 1 test suite.
[  PASSED  ] 10 tests.
```

## Next Steps

- Run profiler to verify 8% reduction
- Phase 2: LUT-based coordinate lookups
