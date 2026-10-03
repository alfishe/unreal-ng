# Sprinter Sp2000 — research: the ZX (Spectrum-compatible) mode

| | |
|---|---|
| **Date** | 2026-10-02 |
| **Status** | Research done; checked on MAME 0.289 with BIOS 3.06 and the owner's MAME-pack hard disk (§9). Design: [tdd-zx-mode.md](tdd-zx-mode.md). **Corrections of 2026-10-02 (Z1-Z3 build)**: §5.5 (the turbo after a reset), §7.2 (the Scorpion INT), §7.3 (the CT5 period is 4 T, the window-3 condition is `#7FFD` bit 2), §10 (what is built) |
| **Branch** | `sprinter-zxmode-design` (documents, the MAME session tool, the reference captures and one disassembly) |
| **Related** | [hardware-reference.md](hardware-reference.md) §3.3 (vROM), §5 (ALL_MODE), §10 (floppy); [bios-versions.md](bios-versions.md); [mame-gap-analysis.md](mame-gap-analysis.md); [peripherals-survey.md](peripherals-survey.md); the ISA design [2026-10-02-sprinter-isa](../2026-10-02-sprinter-isa/research.md) |

## 1. In short

The owner's picture was: "like ZX-Evo, a TRD disk image or a TAP file is taken, something is mapped
somewhere, and then read from RAM in Spectrum mode." The TRD half is right in effect but wrong in
mechanism; the TAP half does not exist on the real machine.

