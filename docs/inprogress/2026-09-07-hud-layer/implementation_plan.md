# HUD Breakpoint Filtering, Step Status & Notification Stack Plan

## Goal Description
Implement the complete set of HUD enhancements:
1. **Filter hidden / stepover breakpoint hits**:
   - Internal / stepover / stepout / temporary breakpoints must NEVER trigger a HUD toast or change the indicator to `BREAKPOINT`.
   - Normal user breakpoints trigger "Breakpoint Hit" and set status indicator to `BREAKPOINT`.
2. **Step completion status transition**:
   - When any step is executed (`NC_EXECUTION_CPU_STEP`), the breakpoint status on HUD changes to:
     - `PAUSE` (when stopped/paused after step), or
     - `EXECUTE` (when running/resumed).
3. **Execute status timeout**:
   - When indicator status changes to `EXECUTE`, it automatically disappears after a standardized timing constant (`HudTiming::IndicatorExecuteTimeout = 1500ms`), with zero magic numbers.
4. **Global notification stack limited to 3 with hard eviction**:
   - Configure global limits: `DefaultVisibleToasts = 3`, `DefaultQueuedToasts = 3`.
   - Hard eviction in `HudModel::notify`: when a 4th toast arrives, immediately evict the oldest / lowest priority toast so the stack never exceeds 3.
5. **Breakpoint hit — single notification on screen, no stacking**:
   - When a breakpoint is hit, clear all previous toasts from screen.
   - Post single breakpoint toast with `dedupKey = "breakpoint"` and `coalesceCount = false`.
   - Subsequent breakpoint hits update the single notification in-place without stacking.

---

## Proposed Architecture & Changes

### 1. Core Breakpoint Modeling (`core/`)

#### [MODIFY] [`breakpointmanager.h`](file:///Users/dev/Projects/Local%20GitLab/unreal/core/src/debugger/breakpoints/breakpointmanager.h)
- Add `bool hidden = false;` to `BreakpointDescriptor`.

#### [MODIFY] [`notifications.h`](file:///Users/dev/Projects/Local%20GitLab/unreal/core/src/emulator/notifications.h)
- Extend `BreakpointTriggeredPayload`:
  - Add field `bool hidden{false};`.
  - Add optional constructor parameter `bool isHidden = false` with default value for backwards compatibility across all callers and tests.

#### [MODIFY] [`z80.cpp`](file:///Users/dev/Projects/Local%20GitLab/unreal/core/src/emulator/cpu/z80.cpp), [`memory.cpp`](file:///Users/dev/Projects/Local%20GitLab/unreal/core/src/emulator/memory/memory.cpp), [`portdecoder.cpp`](file:///Users/dev/Projects/Local%20GitLab/unreal/core/src/emulator/ports/portdecoder.cpp)
- When triggering a breakpoint and creating `BreakpointTriggeredPayload`:
  - Query `BreakpointManager` to check if `bp->hidden || bp->note == "StepOver" || bp->note == "StepOut" || bp->group == "TemporaryBreakpoints"`.
  - Set `isHidden = true` on `BreakpointTriggeredPayload`.

#### [MODIFY] [`emulator.cpp`](file:///Users/dev/Projects/Local%20GitLab/unreal/core/src/emulator/emulator.cpp)
- In `Emulator::StepOver()`:
  - Mark `bpDesc->hidden = true;` in addition to `bpDesc->note = "StepOver";`.
  - In `breakpoint_handler` when the stepover breakpoint is hit:
    - Post `NC_EXECUTION_CPU_STEP` via MessageCenter so observers know the step operation has finished.

---

### 2. Qt UI & Debugger Layer (`unreal-qt/`)

#### [MODIFY] [`debuggerwindow.cpp`](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/debugger/debuggerwindow.cpp)
- In `stepOut()`:
  - Mark `bpDesc->hidden = true;` when setting the temporary step-out breakpoint.

---

### 3. HUD Layer (`unreal-qt/src/hud/`)

#### [MODIFY] [`hudtiming.h`](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/hud/core/hudtiming.h)
- Add standardized duration constant:
  ```cpp
  // --- Indicator Durations & Timeouts ---
  inline constexpr std::chrono::milliseconds IndicatorExecuteTimeout{1500};
  ```
- Update `HudLimits`:
  ```cpp
  namespace HudLimits
  {
      inline constexpr size_t DefaultVisibleToasts = 3;
      inline constexpr size_t DefaultQueuedToasts = 3;
  }
  ```

