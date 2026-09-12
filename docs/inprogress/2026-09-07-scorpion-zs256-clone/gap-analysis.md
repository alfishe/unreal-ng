# Gap Analysis — Current Code vs. Required Clone

Subsystem-by-subsystem diff of what `MM_SCORP`/`MM_PROFSCORP` implement today against
the normative behavior in [hardware-reference.md](hardware-reference.md). Line
references are to the current working tree; they will drift as tasks land — the *named
symbols* are the stable identifiers.

Legend: ✅ matches spec · ⚠️ partial/incorrect · ❌ missing.

---

## 1. Machine model & configuration

| Requirement | Current state | Verdict |
|---|---|---|
| Model entries `MM_SCORP` / `MM_PROFSCORP` in enum | `platform.h:299-300` | ✅ |
| Model table: name, default RAM, available RAMs | `config.h:56-57`: "ZS Scorpion"/"SCORPION", 256, `RAM_256\|RAM_1024`; "PROFSCORP" same | ⚠️ works, but nothing enforces the RAM sizes downstream (no `ram_mask`) |
| Canonical frame geometry 69888T / 224T / intstart 1794 / intlen 32 | `config.cpp:560-586` — `MM_SCORP` falls into `default:` (no entry); `config.cpp:275` documents "ScorpionZS256: 69888" but the default `frame` is the Pentagon 71680 | ❌ runs on Pentagon geometry |
| Video mode → discrete logic, contention off | `screen.cpp:445-461` keys on `VideoModeEnum`; Scorpion has **no mode** → keeps constructor default `M_PENTAGON128K` (`screenzx.cpp:13`); `screen.cpp:168-175` model switch has no `MM_SCORP` case | ❌ Ferranti-class timing *and* contention are wrong; `ulacontention.h:24` already documents "Used by Pentagon, **Scorpion**, Profi" — the intent exists, the wiring does not |
| `trdos_present` default true for Scorpion | `config.cpp:297` — INI `[beta128] beta128=1`, default **true**, model-agnostic | ✅ |
| ROM path config `scorp_rom_path` / `prof_rom_path` | `config.cpp:260-261`, INI keys `SCORP`/`PROFROM` | ✅ |
| Shipped ProfROM config actually boots | `data/configs/spectrum3/unreal.ini:526` = `PROFROM=rom\scorp_prof401.ROM:0` — heritage `:<page>` suffix of unknown handling in `CopyStringValue`, and the file is **512 KB**, which `rom.cpp:282` rejects for `MM_PROFSCORP` | ❌ never booted |

## 2. ROM subsystem

| Requirement | Current state | Verdict |
|---|---|---|
| Load 64 KB bundle, map logical pages | `rom.cpp:180-193` assigns `page0→base_sys_rom`, `page1→base_dos_rom`, `page2→base_128_rom`, `page3→base_sos_rom` — but the signature-validated `scorpion.rom` (`07c190ae…`, matches `rom.cpp:48`) is **BASIC128/48K/Service/TR-DOS** order (hardware-reference §5.1 byte-level verification) → **all four ROM roles scrambled** | ✅ **fixed 2026-09-08** (commit `3f49622c`): now `page0→base_128_rom, page1→base_sos_rom, page2→base_sys_rom, page3→base_dos_rom`, guarded by `scorpionrommapping_test.cpp` |
| `MM_SCORP` size validation | none (only `MM_PROFSCORP` checked) | ⚠️ |
| ProfROM sizes 128/256 KB accepted | `rom.cpp:280-287` — checks `loadedBanks ∈ {4,8,16}` (uses `LOGERROR`, not `MLOGERROR`) | ✅ for 64-256 KB; the shipped 512 KB `scorp_prof401.rom` is rejected |
| ProfROM state fields | `EmulatorState::profrom_bank` (`platform.h:877`) and `TEMP::profrom_mask` (`platform.h:665`) survive from the original `COMPUTER`/`TEMP` structs, never written or read | ⚠️ present, dormant — reuse them (HW §12 item 11) |
| ProfROM quadrant state machine (`switch_table`) | **absent** — no `profrom_bank`, no `CF_PROFROM` producer, no `set_scorp_profrom` port | ❌ |
| Read strobe hook (`#0100-#010F` while service ROM active) | absent (was also dormant in original UnrealSpeccy) | ❌ |
| Extended ROM 512 KB-2 MB | `MAX_ROM_PAGES = 64` (1 MB) in `platform.h:244`; no `#7EFD` handling anywhere (`p7EFD` field survives in `EmulatorState`, `platform.h:809`) | ❌ |
| `MAX_ROM_PAGES` consumers that hard-code 64 | `tools/python/emulator_discovery.py:37`, `tools/python/monitor_mmap_file.py:73` (mmap layout — will silently misread after the bump); `loader_z80.h:169` staging array (grows automatically); `labelmanager.cpp:576`, `memory.cpp:1117` (comments) | ⚠️ audit list for Task 2 |
| ROM-set mode for 4 base pages | `use_romset` path exists | ✅ |