- **TRD and SCL: yes, from RAM, but in software.** The DSS program `SPECTRUM.EXE` (the "Spectrum
  launcher") reads the whole image file from the hard disk or floppy into RAM pages, registers those
  pages with the BIOS as a **RAM disk**, and tells the BIOS "TR-DOS drive A is RAM disk E". The
  Sprinter's own TR-DOS (version 7.0x, a rewrite of TR-DOS 5.04) looks up the drive table before every
  sector access and, for a RAM-disk drive, asks the BIOS for the sector instead of talking to the floppy
  controller. **Nothing in the logic chip (PLD) traps floppy ports.** That is the difference to ZX-Evo:
  there the hardware catches the WD1793 port accesses ("vdos") and runs a small Z80 emulation of the
  drive; on the Sprinter only programs that go through TR-DOS see the RAM disk. A game with its own
  track loader (direct `OUT` to the WD1793) reads the real floppy drive instead.
- **SCL** is unpacked by the community launcher (v2.03) into the same TRD layout in RAM. The Peters
  Plus launcher (2002) takes TRD only.
- **TAP: no software for it.** Neither launcher loads TAP, SNA or Z80 files (the community launcher
  lists "Load TAP image" and "Load SNA file" as not done, `spectrum.asm:46-47`). What the board does
  have is a **real tape input** (the `KMPS` connector, PLD `TAPE_IN` → `#FE` bit 6): a cassette player
  or a PC playing a WAV works with the ROM loader, at 3.5 MHz.
- **Snapshots: none on the real machine.** MAME can load one (its Spectrum parent's loader) once the
  machine is in ZX mode; it worked in our run (§9.5).
- **How a user starts the ZX mode:** from DSS with `SPECTRUM.EXE [mode.ZX] [image.TRD|.SCL]` (every
  BIOS), or with **ESC at the BIOS boot prompt** (community BIOS 3.06 and later only: those carry the
  Spectrum ROMs in their own flash). Back to DSS: Ctrl+Alt+Del (or RESET) when the mode was started with
  `/ret-fn`; the BIOS's reset intercept brings the launcher back, which restores the DSS screen.
- **The ZX mode itself** is the Standard PLD configuration with a different set of RAM pages and
  register values: the Spectrum ROMs are RAM pages mapped read-only ("vROM"), Spectrum RAM pages are
  ordinary RAM pages reached through the `#7FFD` / `#1FFD` cells, the screen is the PLD's Spectrum mode
  of the mode table, the frame is 320 lines (Pentagon) or 312, the INT position is Pentagon, Scorpion or
  Spectrum, and the CPU runs at 3.5 or 21 MHz. There is **no ULA-style contention**; an optional PLD
  "original waits" mode (ALL_MODE bit 2) slows screen-memory accesses on a fixed 4 T cycle (§7.3).

**MAME** runs all of this correctly where it runs the real software (TRD from RAM disk, SCL, the
reset back to DSS), and loads a snapshot in ZX mode. It gets one thing wrong: **its tape input never
toggles** (`kbd_fe_r` forces `#FE` bit 6 to 0, `sprinter.cpp:1689-1694`), so tape loading cannot work
there. It lacks the "original waits".

## 2. Glossary

| Term | Meaning |
|---|---|
| **ZX mode / Spectrum mode** | The Sprinter behaving as a ZX Spectrum 128 (Pentagon, Scorpion or "original" flavor), running Spectrum ROMs and software |
| **DSS** (Estex DSS) | The Sprinter's disk operating system (MS-DOS-like, FAT12/16) |
| **PLD** | The Altera ACEX chip that holds all the Sprinter's logic; its "configuration" is loaded from the BIOS flash at power-on |
| **Port table** (DCP) | RAM page `#40`: for every port address the PLD looks up an internal code there. The BIOS writes it; ZX-mode programs change entries |
| **Cell** | One of the PLD's 64 registers `#C0-#FF` (page numbers, `#7FFD`, `#1FFD`, ...) |
| **vROM** | A RAM page that holds a Spectrum ROM image and is mapped read-only at `#0000`. Cells `#E0-#EF` say which RAM page is which ROM |
| **vRAM** (in BIOS-TT) | The RAM pages the BIOS allocates as Spectrum RAM pages 0-7 (or 0-31, 0-15) |
| **RAM disk** | A chain of 16 KB RAM pages the BIOS manages as a disk; up to 16 (`E:` to `T:`) |
| **Drive table** (`DISK_TYPE`) | Four bytes in the BIOS system page: what TR-DOS drive A-D is (0-3 = floppy drive, 4+n = RAM disk n, `#40`+n = hard-disk partition) |
| **Launcher** | `SPECTRUM.EXE` in `C:\ZX\`: reads a `.ZX` file, loads ROMs and the image, sets up the ZX mode |
| **`.ZX` file** | A 13-line text file: name, seven or ten ROM file names, an option line (`/turbo /7FFD ...`), a palette file |
| **Reset intercept** | A BIOS feature: after Ctrl+Alt+Del or RESET, the BIOS finds a marked program in RAM and runs it instead of booting; the launcher uses it to come back |
| **TR-DOS 7.0x** | The Sprinter's TR-DOS (`SP_TRD.ROM`), source in the community `ZX-SP ROMs` tree; it knows RAM disks, hard disks and DOS-format floppies besides TR-DOS floppies |
| **Beta Disk / WD1793** | The Spectrum floppy interface standard; the Sprinter has a real WD1793 |
| **vdos** (ZX-Evo) | ZX-Evo / TS-Conf hardware that traps WD1793 port accesses and runs Z80 code to emulate a drive |
| **T, T-state** | One clock cycle of the Z80 at 3.5 MHz (0.286 µs) unless said otherwise |

## 3. How a user runs Spectrum software

### 3.1 The paths

| # | Path | What the user does | Where the data is read from | BIOS |
|---|---|---|---|---|
| A | ESC at the boot prompt | `<ESC> TO ZX-MODE` | real floppy (TR-DOS) | 3.06+ (3.04: "Spectrum ROM not installed. Use spectrum.exe") |
| B | Launcher, no image | `C:\ZX> spectrum p128.zx` | real floppy, or the TR-DOS commands of path F | any |
| C | Launcher with a TRD | `spectrum atarin.trd` (default mode from `SPECTRUM.CFG`) or `spectrum p128.zx game.trd` | the RAM disk (drive A) | any |
| D | Launcher with an SCL | `spectrum bcity.scl` | the RAM disk; unpacked to TRD layout | launcher v2.03 (needs DSS 1.71.36, so BIOS 3.06+) |
| E | Flex Navigator / FN | ENTER on a `.TRS/.TRO/.TRX` file whose extension is bound to `spectrum.exe <mode>.zx !:!\!.!` in `FN.EXT`; Flex Navigator has its own TRD runner that needs `spectrum.exe norun` first | as C | any |
| F | TR-DOS 7.0x commands | inside TR-DOS: `/HDD` (attach the hard disk), `/DIR *.trd`, `/LOAD E game.trd` (file → RAM disk E), `/RMD E` (drive → RAM disk E), `/FDD` (back to the floppy), `/SAVE E file` (RAM disk → existing file) | RAM disk or real floppy | any |
| G | Tape | in a non-turbo mode: 128 menu "Tape Loader" (or `LOAD ""`), play the tape into `KMPS` | the tape input | any |

Sources: the launchers' READMEs (`ZX\README.ENG` of 2002, `ZX\_README.TXT` of v2.03 on the MAME-pack
disk), the community launcher source `spectrum.asm`, doc.sprinter.ru "Работа с HDD и RAM-Disk через
TR-DOS" (local copy under `documentation/doc-sprinter-ru/programmirovanie-v-tr-dos-dopolnitelnye-komandy/`).

### 3.2 Worked example: a TRD from the hard disk (path C, as run on MAME, §9.2)

1. DSS prompt `C:\>`. The user types `cd \trd` and `spectrum atarin.trd`.
2. The launcher (v2.03) prints its banner, reads `C:\ZX\SPECTRUM.CFG` (mode "Default (Sprinter ZX)",
   options `/sprinter /turbo /7FFD /1FFD /ret-fn`), loads `SP_128.ROM`, `SP__48.ROM`, `SP_TRD.ROM` into
   freshly allocated RAM pages, then the image: `ATARIN.TRD` is 655 360 bytes = 40 pages of 16 KB; it
   asks the BIOS for a 40-page block, reads 16 KB per page through window 3 ("Image loading: 100%").
3. It hands the block to the BIOS as a RAM disk (`BLK_TO_RAMD`), maps RAM disk E to TR-DOS drive A
   (`RAMD_TO_DRV`), and calls `GOTO_SPECTRUM` with the options.
4. The BIOS sets the INT position and frame height (`FN_SYNC`), the Spectrum palette, patches the port
   table (an AY quirk, §6), opens the 32×24 Spectrum window, maps the Spectrum RAM pages into the
   `#7FFD` cells, copies a short stub to `#5B00` and jumps there; the stub sets ALL_MODE, `#1FFD`,
   `#7FFD` and the CNF/turbo byte and jumps to `#0000` of the BASIC 128 vROM.
5. The 128 menu titled "Sprinter" appears (TR-DOS, Hardware, 128 BASIC, Calculator, 48 BASIC,
   Options). ENTER on TR-DOS: "Sprinter TR-DOS v.7.03 (c) 2025 Sprinter Team".
6. `RUN` (key R in keyword mode) loads `boot` from drive A, which is the RAM disk: the demo runs.

At no point is the WD1793 touched (MAME had no floppy inserted).

## 4. The launcher

Two programs carry the same name and options:

| | Peters Plus `SPECTRUM.EXE` (2002, Ivan Mak) | Community `SPECTRUM.EXE` v2.03 beta (2025, Ivan Mak and Anatoliy Belyanskiy) |
|---|---|---|
| Where | DSS 1.62 floppy `ZX\`, ZXMAK2 disk `ZX\` (2 696 bytes, CRC `98FDD183`); the 2 816-byte variant in the PP launcher archive | MAME-pack disk `C:\ZX\` (3 535 bytes) |
| Source | none published; disassembled here: [docs/disasm/software/sprinter/spectrum-launcher-pp/](../../disasm/software/sprinter/spectrum-launcher-pp/README.md) | `software/spectrum-mode/spectrum-exe-tt/spectrum.asm`, `trdscl.a80` (local collection) |
| Needs | DSS 1.6x, any BIOS | DSS ≥ 1.71.36 (checked at start), BIOS with `GOTO_SPECTRUM` and `ZX_MEMORY_MANAGER` (3.06+) |
| ROM pages | fixed RAM pages `#42-#47`, set through a trick: it writes an internal code into the port-table entry of port `#0000` and does `OUT (#0000)` (`SetCellViaPortTable`) | allocated by the BIOS (`GetMem`), registered with `ZX_MEMORY_MANAGER` fn 128 |
| Spectrum RAM | physical pages 0-7 (8-15 for Scorpion): Spectrum page n = physical page n | a BIOS-allocated block; cells `#F0+n` point at it |
| Images | TRD → RAM disk E → drive A (BIOS `#93`, `#92`, `#C7`, `#CB`) | TRD and SCL → RAM disk E → drive A; with `/rmd-keep` separate RAM disks for ZX and DSS |
| Entry | its own stub at `#FF00` | BIOS `GOTO_SPECTRUM` (`bios/rom/ZX/ZX_FUNC.ASM`) |

The `.ZX` options and what each one does on the hardware:

| Option | Effect | Mechanism |
|---|---|---|
| `/turbo` | CPU at 21 MHz | CNF/SYS byte, bit 0 (turbo) |
| `/sprinter` | Sprinter ports stay reachable from Spectrum programs | port-table map choice (the CNF value) |
| `/7FFD`, `/1FFD` | the 128 / Scorpion paging ports work | CNF "clean" bits (the PLD clears `#7FFD` / `#1FFD` bits otherwise; hardware-reference §5) |
| `/mem512` | Pentagon 512 (`#7FFD` bits 6-7) | CNF bit 7; 32 Spectrum pages |
| `/lines312` | 312-line frame (69 888 T) instead of 320 (71 680 T) | PLD codes `#2C` / `#2D` via `FN_SYNC` |
| `/sc-int`, `/origin` | INT position: Scorpion, or original Spectrum; default Pentagon | `FN_SYNC` mode 1 / 3 (2 = Pentagon) |
| `/origin` (also) | "original waits" on | ALL_MODE = `#FA` instead of `#FE` (bit 2 = 0) |
| `/to-trdos` | start TR-DOS and run `boot` | the stub jumps to `#3D29` with `#7FFD` = `#10` |
| `/ret-zx`, `/ret-fn` | after Ctrl+Alt+Del: restart the Spectrum, or go back to DSS | BIOS reset intercept (`RST_CONF.CUSTOM`) |
| `/no-run` | load the ROMs and stop (used before Flex Navigator's own TRD runner) | — |
| `/load-pal` | load a 4 KB palette file (line 13) | BIOS `PIC_SET_PAL` palettes 4-7 |

The modes shipped on the MAME-pack disk: `SPECTRUM.CFG` and `SP.ZX` "Sprinter ZX" (`/sprinter /turbo
/7FFD /1FFD /ret-fn`, Sprinter ROMs), `P128.ZX` (`/7FFD /ret-fn`), `P512.ZX` (`/turbo /7FFD /mem512
/ret-fn`), `SC256.ZX` (Scorpion ROMs, `/turbo /7FFD /1FFD /sc-int /lines312 /ret-fn`), `ORIGIN.ZX`
(standard 128 and 48 ROMs, TR-DOS 5.04Em, `/7FFD /origin /lines312 /ret-fn`).

**Consequence for TR-DOS:** only `SP_TRD.ROM` (TR-DOS 7.0x) knows RAM disks. `ORIGIN.ZX` and `SC256.ZX`
use plain TR-DOS 5.04Em / Scorpion 5.04, which drive the WD1793 directly: an image given on the command
line goes into the RAM disk, but those TR-DOS versions never look there and read the real floppy.

## 5. The BIOS side

### 5.1 ESC at the prompt (3.06+)

Community BIOS images carry BASIC 128 (menu "Sprinter"), BASIC 48 and TR-DOS in ROM pages 2-4
(bios-versions.md §2). SETUP option "what to load into vROM at start": 0 = nothing (as 3.04), 1 = from
ROM when the "ZX" mark is missing, 2 = at every restart (`ZX_FUNC.ASM`, `MANAGE_ZX_PAGES` comment).
ESC then enters the same `GOTO_SPECTRUM` path with Pentagon timing unless CMOS says otherwise.

### 5.2 `GOTO_SPECTRUM` (BIOS-TT `bios/rom/ZX/ZX_FUNC.ASM`)

Input: D = start (0 BASIC 128, 1 BASIC 48, 2 TR-DOS, 4/5 TR-DOS / BASIC 48 with the 128 ports locked),
E = CNF/SYS byte (turbo, ports, 512), L / H = the vROM / vRAM block ids, B = ALL_MODE, A = INT mode,
palette and 312-line bits. Steps: `FN_SYNC` twice (INT position, frame height), the ZX palette,
`DCP_CONFIG` for the AY write port (some demos write `#C0FD` instead of `#FFFD`; the BIOS widens the
port-table entry), the text window 32×24, `SWAP_RAM_DRIVES.ZX`, RGADR and RGMOD = 0, the Spectrum RAM
pages into cells `#F0-#F7` (and `#F8-#FF` for Scorpion / 512) by writing each page number through
window 3 while `#7FFD` selects it, then the `RES128_PROG` stub at `#5B00`.

### 5.3 The memory model of the community BIOS

The BIOS function `ZX_MEMORY_MANAGER` (sub-function in B) allocates "vRAM" blocks of 3 pages (48K), 2+6
(128K), 2+30 (512K) or 2+6+8 (Scorpion 256K, avoiding the ISA pages `#D0-#DF` and the reset page `#A0`),
and holds the vROM block id. The system page records both ids; `FREE` releases them (the "ZX" mark in
page `#41` is cleared with the vROM). Example: on a 4 MB board after DSS has started, cells `#F0-#F7`
point at eight free pages from the top of RAM, not at pages 0-7 as the Peters Plus launcher made them.

### 5.4 TR-DOS ↔ BIOS

TR-DOS 7.0x reaches the BIOS by switching the ROM window at fixed addresses: `#3FF0` (`OUT
(SYS_PORT.ROM)` with the BIOS page, for API `#4x`) and `#3FF8` (API `#80-#FF`); the code continues at
the same address in the BIOS ROM, which returns the same way. Before each read/write TR-DOS calls
`GET_DISK_REDIR` (the drive table byte of the current drive):

| Drive table value | Meaning | TR-DOS path |
|---|---|---|
| 0-3 | floppy drive 0-3 | the WD1793 directly (ports `#1F`, `#3F`, `#5F`, `#7F`, `#FF`), as TR-DOS 5.04 |
| 4-19 | RAM disk 0-15 (`E:`..`T:`) | `READ_WRITE_RAMD`: the BIOS maps the sector's RAM page and copies 256 bytes |
| `#40`+n | hard-disk partition n | `/HDD`: the BIOS reads the FAT root (TR-DOS sees DOS files) |
| other | — | "disk error" |

(`TRDOS/TR_RMD_S.ASZ`, `TRDOS/TR_HDD_4.ASZ`, `bios/exp/FUNC_FOR_TRDOS.ASM`.)

### 5.5 Back to DSS

The launcher installs a reset intercept: page `#41` gets the "ZX" mark at `#FFFE` and the windows and a
return address at `#FFF0-#FFF6` (Peters Plus) or `RST_CONF.CUSTOM` (community). Ctrl+Alt+Del is a
**CPU reset from the PLD keyboard block** (the PLD stays configured, RAM is kept). The BIOS starts,
sees the intercept, and jumps back into the launcher, which either restarts the Spectrum (`/ret-zx`;
SPACE right after Ctrl+Alt+Del still goes to DSS) or restores the DSS text screen and exits with "EXIT
from Spectrum mode" (`/ret-fn`). The SPACE check works both ways (`FIRST_PREPARE`: `IN (#7FFE)`, `AND #1F`,
`CP #1E`): with `/ret-fn`, SPACE held at that moment restarts the Spectrum instead. The reset also presets the PLD's
turbo bit (`DCP.TDF:663`, `TB_SW.prn = /RESET`): the BIOS and the launcher's return run at 21 MHz whatever the
Spectrum mode ran at (MAME keeps its `m_turbo`; corrected in unreal-ng 2026-10-02). The PLD source has an NMI (Alt+F12) behind a build option `NMI_ON`,
**off** in the released configuration (`SP2_ACEX.TDF:8`, `:731-734`); there is no magic button.

## 6. Ports and memory in ZX mode

Everything below already exists in unreal-ng (S1-S3a) and equals MAME (mame-gap-analysis §2.3-2.4):

- **Window 0:** the vROM page chosen by `#7FFD` bit 4, `#1FFD` bit 1 and the TR-DOS signal (fetch from
  `#3D00-#3DFF` with BASIC 48 mapped → on; fetch at `#4000` or above → off).
- **Windows 1, 2:** Spectrum pages 5 and 2 (from the cells the BIOS set).
- **Window 3:** cell `#F0 + Spectrum page` (`#7FFD` bits 0-2, 6-7, `#1FFD` bit 4 per the CNF bits).
- **Ports:** the port table of the chosen map: `#FE` (keyboard matrix built by the PLD from the PC
  keyboard, border, beeper, tape), `#7FFD`, `#1FFD`, AY `#FFFD`/`#BFFD`, Kempston joystick `#1F`, Kempston
  mouse `#FADF`/`#FBDF`/`#FFDF`, Covox `#FB`, Beta `#1F/#3F/#5F/#7F/#FF` while TR-DOS is active.
- **Keyboard:** ALL_MODE bit 0 = 0 enables the ZX matrix (`KEYS.ena = !ALL_MODE0`, `SP2_ACEX.TDF:320`).

## 7. Screen, timing, interrupts, sound

### 7.1 Screen

ALL_MODE bit 0 = 0 turns on the Spectrum screen shadow: CPU writes to `#4000-#5AFF` (and to window 3
when it holds Spectrum page 5 or 7) are copied into video RAM in the PLD's Spectrum layout; the mode table
the BIOS writes shows a Spectrum screen with border squares (hardware-reference §6). The border color
comes from `#FE` bits 0-2 through the border squares. The picture is 256×192 doubled horizontally
inside the 640×256 picture (MAME screenshot [mame-menu-sprinter.png](../../../testdata/machines/sprinter/reference/zx-mode/mame-menu-sprinter.png)).
The mode table is not bypassed: the launcher's table (dumped 2026-10-02 from the 128 menu and the TR-DOS
prompt) has border squares `#F8` around 32 × 24 ZX-40 squares from square (4, 4), `m0` = `#30` | third << 6,
`m1` = `m2` = the cell's address low byte; the reports classify them as Spectrum squares
([tdd-video.md](tdd-video.md) §7, "Spectrum screen squares").

### 7.2 Frame and INT

| Mode | Lines | T per frame (3.5 MHz) | INT position (MAME, FN_SYNC) |
|---|---|---|---|
| Pentagon (default) | 320 | 71 680 | line 287 (T 192) |
| Scorpion (`/sc-int`) | 320 or 312 | 71 680 / 69 888 | line 271 per `int.csv`; **line 287 T 192 with launcher v2.03 on BIOS 3.06** (SC256.ZX, MAME and unreal-ng, identical mode tables; correction 2026-10-02) |
| Spectrum (`/origin`) | 312 (`/lines312`) | 69 888 | line 295 (T 192) |

(roadmap §6.1, `int.csv`; the 2026-10-02 measurements: [tdd-zx-mode.md](tdd-zx-mode.md) §4.1.) Line = 224 T. With `/turbo` every T is six CPU clocks plus the 21-MHz memory
wait rule (hardware-reference §1).

### 7.3 "Original waits" (ALL_MODE bit 2)

The PLD source (`sprinter-computer-hard/ACEX/SP2_ACEX.TDF:558-559`, the `UPDATE` build):

```
WAIT_ORIG = /MR or CT5 or ALL_MODE2 or ((!(V_RAM & A14 & A15) & !(A14 & !A15)) or TURBO)
/WAIT_ALL = DECODE./WAIT & WAIT_ROM & WAIT_ORIG
```

In words: with bit 2 = 0, turbo off, a memory access to `#4000-#7FFF`, or to window 3 while `#7FFD` bit 2 is set
(`V_RAM = PN2`, `DCP.TDF:577`: Spectrum pages 4-7, not "a Spectrum screen page"), waits while `CT5` = 0.
`CT[5..0]` is the video counter's low part (`VIDEO2.TDF:280-298`): `CT[2..0]` is a **mod-6** counter (0, 1, 2, 4, 5,
6) and `CT[5..3]` steps once per six 42 MHz clocks, so `CT5` is low for 24 clocks and high for 24: a **48-clock =
4 T period**, 56 periods per 224-T line, one per 16-pixel square, the same on every line and in the border.
(Correction 2026-10-02: the first reading took `CT[5..0]` as a plain 6-bit counter - 64 clocks, 5.33 T.)

