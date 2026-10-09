# TTD state registry

Every item of emulator state that time travel records, rebuilds, or deliberately leaves out, in one place. Part of the [TTD v1 → v2 migration](README.md); the rules behind the classes are [engine decisions 34–36](engine-decisions.md#h-classes-of-recorded-data).

First filled 2026-10-02 from a code audit of branch `ttd-engine`. A change that adds, drops or moves device state updates this page in the same commit (decision 36).

## How to read the tables

| Column | Meaning |
|---|---|
| Item | One piece of state as the code holds it |
| Machines | Where it exists |
| Stream | Where it is recorded: **v1** (the current `TimeTravelManager` checkpoint) and **engine** (`TimeTravelEngine`). `—` means not recorded |
| Class | **R** required, **D** derived, **T** telemetry, **H** host-facing (not recorded by design), **N** a device not recorded by design, named in the header; see [decision 34](engine-decisions.md#h-classes-of-recorded-data) |
| Size | The region's size, or the device state's size (the size it reached for a variable one); a range across machines |
| Variability | The steady rate after the first capture (which stores every piece once): "constant", "rare", "often when active" or "every frame", with bytes and versions per frame and the cases at both ends |
| Status | `ok`, or the gap number from [Gaps](#gaps) |
| Source | The serializer or the field, `file:line`, paths relative to `core/src/` |

Size and Variability come from the benchmark matrix's per-region figures (BM-9, `bm9_<region>_*`; full set, 600 frames, 2026-10-09), not from estimates; method, the raw spread and the comparison with the E6 model: [e6-comparison.md](e6-comparison.md). Rows the matrix does not reach (no configuration fits the device) stay empty.

Peripheral ids are `ttd::PeripheralId` (one byte, v1); region ids are `ttd::TTDRegionId` (`debugger/ttd/engine/ttdregion.h`).

## 1. Memory: pages and regions

| Item | Machines | Stream v1 | Stream engine | Class | Size | Variability | Status | Source |
|---|---|---|---|---|---|---|---|---|
| Machine RAM, 16 KB pages | all | page store (dirty pages, XOR deltas) | region 0 `MachineRam`, 4 KB pieces | R | 96 KB–4 MB | every frame: 15.6 B/frame (PLUS2A/idle) to 1237.0 (PENTAGON/game); up to 10.48 versions/frame | ok | `timetravelmanager.cpp` capture; engine `timetravelengine.cpp` |
| ROM pages | all but ATM3, TSL | — (fixed, fingerprinted) | — | R (constant) | | | ok | ROM reload ends the session |
| ZX-Evo ROM = the 29F040 flash, pages 0-31 (written by flash commands; saved outside the session to its own file, never during a replay) | ATM3, TSL | — (v1 keeps no ROM) | region 18 `EvoFlash`, pieces marked by the chip's programs and erases | R | 512 KB | constant after the first capture | ok in the engine | `emulator/memory/atm/evoflash.cpp` `TTDRegions`; `io/flash/flash29f040b.cpp` |
| General Sound RAM | GS classic | inside the GS blob (id 5) | region 1 `GeneralSoundRam` | R | 128 KB–512 KB | often when active: 0.0 B/frame (PENTAGON+gs128/idle) to 516.6 (PENTAGON+gs512/gs-upload); up to 0.60 versions/frame | ok | `sound/chips/soundchip_gs.cpp` |
| GS lightweight upload store | GS lightweight | not recorded (the card is class N) | — | N | | | ok | `debugger/ttd/ttdmachineperipherals.cpp` |
| MoonSound wave SRAM (RAM after the ROM) | MoonSound | — | region 3 `MoonSoundWaveMemory` | R | 1 MB | constant after the first capture | gap 3 | `soundchip_moonsound.cpp` |
| NeoGS RAM | NeoGS | — | region 4 `NeoGSRam` | R | 2 MB | rare: 0.0 B/frame (PENTAGON/idle) to 64.6 (PENTAGON+beta/disk-loading); up to 0.06 versions/frame | gap 3 | `soundchip_neogs.cpp:1392-1398` |
| NeoGS flash | NeoGS | — | region 5 `NeoGSFlash` | R | 512 KB | constant after the first capture | gap 3 | `flash29f040b.cpp` |
| Sprinter video RAM | Sprinter | whole blob (id 28) | region 6 `SprinterVideoRam` | R | 256 KB | every frame, 222.8 B/frame (9.74 versions/frame) | ok | `debugger/ttd/sprinter/ttdsprinter.cpp` |
| Sprinter fast RAM | Sprinter | whole blob (id 30) | region 7 `SprinterFastRam` | R | 64 KB | rare, 3.1 B/frame (0.02 versions/frame) | ok | same |
| FT812 RAM_G, DL0, DL1, REG, CMD, SPECIAL, INFLIGHT | TSL-VDAC2 | zero-run blob (id 42) | regions 8–14 `Vdac2*` | R | RAM_G 1 MB, DL0 8 KB, DL1 8 KB, REG 4 KB, CMD 4 KB, SPECIAL 4 KB, INFLIGHT 1088 KB | constant after the first capture | ok | `debugger/ttd/tsconf/ttdvdac2.cpp` |
| ZX-Evo AVR EEPROM | ATM3, TSL | — (v1 only: not recorded) | region 15 `EvoAvrEeprom`, compared at each capture | R | 4 KB | constant after the first capture | ok in the engine | `memory/atm/evoavr.cpp` `TTDRegions` |
| SMUC NVRAM (EEPROM contents) | Scorpion, ProfScorpion | — (v1 only: not recorded) | region 16 `SmucEeprom`, compared at each capture | R | 2 KB | constant after the first capture | ok in the engine | `io/rtc/smucnvram.cpp` `TTDRegions` |
| ZX-Evo font RAM | ATM3 | whole blob (id 22) | blob (region candidate) | R | 2054 B | constant after the first capture | ok | |
| Disk sectors written while recording | all with disks | barrier marker only | per [decision 25](engine-decisions.md#e-machines-and-devices): a media version per checkpoint (planned) | R | | | gap 11 | `emulator/media/mediamanager.cpp:651` |

## 2. CPU and machine core

| Item | Machines | Stream v1 | Stream engine | Class | Size | Variability | Status | Source |
|---|---|---|---|---|---|---|---|---|
| Z80 registers, MEMPTR, Q, IFF, IM, INT/NMI, HALT cycle | all | `TTDCpuState` (48 B) | same record | R | | | ok | `debugger/ttd/ttdcheckpoint.cpp:22-126` |
| Z80 pending NMI | all | `TTDCpuState::nmi_pending` (former pad byte 31) | same record | R | | | ok | `emulator/cpu/z80.h` `IsNmiPending` |
| Z80 ZX-Poly local INT fields, `frameIntMasked` | ZX-Poly | — | — | R | | | ZX-Poly is refused | `z80.h:496-501, 828` |
| Chipset: t-states, frame counter, ports 7FFD / FE / EFF7 / BFFD / FFFD / FF77, ULA+, turbo ratio, multiplier | all | `TTDChipsetState` (120 B) | same record | R | | | ok | `ttdcheckpoint.h:156-218` |
| `current_z80_frequency` (Hz) | all, read by the ATM2 keyboard controller and the COM port | derived on restore from the multiplier | same | D | | | ok | `debugger/ttd/ttdcheckpoint.cpp` `RestoreChipsetState` |
| `scorpion_turbo` (Turbo+ latch) | Scorpion, ProfScorpion | ScorpionProfROM (id 6) byte 5; restore re-syncs the wait overlay and the /INT step hook | blob | R | | | ok | `debugger/ttd/scorpion/ttdscorpionprofrom.cpp` |
| `pFFBA` / `p7FBA` (SMUC routing) | Scorpion, ProfScorpion | Smuc (id 54) | blob | R | | | ok | `debugger/ttd/scorpion/ttdsmuc.cpp` |
| +3 floating-bus byte (last contended byte) | +2A, +3 | Plus3Paging (id 13) byte 1, flag in byte 2 | blob | R | | | ok | `debugger/ttd/plus3/ttdplus3paging.cpp` |
| Memory bank caches, contention slot table, wait overlays, flash phase | all | rebuilt | rebuilt | D | | | ok | `UpdateZ80Banks` after restore |
| Frame-cost and screen-digest counters | all | — | — | T | | | ok | `platform.h:1093-1106` |

## 3. Device state

One row per device blob; the device's memory is in section 1.

| Device | Machines | Stream v1 | Stream engine | Class | Size | Variability | Status | Source |
|---|---|---|---|---|---|---|---|---|
| TurboSound / AY chips, queues, phases | TS slot fitted | id 0 | blob | R | 985 B | every frame: 65.5 B/frame (ATM710-turbo/idle) to 70.7 (PENTAGON+ay/idle); up to 1.00 versions/frame | ok | `sound/chips/soundchip_turbosound.cpp:482/539` |
| WD1793 + 4 drives (Beta Disk) | all Beta machines | id 1 | blob | R | 258 B | often when active: 0.0 B/frame (48K/idle) to 25.2 (PENTAGON+beta/disk-loading); up to 0.85 versions/frame | ok | `io/fdc/wd1793.cpp:4170/4284` |
| WD1793 command context (rate retry, transfer pointers: the sector, the track being read or written, the position) | every Beta machine | id 35, registered with the BetaDisk | blob | R | 117 B | often when active: 0.0 B/frame (48K/idle) to 4.4 (PENTAGON+beta/disk-loading); up to 0.27 versions/frame | ok | `debugger/ttd/ttdmachineperipherals.cpp`; `ttdwd1793context.h` |
| Tape | all | id 2 | blob | R | 75 B | rare: 0.0 B/frame (48K/idle) to 0.7 (PENTAGON+moon/moon-upload); up to 0.07 versions/frame | ok | `io/tape/tape.cpp:1175/1197` |
| Covox / Soundrive latches | Covox / Soundrive fitted | id 3 | blob | R | 13 B | often when active: 1.3 B/frame (PENTAGON/idle) to 4.3 (PENTAGON+beta/disk-loading); up to 0.68 versions/frame | ok | `sound/covox.cpp:388/401` |
| TSFM: 2 × YM2203, timers | TurboSound FM | id 4 | blob | R | 2108 B | every frame: 92.6 B/frame (TSCONF/idle) to 283.1 (PENTAGON+tsfm/music-tsfm); up to 1.00 versions/frame | ok | `soundchip_turbosoundfm.cpp:820/883` |
| General Sound classic: Z80, registers | GS classic | id 5 | blob without RAM (95 B) | R | 99 B | every frame: 25.7 B/frame (PENTAGON+gs512/idle) to 27.1 (PENTAGON+gs512/gs-upload); up to 1.00 versions/frame | ok | `soundchip_gs.cpp:844/850` |
| Scorpion ProfROM, 7EFD, 1FFD, Turbo+ latch | Scorpion, ProfScorpion | id 6 (8 B, 2 reserved) | blob | R | 12 B | rare, 0.0 B/frame (0.01 versions/frame) | ok | |
| Kempston mouse | all | id 7 | blob | R | 12 B | constant after the first capture | ok | |
| ATM paging, palettes, AVR volatile bytes | ATM450, ATM710, ATM3 | id 8 | blob | R | 140 B | constant after the first capture | ok | `debugger/ttd/atm/ttdatmpaging.cpp:35-36` |
| Profi paging, palette | Profi | id 9 | blob | R | 38 B | constant after the first capture | ok | |
| MoonSound latches + OPL4 state | MoonSound | id 10 | blob | R | 4562 B | every frame: 29.1 B/frame (PENTAGON+moon/moon-upload) to 39.4 (SCORPION/idle); up to 1.61 versions/frame | ok | `soundchip_moonsound.cpp:486/509` |
| GS lightweight card (interpreter, mod player, store) | GS lightweight | not recorded; header `not_recorded_mask` bit 11 (flag bit 11). Its blob code stays for machine state transfer | same | N | | | ok | `debugger/ttd/ttdmachineperipherals.cpp`; `ttddumpformat.h` `kFlagsHasNotRecordedMask` |
| NeoGS registers, Z80, DMA, VS10xx, SPI, flash command state, SD | NeoGS | id 12 | blob | R | 21660 B | every frame: 27.5 B/frame (TSCONF/idle) to 43.3 (ATM710-turbo/idle); up to 1.01 versions/frame | ok | `soundchip_neogs.cpp:1392-1406` |
| +3 paging (1FFD) | +2A, +3 | id 13 | blob | R | 8 B | constant after the first capture | ok | |
| uPD765 | +3 | id 14 | blob | R | 388 B | constant after the first capture | ok | `io/fdc/upd765.cpp:1381/1427` |
| SD card + Z-Controller | ATM3, TSL | id 15 | blob | R | 2696 B | constant after the first capture | ok | `io/sdcard/sdcardspi.cpp:545/571` |
| TS-Conf state: registers, CRAM, SFILE, DMA, TSU, INT | TSL | id 16 | blob | R | 2204 B | every frame, 25.1 B/frame (1.00 versions/frame) | ok | |
| ZX-Evo ROM flash command state (mode, toggle, DQ7 source, erase sectors, pending program, window and busy ends in base t-states) | TSL, ATM3 | EvoFlash (id 61, 33 bytes) | blob | R | 37 B | constant after the first capture | ok | `debugger/ttd/atm/ttdevoflash.cpp`; `emulator/memory/atm/evoflash.cpp` |
| ZX-Evo AVR volatile bytes (EEPROM window, ext type, LEDs) and the /WAIT ports' timing (`EvoAvrWait`: main-loop phase, EEPROM write end; #xxEF and #BFF7 share it) | TSL, ATM3 | EvoAvrVolatile (id 55, v2, 20 bytes) | blob | R | 24 B | every frame, 6.5 B/frame (1.00 versions/frame) | ok | `debugger/ttd/atm/ttdevoavrvolatile.cpp` |
| IDE / ATA / ATAPI channel | any `[HDD] Scheme` | id 17 | blob | R | 4256 B–8500 B | rare, 0.2 B/frame (0.01 versions/frame) | ok | `debugger/ttd/ide/ttdatachannel.cpp:68/89` |
| ATA write-protect switch | IDE | — | — | R | | | gap 12 | `io/ide/ata/atadevice.h:196` |
| DS12887 / DS1685 real-time clock (emulated time while recording) | ATM3, TSL, Profi v5, Scorpion, Sprinter | id 18 | blob | R | 340 B | rare, 0.3 B/frame (0.08 versions/frame) | ok | `rtc/ds12887.cpp:584/620` |
| SMUC serial EEPROM link, IDE window registers | Scorpion, ProfScorpion | Smuc (id 54) | blob | R | 40 B | constant after the first capture | ok | `io/rtc/smucnvram.h` `LinkState` |
| ZX-Evo PS/2 keyboard | ATM3, TSL | id 19 | blob | R | 60 B | constant after the first capture | ok | |
| ZX-Evo AVR F12 hold (down flag, press time in emulated microseconds) | ATM3, TSL | 19 | — | R | | | gap 15 fixed | `evoavr.cpp` OnPcKey, EvoPs2 blob |
| ZXNETUSB + W5300 + virtual network | network card | id 20 (30.6 KB fixed) | blob | R | 67139468 B | constant after the first capture | gap 1 | `debugger/ttd/network/ttdzxnetusb.cpp:20/42` |
| ZX-Evo extras | ATM3 | ids 21, 22 | blob | R | | | ok | |
| Kempston joystick | decoders answering #1F | id 23 | blob | R | 6 B | constant after the first capture | ok | |
| 16550 COM port | ATM3 AVR, ZX-WiFi | id 24 (~49 KB fixed) | blob | R | 932 B | often when active: 0.0 B/frame (ATM3/idle) to 5.8 (TSCONF/idle); up to 1.00 versions/frame | gap 1 | |
| Sprinter PLD and decoder | Sprinter | id 25 | blob | R | 477 B | every frame, 8.4 B/frame (0.96 versions/frame) | ok | `ttdsprinter.cpp` |
| ATM2 keyboard controller (MCS-51) | ATM710 | id 26 | blob | R | 1164 B | every frame: 55.2 B/frame (ATM710/idle) to 58.5 (ATM710-turbo/idle); up to 1.00 versions/frame | ok | `ttdatm2kbc.cpp:16/23` |
| Machine serial peer | serial link | id 27 | blob | R | | | gap 1 | |
| Sprinter Z84C15 | Sprinter | id 29 | blob | R | 232 B | rare, 0.2 B/frame (0.01 versions/frame) | ok | |
| Sprinter input | Sprinter | id 31 | blob | R | 93 B | constant after the first capture | ok | |
| Sprinter Covox-Blaster | Sprinter | id 32 | blob | R | 549 B | constant after the first capture | ok | |
| Sprinter ISA, pads | — | ids 33, 34 reserved | — | — | | | devices not built | |
| ATM I/O bus | ATM | id 36 | blob | R | 8 B | constant after the first capture | ok | |
| ATM2 I/O ESP card | ATM2IOESP | id 37 | blob | R | | | gap 1 | |
| ESP module serial line `_zxLine` | ESP cards | inside the card's blob (`EspModuleState::zxLineFormat` / `zxLineBaud`, former reserved fields) | blob | R | | | ok | `io/serial/esp/espmodule.cpp` |
| ZX-Evo PS/2 mouse | ATM3, TSL | id 38 | blob | R | 12 B | constant after the first capture | ok | |
| ZiFi line, ZiFi | ZiFi | ids 39, 40 | blob | R | 16777256 B–16826204 B | often when active: 0.0 B/frame (TSCONF/idle) to 5.8 (TSCONF/idle); up to 1.00 versions/frame | gap 1 | |
| CD drive | IDE with a CD unit | id 41 | blob | R | 5740 B | constant after the first capture | ok | `debugger/ttd/ide/ttdcddrive.cpp:55/74` |
| VDAC2 card and FT812 control state | TSL-VDAC2 | id 43 | blob | R (metrics: T, see §5) | 9732 B | every frame, 13.3 B/frame (1.00 versions/frame) | ok | `ttdvdac2.cpp` |
| ZX keyboard matrix, pressed keys | all | KeyboardMatrix (id 56), variable size | device state in the checkpoint ([decision 37](engine-decisions.md#h-classes-of-recorded-data)); key changes are events | R | 526 B | rare, 0.3 B/frame (0.08 versions/frame) | ok | `io/keyboard/keyboard.cpp` TTD region |
| Disk autostart one-shot hook | TR-DOS | — | — | R | | | gap 16: the rewrite is a recorded edit (2026-10-07) | `io/fdc/diskautostart.h:75-77`, `z80.cpp` |
| RZX player position and counters | RZX playback | RzxPlayback (id 57), 84 bytes; registered when a recording was played on the machine | device state in the checkpoint; a restore installs or removes the playback's hooks as it was then; each RZX frame end is an `InterruptFrame` fact (Phase 3, Step 2) | R | | | ok (2026-10-04) | `rzx/rzxttdstate.cpp` |

## 4. Journals and events

| Item | Machines | Stream v1 | Stream engine | Class | Size | Variability | Status | Source |
|---|---|---|---|---|---|---|---|---|
| Port reads (IN values) | all except TS-Conf, Sprinter, Profi, Scorpion, NeoGS, Next | port journal (absolute indices) | event stream ([decision 24](engine-decisions.md#e-machines-and-devices)) | R | | | ok | `debugger/ttd/ttdportjournal.h:161`; switch-off reasons `timetravelmanager.cpp:2073-2108` |
| Input events (keys, mouse, PC keys) | all | input journal | event stream | R | | | ok | `ttdinputjournal.cpp` |
| Network payload records | network cards | input journal, renumbered on eviction | event payloads, absolute numbers, kept while a checkpoint refers to them ([decision 24](engine-decisions.md#e-machines-and-devices)) | R | | | gap 1 (v1 only) | `ttdinputjournal.cpp:110-134` |
| Memory writes (write journal) | all | ring | derived index ([decision 17](engine-decisions.md#c-what-the-engine-provides)) | D | | | ok | |
| Media changes, write-protect toggles, queued swaps | all with media | barrier / invalidation | events + media versions | R | | | gaps 11–13 | `mediamanager.cpp:516, 678, 959-975`; `io/fdc/floppydriveslot.cpp:13` |
| Clock / speed changes | all | ends the session | event ([decision 20](engine-decisions.md#e-machines-and-devices)) | R | | | ok | `emulator/emulator.cpp:650, 969, 1702, 1973, 2168` |

## 5. Telemetry

None of these is read back by emulation. Each becomes an optional frame-boundary stream ([decision 35](engine-decisions.md#h-classes-of-recorded-data)), off by default.

| Item | Machines | Stream v1 | Stream engine | Class | Size | Variability | Status | Source |
|---|---|---|---|---|---|---|---|---|
| VDAC2 line-budget metrics of the last frame | TSL-VDAC2 | inside id 43 | stays in the chip's state (see note) | T | | | decided 2026-10-03 | `EveSaveState` |
| IDE activity LED | IDE | — | telemetry stream | T | | | correct after a seek (2026-10-09): a replay does not count | `io/ide/idecontroller.h:112`; `ata/atadevice.cpp` (not counted while `ttdReplayActive`) |
| WD1793 published drive / motor state | Beta | — | telemetry stream | T | | | correct after a seek (2026-10-09): the restore posts the drive state | `wd1793.cpp` `TTDLoadState` -> `notifyFDDStateChanged` |
| NeoGS DMA and MP3 activity | NeoGS | partly in id 12 | telemetry stream | T | | | correct after a seek (2026-10-09): the restore takes the counts as seen | `soundchip_neogs.cpp` `TTDLoadState` |
| GS activity counters | GS | — | telemetry stream | T | | | correct after a seek (2026-10-09): kept across a replay | `SoundManager::beginReplayTelemetry` / `endReplayTelemetry` |
| TSFM key-on flags (state report) | TSFM | — | telemetry stream | T | | | correct after a seek (2026-10-09): derived from the restored envelopes | `tsfm/ym2203pair.cpp` `loadChipState` |
| Audio activity indicators, "had sound last frame" (Covox, beeper, CBL) | all | — | telemetry stream | T | | | correct after a seek (2026-10-09): dark after it, as on a pause | `TimeTravelController::PublishSeekedFrame` |
| SD card blocks read / written, last command | ATM3, TSL | inside id 15 | telemetry stream | T | | | stored today, harmless | |
| Socket and virtual-network byte counters, `VirtualNetwork` activity | network | partly inside id 20 | telemetry stream | T | | | stored today, harmless | |
| Covox-Blaster ring writes, INT requests | Sprinter | inside id 32 | telemetry stream | T | | | stored today, harmless | |
| VS10xx frames decoded | NeoGS | inside id 12 | telemetry stream | T | | | stored today, harmless | |
| FDD motor rotation counter | Beta | inside id 1 | telemetry stream | T | | | never read | |
| Contention statistics, screen switch count | all | — | telemetry stream | T | | | | `UlaContention`, `Screen::_screenSwitchCount` |
| Port activity summary, port trace | all | — | telemetry stream | T | | | | `PortDecoder::_activitySummary` |
| ROM / RAM switch trackers, opcode profiler, call trace, memory access tracker | all | — (own buffers) | telemetry streams | T | | | | |
| Media change counters | all | — | telemetry stream | T | | | show live media; replayed guest writes still count (open, TODO) | `MediaManager` |
| RZX desyncs, drift | RZX | — | telemetry stream | T | | | | `rzx/rzxplayer.h` |
| ESP module log | ESP | — | telemetry stream | T | | | | `EspModule::_log` |

**Note on the VDAC2 metrics (2026-10-03).** Master (`14c25eca6`) shows them in the FT812 Debug window at any past position: after a frame seek the restored chip state gives the last finished FT812 frame's metrics, inside a frame the replay that composes the picture measures the lines drawn so far. They are read back at every position the window shows, so they stay inside the chip's state blob rather than in an optional stream; Phase 2 stores only the bytes that change, so they cost bytes only when an FT812 frame finishes.

## 6. Derived (rebuilt after a restore, not recorded)

| Item | Owner | Rebuilt by | Source |
|---|---|---|---|
| Sprinter palette RGBA cache, INT list | Sprinter | region `onRestored` | `ttdsprinter.cpp` |
| FT812 derived state | VDAC2 | `EveMemoryRestored` | `ttdvdac2.cpp` |
| TS-Conf line table, arbiter, clock | TSL | `ApplyState` | |
| W5300 RX / backlog byte counts | network | from the socket buffers | |
| GS lightweight parsed module | GS lightweight | from the store | |
| SD card block map | ATM3, TSL | from the image | |
| Tape classifier, CD audio cursor | tape, CD | from their state | |
| `SoundManager` sample accumulator | all | adopted from TS / TSFM; not when the TS slot is empty (audio only) | |

## 7. Host-facing (not recorded by design)

| Item | How time travel treats it | Source |
|---|---|---|
| Audio ring, render layers, DC filters (AY, Covox, beeper edges) | Muted during replay; the render layer resets on restore. One audible edge or DC step after a seek is allowed | `timetravelmanager.cpp:1705-1716` |
| Host framebuffer | Composed by replay; the present queue is flushed after a seek | `timetravelmanager.cpp:2396-2411`; `ttddisplayparticipant.h` |
| Disk image files, write-through raw images, host folders | Not versioned today (gap 11); planned as media versions | `io/storage/rawimage.cpp:107`; `io/storage/hostfolder/hostfolderfat.cpp:383-405` |
| Network sockets, DNS, DHCP | Through `VirtualNetwork`, recorded as events with payload; settings changes refused while recording | `io/network/networkmanager.cpp:383` |
| Real-time clocks | Emulated time while recording | `debugger/ttd/ttdds12887.cpp:40-48` |
| Card clocks (GS, NeoGS) | Config × speed multiplier; recording holds 1× | |

## Gaps

Ordered by severity. "Breaks replay" means a restore or a replay can give a state the original run never had.

| # | Gap | Effect | Severity |
|---|---|---|---|
| 1 | *v1 only, not fixed (decided 2026-10-02):* v1's history limit (`SetHistoryLimit`) renumbers network payload records on eviction, but references held inside surviving checkpoints (ZXNETUSB, COM port, ZiFi, ATM2 I/O ESP, serial peer) keep the old numbers. The engine does not evict history from memory (decision 28: memory is a cache of the session file) and keeps every payload a checkpoint refers to (decision 24); record numbers there are absolute | Wrong bytes or zeros after a v1 eviction, with a history limit set and unread network bytes | v1 only |
| 2 | *Closed 2026-10-02 by design:* the lightweight GS is not recorded (class N, named in the header); it runs live through seeks | — | — |
| 3 | NeoGS RAM and flash, MoonSound wave SRAM exist only as engine regions; the engine has no restore path yet | Card memory not restored | fixed: the controller restores every region from the engine (Phase 5 switch, edb2f3c40) |
| 4 | *Fixed 2026-10-02:* WD1793 command context declared only on the Sprinter | A checkpoint inside a multi-frame disk transfer ended it early on every other Beta machine | — |
| 5 | *Fixed 2026-10-02:* ZX keyboard matrix in no checkpoint | Machines without the port journal read the live matrix in replay; stuck or missing keys after seek + resume | breaks replay |
| 6 | *Fixed 2026-10-02:* `scorpion_turbo` not stored, waits not resynced after a restore | Wait states, step hook, INT pulse diverge | breaks replay |
| 7 | *Fixed 2026-10-02:* `current_z80_frequency` not restored | ATM2 keyboard-controller waits use a stale frequency | breaks replay |
| 8 | *Fixed 2026-10-02 except the contents (gap 10):* SMUC: `pFFBA` / `p7FBA`, IDE registers, NVRAM I2C state and contents not stored | RTC and IDE writes routed wrongly after a seek | breaks replay |
| 9 | *Fixed 2026-10-02:* TS-Conf has no serializer for the ZX-Evo AVR volatile bytes | EEPROM window and ext type wrong after a seek | breaks replay |
| 10 | *Engine: fixed 2026-10-02 (regions 15, 16); v1 only otherwise:* ZX-Evo AVR EEPROM and SMUC EEPROM not recorded | NVRAM writes survive a seek back in v1 | v1 only |
| 11 | Sectors written while recording are only barriers; host-side image changes reach replay | A backward seek reads post-write sectors | breaks replay |
| 12 | Write-protect toggles not recorded; ATA protect switch in no checkpoint | | breaks replay |
| 13 | A media swap queued before recording can land mid-recording without a marker | | breaks replay |
| 14 | *Fixed 2026-10-02:* ESP module `_zxLine` not restored, reapplied stale | | breaks replay |
| 15 | ZX-Evo F12 soft reset reads the host clock | Not sealed | fixed 2026-10-07: the hold is measured in emulated time and is part of the EvoPs2 state (layout 2, 56 bytes) |
| 16 | Edge cases (*NMI pending and +3 floating-bus byte fixed 2026-10-02; RZX playback position 2026-10-04*): disk autostart hook (fixed 2026-10-07: its rewrite is a recorded edit, a replay never runs the hook), network state marked "incomplete" with a warning only (fixed 2026-10-08: what the fixed arrays do not hold and bytes received before the recording go into a blob tail, `netstatetail.h`) | | closed |
| 17 | Telemetry items stale after a seek (§5) | Wrong LEDs, state report, status bar | wrong UI |
| 18 | `ttd.ksy:532` says NeoGS memory is in the blob | | documentation |

What the recording refusal check cannot see: it proves only that every declared id has a serializer. Undeclared state (gaps 5–10), size-check skips (gap 2) and region-only memory (gap 3) pass it. Phase 2 adds a per-device declaration of every field with its class, checked against this registry.