#### [MODIFY] [`hudmodel.h`](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/hud/core/hudmodel.h) & [`hudmodel.cpp`](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/hud/core/hudmodel.cpp)
1. **Global Stack Limit of 3 & Hard Eviction**:
   - In `HudModel::notify`:
     - While `_toasts.size() >= _queuedToasts`: hard-evict the lowest priority / oldest toast.
2. **Breakpoint Hit — Single Notification on Screen, No Stacking**:
   - In `HudModel::onBreakpoint`:
     - Check if hidden (`p->hidden` or descriptor lookup). If hidden: return immediately.
     - Clear existing toasts `_toasts.clear()` so the breakpoint notification is exclusively displayed.
     - Post toast with `req.dedupKey = "breakpoint"` and `req.coalesceCount = false`.
     - Set indicator `"pause"` $\to$ `HudState::Active`, `"BREAKPOINT"`.
3. **Indicator TTL & Auto-Expiration**:
   - In `setIndicator()`: accept `std::chrono::milliseconds ttl = std::chrono::milliseconds(0)`.
   - In `expire(now)`: remove expired indicators where `ind.ttl.count() > 0 && (now - ind.created) >= ind.ttl`.
4. **Step Status Handling**:
   - Subscribe `HudModel` to `NC_EXECUTION_CPU_STEP`.
   - Handler `onCpuStep()`:
     - If paused $\to$ set indicator `"pause"` to `"PAUSE"`.
     - If running $\to$ set indicator `"pause"` to `"EXECUTE"`, ttl `IndicatorExecuteTimeout`.
5. **State Transition Sync**:
   - In `onEmulatorState()`:
     - `StatePaused` $\to$ `"PAUSE"`.
     - `StateRun` / `StateResumed` $\to$ `"EXECUTE"`, ttl `IndicatorExecuteTimeout`.
     - `StateStopped` $\to$ clear indicator `"pause"`.

#### [MODIFY] [`hudoverlay.cpp`](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/hud/qt/hudoverlay.cpp)
- In `drawIndicator()` for `el.id == "ind/pause"`:
  - LED dot color:
    - `"EXECUTE"` $\to$ Green (`rgb(50, 220, 110)`).
    - `"BREAKPOINT"` $\to$ Red (`rgb(255, 50, 60)`).
    - `"PAUSE"` $\to$ Amber (`rgb(255, 190, 45)`).

---

### 4. Automated Tests (`core/tests/` & `unreal-qt/tests/hud/`)

#### [MODIFY] [`instance_tagged_payloads_test.cpp`](file:///Users/dev/Projects/Local%20GitLab/unreal/core/tests/common/instance_tagged_payloads_test.cpp)
- Add unit test verifying `BreakpointTriggeredPayload` correctly stores and exposes the `hidden` boolean flag.

#### [MODIFY] [`hudmodel_test.cpp`](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/tests/hud/hudmodel_test.cpp)
- Update `EventMapping_EmulatorStateChange`:
  - `StatePaused` sets `ind/pause` to `"PAUSE"`.
  - `StateRun` sets `ind/pause` to `"EXECUTE"`.
  - After `IndicatorExecuteTimeout`, `"EXECUTE"` indicator expires.
- Add `EventMapping_BreakpointTriggered_HiddenFiltered`:
  - Verifies hidden breakpoint hit posts no toast and sets no `BREAKPOINT` indicator.
- Add `EventMapping_CpuStep_UpdatesBreakpointToPause`:
  - Verifies that after hitting a breakpoint, executing a step (`NC_EXECUTION_CPU_STEP`) changes status from `"BREAKPOINT"` to `"PAUSE"`.
- Add `EventMapping_CpuStep_UpdatesBreakpointToExecuteAndExpires`:
  - Verifies that executing a step into running state sets `"EXECUTE"` which expires after `IndicatorExecuteTimeout`.
- Add `GlobalStackLimit_HardEvictionAtThree`:
  - Posts 5 toasts and verifies that snapshot contains at most 3 toasts and oldest are hard-evicted.
- Add `BreakpointHit_OnlySingleNotificationOnScreen_NoStacking`:
  - Posts multiple toasts, then triggers breakpoint. Verifies that only the single breakpoint toast remains. Subsequent breakpoint hits replace it without stacking.

---

## Verification Plan

### Automated Tests
```bash
# 1. Build project (0 warnings required)
ninja -C cmake-build-release

# 2. Run HUD model unit tests
./cmake-build-release/bin/hud-core-tests

# 3. Run parallel core test suite
./scripts/run-tests-parallel.sh ./cmake-build-release/bin/core-tests 4
```
- Expected: All tests pass with zero warnings and 100% pass rate.
