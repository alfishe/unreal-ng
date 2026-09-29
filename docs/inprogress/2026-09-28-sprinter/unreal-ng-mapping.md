# Sprinter Sp2000 — mapping onto unreal-ng

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Review round 1 done (2026-09-28); code references checked at `94219b58` (master, 2026-09-28). The shared changes in §2 land before the Sprinter (PLAN #60, #41, #13a, #58, #55) |
| **Design** | [technical-design.md](technical-design.md) and the `tdd-*.md` files |

Nothing in `core/src`, `core/automation`, `core/tests`, `unreal-qt` or `data` mentions the
Sprinter today (`grep -ri sprinter`, 2026-09-28). Three buckets: what the Sprinter uses
unchanged, what it extends, and what is new.

## 1. Reused as-is

| Piece | Where | Used for |
|---|---|---|
| Z80 core, IM 0/1/2, `HandleINT(vector)` | `core/src/emulator/cpu/z80.cpp:1150-1235` | the CPU (the Z84C15 core is a plain Z80) |
| M1 hook `IMachineM1Hook` | `core/src/emulator/cpu/z80.h:290-295`, `:445` (built for ATM3 E3) | DOS in/out, accelerator opcodes, `#1F` operand rewrite |
| 256 RAM pages, 128 ROM pages | `core/src/emulator/platform.h:250-253` | 4 MB RAM, 16 ROM pages; no memory-limit change needed |
| TTD page journal, page 255 as a normal page | `core/src/debugger/ttd/`, `3a6eabc6` | RAM, the port table page, the graphics area |
| WD1793 + drives + TRD/SCL/FDI/UDI/TD0 loaders | `core/src/emulator/io/fdc/wd1793.h`, `core/src/loaders/disk/` | floppies and TR-DOS |
| AY / TurboSound (one chip), beeper, `Covox` | `core/src/emulator/sound/` (`soundmanager.cpp:67-122`, `covox.h:48`) | AY, beeper, plain Covox |
| ZX key matrix | `core/src/emulator/io/keyboard/keyboard.h:159`, `:334` | code `#40` |
| Kempston mouse device (`PeripheralId::KempstonMouse`) and joystick helpers | `core/src/debugger/ttd/ttdserializable.h:51`; `portdecoder.h` | code `#58`, `#15` |
| Storage seam: `IBlockDevice`, `RawImage`, `MemoryDisk`, `SessionWriteMap` | `core/src/emulator/io/storage/` (E5) | IDE media, the built HDD test image |
| Port trace / breakpoints (`OnPortInComplete` / `OnPortOutComplete`) | `core/src/emulator/ports/portdecoder.h:643-652` | tracing, with an extra "code" tag |
| Model creatability rule (decoder + config file) | `core/src/emulator/config.cpp:784`; `portdecoder.cpp:56-135` | `SPRINTER` becomes creatable when both exist |

## 2. Extended (shared code gets a generic change)

| Piece | Today | Change for the Sprinter | Also needed by | Spec |
|---|---|---|---|---|
| CPU clock multiplier | `hw_turbo_shift` / `hw_turbo_shift_applied` = log2, so 2× / 4× only (`platform.h:960-980`, `z80.cpp:602`; TTD checkpoint `ttdcheckpoint.h:190`) | replaced everywhere by `hw_turbo_ratio` / `hw_turbo_ratio_applied` 1-8 (21 MHz = ×6); no converter, TTD fixtures re-recorded; PLAN #60 | ATM710, ATM3, Scorpion (ratio 2/4) | technical design §3 |
| Wait states | ULA contention only (`memory.h:356`, `video/ulacontention.h:66`) | per-bank "has waits" flag; the cost from `SprinterWaits::ExtraClocks(kind, t)` (phase-dependent); PLAN #60 | ATM3 E9 rasters (contention by clock) | technical design §4 |
| Memory write path | non-virtual, "no model reacts to writes" (`memory.h:286-296`) | a write-only `HostBusOverlay` (built, PLAN #60(a); zero cost while not installed, several overlays chained) | TSConf P0 (FM window), ATM3 E8 (flash writes) | TSConf technical design §3.5 item 2 |
| Interrupts | fixed `intstart/intlen` window, vector always `#FF` | `IInterruptSource` with `OnReti()` for the Z84C15 daisy chain (built, PLAN #60(a)) | TSConf P0 | TSConf technical design §3.4; [tdd-accel-sound-input.md](tdd-accel-sound-input.md) §5.1 |
| Cache pages | `MAX_CACHE_PAGES = 2`, cache not emulated (`memory.cpp:788-793`, `:924`) | 4 pages, mapped by `SprinterMemory` | Pentagon cache (`#FB/#7B`), if ever | [tdd-ports-memory.md](tdd-ports-memory.md) §5.1 |
| Screen selection | only `ScreenZX` instantiated (`videocontroller.cpp:9-31`) | a `Screen` subclass per model family (PLAN #60); `R_736_288` raster; for the Sprinter the renderer comes from the PLD configuration module | TSConf (`ScreenTSConf` exists, not instantiated) | [tdd-video.md](tdd-video.md) §1-2 |
| WD1793 | no data-rate notion (`wd1793.h:944-957`) | `rateCheck` option: DD/HD rate vs medium density | +3 / Profi PC formats | [tdd-storage.md](tdd-storage.md) §2.3 |
| Disk loaders | no raw PC `.img` (`.img` = MGT only, `loader_mgt.cpp:33-34`) | `LoaderRawPcFloppy` 720 KB / 1.44 MB, registered by size | Profi CP/M, +3 (storage manager G9) | [tdd-storage.md](tdd-storage.md) §2.4 |
| IDE core (planned, PLAN #13a) | `io/hdd/hdd.*` is a stub (`hdd.cpp`, 20 lines) | **two** `AtaChannel`s per `Core`; latch pattern (e) `A8HalfLatch` | — | IDE design §5; [tdd-storage.md](tdd-storage.md) §3.1 |
| `HostFolderFat` (planned, PLAN #58 M1) | not built | `BootProfile` strategy: partition type + reserved sectors LBA 1-3 (`SprinterDssBootProfile`); FAT32 refused on Sprinter IDE slots | any machine whose firmware boots from a folder volume | [tdd-storage.md](tdd-storage.md) §5 |
| RTC | **built (PLAN #60(c), 2026-09-28)**: `Ds12887` (`io/rtc/ds12887.*`) is the shared MC146818 core (clock, NVRAM, file, fixed time, TTD id 18); `EvoAvr`, Profi and the SMUC run on it | the Sprinter wires its ports, a 128-cell chip with century `#32`, and `[SPRINTER] CmosFile=` | ATM3, Profi, SMUC, ZX-Evo already on it | [tdd-storage.md](tdd-storage.md) §4 |
| Keyboard | no PS/2/AT path (E2b deferred) | the E2b event (ZX + PC key) + `Ps2Set2Encoder` | ZX-Evo E2b (PLAN #55) | [tdd-accel-sound-input.md](tdd-accel-sound-input.md) §3 |
| Automation per-model switches | `state_memory_api.cpp:175`, `cli-processor-state.cpp:338`… | Sprinter rows | — | [tdd-integration.md](tdd-integration.md) §3 |
| Port trace records | address + value | + internal code and its name (PLAN #60) | any table-driven decoder | D10 |

## 3. New from scratch

| Piece | Location | Size |
|---|---|---|
| `PortDecoder_Sprinter` (lookup, dispatch, cells, start-up gate, config loader) | `core/src/emulator/ports/models/portdecoder_sprinter.*`, `…/models/sprinter/` | M |
| `SprinterPldConfiguration` extension point + registry, `SprinterPldStandard` module, a stub module for tests | `core/src/emulator/ports/models/sprinter/` ([tdd-ports-memory.md](tdd-ports-memory.md) §6.1) | S-M |
| BIOS 3.04 disassembly (pages 8 and 0) and symbols | `docs/disasm/rom/sprinter/`, `data/symbols/sprinter/` | S (S0) |
| `SprinterMemory` (bank formula, graphics pages, intercept actions, `#1F` rewrite read) | `core/src/emulator/memory/sprinter/` | M |
| `SprinterAccelerator` | `core/src/emulator/memory/sprinter/` | M |
| `SprinterVideoRam`, `ScreenSprinter`, `SprinterIntSource`, `SprinterVideoMapper` | `core/src/emulator/video/sprinter/` | L |
| `CovoxBlaster` | `core/src/emulator/sound/sprinter/` | S |
| Z84C15 package: `Z84Sio`, `Z84Ctc`, `Z84Pio`, `Z84SystemRegs` | `core/src/emulator/io/z84c15/` | M |
| `IdeAdapterSprinter` | `core/src/emulator/io/hdd/adapters/` | S |
| TTD serializers (ids 15-19) | `core/src/debugger/ttd/sprinter/` | M |
| Qt docks: port table, PLD registers, VRAM / mode table / palettes | `unreal-qt/src/debugger/` | M |
| Automation: `state/sprinter`, port-table endpoints, CLI/Lua/Python/MCP | `core/automation/…` | S-M |
| Config + ROM folder | `data/configs/sprinter/unreal.ini`, `data/rom/sprinter/` | S |

## 4. Plugging into the in-progress designs

| Design | What the Sprinter takes | What it gives back |
|---|---|---|
| **Media manager** ([2026-09-28-storage-manager](../2026-09-28-storage-manager/technical-design.md), PLAN #58) | slots `fdd.a-d`, `ide0.master/slave`, `ide1.master/slave`; a CD = a unit configured `cdrom` (on the Sprinter `ide0.slave` is empty by default; an empty CD unit there is a config option once ATAPI exists, review round 1 Q5; [integration-ide-cd.md](../2026-09-28-storage-manager/integration-ide-cd.md) §2); `HostFolderFat` FAT16 default; format registry with raw PC floppies; TTD rules; model-switch transfer | the `BootProfile` hook in `HostFolderFat`; the raw PC floppy loader; a second consumer of `ide1.*` ids |
| **IDE core** ([2026-09-25-ide-hdd-design.md](../2026-09-21-profi/2026-09-25-ide-hdd-design.md), PLAN #13a) | `AtaChannel`, `AtaDisk`, `AtapiCdrom`, `idelatch.h`, rollout-1 command list | two channels per `Core`; latch pattern (e) |
| **TSConf** ([technical-design.md](../2026-09-27-tsconf/technical-design.md), PLAN #41) | write intercept, interrupt source (landed with TSConf, before the Sprinter); the isolation-test pattern | a second user of both hooks (proves they are generic); `OnReti()` on the interrupt source |
| **ZX-Evo BaseConf** ([implementation-plan.md](../2026-09-15-atm-baseconf-highres-ports/implementation-plan.md), PLAN #55) | the M1 hook (E3, done); E2b keyboard event + `Ps2Set2Encoder` | a second consumer of E2b (the AT stream into an SIO) |
| **TTD v2** ([2026-09-25-ttd-v2-migration](../2026-09-25-ttd-v2-migration/), PLAN #40) | V1 memory regions for the 256 KB video RAM | — |
| **Video debug translation** (PLAN #42) | `IVideoMapper` | `SprinterVideoMapper` (a per-square mapper, unlike the ZX/ATM/Profi ones) |

## 5. Gaps found in unreal-ng that the Sprinter exposes

| Gap | Where | Handled in |
|---|---|---|
| Turbo multiplier is a power of two | `platform.h:970` | PLAN #60 (clock ratio) |
| No device-supplied IM 2 vector (always `#FF`) | `z80.cpp:1124` | interrupt source (TSConf #41) + Z84C15 chain |
| Cache pages allocated but never mapped | `memory.cpp:788-793` | S1 (`SprinterMemory`) |
| `[HDD] Scheme` and `CMOS=` ini keys are not parsed | `config.cpp` (no reader) | media manager `[MEDIA]` keys; `[SPRINTER] CmosFile` |
| `INI`/`OUTI` tests check timing only (`core/tests/emulator/cpu/io_phase_test.cpp:169-200`), not the B value on A15-A8 | Z80 core | test plan §2.5 (the Sprinter IDE depends on it) |
