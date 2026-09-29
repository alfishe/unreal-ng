# Sprinter Sp2000 — technical design (index and shared infrastructure)

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Review round 1 done (2026-09-28); decisions applied. Nothing implemented |
| **Inputs** | [hardware-reference.md](hardware-reference.md) (**HW**), [goals-and-requirements.md](goals-and-requirements.md) (FR-*, ACC-*), [high-level-design.md](high-level-design.md) (D1-D11) |
| **Mapping** | [unreal-ng-mapping.md](unreal-ng-mapping.md): what is reused, extended, new |

## 1. Documents

| File | Covers |
|---|---|
| [tdd-ports-memory.md](tdd-ports-memory.md) | `PortDecoder_Sprinter`: PLD state, port-table lookup, code dispatch, start-up gate; `SprinterMemory`: windows, vROM, fast RAM, graphics pages, write intercept, reset page, ISA; PLD configuration loader and the `SprinterPldConfiguration` module registry; clock and wait states; Z84C15 fixed ports |
| [tdd-video.md](tdd-video.md) | video RAM, `ScreenSprinter` renderer, mode table, palettes, frame geometry, INT source, HOLD, debug views |
| [tdd-storage.md](tdd-storage.md) | floppy (density, PC images, `#0F`), IDE adapter (latch, two channels), CMOS, media-manager slots, the DSS boot profile for folder volumes |
| [tdd-accel-sound-input.md](tdd-accel-sound-input.md) | accelerator, AY / beeper / Covox / Covox-Blaster, keyboard (matrix + AT codes), mouse, joystick, the Z84C15 SIO/CTC/PIO package |
| [tdd-integration.md](tdd-integration.md) | model registration and config, ROM set, TTD and snapshots, automation surfaces, Qt GUI and debugger |

## 2. Shared infrastructure the Sprinter needs

