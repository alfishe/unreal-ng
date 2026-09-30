# coemu - running the same test program on several emulators

A co-emulation harness. It loads one ZX Spectrum test program on every emulator it can find, on each machine
they have, the way a user would. It then takes the program's results from memory and puts them in one table.
It answers "which emulator gets this right, on which machine, and where do they differ?".

The first program it runs is the contention probe,
[`../contention/ctprobe`](../contention/ctprobe/README.md).

## Quick start

```
tools/verification/coemu/run-all.sh                  # every emulator found, every machine
tools/verification/coemu/run-all.sh 48k plus3        # only these machines
tools/verification/coemu/xpeccy-plus/run.sh 48k      # one emulator
```

`run-all.sh` ends with a table like this (also written to `out/summary.md`):

| Machine | unreal-ng | xpeccy-plus | mame |
|:--|:--|:--|:--|
| 48k | ok | ok | skipped: mame not found |
| plus3 | ok | wrong: 367 values wrong in 26 checks | skipped: mame not found |

| Result | Meaning |
|:--|:--|
| `ok` | The program ran to the end and every value is as expected |
| `wrong` | The program ran to the end and some values differ; see `out/<emulator>/<machine>.compare.txt` |
| `error` | The program did not finish (timeout, build failure, a crash); see `out/<emulator>/<machine>.log` |
| `skipped` | The emulator is not installed, or has no such machine, or its runner does not do that machine yet, or the program found it cannot measure on this machine |

## The compatibility matrix

For the contention probe, `run-all.sh` also writes `out/matrix.html` (by [`matrix.py`](matrix.py)): a summary
grid of every emulator and machine and, per machine, every check of the probe by emulator. A cell shows whether
the check came out right, the whole row shifted by some ticks, or how many values differ; hovering it shows the
measured and expected rows. `matrix.py --md <file>` also writes the summary grid as Markdown, and `matrix.py
--out-dir <dir>` builds the matrix from the results of an earlier run.

Reports kept in the repository, with each difference explained: [reports/](reports/) (latest:
[2026-09-30](reports/2026-09-30-ctprobe-matrix.md), all eleven emulators on the same probe build).

## Emulators