## 3. Memory manager (paging)

| Requirement | Current state | Verdict |
|---|---|---|
| `#0000` priority chain (RAM0 → Service → DOS/session → `#7FFD[4]`) | `memory.cpp:782-840` `UpdateZ80Banks()` — generic 128K logic only: `CF_TRDOS ? (bit4? DOS : SYS) : (bit4? 48k : 128k)`; **no `p1FFD` consult at all** | ❌ RAM-at-`#0000` and Service-ROM-by-`#1FFD[1]` unreachable |
| ROM3 at `#0000` while a DOS session is open with `p7FFD[4] = 0` (HW §4.4 rule 3) | generic path maps the **service** ROM: `memory.cpp:796-806` → `SetROMSystem()` when `CF_TRDOS && !(p7FFD & 0x10)` (heritage `set_banks()` semantics) | ❌ breaks the Shadow-monitor "128 TR-DOS" path |
| `#1FFD` bit 2 | no memory function on hardware (HW §4.3, §12 item 9) | ➖ out of scope by decision |
| `#C000` bank = `p7FFD[2:0] + p1FFD[4]<<3 (+ p1FFD[7:6]<<4)` | absent — `Port_7FFD` calls `SetRAMPageToBank3(value & 7)` directly (`portdecoder_scorpion256.cpp:292`) | ❌ banks 8-15 (+16-63) unreachable |
| `ram_mask` clamp by installed RAM | no `ram_mask` equivalent (heritage `TEMP::ram_mask` unused) | ❌ |
| `#4000`=bank5, `#8000`=bank2 fixed | `reset()` sets them once (`portdecoder_scorpion256.cpp:41-43`) and nothing rewrites them for Scorpion | ✅ (by inertia) |
| Screen select bank 5/7 | `Port_7FFD` → `SetActiveScreen` (`:298-299`) | ✅ |
| `#7FFD[5]` lock scopes to `#7FFD` only | `_7FFD_Locked` handled correctly (`:290-296`) — and `Port_1FFD` is a stub, so trivially "not locked" | ✅ (accidentally) |
| `SetROMMode` (`RESET=` boot modes) Scorpion semantics | `memory.cpp:726-779` — `p1FFD &= ~7` on every ROM-mode change would *clear* RAM0/Service bits; no Scorpion branch | ⚠️ needs guard |

## 4. Port decoder

