# Guide for AI agents: timing test programs, checking them on unreal-ng and on other emulators

**Date:** 2026-09-29 · **Belongs to:** [TODO.md](TODO.md) (PLAN #61) · **Background:** [walkthrough.md](walkthrough.md)

How to write a ZX Spectrum test program that checks a hardware timing effect (contention, floating bus, snow,
Even M1, ...), how to check it on unreal-ng, and how to compare it with other emulators. The examples are
ctprobe and the co-emulation harness; follow their patterns.

## 1. Before writing anything

1. **Find the hardware truth.** In this order: circuit (schematics, EPLD / PAL equations, RTL of a faithful
   re-creation), published hardware measurements (photos, videos, tables with the machine named), designers'
   and programmers' reports. Emulators come last and only as a consensus of several. Write the sources and
   their confidence into `docs/inprogress/<date>-<topic>/research.md` at once.
2. **Look for existing test programs** (the zxe.io test wiki is a good index) and check whether each is usable
   as a reference: does it sync to an exact T-state, or does its picture depend on how it was loaded? Is there
   a hardware result for it?
3. **Decide what the test must show** in numbers. A picture is for people; numbers are for the oracle and for
   comparing emulators.

## 2. Writing the test program

- **Language and build:** Z80 assembly in `tools/verification/<area>/<name>/`, assembled by unreal-ng's
  in-tree assembler from the test suite, so the committed `.tap` / `.trd` / `.sym` are always the source's
  (a drift test compares them; `UNREAL_<NAME>_EXPORT=1` rewrites them).
- **Load like a user:** a BASIC loader on tape (`LOAD ""`) and a TR-DOS disk (`RUN`). It must work from 48 BASIC,
  128 BASIC and +3 BASIC.
- **Print through the ROM**, as ZEXALL does: CLS, CHAN-OPEN 2, `RST #10`, numbers through the calculator. One line
  per check as it runs; a summary that stays on screen; the border green or red at the end. The ROM's interrupt
  handler repages from `BANK_M` / `BANK678`: keep them in step with what you page.
- **Exact timing:** use the Bobrowski / Rak engine (`CODETIME`, `FRAMETIME`, `DELAY`; see ctprobe's
  `engine.asm`) rather than a HALT loop. A HALT or 4 T loop leaves a 0-3 T phase that depends on the load.
- **Detect, do not assume:** machine class from measurements (largest wait, frame length, what an unused port
  reads), paging from a write test, turbo from the frame counter. Print what was detected.
- **Make it safe on every machine:** never write a port that another machine decodes differently unless the
  detection says it is safe (an `IN #1FFD` latched #FF into a 128K's `#7FFD`).
- **Results in memory:** a `DONE` byte, the results table, and the expected tables, all between `START` and
  `PROBEEND` in the `.sym`, so a harness can dump and compare them.
- **A README for people** next to it: what it tests, how to load it on each machine, what the screen means, what
  to do when a value is wrong.

## 3. The oracle

- The expected values are computed in the test suite by an oracle that owes nothing to the emulator: pattern
  tables, raster geometry, the bus cycles of each instruction (FUSE's tables), the rule being tested.
- The generator writes the expected tables into the program, one per machine class.
- Every constant in the oracle or in the program gets a comment with its derivation or its source.

## 4. Checking on unreal-ng

- **Unit tests on the invariant first** (for a core change): for example `EvenM1_Test` checks "a fetch from RAM
  on an odd T-state waits one; from ROM never" directly on the CPU, independent of any test program.
- **The program in the test suite:**
  - a quick test (one case, under a second) in the default run;
  - a sweep of every case on every machine against the oracle, opt-in with `UNREAL_TIMING_SUITES=1`;
  - the committed `.tap` / `.trd` loaded the way a user loads them, opt-in the same way;
  - the drift test (committed files equal the source).
- **Fingerprints:** if timing changes on purpose, update the rows in `contentionregression_test.cpp` one by one
  with the old value in a comment. An unexpected row change is a bug.
- **Before a commit:** full build with zero warnings, all of `core-tests` (`test-parallel`), and the opt-in suites
  that cover the change.

## 5. Comparing with other emulators

- **Harness:** `tools/verification/coemu/run-all.sh [machines]` (README there). Point the runners at the emulators
  through their variables (`XPECCY_DIR`, `ZXMAK2_DIR`, `MAME_BIN`, `FUSE_DIR`, `ZESARUX_BIN`,
  `SKOOLKIT_PYTHON`, `UNREAL_BUILD`); an emulator not found is `skipped`.
- **Never pop windows:** runners are headless (MAME with SDL's dummy drivers, ZEsarUX with `--vo null`).
- **Rebuild the program first** and do not re-export while a runner is still running (ZEsarUX runs in real time
  and reads the `.sym` at start; a re-export in between gives a wrong dump size).
- **Read the differences, do not count them:** the compare script prints each wrong row with the expected one
  and whether it is a shift ("1 tick later"). Group the emulators: which agree with the oracle, which with each
  other, and why. Look up the emulator's code for the rule in question before deciding who is wrong.
- **An emulator that fails is not automatically wrong, and one that passes is not automatically right.** A
  correction derived on unreal-ng must be confirmed by an emulator with its own implementation of the effect.
- **Write the results** into the program's README ("Results so far"): emulator, version, per machine, in words.

## 6. When emulators cannot decide

Some things only hardware settles (in this work: P-05 on the 128K, which Scorpion boards have Even M1, the Even M1
start offset). Say so in the README, prepare the files, and describe exactly what to run and what to photograph.
Do not pick a winner from emulators alone.

## 7. Working rules that apply throughout

- Chat with the user in Russian; code, docs and comments in English (American spelling).
- Research and findings go into `docs/inprogress/` immediately, not only into the conversation.
- Artifacts go to `scratch/`; no absolute paths of your machine in committed files.
- Commit only when asked; land on master carefully when other sessions have uncommitted files there.
