# coemu runner: SkoolKit

Runs the test program in [SkoolKit](https://skoolkit.ca)'s Z80 simulator with memory and I/O contention. SkoolKit
is a disassembly toolkit by Richard Dymond, not a full emulator, but its contention model is small, readable
and passes all 72 tests of Butler's 48K timing suite. That makes it a useful third opinion next to FUSE and
unreal-ng when emulators disagree on Sinclair timing.

Design and acceptance criteria: [docs/inprogress/2026-09-29-coemu-skoolkit](../../../../docs/inprogress/2026-09-29-coemu-skoolkit/requirements.md).

## What it needs

SkoolKit 10 (tested with 10.1), in any Python 3 the runner can find:

```
python3 -m venv tools/verification/coemu/skoolkit/build/venv      # build/ is ignored by git
tools/verification/coemu/skoolkit/build/venv/bin/pip install skoolkit
SKOOLKIT_PYTHON=tools/verification/coemu/skoolkit/build/venv/bin/python tools/verification/coemu/skoolkit/run.sh
```

How it is found, in this order:

| Where | Used when |
|:--|:--|
| `SKOOLKIT_PYTHON` | set: a Python interpreter that can `import skoolkit` |
| `python3` | it can `import skoolkit` (for example after `pip install --user skoolkit`) |
| `tap2sna.py` on `PATH` | the interpreter named in its first line (a virtual environment's `bin` on `PATH`) |

If none has SkoolKit, every machine is `skipped` and the runner exits 3.

## Machines

| Machine | SkoolKit machine | Loading |
|:--|:--|:--|
| `48k` | 48K | `LOAD ""`, typed and played through the ROM loader by SkoolKit's simulated LOAD |
| `128k` | 128K | the 128K menu's tape loader, the same way |
| every other name | - | `skipped`: SkoolKit simulates only the 48K and the 128K |

## How it runs

1. **Load.** `tap2sna` (SkoolKit's simulated `LOAD ""`) plays the program's `.tap` through the ROM loader and
   stops when the BASIC loader jumps to the program (`START` in the `.sym`). Loading uses SkoolKit's defaults
   (fast loading of ROM blocks); the program measures only what happens after it starts.
2. **Run.** From that state, `skoolkit-run.py` runs SkoolKit's contention simulator (the C one when SkoolKit's
   C extension is installed) 50 frames at a time with interrupts on, until the program's `DONE` byte is 1,
   then 50 frames more so the last lines are on the screen. The slices do not change the timing: all state stays
   in the simulator between them, and two runs give identical results.
3. **Results.** `out/skoolkit/<machine>.bin` (memory from `START` to `PROBEEND`), `.screen.txt` (the screen read
   back as text), `.log` (SkoolKit version, simulator, frames, seconds), then the usual `.result` and
   `.compare.txt`.

A full contention-probe run takes about 2 seconds per machine.

`skoolkit-run.py` uses SkoolKit's Python classes (`Snapshot`, `from_snapshot`, `Tracer`), the ones its
`trace.py` is built from, because `trace.py` can stop only on a time, a count or one address, not on "the
`DONE` byte became 1". These classes are internal to SkoolKit, so the driver accepts only SkoolKit 10 and says
so in the log for any other version.

## Known differences

On the contention probe (2026-09-29, SkoolKit 10.1) the only difference on both machines is **P-02, the
floating bus**: SkoolKit's unused ports always read `#FF`, while a real 48K / 128K returns the screen byte the
ULA is fetching at that moment. See the probe's [Results so far](../../contention/ctprobe/README.md#results-so-far).
