# TDD: ATM Turbo 2+ v7.10 RAM waits at 7 MHz

**Date:** 2026-10-02 · **Rule:** [reference-atm710-turbo-waits.md](reference-atm710-turbo-waits.md) ·
**Requirements:** [machine waits](../2026-09-29-machine-waits/requirements.md) (R1-R6, AC1-AC4)

## 1. The rule

In turbo (#FF77 bit 3) every Z80 machine cycle to a window mapped to RAM - opcode fetch, memory read,
memory write - is stretched by `2 + (t mod 2)` T-states at 7 MHz, `t` = the 7 MHz clock of the cycle's T1 in
the frame (RAM slots start on even clocks). ROM, I/O, interrupt acknowledge, refresh and internal cycles do
not wait; neither does anything at 3.5 MHz.

## 2. Design

| Part | Choice |
|:--|:--|
| Overlay | `Atm710TurboOverlay` (`core/src/emulator/memory/atm/`), a `HostBusOverlay` like `ScorpionTurboOverlay`: `onReadM1`, `onRead`, `onWrite` add `RamWait(Z80::AccessStartClock())` through `Z80::AddWaitStates` when the window is RAM (`Memory::IsWindowRom` is false). No state of its own |
| When it applies | installed while #FF77 bit 3 is set (`PortDecoder_ATM710::SyncTurboRamWaits`, called from `updateTurboMode`, so from #FF77 / #EFF7 writes and reset); the waits follow the applied clock (`hw_turbo_ratio_applied == 2`) and the `contention` feature |
| ZX-Evo | `PortDecoder_ATM3` derives from the ATM710 decoder with `v710Board = false` (the same flag that leaves out the keyboard controller) and overrides `updateTurboMode`: its turbo waits stay `EvoTurboOverlay`'s |
| TTD, snapshots | the checkpoint restores #FF77 with the core state, past the decoder: `TTDAtmPaging::TTDLoadState` calls `SyncTurboRamWaits`. Nothing to save |
| State report | the contention report's `atm710_turbo_waits`: `active` / `off` / `contention_off` (WebAPI, MCP, CLI, Lua, Python read the same node) |

The overlay adds the wait after the byte transfer (`memorywaitoverlay.h`): the instruction's length and every
later T-state are exact, only the byte's position inside the stretched cycle differs.

## 3. Tests (`core/tests/emulator/memory/atm/atm710turbooverlay_test.cpp`)

| Test | Checks |
|:--|:--|
| `RamNopTakesSixOrSeven` | NOP from RAM: 6 T from an even clock, 7 from an odd one, then 6 (every M1 ends on an even clock) |
| `RamReadsAndWritesWait` | `LD A,(HL)` 11 / 12, `LD (HL),A` 11 |
| `RomAndIoDoNotWait` | a ROM read 3 T; `OUT (#FE),A` 6 + 5 + 4 |
| `NoWaitsAtThreeAndAHalfMegahertz`, `TheContentionFeatureOffRemovesTheWaits` | the negatives |
| `TurboInstallsTheOverlay` | installed with #FF77 bit 3; the state report |
| `ATtdRestoreSyncsTheOverlay` | a restore of #FF77 past the decoder |
| `TheZxEvoDoesNotGetThem` | ATM3 |

AC2: the other machines' fingerprints and ATM710 at 3.5 MHz (its golden row, the CP/M boot tests) unchanged.

## 4. The effect that found it

NedoOS's ESP driver on the ATM2 COM (keyboard controller RS-232) polls `IN #FE` from RAM in turbo. With the
waits its gaps between reads leave the controller's 8051 a window for its serial interrupt in part of the
iterations; without them none. NedoOS `osatm2esp.trd` with `espcom.ini` (`comType = 1`, `divider = 3`),
`wget example.com/` at 7 MHz: DNS, CONNECT and a 1028-byte HTTP reply, one frame of 1319 lost and recovered
by the protocol (before: 6 received, 12 lost, stuck after SOCKET).

## 5. Open (reference §7)

Q1 the INT edge's slot phase (at most 1 T after each INT acceptance); Q2/Q3 the odd-phase race and the wait
release on reworked boards; Q4 writes and odd-phase reads (not drawn in the manual's diagram); Q5 the WD1793
port wait at 7 MHz (`/VGCS` through R1C9) - not modeled yet.