| Requirement | Current state | Verdict |
|---|---|---|
| `#7FFD` full semantics | `portdecoder_scorpion256.cpp` — 3-bit bank, ROM0/1, screen, lock | ⚠️ 128K-subset. **D4 polarity fixed 2026-09-08** (commit `3f49622c`): the arm read `SetROMMode(bit4 ? RM_128 : RM_SOS)`, which selected the wrong ROM *and* wrote the complement of D4 into `state.p7FFD` (`SetROMMode` rewrites that bit), corrupting the latch snapshots/TTD/debugger read back. Task 3 rewrites this body — **preserve the corrected polarity**: D4 clear → `RM_128`, D4 set → `RM_SOS`. The RAM-bank bits are still never latched into `p7FFD` (Task 3) |
| `#1FFD` write handler | **empty stub** (`:317-321` — `(void)value; (void)pc;`) | ❌ |
| `#1FFD` read returns `#FF` | `DecodePortIn` has no arm — falls into generic `PeripheralPortIn` (warn + `0xFF`) | ⚠️ value correct, path noisy (logs a "no peripheral" warning on every read) |
| `#7FFD` reads fall through to unattached | no arm → same fall-through | ⚠️ same |
| `#FF` border write | absent | ❌ |
| AY mirror-tolerant decode | `DecodePortIn/Out` `#xC002` arms (`:77-87`, `:156-171`) | ✅ |
| `#FE` selective decode | `IsPort_FE` mask `0x0023`/match `0x0022` (`:205-221`) | ✅ (documented GAL equation) |
| `SetRAMPage`/`SetROMPage` debug-sync overrides | empty stubs (`:191-199`) | ❌ (debugger port-write injection does nothing) |
| `reset()` defaults | sets 128K-style defaults incl. `Port_7FFD(0)`; border white | ⚠️ border should be black (§HW-12.8); `p1FFD` not reset |
| FDC ports visible in DOS session | WD1793 answers unconditionally; `CF_DOSPORTS` is already raised on session (`memory.cpp:827`) and the Pentagon decoder already gates at decode time (`portdecoder_pentagon128.cpp:99-102`, cites original-US `io.cpp`) — the Scorpion decoder needs the same arm plus the §12.3 monitor-paged exception (`p1FFD[1]`) | ⚠️ pattern exists, Scorpion arm missing (Task 5) |

## 5. TR-DOS session machinery

| Requirement | Current state | Verdict |
|---|---|---|
| Trap arms from ROM1/ROM2 only, never RAM-`#0000`/ROM0 | `memory.cpp:829-837` generic arm: `(p7FFD & 0x10) && trdos_present` + RAM-bank-0 guard. ROM0 (`p7FFD[4]=0`) → not armed ✅; RAM-`#0000` → guarded ✅; **Service Monitor (`p1FFD[1]`, `p7FFD[4]=0`) → NOT armed** ❌ — breaks the boot menu's "128 TR-DOS" entry | ⚠️ |
| Unpage on execution from RAM (`CF_LEAVEDOSRAM`) | `memory.cpp:819-821` default branch gives Scorpion `CF_LEAVEDOSRAM`; `z80.cpp:211-220` implements it | ✅ |
| Session-gated FDC ports (Scorpion decoder) | see §4 — missing arm | ❌ |

## 6. MNI / NMI

| Requirement | Current state | Verdict |
|---|---|---|
| `Z80::RequestNonMaskedInterrupt()` | **empty function** (`z80.cpp:654`) | ❌ |
| NMI acceptance cycle (push PC → `#0066`, IFF1→IFF2, `RETN` restore via `nmi_in_progress`) | state field exists (`platform.h:812`), processing commented out (`z80.cpp:668-692` — including the original Scorpion `pc > 0x4000` logic) | ❌ |
| MNI side effect: set `p1FFD[1]` only, preserve rest | absent | ❌ |
| UI / WebAPI / CLI trigger surface | absent (no NMI action anywhere). Actions live in `unreal-qt/src/menumanager.cpp` (not `mainwindow.cpp`). F11: Full Screen moved to Ctrl+F in ced71710, stale Help text "F11 - Full Screen" at `menumanager.cpp:834`, debugger Step In uses F11 (`debuggerwindow.cpp:85`, window-scoped) | ❌ |
| NMI accept hook site | `ProcessInterrupts()` runs before every `Z80Step()` (`z80.cpp:409`); `Z80::retn()` is an empty stub already called by the `ED45` handler (`op_ed.cpp:161`) | ✅ sites exist |
| TTD capture of `nmi_in_progress` | already in `TTDCpuState` (`ttd_checkpoint.cpp:65,109`, `machine_state_hash.cpp:95`) | ✅ |

