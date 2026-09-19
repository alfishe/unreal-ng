# Walkthrough: Switchable Fast Disk Loading & TTD Shortcut Guarding

Implemented switchable fast disk loading (Layer A FDC timing acceleration + Layer B `$3FEC` `INI` ROM sector drain trap) and automatic shortcut disabling/restoration during Time-Travel Debugging (TTD) recording.

## Key Accomplishments

### 1. TTD Shortcut Guarding & State Restoration
- Added TTD override tracking in [`FeatureManager`](core/src/base/featuremanager.cpp).
- When TTD recording starts (`onTtdRecordingStarted()`), original states of `fastdisk`, `fasttape`, and `turbotape` are saved.
- During active TTD recording:
  - `isEnabled()` returns `false` for `fastdisk`, `fasttape`, and `turbotape`.
  - `setFeature()` rejects enabling any of these shortcut features via API/CLI/WebAPI.
  - Qt desktop UI menu items (`_fastDiskAction`, `_tapeTrapsAction`, `_turboTapeAction`) are disabled and unchecked in `updateMenuStates()`.
- When TTD recording stops (`onTtdRecordingStopped()`), original shortcut states are automatically restored.

### 2. Fast Disk Load Architecture
- Registered `fastdisk` (`fdisk`) in `FeatureManager` (category: performance, default: `on`).
- Implemented Layer A FDC rotational delay acceleration (~100 T-states teleport when armed) in [`WD1793`](core/src/emulator/io/fdc/wd1793.cpp).
- Created [`DiskFastLoad`](core/src/emulator/io/fdc/diskfastload.cpp) component for Layer B `$3FEC` `INI` ROM sector drain trap, verifying TR-DOS 5.03/5.04T signature (`00 C3 69 2F` at `$3D13`) and `INI` opcode (`ED A2` at `$3FEC`).
- Hooked `$3FEC` trap execution in [`Z80Step()`](core/src/emulator/cpu/z80.cpp).

### 3. Desktop UI & Automation Interfaces
- Exposed `fast_disk` setting in CLI (`cli-processor-settings.cpp`) and WebAPI (`settings_api.cpp` under `io_acceleration` group).
- Added `Fast Disk` menu item under Machine menu in Qt desktop UI ([`menumanager.cpp`](unreal-qt/src/menumanager.cpp), [`mainwindow.cpp`](unreal-qt/src/mainwindow.cpp)).

---

## Verification & Test Results

### Automated Unit Tests
Created [`core/tests/emulator/io/fdc/diskfastload_test.cpp`](core/tests/emulator/io/fdc/diskfastload_test.cpp) covering:
1. `IsArmed_WhenConditionsMet`: Validates trap arming when TR-DOS ROM signature, FDC disk, and `fastdisk` feature are present.
2. `IsArmed_ReturnsFalseWhenNotTRDOS`: Confirms trap declines when outside TR-DOS ROM.
3. `IsArmed_ReturnsFalseWhenFeatureDisabled`: Confirms feature toggle control.
4. `IsArmed_DisabledDuringTTDRecordingAndRestoredOnStop`: Validates TTD auto-disabling of shortcuts, blocking API changes during recording, and full state restoration on stop.
5. `CheckROMSignature_RejectsInvalidSignature`: Verifies fallback to standard FDC emulation on non-standard ROMs.

```bash
./cmake-build-release/bin/core-tests --gtest_filter="DiskFastLoad_Test.*"
# Output: 5 tests PASSED (37 ms total)
```

### Full Parallel Test Suite
```bash
cmake --build cmake-build-release --target test-parallel
# Output: All 6,123 tests PASSED across 4 shards (4.5s elapsed)
```

### Build Compliance
- Zero compiler warnings or errors (`ninja -C cmake-build-release`).
- No git commits created (strictly adhering to rule).
