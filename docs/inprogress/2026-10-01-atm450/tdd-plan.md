# ATM450 — TDD plan

Test-first ordering, each phase leaves the tree green. Conventions: CUT pattern
and fixtures per [core/tests/README.md](../../../core/tests/README.md);
scratch files via `TestPathHelper::GetUniqueTestScratchPath()`;
`EnableTurboMode()` only on boot-bound tests that never assert pixels;
no `sleep_for`. Every decoder test targets
`core/tests/emulator/ports/models/portdecoder_atm450_test.{h,cpp}` unless
named otherwise.

## Phase 0 — registry (red → green in one commit)

| # | Test | File | Notes |
|---|---|---|---|
| T0.1 | `EmulatorManager_Test` "no factory case" loop drops `MM_ATM450` (edit `emulatormanager_test.cpp:574-576`); add the mirror of the TSL block: `IsModelSupported(MM_ATM450)` true + `IsModelCreatable` true after `data/configs/atm450/unreal.ini` lands | `emulatormanager_test.cpp` | The ini ships in the same commit or the creatable half stays red |
| T0.2 | Model create via automation: `POST /emulator/start {"model":"ATM450"}` returns an id (WebAPI test pattern from `modelswitch_test.cpp`) | `core/tests/automation/` | |

## Phase 1 — decoder unit tests (CUT, no ROM needed)

Port arms from [requirements.md](requirements.md) R2; reference lines in
[cross-mapping.md](cross-mapping.md) §3. Write these against the not-yet-existing
decoder class (red), then implement until green.

| # | Behavior under test | Reference pin |
|---|---|---|
| T1.1 | `#FDFD` group write latches `pFDFD`; decode is the group `(port & 0x8202) == 0x8000`, so `#FDFD`, `#FDF5`, `#FCFD`-family members hit while `#7DFD` (A15=0) and `#F3FD` (A9=1) do not | io.cpp:577-582 |
| T1.2 | RAM page at `#C000` = `(p7FFD & 7) \| (pFDFD & 7) << 3`, masked by RAM size: full 6 bits at 1024K, top bit(s) drop at 512K | memory.cpp:138-139 |
| T1.3 | `aFE` latch: write `#xxFE` latches A15-A8 into `aFE`; writes to odd ports do not; `aFE.7` 1→0 switches window 0 to RAM page 0 and window 1 to RAM page **4** (not 5) | io.cpp:446-469, memory.cpp:140-145 |
| T1.4 | ROM priority matrix (R2 arm 4), one test per row: 7FFD.5 clears CPSYS; TR-DOS+FDFD.3 forces CPSYS; CPSYS → sys ROM; TR-DOS → dos ROM; else 7FFD.4 → sos/128 | memory.cpp:148-161 |
| T1.5 | `aFB` latch on reads with A2=0 (`#FB`, `#7B`, `#x0B`-family); a read with A2=1 does not latch; latching updates banks when CPSYS flips | io.cpp:1040-1055 |
| T1.6 | `#FE` read bit 7: `atm450_z` zero windows at t=7200/7284/7326 (+40 T each), 0x80 elsewhere; pin three in-window and three out-of-window samples | atm.cpp:315-324 |
| T1.7 | Bright border from A3 of the `#FE` write (`(port & 8) ^ 8`), reusing the family `atmBorderBright` state | io.cpp:461-463 |
| T1.8 | Negative space: `#FF77`, `#xxFF7`, `#EFF7`, `#xxBF`, `#xxBE`, gluk `#DFF7/#BEF7`, SD `#77/#57` writes change nothing on ATM450 (state dumps identical) | io.cpp:231 gate |
| T1.9 | `#FFFD` (AY data) write side effects on `pFDFD` — pins whatever OQ-3's reference-order research decides; the assertion is written after reading the reference, not guessed | io.cpp order |
| T1.10 | `ApplyBootROMDefaults`: RM_128/RM_SOS/RM_SYS/RM_DOS table incl. the 450-specific `RM_DOS` keeps `p7FFD.4` set | memory.cpp:375-400 |

