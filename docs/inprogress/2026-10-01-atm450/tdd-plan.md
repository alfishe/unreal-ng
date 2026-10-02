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
| T1.1 | `#FDFD` group write latches `pFDFD`; decode is `(port & 0x8202) == 0x8000`, so `#FDFD`, `#FDF5`, `#FCFD` hit while `#7DFD` (A15=0 → palette) and `#F3FD`/`#FFFD` (A9=1 → AY) do not | io.cpp:577-582 |
| T1.2 | RAM page at `#C000` = `((p7FFD & 7) \| (pFDFD & 7) << 3) & ramMask`: bit 2 of pFDFD drops at 512K, counts at 1024K | memory.cpp:138-139 |
| T1.3 | `aFE` latch takes the **low** address byte of `#xxFE`-group writes (`OUT (#7E)` → aFE = #7E whatever the high byte); odd ports do not latch; A7=0 → window 0 = RAM page 0 (writable), window 1 = RAM page **4**; A7=1 restores ROM + page 5 | io.cpp:466, memory.cpp:140-145 |
| T1.4 | ROM priority matrix (R2 arm 4), one test per row: 7FFD.5 clears CPSYS (sticky); TR-DOS+FDFD.3 sets CPSYS (sticky); CPSYS → sys ROM; TR-DOS → dos ROM even with 7FFD.4=0; else 7FFD.4 → sos/128. Each row asserts the signature bytes of the mapped `atm1.rom` page (pins R3 page order) | memory.cpp:148-161 |
| T1.5 | `aFB` latch takes the **low** byte of an unclaimed A2=0 read (`IN (#FB)` → CPSYS on, `IN (#7B)` → off) and the read returns `#FF`; A2=1 reads do not latch; a GS `#BB` read with the card fitted does **not** latch (claimed first) | io.cpp:724, 1040-1055 |
| T1.6 | `#FE` read bit 7: zero windows at t=7200/7284/7326 (+40 T each), 0x80 elsewhere; three in-window and three out-of-window samples incl. both window edges; bits 6-0 unchanged from the keyboard read | atm.cpp:315-324 |
| T1.7 | Bright border from A3 of the `#FE` write (`#F6` → bright, `#FE` → not) into `atmBorderBright` | io.cpp:461-463 |
| T1.8 | Palette: `#7DFD`-group write stores into the cell = current 4-bit border color; ATM1 bit layout `--grbGRB` (one test per channel: high bit alone, low bit alone); no DOS / pen2 gate; `#xxFF` writes do **not** touch the palette on 450 | io.cpp:533-537, draw.cpp:440-447 |
| T1.9 | Negative space: `#FF77`, `#xxF7`, `#EFF7`, `#xxBF`, `#xxBE`, gluk `#DFF7/#BEF7`, SD `#77/#57` writes change nothing on ATM450 (paging state identical) | 450 arm list |
| T1.10 | Reset / `ApplyBootROMDefaults`: RM_DOS → aFE=#E0, aFB=0, dos ROM at 0, 7FFD.4 kept set; RM_128/RM_SOS/RM_SYS → aFE=#80, aFB=#80, sys ROM at 0; pFDFD cleared | z80.cpp:91,123-133; memory.cpp:375-400 |
| T1.11 | aFE bits 6-5 change → `Screen` re-detects the mode (`#9E` → M_ATM16, `#BE` → M_ATMHR, `#FE` → ZX) | screen.cpp:336 |
| T1.12 | 48K lock: `p7FFD.5` blocks later `#7FFD` writes; `#FDFD` writes still land (UnrealSpeccy has no lock on FDFD) | io.cpp:545-558, 577 |

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
| T3.3 | `ATM450_SysRomBoot_Test` | Default reset (`RM_128`) starts in the sys ROM (page 0) and reaches its own menu/next stage; record what it does with the FDC ports (first evidence for OQ-7) |
| T3.4 | `ATM450_GameRepro_Test` | An ATM 16-color (aFE mode 0) title loads and renders — proves DetectModeATM1 + renderer end-to-end; assert on rendered pixels (no turbo mode here) |
| T3.5 | `ATM450_RamAt0Mode_Test` | aFE.7=0 → code runs from RAM page 0 at `#0000` (poke a JP, run, assert PC) |

## Phase 4 — TTD and state

| # | Test | Notes |
|---|---|---|
| T4.1 | `TTDAtmPaging` round-trip includes `aFE`, `aFB`, `pFDFD` and the palette cells (`pFDFD` is not in the blob today — add it with a layout bump; an old-layout blob still loads with pFDFD = 0) | ttdatmpaging.cpp:30 |
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
