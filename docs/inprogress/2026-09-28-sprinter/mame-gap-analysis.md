# Sprinter Sp2000: unreal-ng compared with MAME's `sprinter` driver (gap analysis)

| | |
|---|---|
| **Date** | 2026-10-02 |
| **Status** | Analysis only. No code changed |
| **MAME side** | `emulators/github/mame/src/mame/sinclair/sprinter.cpp` (source `f43983b6`, the same tree as the local 0.289 build `scratch/mame-sprinter/zxsp`), with `beta_m.cpp`, `devices/cpu/z80/z84c015.cpp` / `tmpz84c015.cpp` / `z80.lst`, `devices/bus/isa/zxbus_adapter.cpp`, `devices/bus/spectrum/zxbus/bus.cpp`; the owner's MAME pack `mame_release_v306_25.05.2025` (MAME **0.277**, `_306.bat`, `cfg/sprinter.cfg`, `roms/`) |
| **unreal-ng side** | `master` at `1d5bdf767` (S0-S4 input, automation, and **S3b merged**); branch `sprinter-s5` with the uncommitted accelerator in the worktree `scratch/wt-sprinter-s5` (`core/src/emulator/memory/sprinter/sprinteraccelerator.{h,cpp}`) |
| **Related** | [roadmap-and-plan.md](roadmap-and-plan.md), [TODO.md](TODO.md), [hardware-reference.md](hardware-reference.md), [s4-input-outcome.md](s4-input-outcome.md), [automation-outcome.md](automation-outcome.md), [bios-versions.md](bios-versions.md), [reference captures](../../../testdata/machines/sprinter/reference/README.md) |

Citations: MAME lines are `sprinter.cpp:NNN` unless another file is named (paths under
`emulators/github/mame/src/`). unreal-ng lines are repository paths. "S5 worktree" means the
uncommitted files of `scratch/wt-sprinter-s5`.

## In short

110 features compared:

| Status | Count | Meaning |
|---|---|---|
| **equal** | 57 | same behavior (often verified against a MAME capture) |
| **ours better** | 12 | unreal-ng follows the board (PLD sources, chip data sheet) where MAME simplifies |
| **MAME wrong** | 5 | MAME's behavior is demonstrably not the board's (§3); unreal-ng has the right one |
| **partial** | 13 | unreal-ng has a part of what MAME offers |
| **missing** | 15 | MAME has it, unreal-ng does not |
| **both missing** | 4 | neither emulator has it |
| **open** | 4 | the two differ and nobody knows yet which is right |

The core machine (CPU, PLD port table, memory, video, floppy, IDE, keyboard, CMOS) is at MAME's level
or above. What MAME has and unreal-ng lacks is concentrated in four places: **sound** (Covox,
Covox-Blaster, the 16-bit DAC), **expansion slots** (the ISA I/O window, the ZX-bus adapter and the
NeoGS card that the owner's own MAME setup uses), **save states** (MAME has them; unreal-ng refuses
TTD until S7) and **extras** (extended joystick pads, serial mouse variants, CD audio, CHD images,
the Game PLD configuration).

Two findings change earlier statements:

- **MAME also ends the frame INT at the acknowledge.** `irqack_cb` clears all three INT requests
  (`sprinter.cpp:1962-1964`), and the Z80 core calls it on every acknowledge (`z80.lst:958`).
  [roadmap-and-plan.md](roadmap-and-plan.md) §6 and §6.1 say "MAME keeps the line for the full 32 T";
  that holds only for an INT nobody acknowledges. The two emulators agree; see §3 item 3 for the
  bug this causes in MAME.
- **The owner's `_306.bat` does not run on MAME 0.289**: with `-ata2:0 cdrom` the NeoGS SD card
  becomes `harddisk3`, so `-hard4 neogs.chd` stops with `Error: unknown option: -hard4`
  (checked on the local build). On 0.289 the card is `-hard3`.

## 1. What the owner uses in practice (MAME pack)

`_306.bat` (MAME 0.277):

| Option | Meaning |
|---|---|
| `-bios v3.06` | BIOS 3.06, MAME's image `sp2k-3.06.rom` (CRC `187f4382`) |
| `-beta:wd179x:0 525qd`, `-beta:wd179x:1 35hd`, `-flop2 dss171u.img` | drive A 5.25" 80-track, drive B 3.5" HD with DSS 1.71 |
| `-hard1 sp_hdd_sys.chd`, `-hard2 sp_hdd_media.chd` | primary master: system disk; second disk (with `-ata2:0 cdrom` it lands on the secondary slave) |
| `-ata2:0 cdrom -cdrom SprinterCD.iso` | an ATAPI CD on the secondary master, plus MAME's default CD on the primary slave |
| `-isa0 zxbus_adapter -isa0:zxbus_adapter:card neogs -hard4 neogs.chd` | ISA slot 0: the ZX-bus adapter with a **NeoGS** card and its SD card |
| `-nomouse` | the host mouse is not captured by default (`cfg/sprinter.cfg` maps the serial mouse axes for when it is) |

`cfg/sprinter.cfg` enables the emulated Microsoft Natural keyboard (`:kbd:ms_naturl`) and stores the
`TURBO` toggle. So in practice the owner runs **BIOS 3.06, DSS 1.71 from a CHD hard disk, a CD, and a
NeoGS** - three of these four are gaps (§4 items 3, 6, 7).

The pack's `roms/kb_ms_natural.zip` makes the keyboard work on the local 0.289 build too (a 3-second
headless run with `-rompath` pointing at the pack's `roms/` folder starts without `-kbd ""`), which
removes the blocker named in [s4-input-outcome.md](s4-input-outcome.md) ("MAME reference: not used").

## 2. Feature by feature

Phases: **S5** (accelerator, in flight), **S7-TTD** (next), **Audit** (automation and documentation
completeness, after S7-TTD), **S6** (sound), **S7** (Qt docks, ATAPI CD wiring, native snapshot, docs),
**Deferred** (owner decisions: BIOS flash, Game / DooM / Video configurations, renderer speed),
**new** = proposed in §4.

### 2.1 CPU and the Z84C15's on-chip devices

