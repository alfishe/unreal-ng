# Scorpion ZS-256 Clone — Complete Implementation

Directory status: **implementation in progress** — Tasks 0-7 and 12 done (Tasks 0-6 +
defect fixes committed in `3f49622c`, the PROFSCORP instantiation fix in `535b8238`,
Task 7 incl. the ProfROM plane fix in `9e85136e`, Task 12 hardware turbo in the working
tree), Tasks 8-11 outstanding. See *Execution status* below and the
[verification/](verification/) E2E records.
Review pass 2026-09-08: every file/line claim was re-verified against the working tree,
factual errors corrected, and the two open design questions resolved by primary-source
research — see *Decisions log* below.

## Goal

Turn the existing skeletal `MM_SCORP` machine definition into a full, hardware-accurate
Scorpion ZS-256 clone: 256 KB (and heritage 1024 KB) RAM paging, the complete `#1FFD`
register, Shadow Service Monitor with MNI ("Magic" NMI button), built-in Beta-128 TR-DOS
with the Scorpion-specific `#3Dxx` trap semantics, Sinclair-matching video timing with
contention-free discrete logic, the Turbo+ hardware 7 MHz turbo flip-flop, Scorpion
snapshot (`.z80` hw=10) round-tripping — and the
**full ROM subsystem range**: minimal 64 KB ROM (4 pages), ProfROM extended ROM
(128/256 KB quadrant switching), and multi-megabyte ROM-disk images (up to 2 MB) via the
GMX-compatible direct window select.

The **ProfROM variant (`MM_PROFSCORP`) is in scope as a first-class deliverable** — it
shares the Scorpion paging core and adds the ProfROM quadrant state machine.

## Why this work exists

The current `PortDecoder_Scorpion256` behaves like a Spectrum 128K with a Scorpion name
plate: `Port_1FFD` is an empty stub, `Memory::UpdateZ80Banks()` has no Scorpion branch at
all, `Z80::RequestNonMaskedInterrupt()` is an empty function with the original Scorpion
NMI logic commented out, the machine runs on borrowed Pentagon video timing, and `.z80`
snapshots with hardware ID 10 load nothing (`Z80_256K` mode is declared but not
implemented). Every unique Scorpion feature — the reason the machine was historically
significant — is missing.

## Reference

