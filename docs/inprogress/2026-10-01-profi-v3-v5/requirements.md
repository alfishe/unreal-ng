# Profi v3 / v5: requirements

**Date:** 2026-10-01 · part of [README.md](README.md) · evidence in [cross-check.md](cross-check.md)

## 1. Goals

| ID | Goal |
|:--|:--|
| G1 | Two machines, **Profi v3** (Kramis / TOO "Profi" boards v3.x) and **Profi v5** (Kondor boards v5.0x), each behaving like its board and booting its factory firmware |
| G2 | Every difference rests on independent evidence ([cross-check.md](cross-check.md)); a claim only one emulator makes is not built until a primary source confirms it |
| G3 | Nothing changes for the existing `PROFI` model unless a v5 source says it should: its configs, snapshots, TTD recordings and tests keep working |
| G4 | No cost for other machines: the board differences live in the Profi decoder and its own overlays, never on a shared hot path |
| G5 | Automation parity: every automation surface (CLI, WebAPI + OpenAPI, MCP, Lua, Python) and Qt know both boards |

Not goals: Profi v4.01 as a third machine (its extras over v5 are listed in cross-check P10, nothing runs on
them alone); the XT keyboard controller; the COM port (8251 / 8253) and the Kondor modem; the v5 front-panel
CP/M switch as a boot selector (section 4, Q6).

## 2. Requirements

Confidence: **H** = confirmed by independent sources; **M** = one primary source (a manual or a factory ROM)
and no contrary source; **O** = open, needs the evidence step named in [design.md](design.md) section 8 before it is
built.

### 2.1 Machines and firmware

| ID | Requirement | v3 | v5 | Conf. | Evidence |
|:--|:--|:--|:--|:--|:--|
| R1 | Model ids: v5 keeps `PROFI` (`MM_PROFI`, alias `PROFI5`); v3 is a new `PROFI3` (`MM_PROFI3`, appended to the enum) | `PROFI3` | `PROFI` | - | design 2 |
| R2 | Default system ROM | `rom/profi/kramis-v02.rom` | `rom/profi.rom` (unchanged) | H | roms.md |
| R3 | The other factory images load through `[ROM] PROFI3=` / `PROFI=` | `kramis-v03.rom` | `bios10*.rom`, `bios20*.rom` | H | roms.md |
| R4 | RAM: page = `((DFFD & 7) << 3) \| (7FFD & 7)`, wrapping on smaller boards | 512K default, 1024K allowed | 1024K default, 512K allowed | H | M1-M3 |
| R5 | 256K and 768K v3 boards, with unfitted chip rows reading `#FF` | - | - | O | M4 (XP+ only) |
| R6 | Reset enters the SYS ROM; ROM order SYS, DOS, 128, 48 | yes | yes | H | M5 |

### 2.2 Ports and devices