Worked example: an `LD A,(#4000)` whose T2 (where the CPU samples /WAIT) falls on the first low T of `CT5` waits
2 T, on the second low T 1 T, on the high half 0 (0.75 T over the four phases). `LD A,(nn)` repeated every 13 T settles
on alternating 2-T and 0-T waits: 1 T per read, what the zxtime program measures in unreal-ng
(testdata/machines/sprinter/zx-timing). That is a **uniform slowdown**, not the ULA's frame-position pattern, so
timing-exact multicolor effects do not match a real Spectrum either way. It is used by `ORIGIN.ZX`. MAME does not
model it; unreal-ng does since 2026-10-02 (tdd-zx-mode §3.3, the phase relative to the frame is a placeholder until a
board is measured). Whether the released bitstream was built from this `UPDATE` sheet is **unverified** (the
BIOS-TT changelog mentions the bit as `FN_SINC` bit 3): zxtime on a board answers it.

### 7.4 Sound

AY (one chip, 1.75 MHz), beeper and tape out on `#FE` bits 4 / 3, Covox. Programs that use a General
Sound (`#BB` / `#B3`) do not reach the ZX-bus card through Spectrum ports: on the Sprinter the GS sits on
the ISA ZX-bus adapter, reached through window 3 ISA cycles (the ISA design, [research.md](../2026-10-02-sprinter-isa/research.md):
BIOS 3.04 decodes `#xxBB` to code `#32`, which nothing connects to a slot).

