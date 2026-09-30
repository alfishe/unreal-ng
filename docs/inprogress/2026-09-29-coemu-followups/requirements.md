# Co-emulation harness follow-ups: requirements

**Date:** 2026-09-29 · **Plan:** follow-up 5 of PLAN #61 ([m1-contention TODO](../2026-09-28-m1-contention/TODO.md)) ·
**Status:** [TODO.md](TODO.md)

## 1. Background

The co-emulation harness ([tools/verification/coemu](../../../tools/verification/coemu/README.md)) runs one
test program (first of all the contention probe,
[ctprobe](../../../tools/verification/contention/ctprobe/README.md)) on every emulator it finds and puts the
results in one table. It has runners for unreal-ng, xpeccy-plus, Xpeccy, MAME, FUSE, ZEsarUX, ZXMAK2 and
SkoolKit. Five items were left when the contention work closed:

- MAME's Scorpion (`scorpio`) once reset while the probe ran; it runs now, and nobody knows why it failed.
- Three more emulators have source available and a way to drive them without a window: Kozynax (C#, a
  descendant of ZXMAK2 with a command-line front end), ZX-M8XXX (JavaScript, driven through
  `window.zxDebug` in a headless browser) and spec_chum (Rust, a loopback HTTP API).
- fusetest, FUSE's own timing test program, exists only as source for the pasmo assembler.
- The probe's negative check X-04 ("contention changes time only: registers and memory come out the same
  with contention on and off") was never written.

## 2. Goals

| ID | Goal |
|:--|:--|
| G1 | Every emulator that can run the probe headlessly on this machine has a runner, so a disagreement is settled by a majority, not by one emulator |
| G2 | Past failures of the harness are understood, not just gone |
| G3 | FUSE's own timing test runs on unreal-ng as a second, independent program |
| G4 | The probe's claim that contention only changes time is tested |

Non-goals: fixing other emulators; a GUI for any of them.

## 3. Requirements

| ID | Requirement |
|:--|:--|
| R1 | MAME `scorpio`: the cause of the earlier reset is found and written down (which MAME behavior, which harness step), with the fix that removed it or the condition that makes it come back |
| R2 | New runners `kozynax/`, `zx-m8xxx/`, `spec-chum/` follow the harness contract (README "Adding an emulator"): discovery by environment variable, machine names, `skipped` with a reason for machines the emulator lacks, results and exit codes, the program loaded as a user would |
| R3 | Each runner's README says what it needs, how it loads the program, which settings it chose, and each value it reports differently from the expected table, with the reason where known |
| R4 | fusetest is assembled from its source by a pinned assembler build, and its result on unreal-ng's 48K, 128K, +3 and Pentagon is recorded; if it can run under the harness (a `DONE` byte and a compare script), it does |
| R5 | X-04: unreal-ng runs the probe with the `contention` feature on and off; everything the probe computes except time (registers and memory the fragments produce) is equal |
| R6 | No absolute paths of the author's machine in committed files; build products, sources of other emulators and results stay out of git |

## 4. Acceptance criteria

| ID | Criterion | Checks |
|:--|:--|:--|
| AC1 | The MAME README states the cause of the earlier Scorpion reset and how it was confirmed (a run that reproduces it, or the MAME change that removed it) | R1 |
| AC2 | `run.sh` of each new runner with the emulator present ends with a result per machine; with it absent, every machine is `skipped` and the exit code is 3 | R2 |
| AC3 | `run-all.sh` shows the new columns | R2 |
| AC4 | Every `wrong` of a new runner is explained in its README | R3 |
| AC5 | fusetest's results per machine are in its notes; a failing test on unreal-ng is either fixed or explained | R4 |
| AC6 | A core test runs the probe with contention on and off and compares the non-time results | R5 |
| AC7 | `python3 tools/fix-absolute-paths.py --path <file>` finds nothing in the changed files; their links resolve | R6 |
