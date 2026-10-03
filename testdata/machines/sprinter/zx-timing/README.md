# zxtime: how fast is a Sprinter's Spectrum mode?

`zxtime` is a small Spectrum program. You run it in the Sprinter's Spectrum mode, it measures the mode's timing
for about five seconds and prints what it found. It runs the same way on a real Sprinter, in unreal-ng and in
MAME, so the three can be compared line by line.

| File | What |
|:--|:--|
| `zxtime.trd` | A TR-DOS disk: `RUN` loads and starts the program |
| `zxtime.tap` | The same as a tape (a BASIC loader and the code) |
| `zxtime.asm` | The source (the emulator's test suite assembles it and keeps the three files above in step) |
| `zxtime.sym` | The program's labels and addresses, for a debugger |

## How to run it on a real Sprinter

1. Copy `zxtime.trd` to the hard disk, for example to `C:\TRD\`.
2. At the DSS prompt type `cd \trd` and then one of these lines (each starts one of the launcher's modes from
   `C:\ZX\`):

   ```text
   spectrum sp.zx zxtime.trd        the default mode: 21 MHz, 320 lines, Pentagon INT
   spectrum p128.zx zxtime.trd      Pentagon 128: 3.5 MHz
   spectrum p512.zx zxtime.trd      Pentagon 512: 21 MHz
   ```

   In the Spectrum menu press ENTER on **TR-DOS**, then `R` and ENTER (RUN).
3. For the two modes whose TR-DOS reads only the real floppy, write `zxtime.trd` to a floppy (or use it as drive
   A), then:

   ```text
   spectrum sc256.zx                Scorpion 256: 21 MHz, 312 lines
   spectrum origin.zx               the original Spectrum: 3.5 MHz, 312 lines, "original waits"
   ```

   ORIGIN.ZX shows the standard 128 menu: move down to **TR-DOS** (the last entry), ENTER, then `R` ENTER.
4. Wait about five seconds. The screen then shows the report and `Done.`. Take a photo of it.

## What the screen means

A report from P128.ZX (3.5 MHz) as unreal-ng and MAME print it:

```text
zxtime 1: Sprinter ZX timing

CPU    3.5 MHz
FRAME  71680 T
RATE   48.83 frames/s

INT    50 in 50 frames
REPEAT 0 came back at once

SCREEN READS, extra T x 1000
#4000        0
#C000 page 5 0
#C000 page 1 0

Done.
```

| Line | Meaning | What to expect |
|:--|:--|:--|
| `CPU` | 3.5 MHz, or "faster than 3.5 MHz" when the mode runs the CPU at 21 MHz | 3.5 MHz in P128.ZX and ORIGIN.ZX, faster in the others |
| `FRAME` | How long one picture (one frame) lasts, in T-states of a 3.5 MHz Z80 | 71680 with 320 lines, 69888 with 312 lines (`/lines312`: SC256.ZX, ORIGIN.ZX) |
| `RATE` | Frames per second (3 500 000 / FRAME) | 48.83 or 50.08 |
| `LOOP` (21 MHz only) | How many passes of the program's counting loop fit into one frame | 7164 in SP.ZX and P512.ZX, 6984 in SC256.ZX (unreal-ng and MAME) |
| `INT` | Interrupts in 50 frames | 50 |
| `REPEAT` | Interrupts that arrived again at once, while the handler had just switched interrupts on: an interrupt pulse that the CPU's acknowledge does not end | 0 |
| `SCREEN READS` | How much longer, in thousandths of a T-state, one read of screen memory takes than a read of other memory (2 000 reads, eight times) | 0 in every mode but ORIGIN.ZX; there unreal-ng shows about 1000 at `#4000` and `#C000 page 5`, 0 at `#C000 page 1` |

The screen-read lines are the reason this program exists. ORIGIN.ZX switches on the Sprinter's "original waits": the
logic chip holds the CPU for a moment when it reads or writes the Spectrum screen memory, to slow programs down
roughly the way a real Spectrum does. The chip's source code says how (a 4-T rhythm locked to the picture, research
in `docs/inprogress/2026-09-28-sprinter/research-zx-mode.md` section 7.3), but nobody has measured a real board, and
MAME does not have this at all (it prints 0).

Worked example: the program reads `#4000` every 13 T-states. With the 4-T rhythm, the reads settle into a pattern of
"wait 2 T, wait 0 T", one T per read on average: `#4000 1000`. A real board that prints about 1000 confirms the
model; a board that prints 0 has no original waits in its logic chip; any other number tells us the rhythm is
different.

## What to report

Please post the photo (or the numbers) for each mode you ran in the Sprinter Telegram chat (`zx_sprinter`), with:

- the BIOS version (the first screen after power-on shows it, e.g. "Firmware v3.06 Hotfix 2");
- the mode you started (`sp.zx`, `p128.zx`, ...).

Most useful is **ORIGIN.ZX**: its three `SCREEN READS` numbers.

If the screen stays empty for more than half a minute, the program did not start: check that you pressed ENTER on
TR-DOS and then `R` ENTER (for SC256.ZX and ORIGIN.ZX the disk must be in the floppy drive, not on the hard disk).

## For developers

- The emulator's tests run the program the same way: `SprinterZxTimeEsc_Test` (BIOS 3.06's own Spectrum mode, ESC
  at the boot prompt, no hard disk) and `SprinterZxTimeModes_Test` (every launcher mode on the owner's system disk,
  `UNREAL_SPRINTER_HDD`), in `core/tests/emulator/machines/sprinter/sprinterzxtiming_test.cpp`.
- After changing `zxtime.asm`: `UNREAL_ZXTIME_EXPORT=1 core-tests --gtest_filter=SprinterZxTimeFiles_Test.Export`
  writes the `.trd`, `.tap` and `.sym` again.
- The program lives at `#8000-#8FFF` with its interrupt table at `#9000` (I = `#90`, handler at `#9191`), its stack
  and the read block at `#A000`: all in RAM page 2, so nothing it times waits on its own code. A debugger can read the
  results from the labels in `zxtime.sym` (`FRAMET`, `INTS`, `REPEATS`, `RREF`, `R4000`, `RC5`, `RC1`, `DONE`).
- MAME runs it with `tools/machines/sprinter/mame-capture/mame-zxsteps.sh`; the comparison table is in
  `docs/inprogress/2026-09-28-sprinter/tdd-zx-mode.md` section 4.1.
