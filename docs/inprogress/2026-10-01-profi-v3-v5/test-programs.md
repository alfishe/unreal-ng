# Profi v3 / v5: emulated test programs

**Date:** 2026-10-02 · part of [README.md](README.md)

The wait states, the turbo switch and the floating bus were checked by running real ZX programs on the emulated
boards, the way a user would: the BIOS menu, then Sinclair 48 / Tape Loader and `LOAD ""`, or TR-DOS and `RUN`.
The probe is `ProfiBoot_Test.DISABLED_RunProgram` in `core/tests/emulator/profi_boot_test.cpp` (environment
variables `PROFI_PROGRAM`, `PROFI_MODEL`, `PROFI_TURBO`, `PROFI_CONTENTION`, `PROFI_KEYS`, ... are listed in its
comment); it saves screenshots to `scratch/profi/`.

The programs and their sources are kept with the materials outside the repository (`materials/test-programs/`,
with a README of download links and checksums). The public copies:
[Tact Meter](https://spectrumcomputing.co.uk/entry/17614/ZX-Spectrum/Tact_Meter),
[Qarx](https://spectrumcomputing.co.uk/entry/3960/ZX-Spectrum/Qarx),
[Academy](https://spectrumcomputing.co.uk/entry/5156/ZX-Spectrum/Academy),
[Shock Megademo](https://spectrumcomputing.co.uk/entry/7726/ZX-Spectrum/Shock_Megademo). SYSTEM TEST 4.30R
(CompoWellcome, 1999) and Floating Spy (RAMSOFT, 2002) come from the zx-pk Profi threads and the Wayback copy of
the RAMSOFT site.

## Tact Meter 1.0: T-states per interrupt

The program counts how many T-states a loop runs between two interrupts, with the loop in each 16K of memory.

| Board, clock | ROM | RAM #4000 | RAM #8000 | RAM #C000 |
|:--|:--|:--|:--|:--|
| v3, 3.5 MHz | 69888 | 69888 | 69888 | 69888 |
| v3, TURBO | 139622 | 86000 | 86000 | 86000 |
| v5, 3.5 MHz (video WAIT, phase 0) | 69874 | 67152 | 67152 | 67152 |
| v5, TURBO (approximation) | 115094 | 105200 | 105200 | 105216 |

The check: xpeccy-plus measured a real v3.2 board in turbo with Tact Meter: 143206 in ROM and 88208 in RAM, on a
71680 T frame. Against the frame, the board gives 1.9979 (ROM) and 1.2306 (RAM); the emulated v3 gives
139622 / 69888 = 1.9978 and 86000 / 69888 = 1.2306. The wait rule read off the schematic reproduces the
measurement to four digits.

## SYSTEM TEST 4.30R

| Page | v5, emulated | v3, emulated | A real 5.06 (solegstar, zx-pk 21644, 2013) |
|:--|:--|:--|:--|
| Ports | "Порт атрибутов не реализован" (no floating bus), Kempston, #7FFD not readable | - | "порт FF реализован (доработка)": his board has the #FF fix |
| Configuration, 3.5 MHz | video WAIT on: Takt/INT 68096, memory "с торможением" (69892-68823); WAIT off (`WaitConfig=pentagon` or `contention` off): Takt/INT 69888, 312 lines, "Profi\48K", "без торможения" | 512K found; TR-DOS 5.03 | Pentagon-type sync PROM, so SB8 most likely in its PENTAGON position: Takt/INT 73024, "без торможения" (72962-72946), INT "Too short!" |
| Configuration, TURBO | Takt/INT 114688 (1.64x of 69888), "раздельным полем памяти" (110622-115715), "TURBO - включилось!" | - | Takt/INT 122976 (1.68x of 73024), "раздельным полем памяти" (121592-124816), "TURBO - включилось!" |

The emulated v5 behaves like the photographed board where the two can be compared: no waits with the Pentagon
setup, the same memory class and a turbo gain within 3 % (the v5 turbo waits are an approximation). The test's
reading with the video WAIT on (68096, "с торможением") has no real counterpart yet: no photograph of a 5.0x board
with SB8 in its PROFI3+ position was found. The interrupt-vector and INT-length lines differ in turbo ("#FF" and
"36 Cycles" here, "???" and "Too short!" on the board); they are not modeled checks.

## Shock Megademo

Gromov: on a v5 with the WAIT the demo is "perfect"; on a Pentagon-type board (no WAIT) it runs wrong. The demo
was run on the emulated v5 with the video WAIT, on the same v5 with the `contention` feature off, and on the
emulated 48K as the reference (its contention is checked against real 48K measurements).

| Part | 48K | v5, video WAIT | v5, no waits |
|:--|:--|:--|:--|
| Opening raster (border color changed every line) | thin stripes run unbroken through border and paper; the paper area does not show | the same: the paper area does not show | the paper area shows as a rectangle of wider stripes: the timing is wrong |
| Every later part (logos, checkerboard, sprite ring, scroller) | - | runs | runs, frames differ |

So the WAIT is what makes the opening effect work, as Gromov says. Two differences from the 48K stay open: a seam
in the left border, where the line's color change becomes visible, and slanted stripes in the top lines. The seam
fits the INT position: the v5 PROM puts INT 14368 T before the paper, 32 T later than the 48K, which moves the
color change out of the horizontal blanking. A photograph of a real v5 running the demo would settle it.

## Floating Spy

On v3 the program reads #FF at its default probe time, 14347 T after INT (the 48K's attribute time). On the v3 frame
that moment is in the right border of paper line 7 (INT is 12580 T before the paper there), so #FF is what the
board gives. No real v3 measurement exists; the rule itself is checked by unit tests against the schematic.

## Qarx and Academy

Both load and run on both boards (the Qarx menu, its scroller and the game screen). Gromov's descriptions (letters
in the top border on a correct board; border and paper pictures in line on v5) have not been judged yet: the effect
has to be found in the running program.