| # | Feature | MAME | unreal-ng | Status | Phase |
|---|---|---|---|---|---|
| C1 | CPU core | generic Z80 core under `z84c015_device` (`z84c015.cpp:23-27`) | own CMOS Z84C00 core, `core/src/3rdparty/z84c15/z84cpu.cpp` | **ours better** | done |
| C2 | 3.5 / 21 MHz (x6) | `set_clock_scale(6)`, `:384-388` | `hw_turbo_ratio = 6`, `core/src/emulator/ports/models/portdecoder_sprinter.cpp:436-460` | **equal** | done |
| C3 | Turbo bit (SYS/CNF write with data bit 1 = 1: bit 0 = turbo) | `:864-871` | `portdecoder_sprinter.cpp:836-843` | **equal** | done |
| C4 | Front-panel turbo key F12 | a host input `TURBO` (`:1941-1942`, `:1767-1771`), outside the keyboard | a bare F12 in the PS/2 stream, as the PLD sees it (`core/src/emulator/ports/models/sprinter/sprinterinput.cpp:64-65`), plus `[SPRINTER] Turbo=1` | **ours better** | done (S4) |
| C5 | Wait states at 21 MHz: align to a 6-clock phase, then `6 - taken` (RAM 3, port 4; none for ROM, fast RAM, or at 3.5 MHz). Example: a RAM read starting at clock 100 costs 2 + 3 = 5 extra clocks | `do_mem_wait`, `:1720-1731` | `core/src/emulator/memory/sprinter/sprinterwaits.h:23-47`, `portdecoder_sprinter.cpp:462-478` | **equal** (the rule's origin is open on both sides: Flex Navigator needs >= 68 T per poll, ours gives ~59 T, [TODO.md](TODO.md)) | open TODO |
| C6 | PLD wait on writes to Z84C15 ports | the write tap reaches `dcp_w` (`:1445-1458`); no wait measured (roadmap §6.1) | adds the port wait | **open** | Audit |
| C7 | Z84C15 wait generator (WCR / MWBR) | registers stored, no waits (`z84c015.cpp:99-103`, `:123-133`) | `core/src/3rdparty/z84c15/z84waits.cpp:20-60`; the PLD loader takes 142 T per bitstream byte, MAME 113 T | **ours better** | done |
| C8 | Chip selects CS0 / CS1 (the loader's fast RAM window) | `z84c015.cpp:55-62`, `sprinter.cpp:1139-1166` | `core/src/3rdparty/z84c15/z84c15.h:220-229`, `core/src/emulator/memory/sprinter/sprintermemory.cpp:187` | **equal** | done |
| C9 | Watchdog | `tmpz84c015.cpp:171-200`; /WDTOUT not wired in the driver | `z84c15.h:261-263`; not wired (`portdecoder_sprinter.cpp:50-52`) | **equal** | done |
| C10 | On-chip daisy chain, `#F4` priority | `tmpz84c015.cpp:143-164` | `z84c15.h:265-272` | **equal** | done |
| C11 | CTC inputs and outputs | TRG0-2 = 875 kHz (42 MHz / 48, `:1993-1995`), ZC0 clocks SIO B (`:2006-2007`), ZC2 feeds TRG3 (`:2008`); the CTC clock is derived from the scaled CPU clock (`tmpz84c015.cpp:249`, `device.cpp:398-410`) | counter mode on the selected TRG edge, timer trigger start, cascade; TRG0-2 875 kHz in real time, ZC/TO2 -> TRG3, ZC/TO0 -> SIO B (`core/src/3rdparty/z84c15/z84ctc.cpp`, `portdecoder_sprinter.cpp` constructor); timers count the CPU clock (3.5 / 21 MHz) as MAME. Bad Apple and dontBlink play (`SprinterCtcDemo_Test`, 2026-10-02) | **equal** | done (branch `sprinter-ctc-trg`) |
| C12 | SIO A (keyboard) | clocked by the keyboard's clock edges (`:1988-1991`); RR0 reads `#7C` | a byte at the end of its 11-bit frame; RR0 `#04` (roadmap §6.1). The BIOS reads bit 0 only | **partial** | Audit |
| C13 | PIO | port A input = joystick 2, port B bit 7 = joystick 2 select (`:1997-1998`, `:1338-1344`) | register file only | **partial** | new (input extras, with I10) |
| C14 | INT vector `#FF` | `:1961` | `core/src/emulator/video/sprinter/sprinterintsource.cpp:117` | **equal** | done |

### 2.2 Interrupts and frame

| # | Feature | MAME | unreal-ng | Status | Phase |
|---|---|---|---|---|---|
| T1 | Frame: 896 x 320 at 14 MHz = 20.48 ms (48.83 Hz), 224 T per line | `:180-190`, `:1981` | `core/src/emulator/config.cpp:1385-1386`, `core/src/emulator/video/sprinter/screensprinter.cpp:75-99` | **equal** | done |
| T2 | 312-line frame (codes `#2C` / `#2D`) | `:775-777`, `:390-395` | `portdecoder_sprinter.cpp:768-774` | **equal** | done |
| T3 | Frame INT positions from the mode table (`#FD` mode bytes) | `update_int`, `:1278-1313`: at the beam column `scr_a = a + 6` of the first square after the INT run | the same squares (`sprinterintsource.cpp:41-66`), the edge where the PLD has it: `CT5` rising, 2 T into that square's period (`VIDEO2.TDF` `INTT`, `SP2_ACEX.TDF:744`), **10 T before MAME's place** (`int.csv` positions less 10 T); INT to the first Spectrum cell 17 990 T (Pentagon 17 988; MAME 17 980) | **MAME wrong** (10 T late; corrected 2026-10-03, research-zx-mode §7.1) | done |
| T4 | INT ends at the acknowledge | `irqack_cb` clears requests 0-2 (`:1962-1964`) | `sprinterintsource.cpp:117` | **equal** (corrects roadmap §6.1) | done |
| T5 | Length of an INT nobody acknowledges | 32 T; frame and keyboard share one timer (`:1715`, `:1736`, `:1742-1746`) | 32 T frame pulse; PLD: 32-64 T | **open** | Audit |
| T6 | An acknowledged on-chip interrupt (CTC / SIO / PIO) | also clears the PLD's frame, keyboard and Covox-Blaster requests (§3 item 3) | the PLD INT sits behind the chip's daisy chain (`portdecoder_sprinter.cpp:368-373`) | **MAME wrong** | done |
| T7 | Keyboard INT (ALL_MODE `& #09 == #09`) | per 11 keyboard clock edges, 32 T pulse (`:1706-1718`) | per received byte, latched until the acknowledge (`sprinterintsource.h:42-46`; PLD `KBD.TDF`) | **ours better** | done (S4) |
| T9 | ZX-mode timing per launcher mode (frame, clock, INT position, INT count, 21 MHz loop speed) | zxtime, `-bios v3.06`, 2026-10-02 ([tdd-zx-mode.md](tdd-zx-mode.md) §4.1) | the same run: every number equal, the mode tables byte-identical, the 128 menu picture pixel-identical | **equal** | done (S8 Z1) |
| T10 | "Original waits" (ALL_MODE bit 2 = 0, PLD `WAIT_ORIG`, ORIGIN.ZX) | none (zxtime: 0 extra T per screen read) | `SprinterOrigWaits`: 4-T CT5 period, windows 1 and 3 with `#7FFD` bit 2, phase derived from the PLD: 0, 2, 1, 0 T by T1 from INT (zxtime: 1.000 T per read every 13 T) | **MAME missing** | done (S8 Z3; phase Q1 closed 2026-10-03) |
| T8 | Covox-Blaster INT every 128 samples | `:1760-1764`, request 2 | `CovoxBlaster` (CNT bit 6 falling, PLD `CBL_INT`) through `SprinterIntSource`, vector `#FF` | **equal** | done (S6) |

### 2.3 PLD port table and codes

| # | Feature | MAME | unreal-ng | Status | Phase |
|---|---|---|---|---|---|
| P1 | Lookup in RAM page `#40` (map, PN5, /DOS, /WR, 9 address bits) | `:584`, `:706` | `core/src/emulator/ports/models/sprinter/sprinterporttable.h`, `portdecoder_sprinter.cpp:498-509`; the first 9 989 BIOS accesses identical in port, value, PC and code | **equal** | done |
| P2 | Start-up gate (window 3 = page `#40`, writes ignored until the first IN) | `:572`, `:688-689`, `:367` | `portdecoder_sprinter.cpp:573-580`, `:624`; `sprintermemory.cpp:110` | **equal** | done |
| P3 | Fixed decodes `#3C/#7C`, `#5C`, `#FB/#7B` | `:576-580`, `:691-703` | `portdecoder_sprinter.cpp:581-586`, `:627-639` | **equal** | done |
| P4 | Cells `#C0-#FF`, CNF "clean" rules, page-3 cell | `:708-709`, `:829-905` | `portdecoder_sprinter.cpp:645-646`, `:798-879` | **equal** | done |
| P5 | `#1F` operand rewrite (`IN A,(#1F)` from RAM reaches the table as `#0F`) | at the operand read, `:1009-1015`, `:1323` | at the I/O cycle, `portdecoder_sprinter.cpp:528-547` | **equal** (same result) | done |
| P6 | TR-DOS signal from the M1 address (`#3Dxx` on, `>= #4000` off) | `:1376-1401` | `portdecoder_sprinter.cpp:411-428` | **equal** | done |
| P7 | Code `#1B`: ISA A19-A14 (RESET and AEN bits ignored) | `:736-746` | `portdecoder_sprinter.cpp:752-754` | **equal** | done |
| P8 | Code `#2E`: reload the PLD | `:779-783` | `portdecoder_sprinter.cpp:775-777` | **equal** | done |
| P9 | Code `#89` read-back | `:661-663` | `portdecoder_sprinter.cpp:710-711` | **equal** | done |
| P10 | Code `#29` (IDE drive address, PC `#3F7`) read | the ATA device's CS1 register 7 (`:631-633`) | `#FF` (`core/src/emulator/io/ide/ideadapter.cpp:466-469`, [tdd-storage.md](tdd-storage.md) §3.4) | **open** | Audit (capture 5.11) |

### 2.4 Memory

| # | Feature | MAME | unreal-ng | Status | Phase |
|---|---|---|---|---|---|
| M1 | RAM | default 64 MB (`:1953`), but page cells are 8 bits, so only 4 MB is reachable | 4 MB (`core/src/emulator/config.h:70`) | **equal** | done |
| M2 | Window 0 (system ROM, fast RAM, vROM, system RAM) | `:320-358` | `sprintermemory.cpp:85-103`, tested over all 256 combinations against a transcription | **equal** | done |
| M3 | Windows 1-3, the `#7FFD` / `#1FFD` page-3 cell | `:360-379`, `:366` | `portdecoder_sprinter.cpp:511-516` | **equal** | done |
| M4 | Fast RAM 64 KB, no wait states | `:332-334`, `:1028` | `portdecoder_sprinter.cpp:471-478` | **equal** | done |
| M5 | Graphics pages `#50-#5F` (line from PORT_Y, `#FF` transparent, video-only) | `:1175-1205` | `sprintermemory.cpp:185`, `:247-261` | **equal** | done |
| M6 | Spectrum screen shadow into video RAM | `:1208-1219` | `sprintermemory.cpp:268-276` | **equal** | done |
| M7 | Reset page `#A0` (with `#1FFD = #10`) | `:1190-1191` | `sprintermemory.cpp:119-120`, `:263-264` | **equal** | done |
| M8 | ISA memory window | reads `#FF`, writes dropped (`:1256-1257`, `:1273-1274`; driver TODO `:43`) | the same (`sprintermemory.cpp:111-116`, `:185-186`, `:261-262`) | **both missing** | - |
| M9 | ISA I/O window (window 3 cell `#D4` / `#D6`): CPU A13-A0 plus `#1B`'s A19-A14 become an ISA I/O cycle. Example: with `#1B = 0`, a read of `#C0BB` is ISA port `#00BB` (a GS command port on the ZX-bus card) | `isa_r` / `isa_w`, `:1246-1276` | reads `#FF`, writes dropped | **missing** | new (S6b) |

### 2.5 Video

| # | Feature | MAME | unreal-ng | Status | Phase |
|---|---|---|---|---|---|
| V1 | Raster 736 x 288 visible, picture 640 x 256 at (48, 16) | `:180-190` | `M_SPRINTER`, `R_736_288` (roadmap §7) | **equal** | done |
| V2 | Graphics 320 x 256 colors, 640 x 16 colors (high nibble = left pixel), low-res 2 x 2 | `draw_tile`, `:430-451` | `core/src/emulator/video/sprinter/sprintervideorenderer.cpp:40-63` | **equal** | done |
| V3 | Text 40 / 80 columns, font, attributes | `draw_symbol`, `:453-497` | `sprintervideorenderer.cpp:65-90` | **equal** | done |
| V4 | Border and blank squares (pen `#400`) | `:475-479` | `sprintervideorenderer.cpp:86` | **equal** | done |
| V4a | Border write timing | `:844` `update_now()` at the port write, the new color from the CPU's I/O access on | `ScreenSprinter::CatchUpToBorderLatch`: the old color up to the I/O cycle's end, where the PLD latches `BORDER` on `/IOWR` rising (`SP2_ACEX.TDF:310-315`), IORQ + 3 T; drawn at IORQ + 4 T to match the PENTAGON picture (owner decision) | **ours better** (MAME draws it from the access; research-zx-mode §7.1) | done |
| V5 | Flash = frame counter bit 4 | `:409` | `screensprinter.cpp:37` | **equal** | done |
| V6 | 8 palettes x 256 pens, R, G, B in video RAM columns `#3E0-#3FF` | `:1238-1243`, `:1984` | `core/src/emulator/video/sprinter/sprintervideoram.h:42-65` | **equal** | done |
| V7 | HOLD (picture shift, code `#CB`) | `:850-851` | `portdecoder_sprinter.cpp:822-825` (power-on `#77` = MAME's {0, 0}) | **equal** | done |
| V8 | Mode page (RGMOD bit 0) | `:858-861`, `:549` | `portdecoder_sprinter.cpp:830-835` | **equal** | done |
| V9 | Changes during a frame | `update_now` catch-up, but pens become colors at frame end: the BIOS fade in frame 60's INT colors MAME's whole frame | each pixel gets the palette of its moment (roadmap §7, ACC-1) | **ours better** | done |
| V12 | When a written byte reaches the picture | `update_now` before the store: the byte shows from the write's moment, pixel bytes and attributes alike | the byte lands 1 T before the write cycle's end (the PLD's write slot); inside a text / Spectrum square the attribute changes at once, the font byte only from the next square (it is latched at the square's start, `VIDEO2.TDF` `LD_PIC`; [tdd-video.md](tdd-video.md) §3) | **ours better** | done (2026-10-03) |
| V10 | "Game" configuration renderer (per-square scroll in mode byte 3; "Thunder in the Deep") | `screen_update_game`, `:499-545`; recognized by the head hash `#3861CFA4` (`:1156-1163`), cell `#EE = #41` | the hash is known, no module: Standard runs with a warning (`core/src/emulator/ports/models/sprinter/sprinterpldconfiguration.h:32-36`) | **missing** | Deferred (after v1) |
| V11 | Game renderer's scroll look-back | `lookback_scroll` (`:555-568`) never changes the square it reads (§3 item 8) | - | **MAME wrong** | Deferred |
| V12 | DooM and Video configurations | none | none | **both missing** | Deferred |

### 2.6 Accelerator (unreal-ng: S5 worktree)

| # | Feature | MAME | unreal-ng | Status | Phase |
|---|---|---|---|---|---|
| A1 | Mode from an unprefixed same-register `LD r,r` (`LD C,C` fill, `LD D,D` length, `LD L,L` copy, vertical variants, `LD B,B` / `HALT` off) | `accel_control_r`, `:917-937` | `sprinteraccelerator.h:42-46`, `kDir` `:81` | **equal** | S5 |
| A2 | Logic function (AND / XOR / OR / plain) | only `#A6` / `#AE` / `#B6` / `#BE`; any other opcode keeps the old function (`:938-951`) | per the PLD: `#86` / `#8E` / `#96` alias, any other opcode resets to plain (`sprinteraccelerator.h:47-50`) | **MAME wrong** | S5 |
| A3 | Fill, copy, vertical (PORT_Y + 1), length (0 = 256) | `:1054-1096` | `sprinteraccelerator.h:51-58` | **equal** | S5 |
| A4 | Double byte (`LD H,H`) | second write at `addr ^ 1` (`:975-979`) | the same (`sprinteraccelerator.cpp:207-227`); the PLD drives the IDE high-byte latch on the other lane - both simplify | **equal** | S5 |
| A5 | Alternate buffer addressing (codes `#C7` / `#CF`, XCNT / AAGR) | `:887-893`, `:1114-1125` | `sprinteraccelerator.h:93-94` | **equal** | S5 |
| A6 | Timing: 6 clocks of 42 MHz per extra access (3 CPU clocks at 21 MHz) | `acc_tick`, `:956-997` | `sprinteraccelerator.cpp:130-146`. Example: `LD C,C : LD (IX+0),A` with length 4 adds 9 clocks | **equal** | S5 |
| A7 | INT stops new operations until the first M1 after `RETI` (PLD `ACC_BLK`) | none | `[SPRINTER] AccelIntSuspend=1` as an option; default 0 since S6 (the literal `ACC_BLK` preset and WAVPLAY, tdd-accel-sound-input §1.3) | **equal** (default) | S5, S6 |
| A8 | Accelerator writes into the Covox-Blaster page `#FD` (8- and 16-bit packing) | `:1069-1087`, every copy | copies (`ACC_DIR` bit 1) while the CBL INT is on (PLD `CBL_WR`), `PortDecoder_Sprinter::OnCblPageWrite` | **MAME simplified** | done (S6) |
| A9 | Windows that are not main RAM are skipped | `accel_mem_r` / `_w`, `:1098-1112` | the same (`sprinteraccelerator.h:55-57`) | **equal** | S5 |

### 2.7 Keyboard, mouse, joystick

| # | Feature | MAME | unreal-ng | Status | Phase |
|---|---|---|---|---|---|
| I1 | AT keyboard on SIO A | the Microsoft Natural keyboard's own MCU (low level, ROM `kb_ms_natural`, `:1987-1991`); nothing drives the host side of the lines; the Z80 SIO overwrites the newest FIFO entry on overrun | `Ps2KeyboardStream` (set 2, typematic, 16-byte buffer), `core/src/emulator/io/keyboard/ps2keyboardstream.h:45`; never held off (PLD KBD_CX = KBD_DX = GND); the Z84C15 SIO overruns as the data sheets and MAME do (newest overwritten, RR1 bit 5 when it reaches the top, latched until Error Reset; 2026-10-03, before: the new byte was dropped and RR1 set at once) | **equal** | done (S4; overrun 2026-10-03, branch `sprinter-ps2-overrun`) |
| I2 | Commands to the keyboard (LEDs, reset, typematic rate) | not wired (only keyboard to CPU) | not modeled | **both missing** | - |
| I3 | ZX matrix on code `#40` with PC key combinations (arrows = CS+5..8, Backspace = CS+0) | `kbd_fe_r`, `:1669-1704`; map `:1773-1891` | shared key event, `portdecoder_sprinter.cpp:701-702` | **equal** | done |
| I4 | Ctrl+Alt+Del; F12 turbo switch | Ctrl+Alt+Del left to the software; F12 a host key toggling the turbo once per press (`:1941-1942`, `turbo_changed`) | the PLD keyboard block (KBD.TDF) decoding the bytes on the wire: `#71` with Ctrl + Alt resets the CPU, every `#07` not after `#F0` without Shift / Ctrl / Alt toggles the switch, the typematic repeats of a held F12 too; an SIO overrun cannot reach it (`SprinterInput::OnWireByte`, 2026-10-03; before: the host key press) | **ours better** | done (S4; wire decode 2026-10-03) |
| I5 | Tape input, `#FE` bit 6 | `:1689-1694`: `data |= 0xe0; data ^= 0x40` leaves bit 6 at 0 and the cassette test can only clear it, so the bit never follows the tape; a TAP in the 128 Tape Loader never loads (2026-10-02, [research-zx-mode.md](research-zx-mode.md) §9.6) | the shared `#FE` path: a TAP loads through 48 BASIC at 3.5 MHz; the tape counts real time (`Tape::SetBaseClockTimeBase`, 2026-10-02), so at 21 MHz the ROM loader fails as on the board | **MAME wrong** | done (S8 Z2) |
| I6 | `#FE` bits 5 (beam below the picture) and 7 (Covox-Blaster half) in CBL mode | `:1696-1701` | `CovoxBlaster::ApplyFeBits` | **equal** | done (S6) |
| I7 | Serial mouse on SIO B | HLE Microsoft (default), Logitech 3-button, wheel, Mouse Systems (`:2000-2005`); baud from CTC ZC0 | Microsoft 2-button at 1 200 baud; SIO B receives only while CTC ZC/TO0 / the WR4 clock mode give 1 200 baud +-5 % (DSS 1.71: 875 kHz / 45 / 16 = 1 215), else the characters are lost (`core/src/emulator/ports/models/sprinter/sprinterinput.cpp`) | **partial** (variants) | baud done 2026-10-02; variants: new (input extras) |
| I8 | Kempston mouse view, code `#58` (`#FADF` / `#FBDF` / `#FFDF`) | its own inputs (`:644-659`, `:1894-1904`) | the same journaled counters as the serial mouse (`portdecoder_sprinter.cpp:707-708`) | **equal** | done |
| I9 | Kempston joystick bits on code `#15` | `joy_ctrl_r(1)` default state (`:605-607`) | `portdecoder_sprinter.cpp:690-696` | **equal** | done |
| I10 | Two extended pads (8 directions, A/B/C/X/Y/Z, Start, Select): pad 1 selected by SIO B DTR toggles, pad 2 read on PIO A with PIO B bit 7; the select counters reset at each frame INT | `:1330-1374`, `:1906-1938`, `:1996-1998`, `:1738` | none | **missing** | new (input extras) |

### 2.8 Floppy

| # | Feature | MAME | unreal-ng | Status | Phase |
|---|---|---|---|---|---|
| F1 | WD1793 through codes `#10-#15` | `:593-607`, `:712-726` | `portdecoder_sprinter.cpp:656-667`, `:688-696`, `:735-741` | **equal** | done (S3a) |
| F2 | Density latch (`#16` / `#17`) during a running ID search | WD clock x2 (`beta_m.cpp:196-202`), but the PLL rate is taken at the command start: BIOS 3.04's HD probe fails (`fdc-probe.csv`) | `SetLatchedClock`, the search re-runs at the new rate (`portdecoder_sprinter.cpp:669-678`, `:742-750`) | **MAME wrong** | done (S3a) |
| F3 | One density write that also re-enables the FDC | `turbo_w` runs before `enable()` (`:729-733`) and is ignored while disabled (`beta_m.cpp:198`) | both applied (`portdecoder_sprinter.cpp:747-749`) | **MAME wrong** | done |
| F4 | FDC off (density write data bit 1) | `:730-733` | `portdecoder_sprinter.cpp:748` | **equal** (unverified in the PLD on both) | done |
| F5 | Drives and drive types | 4 drives, 5.25" QD by default, 3.5" HD / DD and 5.25" HD per drive (`beta_m.cpp:35-40`, `:317-320`) | 4 slots `fdd.a`-`fdd.d`, no drive type (the image decides) | **partial** | low (media manager) |
| F6 | Image formats | MFI, TD0, IMD, DSK, IMG/IMA, IPF, TRD, SCL, ... | TRD, SCL, FDI, UDI, TD0, DSK, HFE, SCP, MGT, raw PC (`core/src/loaders/disk/`) | **equal** (different sets, Sprinter needs covered) | done |
| F7 | FDC time base at 21 MHz | separate device clock | `SetBaseClockTimeBase(true)` (`portdecoder_sprinter.cpp:82-84`) | **equal** | done |
| F8 | Floppy inserted after DSS started | not tried | DSS does not see it ([automation-outcome.md](automation-outcome.md)) | **open** | Audit (capture 5.7) |

### 2.9 IDE and CD

| # | Feature | MAME | unreal-ng | Status | Phase |
|---|---|---|---|---|---|
| H1 | Two channels, select codes `#2A` / `#2B` | `:769-774` | `ideadapter.cpp:479-483` | **equal** | done (S3b) |
| H2 | Data latch shared by both directions, A8 picks the byte | `:613-622`, `:755-760` | `ideadapter.cpp:463`, `:489-494` | **equal** | done (S3b) |
| H3 | Task file (read A8 = 0, write A8 = 1), `#28` alternate status / device control | `:623-630`, `:761-768` | `ideadapter.cpp:464-466`, `:495-502` | **equal** | done (S3b) |
| H4 | Default units | HDD + CD on the primary, two HDDs on the secondary (`:1967-1969`) | empty; `[HDD] Scheme=SPRINTER`, `ImageN` / `CDN` for 4 units | **equal** (a config choice) | done |
| H5 | Hard-disk image formats | CHD, HD, HDV, 2MG, HDI | raw, HDF, HDI, fixed VHD (`core/src/emulator/media/mediaformatregistry.cpp:265-288`); the pack's CHDs must be extracted first | **partial** | new (shared CHD) |
| H6 | ATAPI CD / DVD (data) | any unit; ISO, CUE, TOC, NRG, GDI, CHD | any unit (`CDn=1`); ISO only | **partial** | S7 |
| H7 | CD audio from the primary slave to both speakers | `cdrom_config`, `:1663-1668`, `:1968` | none | **missing** | S7 |

### 2.10 CMOS

| # | Feature | MAME | unreal-ng | Status | Phase |
|---|---|---|---|---|---|
| R1 | Clock / CMOS on codes `#1C-#1E` | DS12885 ("should be DS12887A", `:1966`), host time, nvram file | `Ds12887`, century `#32`, emulated time (repeatable for TTD), `[SPRINTER] CmosFile` (`portdecoder_sprinter.cpp:43-44`, `:137-144`) | **ours better** | done |

### 2.11 Sound

| # | Feature | MAME | unreal-ng | Status | Phase |
|---|---|---|---|---|---|
| S1 | AY clock and stereo | 42 MHz / 24 = 1.75 MHz; A left, B both at half, C right (`:2013-2017`) | one AY (`[SOUND] TurboSound=Single`), 3.5 MHz / 2 = 1.75 MHz (the `FQ=` key was never read); the shared ABC preset (B at half to both sides) | **equal** (clock), panning differs | done (S6) |
| S2 | AY read (code `#52`), write (`#90` / `#91`) | `:640-642`, `:822-827` | `portdecoder_sprinter.cpp:704-705`, `:791-796` | **equal** | done |
| S3 | Beeper | Spectrum ULA path | shared `#FE` path | **equal** | done |
| S4 | Covox (code `#88` with CBL off: both channels) | `:785-793` | `CovoxBlaster::WriteData` with CBL off | **equal** | done (S6) |
| S5 | Covox-Blaster: 256-sample ring, rates from 218.75 kHz / (n + 1) (n = 13 gives 15.6 kHz), stereo, 16-bit (high byte sign-flipped, channels swapped) | `:795-814`, `:1748-1765`, `:1752-1756`, `:1082` | `CovoxBlaster` per the PLD: write address `~A15..A8` with the INT off, rates 2-7 at 218.75 kHz, continuous output, no 16-bit swap | **MAME differs** (addressing, reserved rates, swap) | done (S6) |
| S6 | 16-bit stereo DAC (TDA1543) | two `DAC_16BIT_R2R` (`:2019-2020`) | the "Covox-Blaster" mixer row (COVOX slot), `(word - #8000) / 2` | **equal** | done (S6) |

### 2.12 Expansion slots

| # | Feature | MAME | unreal-ng | Status | Phase |
|---|---|---|---|---|---|
| Z1 | ZX-bus adapter in ISA slot 0 (the default) | `ISA8_ZXBUS` (`devices/bus/isa/zxbus_adapter.cpp:13-38`), `sprinter.cpp:1975` | none | **missing** | new (S6b) |
| Z2 | NeoGS / General Sound on the adapter (the owner's setup) | `zxbus_cards`, `devices/bus/spectrum/zxbus/bus.cpp:84-88` | the NeoGS device exists (`core/src/emulator/sound/chips/neogs/`) and `GSType=NGS` is set in the Sprinter config, but no port reaches it | **missing** | new (S6b) |
| Z3 | Nemo IDE on the adapter | `bus.cpp:86` | none (the Nemo scheme exists for other models) | **missing** | not planned |
| Z4 | PC ISA cards (AdLib, Game Blaster, Sound Blaster 1.0 / 1.5, SSI-2001, Stereo FX, MPU-401, ...) | any of `pc_isa8_cards` in either slot; I/O only, no ISA IRQ / DMA wired (`:1973-1979`) | none | **missing** | new (low) |

### 2.13 Resets, BIOS, configuration

| # | Feature | MAME | unreal-ng | Status | Phase |
|---|---|---|---|---|---|
| B1 | Cells after power-on | `port_default`, `:1533-1539` | `portdecoder_sprinter.cpp:24-29` | **equal** | done |
| B2 | PLD configuration load | 4 096 writes, then a reset (`:1157-1163`) | the whole stream, 473 720 writes, or the fast start (`portdecoder_sprinter.cpp:223-313`) | **ours better** | done |
| B3 | RESET button | MAME's soft reset keeps the configuration (`:1588-1600`) | reloads the PLD as on the board ([tdd-ports-memory.md](tdd-ports-memory.md) §7; `portdecoder_sprinter.cpp:155-165`) | **ours better** | done |
| B6 | Turbo after a CPU reset (Ctrl+Alt+Del, page `#A0`) | `m_turbo` kept across every reset | preset to 21 MHz (PLD `DCP.TDF:663`, `TB_SW.prn = /RESET`; 2026-10-02): a 3.5 MHz ZX mode returns to DSS in turbo | **MAME wrong** | done (S8) |
| B8 | ALL_MODE, RGMOD, PORT_Y after a reset | kept across every reset (`machine_reset`, `:1549-1601`) | ALL_MODE `#FF`, RGMOD and PORT_Y 0 on every `/RESET` (PLD `SP2_ACEX.TDF:1041`, `:958`, `ACCELER.TDF:204`; 2026-10-02): with BIOS 3.07 BETA 1 Flex Navigator came back from the ZX mode without its video mode | **MAME wrong** | done (S8) |
| B7 | BIOS 3.06 Hotfix 2: DSS text at the bottom line | does not scroll (HF2 in MAME's v3.06 slot) | does not scroll either; MAME's own 3.06 (2025) scrolls in both | **equal** (open BIOS question) | TODO |
| B4 | BIOS images | 2.13, 2.17, 3.00, 3.03, 3.04 (default), 3.05, 3.06 (`:2026-2050`) | 3.07 BETA 1 (default since 2026-10-02), 3.06 Hotfix 2, 3.04 (`data/rom/sprinter/`); 3.00 / 3.03 tried from other builds ([bios-versions.md](bios-versions.md)) | **partial** | Audit |
| B5 | BIOS choice at run time | `-bios v3.06` | at create (`"sprinter": {"bios": "3.06"}`) and on a running machine (`POST /sprinter/bios`, loaded at the reset) on every surface; `[ROM] SPRINTER=` the default ([automation-audit-2026-10-02.md](automation-audit-2026-10-02.md) G11) | **equal** | done |
| B6 | BIOS flash writes (updater `UP306.EXE`) | ROM region, not writable | not modeled | **both missing** | Deferred |

### 2.14 State, media, debugging

| # | Feature | MAME | unreal-ng | Status | Phase |
|---|---|---|---|---|---|
| D1 | Save states | `MACHINE_SUPPORTS_SAVE`, `:1468-1520`, `:2059` | TTD refuses to record (`portdecoder_sprinter.cpp:897-902`) | **missing** | S7-TTD |
| D2 | Spectrum media: snapshots (`.sna`, `.z80`, ...), quickload `.scr`, cassette | inherited from `spec128` (`-listmedia`); a 128K SNA loads and runs **in ZX mode** (2026-10-02, `P128.ZX`, `action.sna`: the writes go through the program space and `OUT (#7FFD)`, which the PLD routes); outside ZX mode nothing stops it; the cassette never reaches `#FE` (I5) | tape through the shared path; the snapshot loaders write physical pages 0-7 (Sprinter system pages) and nothing refuses | **partial** (ours wrong for snapshots) | S8 Z5 |
| D3 | Z84C15 system registers in the debugger | WCR, MWBR, CSBR, MCR (`z84c015.cpp:106-109`) | `state/sprinter` on WebAPI, MCP, CLI, Lua, Python ([automation-outcome.md](automation-outcome.md)) | **ours better** | done |
| D4 | Port trace | `LOGIO` lines only (`:679`, `:912`) | trace with internal codes and names (`portdecoder_sprinter.cpp:911-954`), port table decode and lookup | **ours better** | done |
| D5 | Video RAM viewer | graphics / tilemap viewer fed by `gfxdecode` and the tilemap (`:1609-1647`; the tilemap is not used for drawing) | `SprinterVideoMapper` (the sources of a pixel), screen text; on every automation surface the mode table per square, the 8 palettes, the video RAM as region `vram` (read / write / dump), the video change log, raw pens (audit G3-G6, G14); no GUI viewer yet | **partial** (GUI) | S7 |
| D6 | Front-panel LEDs (turbo, drive A / B, NeoGS) | `layout/sprinter.lay`, `m_turbo_led` (`:386`) | the status bar shows the CPU frequency | **partial** | S7 |

## 3. Where MAME is wrong or simplified

Each item names what unreal-ng does instead and the evidence.

1. **No Z84C15 wait generator.** MAME stores WCR / MWBR / CSBR / MCR (`z84c015.cpp:99-103`, `:123-133`)
   but adds no wait state from them. The BIOS loader runs with WCR = 4 (one memory wait), so MAME's loader
   takes 113 T per bitstream byte where the chip takes 142 T (`core/src/3rdparty/z84c15/z84waits.cpp:10-11`
   worked example; [TODO.md](TODO.md) CPU library entry).
2. **The 21 MHz wait rule is a model, not the board.** MAME's "align to 6, then 6 - taken"
   (`:1720-1731`) is not taken from the PLD, whose `/IO` wait counter has per-code lengths. unreal-ng
   copies MAME's rule (`sprinterwaits.h:23-47`) so traces match, but Flex Navigator suggests the board is
   slower: BIOS `WREST` needs at least 68 T per poll, the rule gives ~59 T ([TODO.md](TODO.md)). Real
   hardware has to settle this; MAME cannot.
3. **Acknowledging an on-chip interrupt drops the PLD's requests.** `take_interrupt` calls `irqack_cb`
   for every acknowledge (`z80.lst:958`), also when the vector comes from the Z84C15's CTC, SIO or PIO, and
   the driver wires that callback to clear the frame, keyboard and Covox-Blaster requests
   (`sprinter.cpp:1962-1964`). Example: a CTC interrupt taken while a keyboard INT is pending loses the
   keyboard INT. unreal-ng keeps the PLD INT behind the chip's daisy chain (`portdecoder_sprinter.cpp:368-373`).
4. **One timer for two interrupt sources.** The frame INT and the keyboard INT share
   `m_irq_off_timer`, and `irq_off` clears both (`:1715`, `:1736`, `:1742-1746`): a keyboard byte arriving
   during a frame INT restarts the frame pulse, and the frame timer cuts the keyboard pulse. The PLD holds
   the keyboard INT until the acknowledge (`KBD.TDF`, [s4-input-outcome.md](s4-input-outcome.md)), as
   unreal-ng does.
5. **Floppy density switched during a command.** MAME takes the data separator rate at the command
   start (`pll_reset`) and defers commands written while a search runs, so BIOS 3.04's HD probe (switch
   the latch while READ ADDRESS searches) never finds the ID and MAME cannot boot a 1.44 MB floppy
   ([roadmap-and-plan.md](roadmap-and-plan.md) §8, `fdc-probe.csv`). unreal-ng re-runs the search at the new
   rate.
6. **Density write while the FDC is off.** Codes `#16` / `#17` call `turbo_w` before `enable()`
   (`:729-733`), and `turbo_w` does nothing while the interface is disabled (`beta_m.cpp:198`): one write
   that turns the FDC back on and changes the density keeps the old density.
7. **Configuration load shortened.** MAME resets the CPU after 4 096 configuration writes
   (`:1157-1163`); the real stream is 473 720 writes (~1.9 s at 3.5 MHz). Programs that time the load or
   reload the PLD with `#2E` see a different machine. unreal-ng runs the whole stream (or the fast start
   by choice).
8. **Game configuration scroll look-back.** `lookback_scroll` (`:555-568`) loops over `b` and `a` but reads
   `as_mode(h, v)` with `h` and `v` fixed at the start values, so it never looks at another square. The driver
   itself says the Game rendering is "not fully discovered" (`:45`). A future Game module must not copy it.
9. **Accelerator logic function.** Only the exact opcodes `#A6`, `#AE`, `#B6`, `#BE` set it, and any other
   opcode leaves it (`:938-951`); in the PLD `#86` / `#8E` / `#96` alias AND / XOR / OR and every other fetch
   resets it to plain (`sprinteraccelerator.h:47-50`, S5 worktree). MAME also has no INT-suspend (`ACC_BLK`).
10. **Frame colors.** Pens become colors at frame end, so a palette change in the INT colors the whole frame
    (roadmap §7, ACC-1: 13 239 pixels of frame 60 differ by one fade step).
11. **Smaller ones.** RAM 64 MB by default, of which 4 MB can be addressed (`:1953`); RTC chip DS12885 instead
    of DS12887A on host time (`:1966`); ISA buses without interrupts or DMA (`:1973-1979`), so a Sound Blaster
    card could play FM but no samples; all four floppy drives are 5.25" QD by default (`beta_m.cpp:320`).
12. **Tape input stuck at 0** (I5): `kbd_fe_r` flips bit 6 after setting it, so the tape never reaches the ROM
    loader (research-zx-mode §9.6). The "original waits" (ALL_MODE bit 2) are not modeled either (T10).
13. **Turbo kept across a reset** (B6): the PLD presets its turbo bit on `/RESET`; MAME keeps `m_turbo`, so after a
    soft reset from a 3.5 MHz Spectrum mode the BIOS and DSS run at 3.5 MHz.
14. **ALL_MODE, RGMOD, PORT_Y kept across a reset** (B8): the PLD presets ALL_MODE to `#FF` and clears RGMOD and
    PORT_Y on `/RESET`; MAME keeps them, so a BIOS that reads ALL_MODE back (3.07 BETA 1) returns from a Spectrum
    mode with the accelerator off and the Spectrum screen addressing on.

## 4. Missing in unreal-ng, by priority

Effort on the repository's scale: S < 1 week, M 1-2 weeks, L 2-4 weeks.

| # | Item | Rows | Effort | Owner phase | Note |
|---|---|---|---|---|---|
| 1 | Save state: the whole machine in TTD | D1 | L | **S7-TTD** (next) | already queued; MAME's `save_item` list (`:1468-1520`) is a checklist of PLD fields |
| 2 | Covox, Covox-Blaster (ring, rates, stereo, 16-bit, INT, `#FE` bits 5 / 7), the 16-bit DAC, the accelerator path into page `#FD` | S4-S6, T8, I6, A8 | M | **S6, done** | [s6-sound-outcome.md](s6-sound-outcome.md) |
| 3 | ISA I/O window + ZX-bus adapter + NeoGS on it | M9, Z1, Z2 | M | **new S6b** (after S6) | the owner's MAME setup uses NeoGS; the NeoGS device already exists in unreal-ng, only the path is missing |
| 4 | AY at 1.75 MHz, one AY (no TurboSound FM) in the Sprinter config | S1 | S | **S6, done** | `SprinterSoundTurbo_Test` |
| 5 | BIOS images 2.13, 2.17, 3.00 / 3.03 (MAME builds), 3.05 and MAME's 3.06 (`187f4382`) from the owner's pack `roms/sprinter.zip`; BIOS choice through the API | B4, B5 | S | **Audit** | MAME's 3.06 is the image the owner's DSS 1.71 runs on |
| 6 | CD audio, CUE / CHD CD images, a CD boot check with BIOS 3.06 | H6, H7 | M | **S7** (ATAPI CD) | MAME routes CD audio from the primary slave only |
| 7 | CHD hard-disk images | H5 | M | **new** (shared media work) | today the pack's `sp_hdd_sys.chd` must go through `chdman extractraw` first |
| 8 | Extended joystick pads (two), PIO and SIO B DTR wiring | I10, C13 | S-M | **new "input extras"** | the select counters reset at each frame INT (`:1738`) |
| 9 | Serial mouse variants (Logitech 3-button, wheel); ~~mouse baud from CTC ZC0, CTC trigger inputs~~ (done 2026-10-02, branch `sprinter-ctc-trg`) | I7, C11 | S | **new "input extras"** | |
| 10 | Game configuration module (renderer with per-square scroll) | V10 | L | **Deferred** (after v1) | needs `GAME_00.ACX` analysis; do not copy `lookback_scroll` (§3 item 8) |
| 11 | GUI: video RAM viewer, front-panel LEDs | D5, D6 | S | **S7** (Qt docks) | |
| 12 | Spectrum snapshots on the Sprinter | D2 | S-M | **new** (low) | first check whether MAME's snapshots work (capture 5.10) |
| 13 | PC ISA cards (AdLib and the like) | Z4 | L | **new** (low) | I/O only, as in MAME |
| 14 | Nemo IDE on the ZX-bus adapter | Z3 | S | not planned | the Sprinter has its own IDE |

**Order.** Two suggestions for the queued order (S7-TTD, Audit, S6, S7 rest):

- **Define the S6 state before S7-TTD** (or move S6 ahead of it; S6 is S-M). The Covox-Blaster ring
  (256 x 16 bit, indices, rate phase), the pad select counters and the ISA latch are machine state. If TTD
  ships without them, the TTD format changes again with S6 and the fixture corpus is re-recorded a second
  time. Reserving the blobs in S7-TTD (as `SprinterAccelState` was made padding-free for it) avoids that.
- **S5 and S6 touch one path**: the accelerator's writes into page `#FD` (A8) feed the Covox-Blaster. Keep a
  hook in S5 (the page check) so S6 only adds the ring.

Nothing found argues for moving the Game configuration, the BIOS flash or the renderer cache earlier.

## 5. Reference captures to take with MAME

The local build `scratch/mame-sprinter/zxsp` (MAME 0.289) with
`tools/machines/sprinter/mame-capture/mame-capture.sh`. `MAME_ROMPATH` set to the owner's pack `roms/`
folder gives MAME's own BIOS set and the keyboard ROM; the script then no longer needs `-kbd ""` (it
passes it today, so a `SPC_KBD` switch is the first script change). On 0.289 the NeoGS SD card is `-hard3`
when `ata2:0` holds a CD.

| # | For | What to capture | How |
|---|---|---|---|
| 5.1 | I1, T7, C12 | keyboard bytes on SIO A: arrival time of each byte after a key press, RR0 / RR1, the keyboard INT with ALL_MODE `#09` and `#01` | new mode `kbd`: post a key at a known frame (`manager.machine.natkeyboard`), log SIO A reads and INT acknowledges with T-states |
| 5.2 | S4-S6, T8, I6 | Covox-Blaster: a small program sets `#89` (each rate, stereo, 16-bit, INT), fills the ring with `OTIR`; capture the DAC output (`-wavwrite`), the INT times and the `#FE` reads | the program from the floppy; `-sound` on with `-wavwrite` |
| 5.3 | S1 | AY pitch: a fixed tone (register 0 = `#FF`) for 2 s | `-wavwrite`; the period gives the clock |
| 5.4 | M9, Z1, Z2 | the owner's boot with NeoGS: every access to window 3 while it maps ISA I/O, with `#1B` values (which ISA ports the Sprinter's GS driver uses) | Lua tap on the ISA I/O space of `isa8_0`, BIOS 3.06, `-hard3 neogs.chd` |
| 5.5 | I10, C13 | pad protocol: reads of code `#15` and PIO A, SIO B DTR and PIO B writes, from a program that supports the pads | Lua taps on the I/O space; the program is to be found (INC `SP2000.inc:382-399` names the protocol) |
| 5.6 | H6, H7 | `SprinterCD.iso` on the primary slave with BIOS 3.06: the ATAPI packet commands, whether the BIOS boots or mounts it, CD audio start | port trace of codes `#20-#29`; `-wavwrite` for the audio |
| 5.7 | F8 | a floppy inserted into A after DSS 1.62 reached `B:\>`, then `DIR A:` | Lua `image:load()` at a frame after the prompt; screen and FDC trace |
| 5.8 | A2, A6 | accelerator with the aliased opcodes (`#86` after `LD C,C`) and the time of a 256-byte fill at 21 MHz | extends the `acctest.exe` capture of the S5 worktree (`mame-acctest-306.png`) |
| 5.9 | V10 | the Game bitstream (`GAME_00.ACX`) loaded through `#2E`: the head hash, cell `#EE`, a few frames of its renderer | needs a program that loads it; frames are a reference, not a truth (§3 item 8) |
| 5.10 | D2 | `-snapshot` of a 128K `.sna` in MAME's Sprinter: does it run | one run; decides whether item 12 of §4 is worth it |
| 5.11 | P10 | reads of code `#29` (and the device register `#26`) with four units under BIOS 3.06 | port trace with `SPC_CODES` |

Things MAME cannot answer (no PLD model, or MAME is the model): the wait rule's origin (C5, C6), the
unacknowledged INT length (T5), the configuration end (CONF_DONE). They need the PLD sources or a real
board.
