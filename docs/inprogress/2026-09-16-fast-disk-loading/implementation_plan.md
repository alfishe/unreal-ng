# Fast Disk Loading (TR-DOS Service Trap) — Implementation Plan

Implementation plan for switchable fast disk loading in Unreal-NG via **Layer A (FastFDD timing profile)** and **Layer B (ROM `$3FEC` `INI` sector drain trap)**, as specified in [Fast Disk Loading Design](design.md).

## User Review Required

> [!NOTE]
> **V1 Scope (r1 Revision):** Per design revision r1, v1 focuses on FDC timing compression (Layer A) and opcode-level ROM sector transfer traps (Layer B). The high-risk `$3D13` TR-DOS service ABI reimplementation (Layer C) is deferred to v2. This delivers near-instant loading (1–3 frames per file) with zero risk of breaking BASIC autostart sysvars or TR-DOS service stack frames.

> [!IMPORTANT]
> **Zero Warnings & Quality Policy:** Build must pass with zero compiler warnings (`ninja -C cmake-build-release`). All tests in `core-tests` must pass.

## Open Questions

None. The design is fully reconciled with source research from UnrealSpeccy and Xpeccy+.

## Proposed Changes

### Core Engine & Base Feature System

#### [MODIFY] [featuremanager.h](../../../core/src/base/featuremanager.h)
#### [MODIFY] [featuremanager.cpp](../../../core/src/base/featuremanager.cpp)
- Register `fastdisk` (alias `fdisk`, category `performance`, default `on`).
- Add `kFastDisk`, `kFastDiskAlias`, `kFastDiskDesc` constants.

#### [MODIFY] [emulatorcontext.h](../../../core/src/emulator/emulatorcontext.h)
#### [MODIFY] [emulatorcontext.cpp](../../../core/src/emulator/emulatorcontext.cpp)
- Add nullable `DiskFastLoad* pDiskFastLoad` pointer to `EmulatorContext`.

---

### FDC Core Acceleration (Layer A)

#### [MODIFY] [wd1793.h](../../../core/src/emulator/io/fdc/wd1793.h)
#### [MODIFY] [wd1793.cpp](../../../core/src/emulator/io/fdc/wd1793.cpp)
#### [MODIFY] [fdd.h](../../../core/src/emulator/io/fdc/fdd.h)
#### [MODIFY] [fdd.cpp](../../../core/src/emulator/io/fdc/fdd.cpp)
- Implement `FastFDD` mode / timing compression:
  - Compressed step, restore, and head-settle delays when `fastdisk` feature is armed.
  - Sector rotation teleport: immediately align sector rotational offset when servicing sector read/address commands.
  - Preserve CPU-paced DRQ holding (stalling unconsumed DRQ bytes until handled or 1-revolution timeout).
  - Retain initial BUSY window and 15 ms E-flag settle for Profi/loader compatibility.

---

### Fast Disk Load Component & Hook (Layer B)

#### [NEW] [diskfastload.h](../../../core/src/emulator/io/fdc/diskfastload.h)
#### [NEW] [diskfastload.cpp](../../../core/src/emulator/io/fdc/diskfastload.cpp)
- Create `DiskFastLoad` class handling Layer B `$3FEC` `INI` sector drain trap.
- `IsArmed()` predicate: `fastdisk` enabled, `CF_TRDOS` set, `pBetaDisk` present, live bank signature match (`0x00 0xC3 0x69 0x2F` at `$3D13` and `0xED 0xA2` `INI` at `$3FEC`).
- `HandleSectorDrainTrap(Z80& cpu)`: writes pending sector bytes directly to Z80 memory via `Z80::wd()` with M1 attribution at `$3FEC`, updates `WD1793` sector buffer and status, advances Z80 registers (`HL`, `B`, `PC += 2`).

#### [MODIFY] [core.h](../../../core/src/emulator/cpu/core.h)
#### [MODIFY] [core.cpp](../../../core/src/emulator/cpu/core.cpp)
- Instantiate `DiskFastLoad` during `Core` initialization and register in `EmulatorContext::pDiskFastLoad`.

#### [MODIFY] [z80.cpp](../../../core/src/emulator/cpu/z80.cpp)
- Hook `DiskFastLoad::HandleSectorDrainTrap` inside `Z80::Z80Step()` right after the fast tape block, checking `pc == 0x3FEC`.

---

### Automation & UI Control Surfaces

#### [MODIFY] [cli-processor-settings.cpp](../../../core/automation/cli/src/commands/cli-processor-settings.cpp)
- Add `fast_disk` / `fdisk` setting to CLI settings commands.

#### [MODIFY] [settings_api.cpp](../../../core/automation/webapi/src/api/settings_api.cpp)
- Add `fast_disk` to `io_acceleration` WebAPI settings JSON response/update handlers.

#### [MODIFY] [menumanager.h](../../../unreal-qt/src/menumanager.h)
#### [MODIFY] [menumanager.cpp](../../../unreal-qt/src/menumanager.cpp)
- Add Machine → Fast Disk Loading checkable menu action in Qt GUI.

---

### Automated Tests

#### [NEW] [diskfastload_test.cpp](../../../core/tests/emulator/io/fdc/diskfastload_test.cpp)
#### [MODIFY] [CMakeLists.txt](../../../core/tests/CMakeLists.txt)
- Unit tests for `DiskFastLoad`:
  1. Arming / signature gate tests (valid ROM vs corrupted signature / disarmed).
  2. Timing compression differential tests (Layer A off vs on).
  3. `$3FEC` INI sector drain unit test.
  4. Non-standard ROM fallback / disarm test.

## Verification Plan

### Automated Tests
- Build core tests: `ninja -C cmake-build-release core-tests`
- Run fast disk load unit tests: `./cmake-build-release/bin/core-tests --gtest_filter="*DiskFastLoad*"`
- Run full parallel test suite: `cmake --build cmake-build-release --target test-parallel`

### Manual Verification
- Mount TRD disk image in `unreal-qt`, execute `RUN`, verify near-instant load.
- Toggle Machine → Fast Disk Loading menu item, confirm authentic timing when toggled off.
