# PQ-DOS hard-disk image: which programs start on PROFI-PLUS, and why the others do not

**Date:** 2026-10-04 · machine `PROFI-PLUS` (ROM BIOS Plus 0.41h1) booting the PQ-DOS 2023 HDD image
(`profi/dos/pq-dos/`, FAT16 at LBA 2048, 2 GB) · every `.COM` of `DEMOS` and `GAMES` (116 programs) was started
through an `AUTOEXEC.BAT` of `cd <dir>` + the program name, and screenshots were taken at frames 200, 500, 1000, 1500.

## Summary: is PQ-DOS compatible with Profi CP/M?

Not completely. Of six CP/M programs run on both systems (FLINES8C, WERT#, JAZZY, MAT, COLUMNS, SP), all start
correctly on a stock `PROFI` from a Kondor (Micco) CP/M floppy and five fail or print nothing under PQ-DOS (SP
runs, its logo is wrong because of the program itself). What differs, measured by the BDOS call logs:

- BDOS function 98 (CP/M Plus "parse filename") is not implemented in PQ-DOS: cause 1.
- BDOS function 9 stops at a NUL byte instead of `$`: cause 2.
- For COLUMNS the open of a missing file returns "found" under PQ-DOS (cause 4, settled: PQ-DOS's own behavior, the
  emulated disk path is fine); the cause of JAZZY is not settled (cause 3).

The README of PQ-DOS itself says "almost compatible" with CP/M, MicroDOS and MSX-DOS. A CP/M program that uses CP/M
Plus calls or strings with NUL bytes can fail under PQ-DOS; the sample here is six programs, not all CP/M software.
The findings come from running `QDOS.SYS` in the emulator; no second implementation or real hardware was available to
confirm them. Programs that fail under PQ-DOS should be run from a Micco CP/M floppy.

## Result

| Outcome | Programs |
|:--|:--|
| Normal picture or text | `DEMOS`: FIRST, YG, OLDMOVIE, SP (logo see [sp-demo](../sp-demo/README.md)); games with a graphics mode: VALLEY, WATERFAL, DEAWORLD, MOLE#, XONIX family, SUPMINER, SOKOBAN, PITON1/3/5, UDAB, CAD, LODE-RUN editors, BRKTHRU, PSWXONIX, all 10 of `VILENSKY`; the text games of `FROM1715` |
| Garbage or crash after start | FLINES, FLINES5, FLINES8C, FLINES8F, FLINES_F (Color Lines), FLINES4, WERT#, PINGVIN# (cause 1); JAZZY (cause 3, not settled) |
| Starts, but prints nothing | MAT (cause 2); COLUMNS (cause 4) |
| Refuse to start by their own message | AIRCOBRA ("NOT ENOUGH MEMORY"), MODIO ("Requires SCP 3.0"), WINMINER / WORDLIFE ("Windows not loaded"), the `TANDY` set ("Old / Incorrect DOS version"), MOVIE ("old DOS version") - PC DOS programs |

None of the rows is a video-mode or renderer error: in every case the program's pixels are in the bitmap page the
renderer shows (checked for MAT, SP, FLINES by dumping pages 4 / 6 / `#38` / `#3A`).

## How the causes were found

Each failing program was also run on a stock `PROFI` (BIOS 2.0) from a Kondor CP/M floppy built with the same
files (`FLINES8C`, `WERT#`, `JAZZY`, `MAT`, `COLUMNS`: all start correctly there), then the two systems' CP/M
BDOS calls were compared. The probe test `ProfiBoot_Test.DISABLED_RunProgram`
([`core/tests/emulator/profi_boot_test.cpp`](../../../../../core/tests/emulator/profi_boot_test.cpp)) logs them:

| Variable | Use |
|:--|:--|
| `PROFI_HDD=<image>` | attach an image as `ide0.master` (a copy: PQ-DOS writes to it); `PROFI_MODEL=PROFI-PLUS PROFI_PROGRAM=none` boots it |
| `PROFI_BOOT_FRAMES=n` | frames of the BIOS menu before the run (default 600; the HDD boot reaches its program at about frame 560) |
| `PROFI_BDOSLOG=steps` | one line per BDOS call (function, DE, caller, A / HL / DE on return) after `PROFI_BDOSLOG_MATCH=<hex>` (the program's first bytes at `#100`, as `xxd -p` prints them) or `PROFI_BDOSLOG_ENTRY=n` (the n-th entry of `#100`; PQ-DOS's own shell also enters `#100`) |
| `PROFI_UNTIL_PC=hex`, `PROFI_UNTIL_HIT=n` | run until PC is hit, then the dumps below see that moment |
| `PROFI_WATCH=steps`, `PROFI_WATCH_RECT=col,row,w,h`, `PROFI_WATCH_PAGES=4,6,38,3A` | who writes the screen bytes, with PC and the paging latches |
| `PROFI_DUMP_PAGES`, `PROFI_DUMP_RAM`, `PROFI_DUMP_MEM` | video pages, all RAM, the Z80's 64K view |

## Cause 1: PQ-DOS does not implement BDOS function 98 (parse filename), the program then runs on with garbage

`FLINES*`, `WERT#` and `PINGVIN#` (the "adapted for Profi" programs) start with the CP/M Plus calls
`2D` (set error mode), `1A` (set DMA), `98` (parse a file name into an FCB), `0F` (open). On the Kondor system
(Micco CP/M) call `98` returns HL = 0 and fills the FCB at `#5C` with `FLINES8C DAT`, `0F` opens the file and the
program loads its data (`FLINES8C.DAT` into `#8000`...).

Under PQ-DOS:

```
BDOS c=98 de=170f from=0777 -> a=98 hl=98 de=170f      ; returns its own input, FCB untouched
BDOS c=0f de=8000 from=0783 -> a=ff                      ; open of a blank name fails
```

The program does not check the result and goes on with an empty data area: FLINES draws a screen full of noise
(its sprite and picture data were never loaded: `#8000..` stays 0), WERT# and PINGVIN# run into code that never
was loaded and end with random paging values (`7FFD=#CD`, `DFFD=#D5`). The file was read into memory correctly
(30208 of 30208 bytes of `WERT#.COM` equal at `#100`).

Not an emulator fault: the Z80 code of `QDOS.SYS` returns "not implemented" for function 98; the README of PQ-DOS
calls the system only "almost compatible" with CP/M. Real hardware running the same image does the same, as far
as the emulated hardware is right (the file reads in these programs use the same IDE path as every other
program that works).

## Cause 2: PQ-DOS's BDOS function 9 stops at a NUL byte

`MAT.COM` (My Chess, 1979, CP/M) prints its banner with BDOS 9; the string holds NUL bytes between the tabs
(`0D 0A ... 00 09 "*** MYCHESS PROGRAM ***"`). The screen stays black and the cursor sits in the left column,
15 lines down: only the first run of CR LF was printed. Test program (`ld de,msg / ld c,9 / call 5` with
`msg: db "AB",0,"CD$"`): PQ-DOS prints `AB`, CP/M-style output would be `ABCD`. A CP/M BDOS ends the string at `$` only.

## Cause 3 (not settled): JAZZY

`JAZZY.COM` (a Spectrum demo with a CP/M launcher, "(C) 1997 by Power of Sound Group") runs on the Kondor system and
switches to 48K mode (`7FFD=#20`, `DFFD=#20`, 3.5 MHz). Under PQ-DOS it ends in a loop at `#5B0E` that toggles
`7FFD` with values `#20/#30/#AF/#4B/#5B`: it executes data. The first deviation is not found yet; candidates are a
smaller top of memory under PQ-DOS (JAZZY sets `SP` from `(6)`) or a BDOS result differing from CP/M.

## Cause 4 (settled 2026-10-04): COLUMNS - PQ-DOS's open never reports "not found"

`COLUMNS.COM` opens `COLUMNS.RES` and `MUSIC01.STR` (drive A). Neither file exists on the image. On the Kondor system
the open returns `FF` and the program starts with its defaults (title screen). Under PQ-DOS the open returns 0
("found") and the program reads 128-byte records from nowhere into `#1516...`, ending in a loop with a blank screen.

A 560-byte CP/M `.COM` (hand-assembled, run from `C:\` of the PQ-DOS hard disk image on `PROFI-PLUS`) calls BDOS 0Fh
(open) and then 11h (search first) on a missing and on an existing file and prints A, H, L, B and the FCB bytes
`+0C..+17` after the open:

| | `NOSUCH.TXT` (not on the disk) | `QDOS.SYS` (on the disk) |
|:--|:--|:--|
| BDOS 0Fh open: A H L B | `00 00 00 00` | `00 00 00 00` |
| FCB `+0C..+17` after the open | `00 00 00 00 00 00 00 00 30 DF BC 5C` | `00 00 00 80 00 4A 00 00 30 DF BC 5C` |
| BDOS 11h search first: A | `FF` (not found) | `00` (found) |

- The emulated disk path is fine: search first reads the same directory and tells the two names apart, and the open of
  `QDOS.SYS` fills the FCB with the real size (`00 4A 00 00` = 18944 bytes at `+10`, record count `#80`).
- PQ-DOS's open (0Fh) returns A = 0 for a file that does not exist: it fills the FCB with a zero size and the current
  date and time (`30 DF BC 5C` in both rows) and creates nothing (the root still holds the same 12 files afterwards).
  Its search first is the call that answers "not found". A program that tests the result of open, as COLUMNS does,
  cannot tell that a file is missing.
- So this is a PQ-DOS difference from CP/M (open returns `FF` for a missing file), not an emulator fault. The one check
  still missing is a second PQ-DOS source (real hardware or another emulator), as for causes 1 and 2.

## Open

- JAZZY's root cause (above).
- `S_MIN'.COM`: the name holds an apostrophe, the batch file may not have started it; rerun by hand.
- A second PQ-DOS source (another emulator or a real Karabas Pro) would confirm that causes 1 and 2 are the
  system's behavior.