- **Primary reference repo:** [dimitriuz/ZX-Spectrum_MISTer, branch `scorpion-zs256`](https://github.com/dimitriuz/ZX-Spectrum_MISTer/tree/scorpion-zs256)
  - [`docs/scorpion-zs256-design.md`](https://raw.githubusercontent.com/dimitriuz/ZX-Spectrum_MISTer/scorpion-zs256/docs/scorpion-zs256-design.md) — verified hardware reference and RTL design
  - [`docs/superpowers/plans/2026-08-31-scorpion-zs256.md`](https://raw.githubusercontent.com/dimitriuz/ZX-Spectrum_MISTer/scorpion-zs256/docs/superpowers/plans/2026-08-31-scorpion-zs256.md) — task-by-task implementation plan (FPGA)
- **Emulation reference:** Fuse `machines/scorpion.c` (paging, trap gating, port masks)
- **Heritage reference:** original UnrealSpeccy by S. Zonov — this codebase is its direct
  descendant; several Scorpion-specific fields survive in `platform.h` (`p1FFD`,
  `evenM1_C0`, `border_add/border_and` for the 4T border update) but are unused.

## Document map

| File | Purpose |
|------|---------|
| [hardware-reference.md](hardware-reference.md) | Verified hardware behavior of the real Scorpion ZS-256, comparatively tabled against ZX Spectrum 128K and Pentagon; consolidated history section |
| [gap-analysis.md](gap-analysis.md) | Line-level diff of what the codebase implements today vs. what the clone requires, subsystem by subsystem |
| [design.md](design.md) | Target architecture: module changes, paging/ROM/TR-DOS state machines, port decode, MNI flow, snapshot formats, with diagrams |
| [implementation-plan.md](implementation-plan.md) | The executable plan: ordered tasks, files, steps with checkboxes, acceptance criteria, commit points |
| [testing-plan.md](testing-plan.md) | Unit, integration, regression and boot-verification strategy; quality gates |
| [profrom-nmi-boot-analysis.md](profrom-nmi-boot-analysis.md) | Verified findings: ProfROM plane lifetime, magic-button NMI (DD50 trigger pair, entry chain, park loops, reset stubs), the boot sequence incl. the idle-EAR wedge defect and the turbo firmware chain |
| [profrom-disassembly-and-findings.md](profrom-disassembly-and-findings.md) | Deep static disassembly & reverse-engineering: NMI entry (#0066 -> #0807), RST 30h cross-plane dispatching, 50Hz interrupt / keyboard scanning, and root cause diagnosis of emulator hangs and keyboard dead bugs |
| [profrom-nmi-gaps-and-findings.md](profrom-nmi-gaps-and-findings.md) | Formal gaps analysis (GAP-1 through GAP-5): hardware specification vs. working tree discrepancies, trace evidence, and exact code fix specifications |
| [profrom-smuc-not-found-and-driver-disassembly.md](profrom-smuc-not-found-and-driver-disassembly.md) | SMUC "not found" boot messages: failure mechanism, full page-7 serial-link driver disassembly (#FFBA 3-wire protocol, NVRAM layer, IDE identify), forensics (no lost fix), and the agreed lightweight-stub direction |
| [profrom-service-monitor-menu-flashing.md](profrom-service-monitor-menu-flashing.md) | Service Monitor active item blinking: root cause analysis (port #FF1F Kempston Joystick active-high fire bit returning 0xFF, phantom event 0x80 autorepeat every 5 frames, 4-frame menu redraw loop) and exact fix specification |
| [service-monitor-debugging-guide.md](service-monitor-debugging-guide.md) | Comprehensive efficient debugging guide: memory/subroutine/port breakpoints, Time-Travel Debugging (TTD), deterministic frame stepping, and blinking diagnosis matrix |
| [verification/](verification/) | Live E2E execution records (transferred from `scratch/`): [e2e-base-rom.md](verification/e2e-base-rom.md) (E2E-1/2/3, Tasks 0-6), [e2e-profrom-instantiation.md](verification/e2e-profrom-instantiation.md) (PROFSCORP smoke + E2E-7), [e2e-profrom-quadrants.md](verification/e2e-profrom-quadrants.md) (E2E-4, Task 7), [turbo-cpufreq-chain-verification.md](verification/turbo-cpufreq-chain-verification.md) (turbo ⇄ status-bar chain audit + notification fix). Raw transcripts/artifacts stay in `scratch/e2e*` |

## Scope decisions (agreed defaults)

1. **Base ZS-256 machine + ProfROM variant (`MM_PROFSCORP`)** — both first-class.
   GMX graphics expander and the Turbo+/ISA expansion-board peripherals (SMUC, on-board
   RTC, ISA slots) stay out of scope, but the GMX-style ROM window select (port `#7EFD`
   bits 4-5) is implemented as the extended ROM-disk mechanism — and the Turbo+ **7 MHz
   hardware turbo flip-flop** is implemented for **all** Scorpion configurations
   (hardware-reference §13; added 2026-09-09 from the decoded GAL materials).
2. **RAM: 256 KB and 1024 KB** — matching the model table heritage
   (`RAM_256 | RAM_1024`): `#1FFD` bit 4 supplies bank bit 3, bits 6-7 supply bank
   bits 4-5 (`ram_mask` clamps to installed size).
3. **ROM: the full range must work** — see the table below; `MAX_ROM_PAGES` grows
   64 → 128 pages (1 MB → 2 MB) to host ROM-disk images.
4. **58-key keyboard matrix out of scope** — the standard 40-key Sinclair matrix mapping
   is used; key *positions* differ on real hardware but no software depends on that.
5. **`#FE` selective decode kept** — the existing GAL-equation decode in
   `PortDecoder_Scorpion256::IsPort_FE` already models the Scorpion's partial address
   decode; it stays as documented hardware behavior.
6. **`#1FFD` bit 2 is ignored** — it is an RS-232 line on hardware, not a memory bit
   (hardware-reference §4.3, §12 item 9).
7. **ROM3 while a DOS session is open, regardless of `#7FFD` bit 4** — MISTer/Fuse rule;
   the heritage service-ROM behavior of the generic paging path is not inherited
   (hardware-reference §4.4 rule 3, §12 item 10).
8. **ProfROM quadrant is machine state** — kept in `EmulatorState::profrom_bank`,
   checkpointed by TTD and hashed for divergence detection; paging is derived from it
   (design §4.2, hardware-reference §12 item 11).
9. **Beta128 port gating is decoder-level** (Pentagon pattern); `WD1793` is untouched.
10. **Snapshots load in place** (no model switch), mirroring hw=9 Pentagon handling.

### ROM size matrix (all supported)

| Variant | ROM size | Quadrants | Selection mechanism | Model |
|---------|----------|-----------|---------------------|-------|
| Minimal | 64 KB | 1 | none (fixed) | `MM_SCORP` |
| ProfROM | 128 KB | 2 | read `#0100-#010F` state machine | `MM_PROFSCORP` |
| ProfROM | 256 KB | 4 | read `#0100-#010F` state machine | `MM_PROFSCORP` |
| Extended | 512 KB - 2 MB | 8-32 | `#7EFD` bits 4-5 direct select (+ state machine low bits) | `MM_PROFSCORP` |

Each 64 KB quadrant internally contains the standard 4-page set
(BASIC 128 / 48K BASIC / Service Monitor / TR-DOS); ROM-disk payloads live in the
non-boot quadrants and are paged by the ROM's own software through the same window.

Shipped images (`data/rom/`): `scorpion.rom` 64 KB, `scorp295.rom` 64 KB,
`scorp_prof401.rom` **512 KB** (the real extended image; loads and validates as
8 quadrants since Task 2's validation matrix — see
[verification/e2e-profrom-instantiation.md](verification/e2e-profrom-instantiation.md)).

## Decisions log (2026-09-08)

| Question | Decision | Evidence |
|---|---|---|
| What does `#1FFD` bit 2 do, and what happens when it is cleared? | Not a memory bit; ignored. | Scorpion programmer's guide (MSD #03, zxpress 6048): D2 = RS-232C output, D3 unused, D5 Centronics strobe. Fuse `scorpion.c` reads only bits 0/1/4. MISTer design doc never mentions it. Original UnrealSpeccy `set_banks()`: `if (comp.p1FFD & 4) comp.flags \|= CF_TRDOS;` — set-only, clearing did nothing; the session closed via the normal RAM-execution path. Emulator-only extension, rejected. |
| Which ROM sits at `#0000` while a session is open with `#7FFD[4] = 0`? | ROM3 (TR-DOS). | MISTer: "`trdos_en` forces `page_rom = 3` while a disk is active". Fuse: `beta_memory_map` → `memory_map_romcs_full` overrides the selected ROM. Heritage UnrealSpeccy / generic `UpdateZ80Banks()` map the service ROM here, which would break the Shadow-monitor "128 TR-DOS" boot path. |
| Is the ProfROM quadrant recomputable from latches for TTD? | No — it is stored and checkpointed. | Original `set_scorp_profrom()`: `profrom_bank = switch_table[read_addr*4 + profrom_bank] & mask` — depends on the previous value, i.e. read history. Field survives as `EmulatorState::profrom_bank` (`platform.h:877`). TTD restore is a field copy + `UpdateZ80Banks()` (`timetravelmanager.cpp:1052-1058`), so deriving paging from the byte makes restore automatic. Cost: `sizeof(TTDChipsetState)` is checked on load — old sessions rejected (v1 has no compat promise). |
| Does a `.z80` hw=9 load switch the machine model? | No; Scorpion loads in place too. | `loader_z80.cpp:1005-1019`: hw=9 → `Z80_128K`, no switch in the loader. The only switch, `EmulatorAPI::switchModel` (`lifecycle_api.cpp:689`), stops/removes/recreates the instance — unusable from inside a loader that instance owns. |
| Is the `.z80` save path SNA-only? | No — writer exists. | `Emulator::SaveSnapshot` (`emulator.cpp:1173`) → `LoaderZ80::save()`; only `getModelCodeV3()` lacks a Scorpion code. |
| Where is FDC port gating implemented? | In the port decoder, per model. | `CF_DOSPORTS` is produced but consumed nowhere; `PortDecoder_Pentagon128::DecodePortIn` (`portdecoder_pentagon128.cpp:99`) gates Beta128 ports on `CF_TRDOS`. |

Also found in this pass (parallel review session, verified against the ROM bytes):
`rom.cpp` maps the Scorpion ROM roles Service-first while every shipped bundle is
BASIC128 / 48K / Service / TR-DOS — a pre-existing loader bug, fixed in Task 2
(hardware-reference §5.1).

### Code defects fixed (2026-09-08; committed in `3f49622c`)

Three bugs that exist independently of the Scorpion feature work were fixed directly.
They are **not** the 11-task plan.

| Fix | File | Effect |
|---|---|---|
| ROM role mapping scrambled all four Scorpion ROM pages | `core/src/emulator/memory/rom.cpp` (`MM_SCORP`, `MM_PROFSCORP`) | `#0000` resolved to the wrong ROM on every boot. Now `page0→base_128_rom`, `page1→base_sos_rom`, `page2→base_sys_rom`, `page3→base_dos_rom` |
| `#7FFD` D4 ROM-select polarity inverted | `core/src/emulator/ports/models/portdecoder_scorpion256.cpp` | Selected the wrong ROM *and* stored the complement of D4 into `state.p7FFD`, corrupting the latch snapshots, TTD and the debugger read back. Now D4 clear → BASIC 128, D4 set → 48K BASIC |
| Stale keyboard-shortcut help text | `unreal-qt/src/menumanager.cpp` | Help dialog advertised F11 for Full Screen; the shortcut moved to Ctrl+F in commit ced71710 |

New regression test `core/tests/emulator/memory/scorpionrommapping_test.cpp` (3 cases)
pins both the role pointers and the page contents they are derived from. It was verified
to **fail** against the old mapping before being kept. Full suite: 2188 passed, 0 failed,
zero new build warnings.

Deliberately **not** touched: `Screen::_drawCallbacks` (`screen.h:435`) is under-filled
(17 initializers for 19 slots, so `_borderCallback` is `nullptr`), but it sits in an
`/// region <Obsolete>` block and `ScreenZX::Draw()` overrides the only consumer, so it
is dead code — realigning it would change nothing and risks reviving a null-deref path.
Everything else in the Scorpion decoder and memory manager is feature work owned by
Tasks 3-7.

### Review round 2 (2026-09-08) — defects found by code verification

Each of these would have produced wrong code if the plan had been executed as written.

| # | Defect in the plan | Correction | Evidence |
|---|---|---|---|
| 1 | `ram_mask = (config.ramsize >> 14) - 1` assumed bytes | `(config.ramsize >> 4) - 1` → 15 / 63. As written, `256 >> 14 = 0` and the mask underflows to `0xFF`, unmasking every bank bit and silently defeating the 256 KB clamp | `platform.h:315` `RAM_256 = 256`; `config.cpp:402` logs "RAM Size: %dKb" |
| 2 | Task 1 put `VideoModeEnum` in `platform.h` | It is in `core/src/emulator/video/screen.h:36` | grep |
| 3 | Task 1 listed 2 of 3 `[M_MAX]` tables and did not warn about insertion position | Three positional tables: `rasterDescriptors` (`screen.h:370`), `videoModeName` (`screen.cpp:1300`, `static_assert`), `_drawCallbacks` (`screen.h:435`). `M_SCORPION` must be appended **after `M_BRD`**; inserting it beside `M_PENTAGON128K` shifts every later mode in all three | screen.h / screen.cpp |
| 4 | "copy the Pentagon gating arm" | `IsBeta128Port` is **private** to `PortDecoder_Pentagon128` (`portdecoder_pentagon128.h:57`) — hoist it to the base `PortDecoder` first. `wasBeta128Gated` is already shared | `portdiagrecorder.h:112`, `portdecoder.cpp:390` |
| 5 | `#FF` border arm matched `port == 0x00FF` exactly | Match the **low byte**: `OUT (#FF),A` puts `A` on A15-A8, so the port arrives as `#nnFF`. The planned test S7 uses `LD BC,#00FF` and would have passed while real software failed — new cases P5b / S7b close that hole | Z80 `OUT (n),A` semantics |
| 6 | design §5 listed the decode order as `#FE` → AY mirrors, i.e. a reorder of working code | Live order is AY `#FFFD` → AY `#BFFD` → `#FE` → fallback; insert new arms without moving existing ones (the three are mutually exclusive anyway: `#FE` needs `A1 = 1`, both AY masks need `A1 = 0`) | `portdecoder_scorpion256.cpp:77-95` |
| 7 | "no model-switch path exists" (round-1 claim) was imprecise | `EmulatorAPI::switchModel` exists (`POST /api/v1/emulator/{id}/model`) but stops, removes and recreates the instance, so a loader owned by that instance still cannot use it. The load-in-place decision stands, with the corrected rationale | `lifecycle_api.cpp:689` |

Two heritage behaviors were also recovered and recorded as documented divergences
(hardware-reference §12 items 6 and 6b): the Scorpion `#FF` read composes Beta-128
system-port bits 7:5 over the floating bus, and the selective `#FE` decode is disabled
during a DOS session.

Corrections applied in the first pass: ROM asset sizes (`scorp295.rom` 64 KB /
`scorp_prof401.rom` 512 KB were swapped), `.z80` page numbering (page − 3, not
0-based), GUI file paths (`menumanager.cpp`, not `src/emulator/mainwindow.cpp`), F11
status (free at main-window level since ced71710, stale Help text, debugger Step In
conflict), `nmi_in_progress` already in TTD, `retn()` stub already present, explicit
`MAX_ROM_PAGES` consumer list, the heritage `PROFROM=<file>:0` INI suffix, and the
clean-baseline requirement for Task 0.

## Execution status

| Task | Status |
|------|--------|
| Hardware reference captured | done |
| Gap analysis vs codebase | done |
| Architecture design | done |
| Implementation plan | done |
| Testing plan | done |
| Review + research pass (facts re-verified, decisions logged) | done 2026-09-08 |
| Standalone code defects fixed | done 2026-09-08, committed in `3f49622c` |
| Tasks 0-6 (baseline harness, config/timing, ROM space, paging, port decoder, TR-DOS, NMI/MNI) | done 2026-09-08, commit `3f49622c`; live E2E-1/2/3 — [verification/e2e-base-rom.md](verification/e2e-base-rom.md) |
| PROFSCORP instantiation fix | done 2026-09-09, commit `535b8238`; smoke + E2E-7 (1024 KB / 256 KB RAM) — [verification/e2e-profrom-instantiation.md](verification/e2e-profrom-instantiation.md) |
| Task 7 (ProfROM quadrant state machine + `#7EFD` window) | done 2026-09-09, commit `9e85136e` (incl. the plane-selection fix); live E2E-4 — [verification/e2e-profrom-quadrants.md](verification/e2e-profrom-quadrants.md) |
| Task 12 (hardware turbo — 7 MHz flip-flop, all Scorpion configs) | done 2026-09-09, **uncommitted**; 8 tests + full suite green, live E2E-8 on PROFSCORP/1024K and SCORPION/256K (`scratch/e2e-profrom-boot/pass29.py`) — hardware-reference §13 |
| MNI rework + idle-EAR fix | done 2026-09-10, **uncommitted**; `RequestMNI` rebuilt to the DD50 trigger pair (page 3 of the current plane, latch untouched, read-release strobe — hardware-reference §9, [profrom-nmi-boot-analysis.md](profrom-nmi-boot-analysis.md)), `scorpionmni_test.cpp` rewritten, trigger carried in TTD; `Tape::handlePortIn` idle EAR made steady-high (UnrealSpeccy `tape_bit() = -1` semantics) — the defect that wedged every ProfROM boot (analysis doc §7). Build/suite/live E2E pending at time of writing |
| Tasks 8-11 (ROM-disk ladder, `.z80` hw=10 snapshots, tooling polish, final QA) | not started |
| Turbo CPU-frequency notification chain fix | done 2026-09-11, **uncommitted**; "status bar always 7.0 MHz" report traced port→core→MessageCenter→UI for both `SCORPION` (Base v2.92) and `PROFSCORP` (ProfROM v4.01): in-monitor 7 MHz is hardware-matching (menu toggle `V`/Enter is a RAM flag store only, strobe at exit — [profrom-service-monitor-turbo.md](profrom-service-monitor-turbo.md) §10-§11), real defect was `ApplyHardwareTurboNow` never posting `NC_CPU_FREQ_CHANGED` — both apply paths now share `Z80::NotifyCPUFrequencyChanged()`, regression test `TurboStrobePostsCpuFreqChanged`; live WebAPI round trip 2→1→2 — [verification/turbo-cpufreq-chain-verification.md](verification/turbo-cpufreq-chain-verification.md). Follow-up same day: re-proven with the ROM's own `#04CE`/`#048C` apply (strobe → 3.5 MHz same frame, sticks — verification §4.1); `V` hotkey live-confirmed as the silent staging toggle (turbo doc §6.3); keyboard-injection-requires-running-loop pitfall documented (verification §4.2); SMUC board-presence side-track prototyped and reverted, board stays absent by decision (verification §6) |

Known incidental defect (outside this plan's scope, filed during Task 7 E2E):
WebAPI `DELETE /emulator/{id}` on the instance adopted by the Qt main window can
SIGSEGV in `MenuManager::updateMenuStates` — details in
[verification/e2e-profrom-quadrants.md](verification/e2e-profrom-quadrants.md).