Three of these hooks are already specified by the TSConf design and one exists on master. The
Sprinter adds no second mechanism. Review round 1 accepted every hook in this table as designed
and fixed the order: the Sprinter is the last machine program, so all of them land before it.
The write intercept and the interrupt source come with TSConf (PLAN #41); the pieces this design
introduced (clock ratio, wait-state hook, CMOS core, per-model `Screen` selection, raw PC floppy
loader, WD1793 rate check, port-trace internal codes) form PLAN row #60, done before TSConf.

| Hook | Status | Sprinter use | Spec |
|---|---|---|---|
| **M1 hook** `IMachineM1Hook` / `Z80::machineM1Hook` | **exists** (built for ATM3 E3; `core/src/emulator/cpu/z80.h:290-295`, `:445`) | TR-DOS entry at `#3D00-#3DFF` and exit at `≥ #4000` (the vROM DOS signal); accelerator opcode snooping; the `IN/OUT` + `#1F` quirk | ATM3 [e3-board-nmi.md](../2026-09-15-atm-baseconf-highres-ports/e3-board-nmi.md) |
| **Write intercept** = a write-only `HostBusOverlay` (`observesReads = false`, window `#0000-#FFFF`, the callback picks the bank; `Core::AddBusOverlay`, chained with other overlays) | **built** (PLAN #60(a), 2026-09-29; replaces the per-bank flag of the first design) | video shadow writes, graphics pages, the reset page, ISA pages, accelerator write side; where the plain store must not land, the bank's `_bank_write` points to the trash page | TSConf [technical-design.md](../2026-09-27-tsconf/technical-design.md) §3.5 item 2 |
| **Interrupt source** `IInterruptSource` + `OnReti()`; engine step hook `IMachineStepHook` | **built** (PLAN #60(a) + TSConf INF-5, 2026-09-29; `Z80::interruptSource`, `Z80::machineStepHook`) | INT position from the mode table; keyboard and Covox-Blaster interrupts (all vector `#FF`); RETI for the Z84C15 daisy chain and the accelerator re-arm | TSConf technical design §3.4, §3.8 |
| **Memory subclass** selected in the `Core` factory | pattern exists (`ScorpionMemory`, `core/src/emulator/memory/memory.h:313`) | `SprinterMemory`: bank computation, graphics-page reads | TSConf `TsConfMemory` §3.5 |
| **Clock ratio** (new) | decided (review round 1); PLAN #60(b), **postponed to the Sprinter program** (2026-09-28): no other machine needs it | 21 MHz = 6 × 3.5 MHz | §3 below |
| **Wait-state hook** | **built** (PLAN #60(d), 2026-09-29): `MemoryWaitOverlay` (`core/src/emulator/memory/memorywaitoverlay.h`) - per-slot flags + `ExtraClocks(kind, addr, startClock)`, a host bus overlay so machines without waits pay nothing; port waits: the decoder calls `Z80::AddWaitStates` | turbo memory and port waits | §4 below |
| **CMOS core** `Ds12887` (new) | decided (review round 1); PLAN #60, with the migrations of the existing clocks | Sprinter CMOS | [tdd-storage.md](tdd-storage.md) §4 |

## 3. Clock ratio (new, shared)

Today the guest CPU clock is `base × (speed_multiplier << hw_turbo_shift)`, where
`hw_turbo_shift` is a log2 (`core/src/emulator/platform.h:960-980`, `Z80::ApplyHardwareTurboNow`
`core/src/emulator/cpu/z80.cpp:602`). A ×6 turbo cannot be expressed.

> **Postponed (2026-09-28):** only the Sprinter needs a ×6. ATM, Scorpion,
> Profi, ZX-Evo and TSConf run at ×1/×2/×4, which `hw_turbo_shift` covers, so
> this change is built as the first step of the Sprinter program rather than
> in the shared infrastructure ahead of TSConf.

Decision (review round 1): replace `hw_turbo_shift` / `hw_turbo_shift_applied` **everywhere** by
`hw_turbo_ratio` / `hw_turbo_ratio_applied` (`uint8_t`, 1…8, default 1).

- **No backward compatibility, no converter.** There were no public releases, so nothing old has
  to load. The TTD checkpoint fields change (`core/src/debugger/ttd/ttdcheckpoint.h` ~`:190`, the
  format description `core/src/debugger/ttd/ttd.ksy` ~`:426`) and the TTD fixture corpus is re-recorded.
- **Existing users** set ratio 2 or 4 where they set shift 1 or 2 today (ATM710
  `portdecoder_atm710.cpp:747`, ATM3 `atm3.cpp:494`, Scorpion).
- **Runtime cost: none per memory access.** The product
  `current_z80_frequency_multiplier = speed_multiplier × hw_turbo_ratio` is computed once, at the
  frame boundary where `_applied` is applied today. Audio descaling divides by the ratio instead of
  shifting by `1 << shift` (through `HostSpeedMultiplier`), also once per frame. Recording and
  playback pay nothing extra.

Worked example: the Sprinter switches to 21 MHz in the middle of a frame. The request sets
`hw_turbo_ratio = 6`; at the next frame boundary `hw_turbo_ratio_applied` becomes 6 and the frame
is 71 680 × 6 = 430 080 CPU T-states long, still 20.48 ms of emulated time.

Test: every existing turbo test unchanged (with ratio 2/4 in place of shift 1/2); a new test checks
430 080 T per frame at ratio 6 and a 20.48 ms frame; a TTD round-trip test covers the new
checkpoint fields.

## 4. Wait states (new, shared, minimal)

The Sprinter at 21 MHz stretches every main-RAM access (HW §2). The cheapest correct place is the
memory access path the ULA contention already uses (`UpdateSlotContention`,
`core/src/emulator/memory/memory.h:356`).

MAME's rule depends on the **clock phase** (`t mod 6`), so a fixed per-bank "extra T-states"
number cannot hold it. Decision (review round 1): the per-bank byte is only a flag, "this bank has
waits", computed when banks change. When the flag is set, the cost comes from one function,
`SprinterWaits::ExtraClocks(kind, t)`. Machines without turbo never set the flag and pay nothing
per access.

> **Built as `MemoryWaitOverlay` (PLAN #60(d), 2026-09-29):** the per-slot
> flag is `SetSlotWaits`, the function is the overlay's `ExtraClocks(kind,
> addr, startClock)`; `startClock` = `Z80::AccessStartClock()`, the CPU clock
> the access started at (at the current rate), which is the `t` of the rule
> below. The wait lands after the byte transfer (the overlay runs after the
> normal access); the instruction length and the phase are exact. Port waits:
> `PortDecoder_Sprinter` adds them with `Z80::AddWaitStates`.

v1 rule (MAME, `sprinter.cpp:1720-1731`): in turbo, a RAM access costs
`((6 − (t mod 6)) mod 6) + 6 − 3` extra CPU clocks (21 MHz clocks, `t` = the CPU clock count); a
port access uses the same rule with 4 instead of 3 (MAME calls `do_mem_wait(3)` for memory and
`do_mem_wait(4)` for ports, `:1173`, `:581`); fast RAM costs 0; at 3.5 MHz nothing is added.

Worked example: a RAM read at `t = 100`: `100 mod 6 = 4`, so `(6 − 4) mod 6 = 2`, plus `6 − 3`
gives 5 extra clocks. The same read at `t = 102` (`102 mod 6 = 0`) costs 3. The function is the
only place the rule lives, so a measured model can replace it (D4).

## 5. State isolation

All Sprinter state lives in Sprinter classes. Shared code gets only the generic hooks above plus
registration lines (model enum, config table, factory switch, `Core` memory factory, TTD ids).
A `sprinterisolation_test.cpp` (the TSConf precedent, TSConf technical design §3.3) scans
`core/src` for `MM_SPRINTER` and Sprinter type names outside the allowlist.

## 6. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| BIOS 3.04 depends on PLD behavior nobody documented (e.g. a status bit during POST) | hangs before the logo | trace the BIOS in MAME and in unreal-ng side by side (port trace with codes); the PLD sources are the tie-breaker |
| The loader's bitstream sink ends at the wrong moment | no boot or wrong config | the end is the real bitstream length (write count traced in S0) plus a watchdog timeout; fast start as the default for tests; the full path tested once per ROM version (tdd-ports-memory §6) |
| Accelerator timing interacts with INT and the turbo waits | wrong demo speed | S5 differential tests against MAME frame counts |
| Palette byte order wrong in one source | wrong colors everywhere | settled in S2 by the BIOS setup screen (§4.5 of HW) |
| The media manager (#58) or the IDE core (#13a) slip | S3/S4 blocked | the floppy path (ACC-3) needs neither; the IDE adapter can be written against the IDE design's interfaces and a `MemoryDisk` |
| Performance at 21 MHz with per-access waits and intercepts | not real time | benchmarks from S1; intercept flags only on banks that need them |