## 8. Tape, TAP, SCL, snapshots on the real machine

| Format | Real Sprinter | Evidence |
|---|---|---|
| TRD | launcher → RAM disk → TR-DOS 7.0x drive A; or TR-DOS `/LOAD`; or the real floppy | §3, §5.4 |
| SCL | launcher v2.03 only (`trdscl.a80` builds the TRD layout: catalog, disk info sector, files) | `changes.txt` "+ поддержка SCL" |
| TAP / TZX | no file loader; the tape input on `KMPS` (EAR → `#FE` bit 6, PLD `TAPE_IN`, `SP2_ACEX.TDF:132`, `:388`, `:787`); MIC out = border bit 3 (`:747`) | Peters Plus "buffers for joystick, tape, ..." (`peters-plus/web-2003/arhitecture.htm`); assembly guide `KMPS` (Kempston + tape) |
| SNA / Z80 | none | `spectrum.asm:45-47` (`[ ] Load TAP image`, `[ ] Load SNA file`) |
| Real 5.25"/3.5" TR-DOS floppy | yes, TR-DOS 5.04 / 7.0x on the WD1793, 720 KB latch | ACC-6 (S3a) |

At 21 MHz the ROM tape loader runs six times too fast for a tape that plays in real time: tape needs a
mode without `/turbo` (`P128.ZX` or `ORIGIN.ZX`).