| ID | Requirement | v3 | v5 | Conf. | Evidence |
|:--|:--|:--|:--|:--|:--|
| R10 | `#7FFD` / `#DFFD` decode and bit meanings | shared | shared | H | P1 |
| R11 | The AY decodes A13 (`IN #DFFD` does not read the AY) | yes | yes | H | P3 |
| R12 | Palette at `#xx7E` (A0 = A7 = 0) in DS80 | **no** | yes | H | P4 |
| R13 | Hi-res 512x240 is monochrome (no attribute page); replaces the global `ProfiMonochrome` key for v3 | yes | no (the key stays as a v5 option: a board without palette chips) | H | P4a |
| R14 | `#FE` read bit 7 | 1 | palette-derived (today's KAR GX0, unchanged) | H (v3) / M (v5) | P5 |
| R15 | Extended port map (FDC `#83..#E3`, system `#3F`, RTC, IDE, Covox aliases `#C7`/`#A7`) with CP/M and ROM14 | **no** | yes | H | P6, P7, P10 |
| R16 | CP/M map: FDC `#1F..#7F`, system `#BF`, whatever the TR-DOS latch is | with CP/M, whatever ROM14 is | with CP/M and ROM14 = 0 | H | P7, P9, decoder PROMs |
| R17 | RTC and Profi IDE | **absent** | as today | H | P10 |
| R18 | The port decode of every mode equals the board's port decoder PROM (the tables and their wiring: [decoder-prom.md](decoder-prom.md)) | the v3.2 dump (MDESK) | the printed table of v4.01 / v5.0 (transcribed) | H | P7, P8, P9, design 4.3 |
| R19 | 8255 outside DOS and CP/M: `IN #1F` = Kempston joystick (port A); Covox left `#5F`, right `#3F` | yes | yes | H | P11, P12 |
| R20 | Mouse at `#FADF/#FBDF/#FFDF` with CP/M off | yes | yes | H | P15 |
| R21 | Floating bus: the pixel byte the video latch holds, on an undecoded `IN` with A0 = 1 (design 4.4) | **yes** | none | M (form from the schematic, E3) | P16 |
| R22 | PSG is an AY-3-8910 | yes | yes | H | P17 |

### 2.3 Video timing and CPU clock

| ID | Requirement | v3 | v5 | Conf. | Evidence |
|:--|:--|:--|:--|:--|:--|
| R30 | Frame, line length and INT position come from the board's sync PROM, chosen by `[PROFI] SyncProm=` (design 5.1) | default `0a1d` (the 3.2 board's original PROM): 69888 T, 224 T, INT 12580 T before paper; `fb0579b6` (71680 T, INT 48 T) selectable | default `v503`: 69888 T, 224 T, INT **14368 T** before paper (today 12580) | H | T1-T4a |
| R30a | INT length | 32 T (O) | 28 T (Gromov's 8-8.6 us) | O / M | T5 |
| R31 | Hi-res (DS80) timing comes from the upper half of the same PROM | yes | yes | O (the DS80 CPU clock) | design 5.3 |
| R32 | Turbo: a front-panel switch (no port) in machine control and automation | yes | yes | H (switch exists) | MAN v3.2 p2, p5; v5.0 p2, p5 |
| R33 | v3 turbo is 7 MHz with RAM waits from the board's DRAM arbitration; the wait rule comes from the v3.2 schematic, cross-checked against the measurements available (design 6.2) | yes | - | M (E3) | T5-T8 |
| R34 | v5 turbo up to 15 MHz (third crystal, a configurable clock) | - | yes (rule O) | M (exists) / O (rule) | MAN v5.0 p2, p6 |
| R35 | No wait states at 3.5 MHz | yes | **no**: see R36 | H | M6 |
| R36 | v5: the video controller's WAIT on CPU RAM accesses in standard mode (/REDYT), switchable like the 5.06 jumper SB8 (`[PROFI] WaitConfig=profi\|pentagon`); the ROM one-shot wait as an option (`RomWait`) | - | yes | M (rule from the netlist model, E4) | cross-check 4.4, design 6.4 |
| R37 | AY clock in 512x240: 1.5 MHz on the original boards (12 MHz / 8) | O | O | O | zx-pk; check against the schematics |

### 2.4 Integration

| ID | Requirement |
|:--|:--|
| R40 | TTD: `PROFI3` records and replays bit-exactly; its board-specific state (none beyond `ProfiPaging` and the turbo switch) is in the recording; v5 TTD fixtures do not change |
| R41 | Automation: `list_models` lists `PROFI3` as creatable; `/state/paging`, port-map and port-trace output describe the board's own port set; the MCP resource `unreal://machine/profi` covers both boards |
| R42 | Qt: both machines in the Machine menu; the turbo switch in the machine's controls |
| R43 | Docs: `.recipe/machines/profi.md`, `AGENTS.md` model list, `data/rom/README-ROMS.md`, the permanent machine docs |

## 3. Acceptance

1. `PROFI3` boots `kramis-v02.rom` and `kramis-v03.rom` to the Kramis menu, and the TR-DOS, 128 BASIC and 48 BASIC
   entries each start. `PROFI` still passes `profi_boot_test` and `profi_hdd_test` unchanged.
2. Port tests cover the full 256 low bytes x CP/M x ROM14 x DOS latch on both boards. Where the PROM is transcribed,
   they are checked against the PROM table (design 4.3).
3. The timing tests pin the frame length and the INT-to-paper distance of every `SyncProm=` row on both boards (`int_timing_test`).
4. A TTD record and replay round-trips on `PROFI3`, including a turbo switch mid-recording.
5. All of `core-tests` passes; the other machines' fingerprints are unchanged.

## 4. Open questions

| Q | Question | Blocks | How to settle |
|:--|:--|:--|:--|
| Q1 | ~~v3 frame: 69888 / 12580 or 71680 / 47 T~~ **settled**: both, from different PROMs (cross-check 4); `SyncProm=` with v3 default `0a1d`, the original PROM of a 3.2 board | - | - |
| Q2 | ~~v5 frame: 216 T or 224 T~~ **settled**: 224 T x 312 = 69888, INT 14368 T before paper; 216 T was a decoding mistake (the DD53 load value, cross-check 4.1) | - | - |
| Q9 | ~~v5 /REDYT wait pattern per T~~ **settled** by E4: 1 T on every other T of the paper fetch window, phase from power-on, none in the border ([research-profi-v5-wait.md](research-profi-v5-wait.md), design 6.4) | - | - |
| Q3 | ~~v3 floating-bus form~~ **settled** by E3: the pixel byte only, one tick ahead of the display, `IN` with A0 = 1 ([research-profi-v3-turbo-floatbus.md](research-profi-v3-turbo-floatbus.md) B, design 4.4) | - | - |
| Q4 | ~~v3 turbo wait rule~~ **settled** by E3: RAM waits 2 / 3 7 MHz clocks by the start clock's parity, ROM none; reproduces the 88208 T Tact Meter figure (design 6.2) | - | - |
| Q5 | ~~v5 palette gate~~ **settled**: DS80 alone, A7=0, A0=0; the manual's sentence contradicts its own schematic ([research-profi-v5-open-items.md](research-profi-v5-open-items.md)) | - | - |
| Q6 | ~~v5 front-panel CP/M switch~~ **settled** for the processor board: it holds #DFFD at #00 (built as `FrontPanelSwitch::Cpm`); the manual's "pressed = Spectrum 128" comes from the BIOS, which cannot raise its hi-res menu with #DFFD held at #00 (checked on the emulated BIOS; no emulator models the switch) | - | - |
| Q7 | ~~Does the SYS ROM see the extended map on v5 (Karabas)~~ **settled: no** ([decoder-prom.md](decoder-prom.md)) | - | - |
| Q8 | 256K / 768K v3 boards | R5 | the v3.2 manual p6 memory map, the BOM |
