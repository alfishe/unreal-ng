# Walkthrough: HUD Styling, Filtering, Indicators, and Test Optimizations

All requested HUD enhancements, breakpoint filtering mechanisms, tile consolidations, indicator transitions, and test optimizations have been implemented across the core emulator engine, Qt UI overlay, and test suites.

## Summary of Completed Changes

### 1. Hidden & Step-Over Breakpoint Filtering
- **Core Breakpoint Descriptor**: Added `bool hidden = false;` to [breakpointmanager.h](file:///Users/dev/Projects/Local%20GitLab/unreal/core/src/debugger/breakpoints/breakpointmanager.h#L18). Step-over breakpoints are tagged with `hidden = true` in [emulator.cpp](file:///Users/dev/Projects/Local%20GitLab/unreal/core/src/emulator/emulator.cpp) and step-out in [debuggerwindow.cpp](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/debugger/debuggerwindow.cpp).
- **Notification Payload**: Added `bool hidden{false};` and constructor parameter `isHidden = false` to [BreakpointTriggeredPayload](file:///Users/dev/Projects/Local%20GitLab/unreal/core/src/emulator/notifications.h#L270-L287).
- **Dispatch Routing**: Checks `bp->hidden`, `StepOver`, `StepOut`, and `TemporaryBreakpoints` before posting notifications across:
  - CPU execution: [z80.cpp](file:///Users/dev/Projects/Local%20GitLab/unreal/core/src/emulator/cpu/z80.cpp)
  - Memory access: [memory.cpp](file:///Users/dev/Projects/Local%20GitLab/unreal/core/src/emulator/memory/memory.cpp)
  - Port I/O: [portdecoder.cpp](file:///Users/dev/Projects/Local%20GitLab/unreal/core/src/emulator/ports/portdecoder.cpp)
- **HUD Model Filtering**: [HudModel::onBreakpoint](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/hud/core/hudmodel.cpp#L827) filters out any hidden or stepover breakpoints so they are never displayed on the HUD.

### 2. Step Status Transitions & Auto-Expiration
- **Step Notification**: Posted `NC_EXECUTION_CPU_STEP` on single-step and step-over completion in [emulator.cpp](file:///Users/dev/Projects/Local%20GitLab/unreal/core/src/emulator/emulator.cpp).
- **Status Transitions**:
  - In [HudModel::onCpuStep](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/hud/core/hudmodel.cpp#L876), when paused, sets status to `PAUSE`.
  - When running/resumed, sets status to `EXECUTE` with a finite TTL (`HudTiming::IndicatorExecuteTimeout` = 1500ms).
- **Indicator TTL & Expire**: Extended [HudModel::setIndicator](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/hud/core/hudmodel.cpp) and [HudModel::expire](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/hud/core/hudmodel.cpp) to expire timed indicators automatically.

### 3. Toast Stack Limit & Breakpoint Exclusivity
- **Stack Limit**: Set `DefaultVisibleToasts = 3` and `DefaultQueuedToasts = 3` in [hudtiming.h](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/hud/core/hudtiming.h) with hard FIFO eviction of excess toasts.
- **Breakpoint Exclusivity**: When a breakpoint triggers, [HudModel::onBreakpoint](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/hud/core/hudmodel.cpp#L852-L858) clears all existing toasts, displaying only a single notification on screen without stacking.

### 4. Consolidated FDD Head Positioning Tile, Drive Indication, Firm Placeholders & Monowidth Font
- **Drive Indication & Firm Placeholders**: Formatted FDD head positioning in [HudModel::onFddState](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/hud/core/hudmodel.cpp#L706) as `"%c: H:%u T:%02u S:%02u"` (e.g. `A: H:0 T:42 S:00`). This ensures a strictly invariant 16-character length across all track and sector values.
- **Monowidth Font**: Added `bool monospace{false};` to [HudElement](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/hud/core/hudsnapshot.h#L197) and [HudModel::setIndicator](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/hud/core/hudmodel.h#L52). In [HudOverlay](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/hud/qt/hudoverlay.cpp), `resolveIndicatorFont()` selects `QFontDatabase::systemFont(QFontDatabase::FixedFont)` with `QFont::Monospace` hint, guaranteeing identical character widths and zero content drift/size jitter during disk stepping.
- **Motor Off Disappearance**: Calls `clearIndicator("fdd")` immediately when `p->_state.motorOn == false`, causing the disk indicator to disappear as soon as the spindle motor stops.

### 5. Wallclock Sleep Removal in FDD Notification Tests
- In [fdd_notification_test.cpp](file:///Users/dev/Projects/Local%20GitLab/unreal/core/tests/emulator/io/fdc/fdd_notification_test.cpp#L240), removed `std::this_thread::sleep_for(10ms)`. Both `InsertNullDoesNotSendNotification` and `EjectWhenNoDiskInsertedDoesNotSendNotification` now run synchronously in 0 ms.

### 6. Indicator Border Radius & Vector Icon Rendering
- Unified indicator border radius across all themes (`<= 12.0f`) in [hudtheme.cpp](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/hud/core/hudtheme.cpp) to maintain a sleek rounded box geometry matching the rest of the HUD.
- Extended [HudOverlay::drawIndicator](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/hud/qt/hudoverlay.cpp) to render vector icons alongside indicator text and status LEDs.

### 7. Grouped Snapshot Loaded Notification (Reset + Load Snapshot)
- **Problem**: When loading `.sna` or `.z80` snapshots, the emulator engine invokes `core.Reset()` (posting `NC_SYSTEM_RESET`) immediately before finishing snapshot load (posting `NC_FILE_LOADED`). This spawned multiple separate stacked toasts (`"Reset"` and `"Snapshot Loaded"`).
- **Remediation**:
  - In [HudModel::onSystemReset](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/hud/core/hudmodel.cpp#L906), tagged the reset toast with `dedupKey = "system-reset"` and suppressed trailing reset events if a Snapshot Loaded notification was already created.
  - In [HudModel::onFileLoaded](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/hud/core/hudmodel.cpp#L1020), snapshot loads share `dedupKey = "system-reset"` with `coalesceCount = false`, updating any existing Reset toast in-place and cleaning up any non-deduped Reset entries.
  - Groups the entire machine reset + snapshot loading sequence into **exactly one combined tile event**: `Snapshot Loaded: <filename>` without badge counters or toast stacking.

### 8. Menu Cleanup (Programmatic HUD Settings)
- **Menu Items Removed**: Removed `HUD Theme`, `HUD Scale`, and `HUD Position` submenus and action groups from [MenuManager](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/menumanager.cpp). Kept only the `&HUD Overlay` checkable action.
- **Signal & Slot Cleanup**: Removed unused change signals from `MenuManager` and unused handler slots (`handleHudThemeChanged`, `handleHudScaleChanged`, `handleHudPositionChanged`) from [MainWindow](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/mainwindow.cpp). Theme, scale, and placement are preserved for programmatic use (REST, CLI, script, config).

### 9. Single Reset Notification on Snapshot Load
- **Root Cause**: `Core::Reset()` in [core.cpp](file:///Users/dev/Projects/Local%20GitLab/unreal/core/src/emulator/cpu/core.cpp) was posting two `NC_SYSTEM_RESET` notifications during a single reset call ("Core reset started" and "Core reset finished").
- **Remediation**: Removed the premature start notification; `NC_SYSTEM_RESET` is now posted exactly once upon reset completion.

### 10. Wallclock Sleep Elimination in Snapshot Load Test
- **Root Cause**: [LoadSnapshot_EmitsSingleResetNotification](file:///Users/dev/Projects/Local%20GitLab/unreal/core/tests/emulator/emulator_test.cpp) used a static 50ms wallclock pause (`std::this_thread::sleep_for`) to drain asynchronous emulator boot messages.
- **Remediation**: Replaced with a `TEST_DRAIN_BARRIER` FIFO barrier message and `WaitForCondition`. Test execution time dropped from ~55ms to 5ms with zero flakiness.

### 11. HUD Off by Default on UI
- **Core Feature Flag**: In [FeatureManager::setDefaults](file:///Users/dev/Projects/Local%20GitLab/unreal/core/src/base/featuremanager.cpp), registered `Features::kHud` as `false` (disabled by default).
- **UI State**:
  - In [MenuManager::createViewMenu](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/menumanager.cpp), `_hudOverlayAction` is initialized unchecked (`false`), and synced with the active emulator feature flag in `updateMenuStates()`.
  - In [MainWindow](file:///Users/dev/Projects/Local%20GitLab/unreal/unreal-qt/src/mainwindow.cpp), `_hudOverlay` is initialized hidden (`setVisible(false)`), and `adoptEmulator()` defaults `hudEnabled` to `false`.
  - Users can enable the HUD on demand via the View menu (`&HUD Overlay`) or `Ctrl+Shift+H`.

---

## Verification Results

### Build
- `ninja -C cmake-build-release`: **PASSED** with zero compiler warnings (`-Werror` enforced).

### Automated Tests
1. **`hud-core-tests`**:
   - `HudEasing_Test`: 5 / 5 passed
   - `HudAnimator_Test`: 3 / 3 passed
   - `HudModel_Test`: 35 / 35 passed
   - **Total: 43 / 43 PASSED (100%) in 161 ms**.

2. **`core-tests`**:
   - `FeatureChangedNotification_Test`: 4 / 4 passed (validating `kHud` disabled by default, toggling, and ini reloading).
   - `Emulator_Test.LoadSnapshot_EmitsSingleResetNotification`: passed in 5 ms.
   - Parallel test execution (`./scripts/run-tests-parallel.sh ./cmake-build-release/bin/core-tests`):
     - **All 10 shards PASSED (219 test suites, 0 failures)**.
