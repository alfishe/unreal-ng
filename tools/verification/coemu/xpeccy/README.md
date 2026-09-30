# coemu runner: Xpeccy (upstream), headless

Runs a test program (by default the contention probe,
[`../../contention/ctprobe`](../../contention/ctprobe/README.md)) on upstream Xpeccy
(`github.com/samstyle/Xpeccy`, not its fork [xpeccy-plus](../xpeccy-plus/README.md)) without its GUI, following
the [co-emulation contract](../README.md).

```
XPECCY_UPSTREAM_DIR=<your Xpeccy checkout> ./run.sh            # every machine
XPECCY_UPSTREAM_DIR=<your Xpeccy checkout> ./run.sh 48k plus3  # these
```

Needs CMake, Ninja, a C compiler and zlib. Results go to `../out/xpeccy/`. Upstream ships only one ROM
(`conf/1982.rom`), so the ROM files come from `XPECCY_ROMS` (default: this repository's `data/rom`).

## How upstream defines a machine

Upstream has no machine definition files. A machine is a **profile**: a new profile starts from the template
`conf/xpeccy.conf` (ZX 48K hardware, 48 KB, romset `ZX48`, reset to 48 BASIC) on top of the defaults of
`compCreate()`. The user then picks, in the Setup window, the hardware, the romset (the "preset" button fills
in upstream's ROM file names for that hardware), the memory, the disk interface and the screen geometry.
Upstream ships three geometries: `default` and `Pentagon` (448 x 320 dots, 71680 ticks per frame) and
`Scorpion` (448 x 312, 69888 ticks).

Its defaults switch contention off: contended memory, contended I/O, early timing, 4T border and Even M1
are all off, and the contention pattern is "ULA type A". Choosing the ZX 48K or a Spectrum +2/+3 hardware
does not change them.

The harness builds each profile the way `xcore/profiles.cpp` loads one (`prf_load_conf`, `prfSetRomset`,
`prfSetLayout`). Your own settings in `~/.config` are not read.

## Machines and settings

The settings a user has to choose are chosen for the machine. For the Sinclair machines, contention is
switched on as a user who wants an accurate machine would switch it on: contended memory and I/O, the
matching pattern, and early timing. Every other setting keeps its default: 3.5 MHz, 4T border off, and for the
clones contended memory and I/O off. For the Scorpion, "Even M1" (`scrp.wait`) stays off.

| Machine | Hardware | ROMs (in `XPECCY_ROMS`) | Geometry | Contention | Disk | How it starts |
|:--|:--|:--|:--|:--|:--|:--|
| `48k` | `ZX48K` | `1982.rom` (upstream's `ZX48` romset) | 448 x 312, INT at line 8 dot 120 | mem + I/O, type A, early | - | `LOAD ""` in 48 BASIC, then Play |
| `128k` | `Spectrum +2` | `128.rom` | 456 x 311, INT at 8 / 148 | mem + I/O, type A, early | - | the 128 menu's first item (Tape Loader), then Play |
| `plus2` | `Spectrum +2` | `plus2.rom` | as the 128K | as the 128K | - | as the 128K |
| `plus2a` | `Spectrum +2` | `plus2a.rom` (preset) | 456 x 311, INT at 1 / 118 | mem + I/O, type B, early | - | the +2A menu's first item (Loader), then Play |
| `plus3` | `Spectrum +3` | `plus341.rom` (preset `plus3-41.rom`) | as the +2A | as the +2A | +3DOS | as the +2A |
| `pentagon` | `Pentagon` | `128.rom` at 0, `trdos503.rom` at 48K (preset) | shipped `Pentagon` | off | Beta Disk | `.trd` in A:, `RANDOMIZE USR 15619: REM: RUN` typed in 48 BASIC |
| `scorpion` | `Scorpion` | `scorpion.rom` (preset) | shipped `Scorpion` | off; Even M1 off | Beta Disk | as the Pentagon |
| `profscorp` | `Scorpion` | the first 256 KB of `scorp_prof401.rom` (ProfROM 4.01) | shipped `Scorpion` | off; Even M1 off | Beta Disk | as the Pentagon |
| `atm710` | `ATM2` | `atm2.rom` | shipped `Pentagon` | off | Beta Disk | the ATM BIOS menu: DOWN x4, ENTER (Turbo OFF), UP x3, ENTER (TR-DOS 48 boots the disk) |
| `atm3` | `PentEvo` | `zxevo.rom` (preset) | shipped `Pentagon` | off | Beta Disk | the EVO Reset Service menu: W twice (its stored 7.0 MHz to 3.5), Y (virtual drive A to B, so that A: is the floppy), S (EVO-DOS), `RUN` |
| `profi` | `Profi` | `profi.rom` (preset `PROFI-P.ROM`) | `default` | off | Beta Disk | as the Pentagon |

Notes on the choices:

- Upstream has no 128K or grey +2 hardware. `Spectrum +2` is its only 128K-class Sinclair hardware (it also
  decodes port `#1FFD`, as a +2A does), so the 128K and the +2 use it with their own ROMs.
- Upstream ships no Sinclair geometry. The three used here are the ones xpeccy-plus ships for the same video
  engine (`ULA.48`, `ULA.128`, `ULA.Plus3`).
- ATM Turbo 2+: upstream's preset ROM is `xbios135.rom`. Its 128 menu did not react to typed keys in this
  harness, so the runner uses `atm2.rom`, the ATM ROM unreal-ng and xpeccy-plus use.
- The ATM and the Profi have no geometry of their own upstream, so their frame is 71680 ticks, not the 69888
  of the real machines. The probe's checks on these machines do not depend on the frame length.
- The Pentagon preset expects a 32 KB `pentagon.rom` (128 and 48 BASIC). This repository's `pentagon.rom` is
  64 KB in another order, so `128.rom` (the same two banks) is used.

## How it works

- `ctharness.c` compiles the Xpeccy emulation core (`src/libxpeccy`) straight from the checkout,
  unmodified, with the upstream flags (`-std=gnu99 -O2`, `WORDS_LITTLE_ENDIAN`, `HAVEZLIB`). Qt is not used.
- It mounts the image with the core's own loaders (`loadTAP`, or `loadTRD` into drive A), resets, waits for
  the ROM, and types the keys (each held 4 frames). A key press carries the ZX key and the codes upstream's
  key map gives it (`xcore/keymap.cpp`), as the GUI sends them. A tape then plays in real time through the
  ROM loader (no trap, no fast load); a disk is read through the emulated floppy controller.
- It runs frames until the probe's `DONE` byte is 1, then dumps `START`..`PROBEEND`. If the CPU reaches
  `#0000` after the program has started, the program has crashed (exit 3, reported as `error`). If `DONE`
  never becomes 1, the machine is reported as `error`.
- Options for trying other settings by hand (not used by `run.sh`): `evenm1=0|1`, `early=0|1`,
  `rom=<file>`, `settle=<frames>`, `keys=none|run|basic|k1,k2,...`, `hold=<frames>`:
  `build/ctharness <machine> <romdir> <tap|trd> <sym> <outprefix> [maxframes] [option...]`.

## Results (Xpeccy 0.6.20260708, commit 3a311278, 2026-09-29)

| Machine | Result |
|:--|:--|
| 48K | 501 values wrong in 33 checks. Memory waits come 1 tick early (M1, D and N checks). Port waits come 1 tick late (P-01, P-03, P-04). The floating bus read of port `#FF` (P-02) does not match. |
| 128K, +2 | 575 values wrong in 40 checks: as the 48K, plus the paging checks (M1-P) and P-05 |
| +2A, +3 | 467 values wrong in 33 checks: the gate array's waits come 2 ticks late (M1, D, N), plus port and floating bus checks |
| Pentagon | all as expected |
| Scorpion | all as expected. The probe finds no attribute value on port `#FF` ("no contention"; a real Scorpion is "no contention, attr bus"), so it compares against the plain clone values |
| Scorpion + ProfROM | all as expected, same as the Scorpion |
| ATM Turbo 2+ | all as expected |
| ZX-Evo (BaseConf) | all as expected |
| Profi | all as expected |

With early timing off (its default), the Sinclair machines do better. The 48K has 355 values wrong in 22
checks, the 128K and +2 have 397 in 25, and the +2A and +3 are unchanged at 467. The memory checks M1-01..05
and D-01..03 then pass, and M1-06, D-04, the N checks (internal cycles) and the port checks still fail.

**Scorpion, "Even M1" (`scrp.wait`).** This is upstream's one Scorpion timing option. It adds a tick to every
instruction whose length is odd. Its stock value is off. With it on (`evenm1=1`, not stock), the program
crashes 8 frames after it starts, while it measures the frame: the CPU reaches `#0000`. xpeccy-plus shows
the same crash.
