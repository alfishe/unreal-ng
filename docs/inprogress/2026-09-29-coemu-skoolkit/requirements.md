# SkoolKit as a co-emulation runner: requirements

**Date:** 2026-09-29 · **Plan row:** [#74](../PLAN.md) · **Design:** [tdd.md](tdd.md) · **Status:** [DONE.md](DONE.md)

## 1. Background

The co-emulation harness ([tools/verification/coemu](../../../tools/verification/coemu/README.md)) runs one
test program, first of all the contention probe
([ctprobe](../../../tools/verification/contention/ctprobe/README.md)), on every emulator it can find, and puts
the results in one table. It has runners for unreal-ng, xpeccy-plus, Xpeccy, MAME, FUSE, ZEsarUX and ZXMAK2.

[SkoolKit](https://skoolkit.ca) (Richard Dymond) is a toolkit for disassembling Spectrum programs. Since
version 9 it includes a Z80 simulator with memory and I/O contention and the MEMPTR register, written in
Python with a C extension. Its contention model is compact and readable, and it passes all 72 tests of
Butler's 48K timing suite. This project already used it once as a reference: a frame-exact I/O trace against
SkoolKit found three unreal-ng defects that made Butler test 35 fail
([m1-contention TODO](../2026-09-28-m1-contention/TODO.md)). The RZX replay work uses its `rzxplay.py` as an
independent check ([rzx-replay requirements](../2026-09-29-rzx-replay/requirements.md) RZ-T3).

What SkoolKit is not: a full emulator. It simulates the 48K, the 128K and (for tracing only) the +2. It has no
+2A / +3 gate array, no clones and no disk interfaces.

## 2. Goals

| ID | Goal |
|:--|:--|
| G1 | A third, independent reference for Sinclair contention next to FUSE and unreal-ng, runnable by anyone with `pip install skoolkit` |
| G2 | The runner follows the harness contract (machine names, results, exit codes) so `run-all.sh` picks it up with no special case |
| G3 | The program is loaded the way a user would: through the ROM's tape loader, not poked into memory |
| G4 | Fast and deterministic: a full probe run per machine in seconds, the same result every time |

Non-goals: machines SkoolKit does not simulate; changing SkoolKit; using SkoolKit inside unreal-ng's own tests.

## 3. Requirements

| ID | Requirement |
|:--|:--|
| R1 | A runner folder `tools/verification/coemu/skoolkit/` with `run.sh`, a Python driver and a README, like the other runners |
| R2 | Discovery: `SKOOLKIT_PYTHON` (a Python interpreter that can `import skoolkit`), then `python3` if it can, then the interpreter of `tap2sna.py` found on `PATH`. If none is found every machine is `skipped` and the runner exits 3, with the install hint |
| R3 | Machines `48k` and `128k` run; every other machine name is reported `skipped` with the reason ("SkoolKit simulates only the 48K and the 128K") |
| R4 | Loading: SkoolKit's simulated `LOAD ""` (`tap2sna.py`, `machine=48` / `machine=128`) plays the program's `.tap` through the ROM loader and stops when the BASIC loader jumps to the program's start |
| R5 | Running: from that state, SkoolKit's contention simulator (memory and I/O contention, MEMPTR, interrupts on) runs until the program's `DONE` byte is 1, or `MAX_FRAMES` frames pass (then `error`) |
| R6 | Output: `$OUT/<machine>.bin` (memory from `START` to `PROBEEND`), `$OUT/<machine>.log` (SkoolKit version, simulator class, frames, seconds), `$OUT/<machine>.screen.txt` (the final screen as text) |
| R7 | The SkoolKit version is checked: a version the driver was not written for is reported as `error` with the version found, not a traceback |
| R8 | No absolute paths of the author's machine in committed files; build products and virtual environments stay out of git |
| R9 | The harness README and the probe README list the runner and its results; the differences found are explained for people (what the probe expected, what SkoolKit does, which one real hardware agrees with) |

## 4. Acceptance criteria

| ID | Criterion | Checks |
|:--|:--|:--|
| AC1 | `tools/verification/coemu/skoolkit/run.sh 48k 128k` with SkoolKit installed ends with a result for each machine, and the dumps compare through `ctprobe-compare.py` | R1, R4-R6 |
| AC2 | `run.sh` with no SkoolKit found (for example `SKOOLKIT_PYTHON=/nonexistent`) marks every machine `skipped` and exits 3 | R2 |
| AC3 | `run.sh plus3 pentagon` marks both `skipped` with the reason; `run.sh` with no arguments reports all eleven machines | R3 |
| AC4 | `run-all.sh 48k 128k` shows a `skoolkit` column | G2 |
| AC5 | Two runs give byte-identical dumps | G4 |
| AC6 | A full `48k` + `128k` run takes under a minute on the development machine | G4 |
| AC7 | Every value SkoolKit reports differently from the expected table is explained in the probe README ("Results so far") | R9 |
| AC8 | `python3 tools/fix-absolute-paths.py --path <file>` finds nothing in the changed files; their links resolve | R8 |