## 7. Snapshots

| Requirement | Current state | Verdict |
|---|---|---|
| `.z80` hw=10 → `Z80_256K` mode | mapping exists (`loader_z80.cpp:1017-1019, 1052-1054`) | ✅ detection |
| Load 16 (…64) RAM pages | `case Z80_256K: break;` — **not implemented** (`:474-475`); page mapping `case Z80_256K: BANK_INVALID` with warning (`:1390-1393`) | ❌ |
| Restore `p7FFD`/`p1FFD` from header (v2 offsets 35/36) | `applyPeripheralState` — not Scorpion-aware | ❌ |
| v3 page numbering | 128K path uses the `.z80` convention page − 3 (`loader_z80.cpp:1374-1388`) | ✅ reuse: Scorpion pages 3-18 (…66) → RAM 0-15 (…63) — never 0-based |
| ProfROM quadrant in snapshots | no `.z80` slot for `profrom_bank` | ➖ documented limitation (HW §12 item 11) |
| Model policy for hw=10 | precedent: hw=9 Pentagon maps to `Z80_128K` with **no model switch** (`loader_z80.cpp:1012,1047`). There is **no in-place model-switch path** to reuse: `EmulatorAPI::switchModel` (`POST /api/v1/emulator/{id}/model`, `lifecycle_api.cpp:689`) stops, removes and *recreates* the instance via `CreateEmulatorWithModelAndRAM`, which a loader owned by that instance cannot call on itself; in-emulator model change is unimplemented (`_preferredModel` is applied only inside `Emulator::Init()`, `emulator.cpp:148`) → decided: load in place; on a non-Scorpion host clamp pages to the running model's RAM and `MLOGWARNING` the mismatch. An in-place switch would be a separate feature | 🔧 decided |
| Save Scorpion `.z80` | resolved: `Emulator::SaveSnapshot` already dispatches `.z80` → `LoaderZ80::save()` — a v3 writer with `header.p7FFD` staged (`loader_z80.cpp:146-191`). `getModelCodeV3()` is a 48K/128K-only stub (`loader_z80.cpp:271-283`) → needs code 10, `p1FFD` byte 36, and >8-page staging | ⚠️ narrow, concrete deltas (Task 9) |
| SNA | 128K-only format (comment `loader_sna.cpp:732`) — correctly out of scope | ➖ |
| TTD checkpoints | `p1FFD` and `p7EFD` already captured (`ttd_checkpoint.cpp:151,161`, `machine_state_hash.cpp:105,110`); restore is a field copy + `UpdateZ80Banks()` (`timetravelmanager.cpp:1052-1058`) | ✅ |
| TTD capture of `profrom_bank` | absent from `TTDChipsetState`, `MachineStateHash`, `ttd.ksy`; `sizeof(TTDChipsetState)` is written to `.ttd` and checked on load (`timetravelmanager.cpp:2068,2401`) | ❌ add (layout change; v1 has no compat promise) |

## 8. Video

| Requirement | Current state | Verdict |
|---|---|---|
| `M_SCORPION` video mode (312×224T, discrete fetch, no contention, 1T border) | no enum value; model→mode switch lacks case | ❌ |
| Border power-on black | reset writes white (`portdecoder_scorpion256.cpp:36,46`) | ⚠️ |
| `#FF` border port | absent (§4) | ❌ |

## 9. Debugger / tooling surfaces

