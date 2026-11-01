# Debugger: peripheral I/O and device histories

The Qt debugger (`debuggers/qt`, eCat3's window) reaches eCat 4.4.0's debugger already: our port is taken from ecat3
`dev` of 2026-10-27, after the 4.4.0 tag (all processors in one window, the listing with its jump graph, the backward
disassembler, the Stack / Breakpoints / History / Devices tabs). What eCat 4.4.0 adds beside the debugger is an I/O
ports window: a tab per output device, logging on and off, to a file, clear, and the data shown as text or bytes.

On a Spectrum the counterpart is the port traffic to the machine's peripherals and the state and history of each
peripheral, which runs by its own state machine (PSG, OPL, GS / NeoGS, WD1793 and the rest).

## The device registry is the source

The machine's devices are the slot manager's registry (`core/src/emulator/slots`, [slots.md](../../features/slots.md),
[new-platform-guidelines.md](../../guidelines/new-platform-guidelines.md) §3), not a list of the debugger's own and
not the port trace's tags:

- the **built-ins** of the machine's declaration (`MachineDef::builtIns` in `slots/refdata/machines.cpp`): `ay`,
  `ula`, `beta128`, `kempston-joystick`, `kempston-mouse`, `sd-zc`, `ide-nemo`, `ts-registers`, `covox`, `zifi`,
  `com`, `rtc` ..., each with its functions and its port claims (mask, match, direction, gate);
- the **cards** fitted in the slots (GS, NeoGS, TurboSound FM, MoonSound, MultiSound, Covox, network cards ...),
  each with its functions, its port claims as configured (options) and its media slots;
- read by every plane as `GET /slots` (`DeviceState::Slots`, `slotreport.cpp`).

Every view of the debugger takes its device list from there: a tab per registry device, its ports from its claims,
its state from the device catalog report of the same device, its history from the device's recorder.

## Goals

1. **Peripheral I/O window**: a tab per registry device (built-in or card), with the accesses that hit its claims:
   time (frame, T-state), PC, IN / OUT, port, value, the register it means. Accesses no device claims (paging, system
   ports of the board) go to a board tab. Logging on / off, clear, to a file, raw bytes or decoded.
2. **Device histories**: per device, the events of its own state machine.
   - `beta128` (WD1793): commands (decoded), their completion with the status, track / sector, drive and side, time.
   - `ay` and the TurboSound / TSFM cards: register writes per chip with register names (SSG and FM).
   - GS / NeoGS cards: host commands and data, the card's replies, its ports, DAC fetches, INTs, ZX-DMA.
   - MoonSound (OPL4), MultiSound (SAA) and the rest: the same from their register writes.
3. **Device state** next to the history: the device catalog report of that device (`/state/devices`).
4. Both modes of the debugger: the core in its own process and a running emulator through the WebAPI.

## What the core has (2026-10-31)

| Need | Source | Route | Gap |
|------|--------|-------|-----|
| Devices, their ports | slot manager registry: built-ins and cards with claims | `GET /slots` | the built-ins are reported without their claims and functions (cards have them); GMX, KAY, LSY256, PHOENIX, QUORUM have no declaration |
| Port traffic | `PortDiagnosticRecorder` (feature `porttrace`, 1M ring, filters) | `/profiler/porttrace/*` | attributed by its own `PortDeviceId` tags, not by the registry's claims |
| AY / TS / TSFM writes | `AYLogAnalyzer` (ring 16..65536, `AYLogRecord`) | `POST/GET /ay/log` | no flag for TSFM FM control words; no absolute T-state |
| GS / NeoGS | `GSPortTraceRecorder` (ring 65536, host + card sides) | `/state/audio/gs/porttrace`, `/control/audio/gs/porttrace` | no card-CPU PC history; command semantics only in taps |
| WD1793 | `WD1793Collector`: unbounded vector, never read (a leak); `IWD1793Observer`; `TRDOSAnalyzer` raw FDC ring | `/analyzer/trdos/raw/fdc` | a bounded command history fed by the observer, with a route |
| OPL4, SAA | chip register watch (`ReportChipRegisterWrite`, kinds ay / fm / saa / opl4 / dac ...) | taps only | no store |
| Device state | device catalog | `/state/devices`, per-device routes | not keyed by registry id; links no history routes |

## Device histories: one pattern

Every history is a `history::DeviceHistory` ([device-history.md](../../emulator/design/debugger/device-history.md)):
off (zero cost), ring (standby), capture with any limit, an optional stream to a JSON Lines file; named by the
registry id, listed and configured on every plane through `HistoryControl`. No device keeps a recorder of its own
(owner 2026-10-31); the AY log, the GS port trace, the bus trace and the port trace are to move onto it.

## The hard machines

Every step is checked against the machines where the registry, the devices and the ports part ways:

| Machine | What makes it hard | How the pattern takes it |
|---------|--------------------|--------------------------|
| ZX Spectrum Next | its own sound (NextAudio: three AYs, the DAC) replaces the 128K TurboSound; NextREG, DMA, Copper, UARTs, the SD cards; the registry is still the bare personality (128K built-ins + Kempston) | the AY history is the machine's one, NextAudio writes into it (`chip` 0-2) under the built-in `ay`; the Next's own devices need registry entries before they get tabs |
| TS-Conf (+ VDAC2 build) | the board answers most ports itself (porthit); the TS registers `#xxAF`, the DMA, the palette DAC; the VDAC2 build drops the Nemo IDE | the registry has `ts-registers`, `covox`, `ide-nemo` (switchable); TS-Conf register / DMA histories go under `ts-registers`; VDAC2 changes the palette path, not the ports |
| Scorpion + GMX | GMX has no registry declaration: its ports would be the board's (`owner` empty) | step 1b: declare GMX (ports, functions) before its tab |
| NeoGS / GS | a card with its own Z80, ROM, RAM and ports on both sides; its host port trace has a ring of its own | the card's history goes under its slot (`zxbus.1`), the GS port trace moves onto the pattern (step 4) |

## Plan

| # | Step | State |
|---|------|-------|
| 1 | `/slots` built-ins with their functions and claims (`mask`, `match`, `port` on cards too) | done |
| 1b | declarations for GMX, KAY, LSY256, PHOENIX, QUORUM | open |
| 2 | `slots::PortOwners`: an access belongs to the registry device whose live claim covers it (cards in slot order, then the built-ins on and in their socket; DOS gate). The port trace's `owner` on REST, CLI, Lua, Python | done |
| 3 | `history::DeviceHistory` + `HistoryControl` on every plane; the WD1793 command history on it (in place of `WD1793Collector`, removed) | done |
| 4a | AY / TS / TSFM / the Next's three AYs: one AY history per machine (SoundManager), named by the `#FFFD` owner; the AY log analyzer is its face (its own ring and the device sinks removed) | done |
| 4b | GS / LW / NeoGS: one GS history per machine (SoundManager), named by the card's slot, kept across personality switches; the GS port trace is its face (its own ring removed); pause / resume in the pattern on every plane | done |
| 4c | Sound chip writes on the pattern (`chipwritehistory.h`, `history::Registered`): MoonSound OPL4, Covox / SounDrive, the Fuller Box AY, the ZX-MultiSound (its YM into the machine's AY history, its SAA / SounDrive into `<slot>.chips`, its GS into `<slot>.gs`), the Sprinter's Covox Blaster | done |
| 4d | SD cards (every `SdCardSpi` host: `sd-zc` on TS-Conf / ATM3, the Next's `sd0` / `sd1`, DivMMC, the NeoGS `<slot>.sd`) and the NeoGS MP3 decoder's SCI writes (`<slot>.mp3`) | done |
| 4e | TS-Conf: the register writes (`ts-registers`), the DMA transfers (`ts-registers.dma`), the VDAC2's FT812 SPI transactions (`vdac2`) | done |
| 4f | The Next's own devices (NextREG, DMA, Copper, UARTs) and Scorpion + GMX, after their registry declarations (1b) | open |
| 4g | Port accesses, one history per registry device (owner 2026-11-01: separate, not one global trace): `<device>.ports` for every built-in and card (`beta128.ports`, `zxbus.1.ports`), `board.ports` for what no device claims; each with its own mode, limit and stream, so one device's traffic never floods another's. Off: one "any port history on" flag on the IN / OUT path; on: the owner from a claims snapshot (rebuilt on a slot change), the record (frame, T-state, PC, IN / OUT, port, value, the register it means) into that owner's history only. The global port trace stays the decode-debugging tool | open |
| 4h | Execution histories on the pattern, one per processor: the main CPU (`PcHistory`), the GS / NeoGS card's Z80, the Next's Copper and DMA | open |
| 5 | Qt debugger: the Devices tab (the registry's devices) shows each device's histories apart (its ports, its own events) beside its catalog report; History refreshes while running too and covers every processor (the GS card's Z80) | open |
| 6 | The catalog links each registry device to its state report and history | open |
