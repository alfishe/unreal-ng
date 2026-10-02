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
loader, WD1793 rate check, port-trace internal codes) form PLAN row #60, done before TSConf. The
WD1793 rate check is **built** (2026-09-29) as the general WD1793 clock / data-rate model (row below).

| Hook | Status | Sprinter use | Spec |
|---|---|---|---|
| **M1 hook** `IMachineM1Hook` / `Z80::machineM1Hook` | **exists** (built for ATM3 E3; `core/src/emulator/cpu/z80.h:290-295`, `:445`) | TR-DOS entry at `#3D00-#3DFF` and exit at `≥ #4000` (the vROM DOS signal); accelerator opcode snooping; the `IN/OUT` + `#1F` quirk | ATM3 [e3-board-nmi.md](../2026-09-15-atm-baseconf-highres-ports/e3-board-nmi.md) |
| **Write intercept** = a write-only `HostBusOverlay` (`observesReads = false`, window `#0000-#FFFF`, the callback picks the bank; `Core::AddBusOverlay`, chained with other overlays) | **built** (PLAN #60(a), 2026-09-29; replaces the per-bank flag of the first design) | video shadow writes, graphics pages, the reset page, ISA pages, accelerator write side; where the plain store must not land, the bank's `_bank_write` points to the trash page | TSConf [technical-design.md](../2026-09-27-tsconf/technical-design.md) §3.5 item 2 |
| **Interrupt source** `IInterruptSource` + `OnReti()`; engine step hook `IMachineStepHook` | **built** (PLAN #60(a) + TSConf INF-5, 2026-09-29; `Z80::interruptSource`, `Z80::machineStepHook`) | INT position from the mode table; keyboard and Covox-Blaster interrupts (all vector `#FF`); RETI for the Z84C15 daisy chain and the accelerator re-arm | TSConf technical design §3.4, §3.8 |
| **Memory subclass** selected in the `Core` factory | pattern exists (`ScorpionMemory`, `core/src/emulator/memory/memory.h:313`) | `SprinterMemory`: bank computation, graphics-page reads | TSConf `TsConfMemory` §3.5 |
| **Clock ratio** `EmulatorState::hw_turbo_ratio` | **built** (PLAN #60(b), 2026-09-29, branch `infra-60`) | 21 MHz = 6 × 3.5 MHz | §3 below |
| **Wait-state hook** | **built** (PLAN #60(d), 2026-09-29): `MemoryWaitOverlay` (`core/src/emulator/memory/memorywaitoverlay.h`) - per-slot flags + `ExtraClocks(kind, addr, startClock)`, a host bus overlay so machines without waits pay nothing; port waits: the decoder calls `Z80::AddWaitStates` | turbo memory and port waits | §4 below |
| **WD1793 clock and data rate** | **built** (2026-09-29, commits `64756638`, `f304dde1`): `FdcClockPolicy::Latched` + `WD1793::SetLatchedClock(FdcClock, FdcDataRate)`; the data-rate check is always on, default DD | the `#BD` density latch (codes `#16`/`#17`) sets 1 MHz + 250 kbit/s or 2 MHz + 500 kbit/s; wired in S3a | [tdd-storage.md](tdd-storage.md) §2.3; [WD1793_Clock_And_Data_Rate.md](../../WD1793/WD1793_Clock_And_Data_Rate.md) |
| **CMOS core** `Ds12887` (new) | decided (review round 1); PLAN #60, with the migrations of the existing clocks | Sprinter CMOS | [tdd-storage.md](tdd-storage.md) §4 |

## 3. Clock ratio (new, shared)

> **Built (PLAN #60(b), 2026-09-29, branch `infra-60`).** The guest CPU clock is now
> `base × speed_multiplier × hw_turbo_ratio`; ATM 7.10 / ZX-Evo / Scorpion set 1, 2 or 4.
> `HostSpeedMultiplier`, `AudioTstate` and `TtdUnitsPerTState` divide by
> `hw_turbo_ratio_applied` and skip the division at ratio 1. Tests: `Z80ClockRatio_Test`
> (`z80_test.cpp`: 430 080 T at ratio 6, 20.48 ms frame, boundary rule),
> `SoundAdaptivity_Test.Beeper_ClockRatioKeepsThePitch` (ratio 3 / 6 bit-identical),
> `TTDChipsetStateTest.CaptureRestore_ClockRatio`. The TTD corpus and the CI gate were
> re-recorded.

Before: the guest CPU clock was `base × (speed_multiplier << hw_turbo_shift)`, where
`hw_turbo_shift` was a log2, so a ×6 turbo could not be expressed.

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

**Which clock `t` is (checked against MAME, 2026-10-01).** MAME calls the memory and port
handlers at the *start* of the bus cycle, before the cycle's clocks are counted (`z80.lst`, macros
`rm`, `wm`, `in`, `out`: `m_mreq_cycles !! handler; m_icount -= cycles`), so `do_mem_wait` sees
the clock the cycle starts at. For memory, unreal-ng's `AccessStartClock()` is that clock (now − 3,
the cycle's 3 T are charged before the bus sees it). For ports it is not: every IN / OUT charges only
the first T of its 4-T I/O cycle before it calls the decoder (`op_D3`, `op_DB`, the ED group), so
the port cycle starts at now − 1 = `AccessStartClock() + 2` (`SprinterWaits::IoCycleStart`). S1
took the port wait at `AccessStartClock()`, 2 clocks early. Worked example, the screen-clear loop of
BIOS 3.04 (page 8 `#0BDC`-`#0BED`: `OUT (#89),A`, three RAM writes, `OUT (#89),A`, three RAM
writes): MAME times the two halves 78 and 90 clocks at 21 MHz; with the early port clock unreal-ng
gave 84 and 90 (one more 6-clock slot per pair), with the fix 78 and 90. Over the first 10 000 port
accesses this was 1.25 ms of 21-MHz run time; after the fix the difference is 5.7 µs net (the
remaining per-access differences of ±1-7 clocks cancel out; they are the rounding of MAME's
timestamps and the Z84C15-write wait below). Pinned by `SprinterReference_Test.Bios304_PortTraceMatchesMame`.

Still open (pending the CPU research, `research-cpu-z84c15.md`): where the rule comes from on the
board. The PLD has its own wait counter on `/IO` (`DCP.TDF:537-551`, a per-code wait length from
`W_TAB[]`) and a memory-cycle wait (`/MR_WAIT`, `DCP.TDF:484`); whether MAME's "align to 6, then
6 − taken" matches them, and whether the Z84C15's own wait generator (WCR) adds to it, is not
settled. Also open: the PLD wait on writes to the Z84C15's own ports. unreal-ng adds it (the PLD sees
every IORQ write, tdd-ports-memory §3.2); MAME adds none (its write tap forwards these writes to
`dcp_w`, yet the measured intervals show no wait: `OUT (#19),A` + `LD A,n` = 18 clocks), 16 writes
in the first 10 000 accesses.

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