| Requirement | Current state | Verdict |
|---|---|---|
| Bank names for 16/64 pages in memory viewer `DumpMemoryBankInfo` | generic naming; `SetROMPageFlags` "Everything else" arm already anticipates "extended Scorpion rom bank" (`memory.cpp:1418`) | ⚠️ |
| Alt-M extended-memory-port debug write (heritage UX) | absent | ❌ optional |
| WebAPI model switch | accepts "SCORPION"/"PROFSCORP" via model table | ✅ |
| Port trace attribution | `PortDeviceId::Memory_1FFD` exists (`portdiagrecorder.cpp:856`) | ✅ |

## 10. ProfROM (`MM_PROFSCORP`) specifics

| Requirement | Current state | Verdict |
|---|---|---|
| Shares Scorpion paging core | decoder/dispatch: `MM_PROFSCORP` has **no** `GetPortDecoderForModel` case (`portdecoder.cpp:48-95` — falls to `throw std::logic_error`) | ❌ model selectable in table but unusable |
| Quadrant switching | absent (§2) | ❌ |
| `profrom_mask` by ROM size | absent | ❌ |
| `CF_PROFROM` flag | defined (`platform.h:883`), never set/cleared | ❌ |

## 11. Hardware turbo (7 MHz, Turbo+)

| Requirement | Current state | Verdict |
|---|---|---|
| Turbo flip-flop strobed by `IN` from the `#7FFD`/`#1FFD` register families (`(port & 0xC023)` decode, mirrors included) | `PortDecoder_Scorpion256::DecodePortIn` clocks `EmulatorState::scorpion_turbo` before the decode chain, both `MM_SCORP` and `MM_PROFSCORP` | ✅ 2026-09-09 |
| Reset clears the flip-flop | decoder `reset()` | ✅ 2026-09-09 |
| 2× T-states per 50 Hz frame, INT window scaled, video/AY/FDC unaffected | composes with the host speed multiplier in `Z80::ApplyQueuedFrequencyMultiplier` (frame-boundary apply, incl. the Emulator stepping paths) | ✅ 2026-09-09 |
| Front-panel turbo button (GUI toggle) | not implemented — follow-up, low priority | ❌ optional |

---

## Summary — must-build list

1. **Memory manager**: Scorpion branch in `UpdateZ80Banks()` + `ram_mask` + `#1FFD` consult (bits 0/1/4/6/7 only); ROM3-under-session rule; ProfROM bases resolved from `profrom_bank`; `SetROMMode` guard.
2. **ROM loader**: fix the `base_*_rom` page assignment (verified order BASIC128/48K/Service/TR-DOS) — pre-existing bug affecting the signed `scorpion.rom`.
3. **Port decoder**: real `Port_1FFD`, extended `Port_7FFD`, `#1FFD` read arm, `#FF` border write, reset defaults, debug `SetRAMPage/SetROMPage`.
4. **NMI/MNI**: Z80 NMI implementation + Scorpion magic-button trigger pair (DD50.1 DOS trigger + DD50.2 NMI — HW §9) + three trigger surfaces (GUI, WebAPI, CLI/automation).
5. **TR-DOS**: Scorpion trap-arm rule, ROM3 regardless of `p7FFD[4]` while the session is open, decoder-level FDC gating (Pentagon pattern + monitor-paged exception). No `#1FFD` bit-2 force.
6. **ProfROM**: quadrant state machine over `EmulatorState::profrom_bank` + read strobe hook + TTD checkpoint/hash/ksy field + size ladder (incl. the shipped 512 KB image) + `MM_PROFSCORP` decoder wiring.
7. **Extended ROM**: `MAX_ROM_PAGES` 64→128, `#7EFD` window select, validation matrix.
8. **Video**: `M_SCORPION` mode + config canonical geometry + border power-on.
9. **Snapshots**: `.z80` 256K load/save round-trip.
10. **Tests + docs** throughout.