| Folder | Emulator | How it runs | How it is found |
|:--|:--|:--|:--|
| `unreal-ng/` | unreal-ng | its test suite loads the program as a user would | `UNREAL_BUILD` (a build directory with `core-tests`), else `cmake-build-agent-release` |
| `xpeccy-plus/` | xpeccy-plus | compiles its emulation core, unmodified, into a command-line runner | `XPECCY_DIR` (an xpeccy-plus checkout) |
| `xpeccy/` | Xpeccy (upstream) | compiles its emulation core, unmodified, into a command-line runner; each machine is a profile of that hardware, settings in its README | `XPECCY_UPSTREAM_DIR` (an Xpeccy checkout); `XPECCY_ROMS` for ROMs (default `data/rom`) |
| `mame/` | MAME | headless (`-video none`), a Lua script types the loader and dumps memory; ROM sets built from unreal-ng's ROMs by CRC | `MAME_BIN`, then `mame` on `PATH`; `MAME_ROMPATH` for ROMs |
| `fuse/` | FUSE | builds a copy with a small end-of-frame hook (dump and exit) and the null UI; its own autoload | `FUSE_DIR` (a FUSE source tree) or `FUSE_BIN` (a patched binary) |
| `zesarux/` | ZEsarUX | headless (`--vo null`), one instance per machine; memory read over its ZRCP remote protocol | `ZESARUX_BIN`, then `zesarux` on `PATH` |
| `skoolkit/` | SkoolKit (Z80 contention simulator, 48K and 128K only) | loads the tape with SkoolKit's simulated `LOAD ""`, runs its contention simulator until `DONE` | `SKOOLKIT_PYTHON`, then `python3`, then the interpreter of `tap2sna.py` on `PATH` (`pip install skoolkit`) |
| `zxmak2/` | ZXMAK2 | compiles its engine, unmodified, for .NET into a command-line runner; its stock machines from `machines.config` | `ZXMAK2_DIR` (a ZXMAK2 checkout) or `ZXMAK2_BIN` (a built `zxmak2-harness.dll`); needs `dotnet` |
| `kozynax/` | Kozynax (C# descendant of ZXMAK2) | builds its own `Kozynax.Sdl` project, unmodified, as a library into a .NET command-line runner; its stock machines from `machines.config` | `KOZYNAX_DIR` (a Kozynax checkout) or `KOZYNAX_BIN` (a built `kozynax-harness.dll`); needs `dotnet` |
| `zx-m8xxx/` | ZX-M8XXX (JavaScript, in the browser) | headless Chrome opens it through a small local web server; a page drives its `window.zxDebug` automation interface and its own auto-loader, one machine at a time; ROMs from unreal-ng's `data/rom` | `M8XXX_DIR` (a ZX-M8XXX checkout); `CHROME_BIN`, else Chrome/Chromium on `PATH` or in `/Applications`; `M8XXX_ROMS` for ROMs |
| `spec-chum/` | spec_chum | headless: its loopback HTTP server `spec-chum-agent` (the runner advances frames, types the loader keys, plays the tape and reads memory over HTTP); ROMs from unreal-ng's `data/rom` | `SPEC_CHUM_BIN` (a built `spec-chum-agent`) or `SPEC_CHUM_DIR` (a spec_chum checkout, built with cargo into `build/`) |

Each folder has its own README with what it needs and how it loads the program.

The Unreal Speccy family (classic 0.39.0, the nedopc line, Unreal NS) is Windows-only; a Windows machine (or an
AI agent on one) runs it by hand with [windows-agent-unreal-speccy.md](windows-agent-unreal-speccy.md), which
leaves its results in this harness's layout so `matrix.py` can merge them.

## Machines

Every runner uses the same machine names:

| Name | Machine | How the program is loaded |
|:--|:--|:--|
| `48k` | ZX Spectrum 48K | `LOAD ""` |
| `128k` | ZX Spectrum 128K | the 128K menu's tape loader (128 BASIC: memory paging open) |
| `plus2` | ZX Spectrum +2 (grey) | as the 128K |
| `plus2a` | ZX Spectrum +2A | the +2A menu's loader (+3 BASIC) |
| `plus3` | ZX Spectrum +3 | the +3 menu's loader, from tape |
| `pentagon` | Pentagon 128 | the `.trd` through TR-DOS (`RUN`), or the tape from 128 BASIC |
| `scorpion` | Scorpion ZS-256 | as the Pentagon |
| `profscorp` | Scorpion ZS-256 with ProfROM | as the Pentagon |
| `atm710` | ATM Turbo 2+ | as the Pentagon |
| `atm3` | ZX-Evo (BaseConf) | the `.trd`: reset with SPACE held (the Evo Reset Service goes straight to TR-DOS, at its stored CPU speed, 3.5 MHz by default), then `RUN` |
| `profi` | Profi | as the Pentagon |

## Adding an emulator: the contract

Make a folder `tools/verification/coemu/<emulator>/` with a `run.sh` and a `README.md`.

**`run.sh [machine...]`** runs the program on the named machines (all of them when none are given). A runner
reports a machine it does not do as `skipped`, with the reason. Source
`../common/common.sh`, then:

1. `coemu_init <emulator> "$@"` sets these, from the environment or defaults:

   | Variable | Meaning |
   |:--|:--|
   | `PROGRAM` | The program's files without the extension: `$PROGRAM.tap`, `.trd`, `.sym`, `-compare.py` (default: the contention probe) |
   | `PROGRAM_TAP`, `PROGRAM_TRD`, `PROGRAM_SYM`, `PROGRAM_COMPARE` | Those four files |
   | `OUT` | Where results go (default `out/<emulator>/`) |
   | `MACHINES` | The machines to run |
   | `MAX_FRAMES` | Give up after this many frames (default 60000, about 20 minutes of machine time) |

2. **Find the emulator.** Use an environment variable first (`<EMULATOR>_BIN` for a program,
   `<EMULATOR>_DIR` for a source checkout), then `PATH`: `coemu_find VAR name1 name2`. If it is not there, call
   `coemu_not_found "<what to install or set>"`; that marks every machine `skipped` and exits 3.
3. **Run each machine.** Load the program as a user would (table above) and let it run. Stop when the byte
   at the program's `DONE` symbol is 1 (`coemu_sym DONE` gives its address), or after `MAX_FRAMES` frames.
   Write memory from `START` to `PROBEEND - 1` to `$OUT/<machine>.bin`. Write whatever the emulator printed
   to `$OUT/<machine>.log`, and the final screen as text to `$OUT/<machine>.screen.txt` if you can.
   Keep the emulator's stock settings for that machine, and write any setting you had to choose in the
   README.
4. **Report each machine**: `coemu_compare <machine>` after a dump (it runs `$PROGRAM_COMPARE` and records
   `ok`, `wrong`, or `skipped` when the program says it could not measure), or `coemu_result <machine> error|skipped "<why>"`.
5. `coemu_finish` exits with **0** if every machine that ran is `ok`, **1** if any is `wrong`, **2** if any
   is `error`. **3** (from `coemu_not_found`) means the emulator was not found.

Rules:
- Do not change the emulator's own files. Build products go to the runner's `build/` (ignored by git),
  results to `out/` (ignored).
- No absolute paths of your machine in committed files; everything is found through the variables above.
- The result files are one line each: `ok|wrong|error|skipped <detail>`, in `$OUT/<machine>.result`.

## The program's side

A program can be run here if it:
- loads from a `.tap` (and a `.trd` for TR-DOS machines) and runs by itself;
- sets a byte labeled `DONE` to 1 when it has finished, and keeps its results between `START` and `PROBEEND`;
- comes with a `.sym` (`NAME equ #ADDR` per line) and a `<name>-compare.py dump.bin sym` that exits 0 (all
  as expected), 1 (differences), 2 (bad dump) or 3 (the program could not measure on this machine), and prints a one-line summary last.
