# Walkthrough: Fix WD1793 FDC Idle Stall on Type 2/3 Commands and Regression Test Coverage

## Summary of Changes

We identified and fixed the root cause of the defect on `master` where the WD1793 Floppy Disk Controller (FDC) refused to execute read/write commands and became stuck in an idle state after a disk motor timeout until emulator reset. We also covered the fix with comprehensive unit and integration regression tests.

### 1. Root Cause Resolution in [`wd1793.cpp`](file:///Users/dev/Projects/Local%20GitLab/unreal/core/src/emulator/io/fdc/wd1793.cpp)

1. **Circular Ready/Motor Deadlock in Type 2 and Type 3 Commands**:
   - `isReady()` requires `_selectedDrive->isDiskInserted() && _selectedDrive->getMotor()`.
   - In `startType2Command()` and `startType3Command()`, `prolongFDDMotorRotation()` was previously placed in the `else` branch of `if (!isReady())`.
   - When the drive motor timed out after ~3.0s (15 disk revolutions), `getMotor()` became `false`, causing `isReady()` to return `false`.
   - Any subsequent Type 2 command (Read/Write Sector) or Type 3 command (Read Address, Read/Write Track) entered `if (!isReady())` and aborted immediately to `S_END_COMMAND` without ever calling `prolongFDDMotorRotation()`. The motor was never restarted, and the FDC rejected all future sector operations until a hard emulator reset (which executed Type 1 RESTORE, unconditionally spinning up the motor).
   - **Fix**: Called `prolongFDDMotorRotation()` unconditionally at the start of `startType2Command()` and `startType3Command()` (matching `startType1Command()` and `cmdForceInterrupt()`).

2. **Clean Early Exit on Not-Ready**:
   - Added `if (!isReady()) return;` after `startType2Command()` / `startType3Command()` in `cmdReadSector()`, `cmdWriteSector()`, `cmdReadAddress()`, `cmdReadTrack()`, and `cmdWriteTrack()`. When no disk is inserted, the command transitions to `S_END_COMMAND` and returns immediately without queuing work into the operation FIFO or overwriting the FSM state to `S_FETCH_FIFO`.

3. **Spurious INTRQ on Motor Stop Removed**:
   - In `processFDDMotorState()`, removed the bogus `raiseIntrq()` call inside `if (_selectedDrive->getMotor()) { stopFDDMotor(); }`. Real WD1793 hardware has no motor pin and never raises INTRQ when the drive motor stops spinning. This spurious interrupt was setting bit 7 of Beta128 port `#FF`, confusing guest software polling loops.

---

### 2. Regression Tests Added to [`wd1793_sleep_timeout_test.cpp`](file:///Users/dev/Projects/Local%20GitLab/unreal/core/tests/emulator/io/fdc/wd1793_sleep_timeout_test.cpp)

We added targeted unit regression tests in the real-timing fixture:

- **`MotorTimeoutDoesNotRaiseIntrq`**:
  - Verifies that when the motor times out (~3 s / 15 disk revolutions of inactivity), the motor stops cleanly, `_intrq_out` remains `false`, and Beta128 port `#FF` bit 7 (`WD1793::INTRQ`) remains 0.
- **`ReadSectorStartsMotorAfterTimeoutSleepAndAwakeIdle`**:
  - Verifies that issuing Read Sector (`0x80`) after motor timeout sleep wakes the controller, starts the motor (`getMotor() == true`), and enters the busy state without deadlocking.
  - Verifies that issuing Read Sector during the awake-idle window with the motor stopped also restarts the motor and executes normally.
- **`ReadAddressStartsMotorAfterTimeoutSleep`**:
  - Verifies that Type 3 commands (e.g. Read Address `0xC0`) also properly restart the motor after timeout sleep.
- **`ReadSectorWithoutDiskInsertedFailsGracefully`**:
  - Verifies that when no disk is inserted, Read Sector sets `WDS_NOTRDY`, transitions to `S_END_COMMAND`, clears `WDS_BUSY`, and raises `INTRQ` on completion without hanging.

---

### 3. Model Regression Alignment in [`modelsregression_test.cpp`](file:///Users/dev/Projects/Local%20GitLab/unreal/core/tests/emulator/memory/modelsregression_test.cpp)

- In `ModelsRegression_Test.GoldenBankMaps`, updated the `specs` table row for `"Pentagon128"` to use `RAM_128` instead of `RAM_1024`.
  - Previously, `portdecoder.cpp` only had `if (ramSize == 512)` and defaulted everything else to `PortDecoder_Pentagon128`.
  - With the recent addition of `PortDecoder_Pentagon1024` for `ramSize >= 1024`, passing `RAM_1024` selected 1024K paging (where `#7FFD` bits 6-7 and bit 5 have extended memory functions instead of standard Pentagon 128 behavior), conflicting with the test's `kGoldenRows` assertions for Pentagon 128. Correcting the RAM size to `RAM_128` accurately aligns the model with the test specification.

---

## Verification Results

### Quality Checks
1. **Build**: `ninja -C cmake-build-release`
   - **Result**: Passed with 0 compiler warnings (`-Wall -Wextra -Werror` policy satisfied).
2. **WD1793 Sleep/Timeout Tests**:
   - `./cmake-build-release/bin/core-tests --gtest_filter="WD1793_SleepTimeout_Test.*"`
   - **Result**: 8/8 tests passed in 10 ms (all well under the 50 ms limit).
3. **All WD1793 Suites**:
   - `./cmake-build-release/bin/core-tests --gtest_filter="WD1793*"`
   - **Result**: 105/105 tests passed in 593 ms.
4. **Parallel Test Suite (Sharded)**:
   - `cmake --build cmake-build-release --target test-parallel`
   - **Result**: All 10 shards complete, 100% green.
5. **Sequential Full Suite**:
   - `./cmake-build-release/bin/core-tests`
   - **Result**: 2,943 tests passed, 5 skipped, 0 failed.
