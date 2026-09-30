# coemu runner: xpeccy-plus, headless

Runs a test program (by default the contention probe,
[`../../contention/ctprobe`](../../contention/ctprobe/README.md)) on the stock xpeccy-plus machines without its
GUI, following the [co-emulation contract](../README.md).

```
XPECCY_DIR=<your xpeccy-plus checkout> ./run.sh            # every machine it has
XPECCY_DIR=<your xpeccy-plus checkout> ./run.sh 48k plus3  # these
```

Needs CMake, Ninja, a C/C++ compiler and zlib. Results go to `../out/xpeccy-plus/`.

## Machines

Each harness machine is xpeccy-plus's stock machine of that kind (`res/machines/<id>.conf`), started the way
xpeccy-plus's own autostart (`src/xcore/autostart.cpp`) starts a tape or a disk image:

| Machine | xpeccy-plus machine | Loaded from | How it starts |
|:--|:--|:--|:--|
| `48k` | `zx48` | `.tap` | `LOAD ""` typed in 48 BASIC |
| `128k` | `zx128` | `.tap` | the 128 menu's tape loader (128 BASIC, paging open) |
| `plus2` | `zxplus2` (the 128K with the +2 ROMs) | `.tap` | as the 128K |
| `plus2a` | `zxplus2a` | `.tap` | the +2A menu's loader |
| `plus3` | `zxplus3` | `.tap` | the +3 menu's loader |
| `pentagon` | `pent` | `.trd` in drive A | reset to 48 BASIC, type `RANDOMIZE USR 15619: REM: RUN` (TR-DOS runs `boot`) |
| `scorpion` | `scorp` | `.trd` in drive A | as the Pentagon |
| `profscorp` | - | - | skipped: xpeccy-plus ships `prof39f.rom`, but no stock machine uses it |
| `atm710` | `atm2` | `.trd` in drive A | reset, then paged as a snapshot pages it (48 BASIC), then as the Pentagon |
| `atm3` | `evo-baseconf` | `.trd` in drive A | reset into the BaseConf service menu, press `S` (boot the disk) |
| `profi` | `profi` | `.trd` in drive A | as the Pentagon |

Every machine starts at its base clock (x1 of its `cpu.turbo` steps), as xpeccy-plus does. None of the ROMs
switched to a turbo clock here, so the probe's "faster than 3.5 MHz" stop did not happen.

## How it works

- `ctharness.c` compiles the xpeccy-plus emulation core (`src/libxpeccy`) straight from the checkout,
  unmodified, with the upstream flags (`-std=gnu99 -O2`, `WORDS_LITTLE_ENDIAN`, `HAVEZLIB`). Qt is not used.
- It builds each machine the way xpeccy-plus does for its stock definitions (`machines.cpp` `mac_from_def`,
  the ROM banks and text font, the layout from `res/layouts.conf`, cold RAM patterns); your own settings in
  `~/.config` are **not** read. The Sinclair machines use `earlyTiming = yes`; the clones leave it off, and the
  Scorpion has `scrp.wait = yes` (see below).
- It mounts the image with the core's own loader (`loadTAP`, or `loadTRD` into drive A) and types the keys as
  the autostart does: reset into the autostart bank, wait until the ROM scans the whole keyboard, 150 frames
  more, then each key held 4 frames with its own gap. A tape then plays in real time through the ROM loader
  (no trap); a disk is read by TR-DOS through the emulated FDC.
- It runs frames until the probe's `DONE` byte is 1, then dumps `START`..`PROBEEND` (addresses from
  `ctprobe.sym`). If the CPU reaches `#0000` after the program has started, the program has crashed: the
  harness stops (exit 3) and the machine is reported as `error`.

## Results (xpeccy-plus 7a96d8da, 2026-09-29)

| Machine | Result |
|:--|:--|
| 48K | all as expected |
| 128K, +2 | 28 values wrong in 2 checks: P-05B and P-05D (a port whose high byte points at an odd page at `#C000` does not wait) |
| +2A, +3 | 367 values wrong in 26 checks: the gate array's waits come 2 ticks late, and the extra tick at the end of each line is missing |
| Pentagon | all as expected |
| Scorpion | error: the program crashes while measuring the frame. The stock `scrp.wait = yes` adds a tick to every instruction of odd length, so the engine's exact timing misses the interrupt. With it off (not stock) the probe finishes: 10 values wrong in P-02 (the attribute bus read 2 ticks late) |
| ATM Turbo 2+ | all as expected |
| ZX-Evo (BaseConf) | all as expected |
| Profi | all as expected |

A run of all machines takes about 2.5 minutes.

If xpeccy-plus's own GUI gives different results on the same machine, compare its machine settings with the
stock ones above (layout, `earlyTiming`, `scrp.wait`).