## Phase 2 — video wiring (mostly already green)

| # | Test | Notes |
|---|---|---|
| T2.1 | `ModeMatrix_ATM450_AFEBits` already green — extend it with mid-frame `aFE` transitions if the ATM710 suite has an equivalent (follow `atm_video_modes_suite_test.cpp`) | screen.cpp:336 |
| T2.2 | Screen report (`DeviceState::Screen`) names the mode correctly for aFE 0/1/3 | devicestate.cpp ATM branch |

## Phase 3 — boot tests (real ROM, `data/rom/atm1.rom`)

Mirror the ATM710 pair `core/tests/emulator/machines/atm710/`:
`atm710_trdos_boot_test.cpp`, `atm710_cpm_boot_test.cpp`,
`atm710_game2048_repro_test.cpp` → `core/tests/emulator/machines/atm450/`.

| # | Test | Success criterion |
|---|---|---|
| T3.1 | `ATM450_BootToBasic_Test` | Reset → ROM page per boot mode → BASIC 128 banner pixel/OCR or PC-in-ROM assertion (turbo mode allowed) |
| T3.2 | `ATM450_TrdosBoot_Test` | `#3D13` entry → dos ROM at `#0000`, TR-DOS 5.04T catalog of a scratch TRD |
| T3.3 | `ATM450_CpmSysRom_Test` | CPSYS path: sys ROM visible at `#0000` (resolves OQ-1 — assert the signature of the page actually mapped, e.g. via the ROM signature helper) |
| T3.4 | `ATM450_GameRepro_Test` | An ATM 16-color (aFE mode 0) title loads and renders — proves DetectModeATM1 + renderer end-to-end; assert on rendered pixels (no turbo mode here) |
| T3.5 | `ATM450_RamAt0Mode_Test` | aFE.7=0 → code runs from RAM page 0 at `#0000` (poke a JP, run, assert PC) |

## Phase 4 — TTD and state

| # | Test | Notes |
|---|---|---|
| T4.1 | `TTDAtmPaging` round-trip includes `aFE`, `aFB`, `pFDFD` (extend `ttdatmpaging_test.cpp` with an ATM450 case; add `pFDFD` to the blob if audit finds it missing) | ttdatmpaging.cpp:30 |
| T4.2 | TTD record → aFE mode switch → replay matches frame checksums; port journal attributes the `#FDFD`/`#FE`-group writes with the new decoder's port codes | portdecoder.cpp:571 pattern |
| T4.3 | `TtdClockUnits() == 1` and top-clock time = frame time (guard for the R8 non-goal) | |
| T4.4 | Snapshot (SNA/Z80 where applicable) + model switch away/back keeps the machine state (`modelswitch_test.cpp` pattern) | |

## Phase 5 — surfaces and docs (assert via existing suites)

| # | Item |
|---|---|
| T5.1 | WebAPI `GET /emulator/models` lists `ATM450` creatable; `mcp-tools-test.cpp` `list_models` includes it |
| T5.2 | Device state: `/state/memory` paging report fields valid for 450 (no CMOS/SD sections) — extend `devicestatevideo_test.cpp`-style case if one exists for 710 |
| T5.3 | Qt: `menumanager.cpp` `supportedModels` + manual smoke: create from menu, reset, load a TRD |
| T5.4 | Docs updated: `.recipe/_common/machines.md`, `AGENTS.md` creatable list, `docs/features/automation.md` if it enumerates models |

## Verification gates

- Full build + `test-parallel` with the `-j` cap (50 % cores) before any commit
  request; Linux gcc check via `docker/linux/build.sh --test` (new C++ in
  `core/`).
- The A/B performance rule does not apply (no hot-path branch added: the new
  decoder lives behind the existing model factory exactly like ATM710/ATM3).