## 9. Checked in MAME

MAME 0.289 subset build `zxsp` (`scratch/mame-sprinter/zxsp`), `-bios v3.06`, ROMs from the owner's MAME
pack, the pack's `sp_hdd_sys.chd` rebuilt with a `SYSTEM.BAT` that ends at the prompt instead of starting
`fn` (Flex Navigator), no floppy. Tool: [mame-zxsteps.sh](../../../tools/machines/sprinter/mame-capture/mame-zxsteps.sh)
with [mame-zxsteps.lua](../../../tools/machines/sprinter/mame-capture/mame-zxsteps.lua) (keys at fixed
frames through MAME's natural keyboard, PNGs, cassette play, snapshot load, soft reset). Captures and the
step logs: [testdata/machines/sprinter/reference/zx-mode/](../../../testdata/machines/sprinter/reference/zx-mode/README.md).

### 9.1 Typing

DSS reads the PC keyboard (`:kbd:ms_naturl`), the Spectrum mode MAME's own matrix ports (`:`), so the
session switches the natural keyboard between them (`kbdonly`). MAME's natural keyboard does not type
`:` on the PC keyboard, so paths use `cd \trd` first. DSS 1.71.57 is at its prompt by frame ~1 000.

### 9.2 TRD through the launcher (path C)

`cd \trd`, `spectrum atarin.trd` (frame 1 200): launcher v2.03, "Default (Sprinter ZX)", image loaded
(frame ~1 300); the "Sprinter" 128 menu by frame 1 400 (PLD state: CNF `#07`, ALL_MODE `#FE`, turbo on,
`#7FFD` = 7, window 3 uses cell `#F7`); ENTER → "Sprinter TR-DOS v.7.03 (c) 2025 Sprinter Team ... BETA 4Mb";
`R` ENTER (RUN) → the ATARIN demo runs from the RAM disk (frame 2 600 screenshot). **Works.**

### 9.3 SCL through the launcher (path D)

`spectrum bcity.scl`: "Image loading: 100%", TR-DOS, RUN → the disk's boot menu ("a B.CITY-3", "BCITY").
**Works.**

### 9.4 Back to DSS

MAME soft reset (the RESET button; MAME leaves Ctrl+Alt+Del to the software, mame-gap I4) in the SCL
session: the BIOS starts, the intercept returns to the launcher, the DSS screen is restored with "EXIT
from Spectrum mode" and `C:\TRD>`. **Works.**

### 9.5 Snapshot (MAME's own loader)

`spectrum p128.zx`, at the menu MAME loads `action.sna` (128K SNA) through its `snapshot` image device: the
demo runs (frame 1 700). MAME's Spectrum loader writes through the program space and `OUT (#7FFD)`, both
of which the PLD routes correctly in ZX mode (the `/7FFD` option on). Loading the same file while the
Sprinter runs DSS or the BIOS would write into DSS's pages and the port table would not route `#7FFD`:
MAME does not check (not tried, follows from the code).

### 9.6 Tape (path G)

`spectrum origin.zx` (standard 128 menu "128 / Tape Loader", no turbo), ENTER on Tape Loader, MAME
cassette `greenberet.tap` playing from frame 1 570: PC stays in the ROM edge loop `#05EB-#05FA`, the border
never changes, nothing loads by frame 14 000. **Fails: MAME bug.** `kbd_fe_r` sets bit 6 (`data |= 0xe0`),
flips it (`data ^= 0x40`, now 0), and the cassette test can only clear it (`if (input > 0.0038) data &=
~0x40`): bit 6 is 0 whatever the tape does. The Spectrum parent's code has no XOR (`spectrum.cpp:434-436`).

### 9.7 Not tried in MAME

Path A (ESC; tested on unreal-ng, bios-versions §5), the Peters Plus launcher on BIOS 3.04 (unreal-ng
ACC-6), TR-DOS `/HDD` and `/LOAD`, the "original waits" (MAME has none), Ctrl+Alt+Del (MAME's keyboard
does not reset).

## 10. What unreal-ng has

| Piece | State | Where |
|---|---|---|
| vROM window 0, TR-DOS signal, `#7FFD`/`#1FFD` cells, page-3 cell | done, equal to MAME | `portdecoder_sprinter.cpp`, `sprintermemory.cpp` (S1) |
| Spectrum screen shadow, ZX text/graphics squares, border squares | done | `screensprinter`, `SprinterMemory` (S2) |
| 320/312 lines, INT positions per `FN_SYNC` | done | `SprinterIntSource` (S1) |
| WD1793 + TR-DOS from a real floppy | done (ACC-6) | S3a |
| PC keyboard → ZX matrix, Ctrl+Alt+Del CPU reset | done | S4 (`SprinterInput`) |
| AY, beeper, Covox | done | S6 |
| BIOS 3.06 / 3.07 ESC → ZX menu | done to the menu | `SprinterBiosVersions_Test` |
| Peters Plus launcher + TR-DOS 7.01 on a floppy | done (ACC-6) | `SprinterBoot_Test.Dss162_SpectrumModeTrDosReadsATrd` |
| Community launcher v2.03, RAM disk, SCL, `/ret-fn` | **works** (2026-10-02: every launcher mode, TRD and SCL images, Ctrl+Alt+Del three times; tdd-zx-mode §4.1, §11) | `SprinterZxMode_Test`, `SprinterZxTimeModes_Test` |
| Tape input on `#FE` bit 6 | **works**: 48 BASIC `LOAD ""` loads a TAP at 3.5 MHz (P128.ZX) | `SprinterZxTimeModes_Test.Tape_LoadsAt35MhzNotInTurbo` |
| Tape time base at 21 MHz | **real time** since 2026-10-02 (`Tape::SetBaseClockTimeBase`, Sprinter only): at 21 MHz the ROM loader fails as on the board | `core/src/emulator/io/tape/tape.cpp` |
| Fast tape loading trap | would fire in ZX mode: all three 48 ROMs on the disk (`BASIC_48`, `SP__48`, `SC__48`) carry the LD-BYTES signature at `#0556` | `tapefastload.cpp:110-129` |
| "Original waits" (ALL_MODE bit 2) | **built** 2026-10-02 (4-T CT5 period, phase placeholder) | `SprinterOrigWaits` (`sprinterwaits.h`) |
| Snapshot loading | **wrong**: the SNA/Z80 loaders write physical RAM pages 0-7 (`loader_sna.cpp:657`, `memory.RAMPageAddress`), which on the Sprinter are system pages, not the Spectrum pages; nothing refuses a snapshot on the Sprinter (goals FR-51 asked for a refusal) | `core/src/loaders/snapshot/` |
| ZX-mode state for automation | `registers.all_mode.zx_screen_shadow` (automation audit row 14); the picture: `picture_mode` `spectrum` (machine report, `/state/sprinter/video`, the GUI status bar "Spectrum 256x192, screen 5", 2026-10-02) | `sprinterdevicestate.cpp`, `SprinterPicture` |

### 10.1 ZX-Evo / TS-Conf for comparison

TS-Conf has **vdos**: an access to a Beta port on a drive marked virtual swaps RAM page `#FF` into window 0,
and Z80 code preloaded there plays the drive (`2026-09-27-tsconf/hardware-spec.md` §8.2, built in
unreal-ng). That is the hardware trap the owner had in mind. The Sprinter has no counterpart: its RAM
disk is a TR-DOS feature. Pieces unreal-ng can reuse for the Sprinter's ZX mode: the WD1793 / Beta core
and the TR-DOS selection rule (already used), the tape player and its loaders, the SNA/Z80/SZX parsers,
the media manager's floppy and tape slots, the screen OCR (`SpectrumScreenHas` in the Sprinter tests).

## 11. Sources

| What | Where |
|---|---|
| Community launcher v2.03 | `software/spectrum-mode/spectrum-exe-tt/spectrum.asm`, `trdscl.a80`, `changes.txt`, `param.txt` (local collection, see [materials.md](materials.md)) |
| Peters Plus launcher | `software/spectrum-mode/zx-folder-hdd/SPECTRUM.EXE`, `README.ENG`; disassembly in this branch |
| BIOS ZX functions | [zxgit.org/Tolik-Trek/Sprinter-BIOS](https://zxgit.org/Tolik-Trek/Sprinter-BIOS) `beta` `f546c4e`: `bios/rom/ZX/ZX_FUNC.ASM`, `ZX_MENU.ASM`, `bios/exp/FUNC_FOR_TRDOS.ASM`, `FUNC_RAM_ROM_DRV.ASM`, `ZX_MEM.TXT`, `doc/перехваты ресета.txt` |
| TR-DOS 7.0x | `software/spectrum-mode/zx-sp-roms-tt/SP_TRDOS.ASM`, `TRDOS/TR_RMD_S.ASZ`, `TRDOS/TR_HDD_4.ASZ` |
| TR-DOS extra commands | [doc.sprinter.ru](https://doc.sprinter.ru/) "Дополнительные команды TR-DOS" (local copy) |
| PLD | `firmware/pld/sources/sprinter-computer-hard/ACEX/SP2_ACEX.TDF` (WAIT_ORIG, TAPE_IN, NMI_ON, ALL_MODE) |
| MAME | `src/mame/sinclair/sprinter.cpp` (`kbd_fe_r` `:1669-1704`, `update_memory` `:320-380`, machine config `:1940-2025`), `spectrum.cpp:434-436`, `:785-792` |
| Disk contents | MAME pack `sp_hdd_sys.chd` (`C:\ZX`, `C:\TRD`, `FN\FN.EXT`) |
