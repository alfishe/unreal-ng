# Fix WD1793 FDC Idle Stall on Type 2/3 Commands After Motor Timeout

## Problem Statement

When the floppy disk drive motor times out (~3 seconds / 15 disk revolutions of idle time), the WD1793 FDC enters a state where it refuses to execute any Type 2 (Read Sector, Write Sector) or Type 3 (Read Address, Read Track, Write Track) commands. The controller remains stuck/idle, returning `NOT READY` status (`WDS_NOTRDY`) and never spinning up the drive motor until the emulator is reset. A machine reset boots TR-DOS, which issues a Type 1 command (RESTORE `0x00`), restarting the motor and temporarily masking the defect until the next motor timeout.

This defect was recently exposed following commit `2e9d4521` ("fix(fdc): Stop spinning FDD motor on WD1793 reset") and commit `b794203e` ("fix(fdc): do not run the WD1793 FSM per instruction while idle with motor off"). Previously, `internalReset()` never stopped the motor, so the motor spun indefinitely in earlier builds, masking the bug.

## Root Cause Analysis

### 1. Circular Ready/Motor Deadlock in Type 2 and Type 3 Commands
In `core/src/emulator/io/fdc/wd1793.cpp`:
```cpp
bool WD1793::isReady()
{
    bool diskInserted = _selectedDrive->isDiskInserted();
    bool motorOn = _selectedDrive->getMotor();
    return diskInserted && motorOn;
}
```
`isReady()` requires `_selectedDrive->getMotor()` to be `true`.

In `startType2Command()` (lines 1906–1915) and `startType3Command()` (lines 1955–1964):
```cpp
if (!isReady())
{
    // If the drive is not ready - end immediately
    transitionFSM(WD1793::S_END_COMMAND);
}
else
{
    // Ensure the motor is spinning
    prolongFDDMotorRotation();
    ...
```
`prolongFDDMotorRotation()` is placed **inside the `else` branch** of `if (!isReady())`.
- If the motor has stopped: `_selectedDrive->getMotor()` is `false` $\rightarrow$ `isReady()` is `false`.
- The command takes the `!isReady()` branch and transitions to `S_END_COMMAND`.
- `prolongFDDMotorRotation()` is **never called**.
- The drive motor **never starts**.
- Status register sets `WDS_NOTRDY`.
- All retries fail identically because the motor remains off.

In contrast, `startType1Command()` (line 1857) and `cmdForceInterrupt()` (line 1800) call `prolongFDDMotorRotation()` unconditionally. When the user resets the machine, TR-DOS runs RESTORE (Type 1), calling `prolongFDDMotorRotation()` and starting the motor.

### 2. Missing Early-Exit in Command Handlers
When `startType2Command()` transitions to `S_END_COMMAND` on `!isReady()`, the calling methods (`cmdReadSector`, `cmdWriteSector`, `cmdReadAddress`, `cmdReadTrack`, `cmdWriteTrack`) do not check `isReady()` or return early. They proceed to push work into `_operationFIFO` and overwrite the FSM state with `S_FETCH_FIFO`.

### 3. Spurious INTRQ on Motor Stop
In `processFDDMotorState()` (`core/src/emulator/io/fdc/wd1793.cpp` lines 411–417):
```cpp
if (_selectedDrive->getMotor())
{
    stopFDDMotor();

    // Notify via Beta128 status INTRQ bit about changes
    raiseIntrq(); // BUG
}
```
Real WD1793 / KR1818VG93 hardware has no motor pin and never pulses INTRQ when the motor stops. Raising INTRQ on motor timeout sets bit 7 of Beta128 port `#FF`, falsely signalling command completion to guest software polling `#FF`. (Legitimate Force Interrupt condition I1 is already handled separately in `processForceInterruptConditions()`).

### 4. Kempston Mouse Hypothesis
Investigation and execution traces on `master` confirmed that Kempston mouse decoding has **zero conflict** with FDC operations. While TR-DOS is active (`CF_DOSPORTS` / `CF_TRDOS`), `Default_IsPort_KempstonMouse` returns `false`, and `PortDecoder_Pentagon128` passes all Beta128 ports to the FDC. Boot traces with mouse enabled vs disabled produce byte-identical results.

---

## Proposed Changes

### Core FDC Engine

#### [MODIFY] [`core/src/emulator/io/fdc/wd1793.cpp`](file:///Users/dev/Projects/Local%20GitLab/unreal/core/src/emulator/io/fdc/wd1793.cpp)
1. **`startType2Command()` and `startType3Command()`**:
   - Call `prolongFDDMotorRotation();` unconditionally at command entry, matching `startType1Command()` and `cmdForceInterrupt()`.
   - Then check `if (!isReady()) transitionFSM(WD1793::S_END_COMMAND); else loadHead();`.
2. **`cmdReadSector()`, `cmdWriteSector()`, `cmdReadAddress()`, `cmdReadTrack()`, `cmdWriteTrack()`**:
   - Add `if (!isReady()) return;` immediately after `startType2Command()` / `startType3Command()` so that unready commands (e.g. no disk inserted) cleanly exit without pushing work to the FIFO or corrupting FSM state.
3. **`processFDDMotorState()`**:
   - Remove the bogus `raiseIntrq();` call inside `if (_selectedDrive->getMotor()) { stopFDDMotor(); }`.

---

### Automated Tests

#### [MODIFY] [`core/tests/emulator/io/fdc/wd1793_sleep_timeout_test.cpp`](file:///Users/dev/Projects/Local%20GitLab/unreal/core/tests/emulator/io/fdc/wd1793_sleep_timeout_test.cpp)
1. **`ReadSectorStartsMotorAfterTimeoutSleepAndAwakeIdle`**:
   - Drive the controller until motor times out and controller enters sleep.
   - Issue Read Sector (`0x80`) via `portDeviceOutMethod(PORT_1F, 0x80)`.
   - Assert that the controller wakes, the motor starts spinning, and the sector is read successfully.
   - Repeat for the awake-idle window with motor off.
2. **`MotorTimeoutDoesNotRaiseIntrq`**:
   - Spin the motor, clear INTRQ.
   - Run emulation until the motor times out.
   - Assert that the motor stops, but Beta128 port `#FF` does not report spurious INTRQ (bit 7 remains 0).

---

## Verification Plan

### Automated Tests
```bash
# 1. Compile core-tests
ninja -C cmake-build-release core-tests

# 2. Run WD1793 sleep/timeout tests
./cmake-build-release/bin/core-tests --gtest_filter="*WD1793_SleepTimeout*"

# 3. Run all WD1793 test suites
./cmake-build-release/bin/core-tests --gtest_filter="*WD1793*"

# 4. Run full test suite in parallel to guarantee zero regressions
cmake --build cmake-build-release --target test-parallel
```

### Constraints Checklist
- Zero compiler warnings (`-Wall -Wextra -Werror` policy).
- All tests under 50 ms.
- Zero commits without explicit user request.
