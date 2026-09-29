# ctprobe on xpeccy-plus, headless

Runs `ctprobe.tap` on the stock xpeccy-plus machines without its GUI, and dumps the probe's memory for
`../../ctprobe-compare.py`.

```
XPECCY_DIR=<your xpeccy-plus checkout> ./run.sh            # all four machines
XPECCY_DIR=<your xpeccy-plus checkout> ./run.sh zx48       # one
```

Needs CMake, Ninja, a C/C++ compiler and zlib. Output goes to `out/`: `<machine>.bin` (the memory dump),
`<machine>.log` and `<machine>.screen.txt` (the final screen as text).

## How it works

- `ctharness.c` compiles the xpeccy-plus emulation core (`src/libxpeccy`) straight from the checkout,
  unmodified, with the upstream flags (`-std=gnu99 -O2`, `WORDS_LITTLE_ENDIAN`, `HAVEZLIB`). Qt is not used.
- It builds each machine the way xpeccy-plus does for its stock definitions (`res/machines/*.conf`,
  `res/layouts.conf`); your own settings in `~/.config` are **not** read. All four stock machines use
  `earlyTiming = yes`.
- It loads the tape with the core's own loader and starts it the way xpeccy-plus's autostart does: `LOAD ""`
  typed on the 48K; the boot menu's tape loader on the 128K / +2A / +3, so the probe runs from 128 BASIC with
  paging open. The tape plays in real time through the ROM loader.
- It runs frames until the probe's `DONE` byte is 1, then dumps `START`..`PROBEEND` (addresses from
  `ctprobe.sym`).

## Results (xpeccy-plus 7a96d8da, 2026-09-28)

| Machine | Result |
|:--|:--|
| 48K | ALL VALUES AS EXPECTED |
| 128K | ALL VALUES AS EXPECTED |
| +2A, +3 | 367 values wrong in 26 checks: the gate array's waits come 2 ticks late, and the extra tick at the end of each line is missing |

If xpeccy-plus's own GUI gives different results on the same machine, compare its machine settings with the
stock ones above (layout, `earlyTiming`).
