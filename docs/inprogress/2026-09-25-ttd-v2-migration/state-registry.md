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
| Size | *Reserved.* Fixed size in bytes, or a range `min–max` |
| Variability | *Reserved.* How often it changes and how much: for example "every frame, whole", "rare, a few bytes", "only on media load" |
| Status | `ok`, or the gap number from [Gaps](#gaps) |
| Source | The serializer or the field, `file:line`, paths relative to `core/src/` |

Size and Variability are filled from benchmark measurements (`bm3_*`, per-stream split), not by hand estimates. Until then the audit's sizes are in the Status notes where they matter.

Peripheral ids are `ttd::PeripheralId` (one byte, v1); region ids are `ttd::TTDRegionId` (`debugger/ttd/engine/ttdregion.h`).

## 1. Memory: pages and regions

| Item | Machines | Stream v1 | Stream engine | Class | Size | Variability | Status | Source |
|---|---|---|---|---|---|---|---|---|
| Machine RAM, 16 KB pages | all | page store (dirty pages, XOR deltas) | region 0 `MachineRam`, 4 KB pieces | R | | | ok | `timetravelmanager.cpp` capture; engine `timetravelengine.cpp` |
| ROM pages | all | — (fixed, fingerprinted) | — | R (constant) | | | ok | ROM reload ends the session |
| General Sound RAM | GS classic | inside the GS blob (id 5) | region 1 `GeneralSoundRam` | R | | | ok | `sound/chips/soundchip_gs.cpp` |
| GS lightweight upload store | GS lightweight | not recorded (the card is class N) | — | N | | | ok | `debugger/ttd/ttdmachineperipherals.cpp` |
| MoonSound wave SRAM (RAM after the ROM) | MoonSound | — | region 3 `MoonSoundWaveMemory` | R | | | gap 3 | `soundchip_moonsound.cpp` |
| NeoGS RAM | NeoGS | — | region 4 `NeoGSRam` | R | | | gap 3 | `soundchip_neogs.cpp:1392-1398` |
| NeoGS flash | NeoGS | — | region 5 `NeoGSFlash` | R | | | gap 3 | `flash29f040b.cpp` |
| Sprinter video RAM | Sprinter | whole blob (id 28) | region 6 `SprinterVideoRam` | R | | | ok | `debugger/ttd/sprinter/ttdsprinter.cpp` |
| Sprinter fast RAM | Sprinter | whole blob (id 30) | region 7 `SprinterFastRam` | R | | | ok | same |
| FT812 RAM_G, DL0, DL1, REG, CMD, SPECIAL, INFLIGHT | TSL-VDAC2 | zero-run blob (id 42) | regions 8–14 `Vdac2*` | R | | | ok | `debugger/ttd/tsconf/ttdvdac2.cpp` |
| ZX-Evo AVR EEPROM | ATM3, TSL | — (v1 only: not recorded) | region 15 `EvoAvrEeprom`, compared at each capture | R | | | ok in the engine | `memory/atm/evoavr.cpp` `TTDRegions` |
| SMUC NVRAM (EEPROM contents) | Scorpion, ProfScorpion | — (v1 only: not recorded) | region 16 `SmucEeprom`, compared at each capture | R | | | ok in the engine | `io/rtc/smucnvram.cpp` `TTDRegions` |
| ZX-Evo font RAM | ATM3 | whole blob (id 22) | blob (region candidate) | R | | | ok | |
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
| TurboSound / AY chips, queues, phases | TS slot fitted | id 0 | blob | R | | | ok | `sound/chips/soundchip_turbosound.cpp:482/539` |
| WD1793 + 4 drives (Beta Disk) | all Beta machines | id 1 | blob | R | | | ok | `io/fdc/wd1793.cpp:4170/4284` |
| WD1793 command context (rate retry, transfer pointers: the sector, the track being read or written, the position) | every Beta machine | id 35, registered with the BetaDisk | blob | R | | | ok | `debugger/ttd/ttdmachineperipherals.cpp`; `ttdwd1793context.h` |
| Tape | all | id 2 | blob | R | | | ok | `io/tape/tape.cpp:1175/1197` |
| Covox / Soundrive latches | Covox / Soundrive fitted | id 3 | blob | R | | | ok | `sound/covox.cpp:388/401` |
| TSFM: 2 × YM2203, timers | TurboSound FM | id 4 | blob | R | | | ok | `soundchip_turbosoundfm.cpp:820/883` |
| General Sound classic: Z80, registers | GS classic | id 5 | blob without RAM (95 B) | R | | | ok | `soundchip_gs.cpp:844/850` |
| Scorpion ProfROM, 7EFD, 1FFD, Turbo+ latch | Scorpion, ProfScorpion | id 6 (8 B, 2 reserved) | blob | R | | | ok | |
| Kempston mouse | all | id 7 | blob | R | | | ok | |
| ATM paging, palettes, AVR volatile bytes | ATM450, ATM710, ATM3 | id 8 | blob | R | | | ok | `debugger/ttd/atm/ttdatmpaging.cpp:35-36` |
| Profi paging, palette | Profi | id 9 | blob | R | | | ok | |
| MoonSound latches + OPL4 state | MoonSound | id 10 | blob | R | | | ok | `soundchip_moonsound.cpp:486/509` |
| GS lightweight card (interpreter, mod player, store) | GS lightweight | not recorded; header `not_recorded_mask` bit 11 (flag bit 11). Its blob code stays for machine state transfer | same | N | | | ok | `debugger/ttd/ttdmachineperipherals.cpp`; `ttddumpformat.h` `kFlagsHasNotRecordedMask` |
| NeoGS registers, Z80, DMA, VS10xx, SPI, flash command state, SD | NeoGS | id 12 | blob | R | | | ok | `soundchip_neogs.cpp:1392-1406` |
| +3 paging (1FFD) | +2A, +3 | id 13 | blob | R | | | ok | |
| uPD765 | +3 | id 14 | blob | R | | | ok | `io/fdc/upd765.cpp:1381/1427` |
| SD card + Z-Controller | ATM3, TSL | id 15 | blob | R | | | ok | `io/sdcard/sdcardspi.cpp:545/571` |
| TS-Conf state: registers, CRAM, SFILE, DMA, TSU, INT | TSL | id 16 | blob | R | | | ok | |
| TS-Conf: ZX-Evo AVR volatile bytes (EEPROM window, ext type, LEDs) | TSL | EvoAvrVolatile (id 55) | blob | R | | | ok | `debugger/ttd/atm/ttdevoavrvolatile.cpp` |
| IDE / ATA / ATAPI channel | any `[HDD] Scheme` | id 17 | blob | R | | | ok | `debugger/ttd/ide/ttdatachannel.cpp:68/89` |
| ATA write-protect switch | IDE | — | — | R | | | gap 12 | `io/ide/ata/atadevice.h:196` |
| DS12887 / DS1685 real-time clock (emulated time while recording) | ATM3, TSL, Profi v5, Scorpion, Sprinter | id 18 | blob | R | | | ok | `rtc/ds12887.cpp:584/620` |
| SMUC serial EEPROM link, IDE window registers | Scorpion, ProfScorpion | Smuc (id 54) | blob | R | | | ok | `io/rtc/smucnvram.h` `LinkState` |
| ZX-Evo PS/2 keyboard | ATM3, TSL | id 19 | blob | R | | | ok | |
| ZX-Evo AVR F12 soft reset timer (host clock) | ATM3, TSL | — | — | R | | | gap 15 | `evoavr.cpp:180-190` |
| ZXNETUSB + W5300 + virtual network | network card | id 20 (30.6 KB fixed) | blob | R | | | gap 1 | `debugger/ttd/network/ttdzxnetusb.cpp:20/42` |
| ZX-Evo extras | ATM3 | ids 21, 22 | blob | R | | | ok | |
| Kempston joystick | decoders answering #1F | id 23 | blob | R | | | ok | |
| 16550 COM port | ATM3 AVR, ZX-WiFi | id 24 (~49 KB fixed) | blob | R | | | gap 1 | |
| Sprinter PLD and decoder | Sprinter | id 25 | blob | R | | | ok | `ttdsprinter.cpp` |
| ATM2 keyboard controller (MCS-51) | ATM710 | id 26 | blob | R | | | ok | `ttdatm2kbc.cpp:16/23` |
| Machine serial peer | serial link | id 27 | blob | R | | | gap 1 | |
| Sprinter Z84C15 | Sprinter | id 29 | blob | R | | | ok | |
| Sprinter input | Sprinter | id 31 | blob | R | | | ok | |
| Sprinter Covox-Blaster | Sprinter | id 32 | blob | R | | | ok | |
| Sprinter ISA, pads | — | ids 33, 34 reserved | — | — | | | devices not built | |
| ATM I/O bus | ATM | id 36 | blob | R | | | ok | |
| ATM2 I/O ESP card | ATM2IOESP | id 37 | blob | R | | | gap 1 | |
| ESP module serial line `_zxLine` | ESP cards | inside the card's blob (`EspModuleState::zxLineFormat` / `zxLineBaud`, former reserved fields) | blob | R | | | ok | `io/serial/esp/espmodule.cpp` |
| ZX-Evo PS/2 mouse | ATM3, TSL | id 38 | blob | R | | | ok | |
| ZiFi line, ZiFi | ZiFi | ids 39, 40 | blob | R | | | gap 1 | |
| CD drive | IDE with a CD unit | id 41 | blob | R | | | ok | `debugger/ttd/ide/ttdcddrive.cpp:55/74` |
| VDAC2 card and FT812 control state | TSL-VDAC2 | id 43 | blob | R (metrics: T, see §5) | | | ok | `ttdvdac2.cpp` |
| ZX keyboard matrix, pressed keys | all | KeyboardMatrix (id 56), variable size | device state in the checkpoint ([decision 37](engine-decisions.md#h-classes-of-recorded-data)); key changes are events | R | | | ok | `io/keyboard/keyboard.cpp` TTD region |
| Disk autostart one-shot hook | TR-DOS | — | — | R | | | gap 16 | `io/fdc/diskautostart.h:75-77` |
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
| IDE activity LED | IDE | — | telemetry stream | T | | | wrong after a seek | `io/ide/idecontroller.h:97` |
| WD1793 published drive / motor state | Beta | — | telemetry stream | T | | | stale after a seek | `wd1793.cpp:4398` |
| NeoGS DMA and MP3 activity | NeoGS | partly in id 12 | telemetry stream | T | | | one false LED pulse | `soundchip_neogs.cpp:853-856` |
| GS activity counters | GS | — | telemetry stream | T | | | | `GSActivityCounters` |
| TSFM key-on flags (state report) | TSFM | — | telemetry stream | T | | | wrong in the state report | `soundchip_turbosoundfm.h:117`; `state/devicestate.cpp:237` |
| Audio activity indicators, "had sound last frame" (Covox, beeper, CBL) | all | — | telemetry stream | T | | | | |
| SD card blocks read / written, last command | ATM3, TSL | inside id 15 | telemetry stream | T | | | stored today, harmless | |
| Socket and virtual-network byte counters, `VirtualNetwork` activity | network | partly inside id 20 | telemetry stream | T | | | stored today, harmless | |
| Covox-Blaster ring writes, INT requests | Sprinter | inside id 32 | telemetry stream | T | | | stored today, harmless | |
| VS10xx frames decoded | NeoGS | inside id 12 | telemetry stream | T | | | stored today, harmless | |
| FDD motor rotation counter | Beta | inside id 1 | telemetry stream | T | | | never read | |
| Contention statistics, screen switch count | all | — | telemetry stream | T | | | | `UlaContention`, `Screen::_screenSwitchCount` |
| Port activity summary, port trace | all | — | telemetry stream | T | | | | `PortDecoder::_activitySummary` |
| ROM / RAM switch trackers, opcode profiler, call trace, memory access tracker | all | — (own buffers) | telemetry streams | T | | | | |
| Media change counters | all | — | telemetry stream | T | | | show live media | `MediaManager` |
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
| 3 | NeoGS RAM and flash, MoonSound wave SRAM exist only as engine regions; the engine has no restore path yet | Card memory not restored | breaks replay until the engine restores (Phase 5) |
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
| 15 | ZX-Evo F12 soft reset reads the host clock | Not sealed | breaks replay |
| 16 | Edge cases (*NMI pending and +3 floating-bus byte fixed 2026-10-02; RZX playback position 2026-10-04*): disk autostart hook, network state marked "incomplete" with a warning only | | breaks replay, rare |
| 17 | Telemetry items stale after a seek (§5) | Wrong LEDs, state report, status bar | wrong UI |
| 18 | `ttd.ksy:532` says NeoGS memory is in the blob | | documentation |

What the recording refusal check cannot see: it proves only that every declared id has a serializer. Undeclared state (gaps 5–10), size-check skips (gap 2) and region-only memory (gap 3) pass it. Phase 2 adds a per-device declaration of every field with its class, checked against this registry.
