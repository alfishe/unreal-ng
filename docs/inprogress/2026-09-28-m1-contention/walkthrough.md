# Walkthrough: how M1 contention, Even M1 and the snow work were done

**Date:** 2026-09-29 · **Belongs to:** [TODO.md](TODO.md) (PLAN #61) · **Companion:** [test-writing-guide.md](test-writing-guide.md)

This is the story of the work in order: what was done, what went wrong, and what was learned. It is written so
that someone (a person or an AI agent) picking the work up can see why things are the way they are.

## 1. The starting point

unreal-ng inherited Unreal Speccy's contention: a per-access branch in `Z80::rd` / `wd`, no contention of opcode
fetches, of internal cycles or of I/O, and a floating bus that was off on the Sinclair models. The goal
([design.md](design.md)) was exact Sinclair contention with no cost on the uncontended machines.

## 2. Baselines before any change (phase 1a)

Before touching the core, every creatable model got a timing fingerprint: a fixed code mix run in five
placements, its T-state count and a state hash (`contentionregression_test.cpp`), plus a benchmark
(`BM_ContentionInstructionMix`, [baseline.md](baseline.md)).

Why: every later change could then show exactly which models' timing moved. A change that should touch only the
48K and moves the Pentagon's number is a bug. The fingerprints are updated on purpose, row by row, with the old
value left in a comment.

## 3. The core changes (phases 1b-3)

- **Phase 1b:** contention moved out of `Z80::rd` / `wd` into two more memory interfaces chosen by one selector
  (`Core::SelectMemoryInterface`). The uncontended machines lost the branch (Pentagon 2-5 % faster); opcode
  fetches now wait.
- **Phase 1c:** a `contention` feature and one status report on every automation surface.
- **Phase 1d:** the FUSE test vectors replayed with contention against an independent oracle, with negative
  controls (the clones must never wait).
- **Phase 2:** internal (no-MREQ) cycles, each with the address the CPU puts on the bus (`Z80::Idle`).
- **Phase 3:** the four I/O contention patterns.

Each phase: a failing test first, then the change, then the fingerprints and the suites.

## 4. Test programs from outside (suite H)

Published test programs with hardware results were run in the emulator: Butler's 48K suite (37 tests, hardware
values) and Rak's Timing Test (photos of real screens). They found what unit tests could not:

- **Butler test 35** was first misread as a floating-bus problem. A frame-exact trace of every port access,
  compared against SkoolKit's contention simulator (which passes all 72 results), matched for 158 iterations
  and then diverged. That pointed at three real defects: a Kempston mouse on the "bare" 48K, Beta 128 ports
  answering outside TR-DOS, and the floating bus switched off in the configs. The floating bus itself was also
  2 T late. Lesson: when a test fails, trace against a second implementation until the first divergence; do not
  guess from the test's name.
- **Rak's +2A/+3 photos** showed a 129 T contention window per line where every emulator surveyed uses 128.
  Lesson: hardware photos beat emulator consensus.

## 5. ctprobe, the project's own probe

Published tests answer "does it look right"; they do not print numbers. ctprobe
([tools/verification/contention/ctprobe](../../../tools/verification/contention/ctprobe/README.md)) times 53
code fragments at every T-state around the contention onset with the Bobrowski / Rak measuring engine, and
compares each value with a table computed by an oracle in the test suite (`ctprobe_test.cpp`). The oracle owes
nothing to the emulator: the pattern tables, the raster geometry, and each instruction's bus cycles from FUSE's
tables.

Mistakes on the way, all found by the user running the files:

- The first version printed nothing while it ran and looked hung. The user's rule since: a test program prints
  its progress through the ROM, as ZEXALL does.
- The report scrolled off the screen and hit "out of screen": the ROM's CLS leaves channel K open.
- A +3 started from +3 BASIC lost its paging: the ROM's interrupt handler repages from `BANK_M`, so the probe
  writes `BANK_M` too.
- An unconditional `IN #1FFD` latched #FF into a 128K's `#7FFD` (found by FUSE).
- MAME's 128K was classified as a gate array: classification now uses the largest wait, not one exact value.

## 6. The co-emulation harness

`tools/verification/coemu` runs one program on every emulator it can find and puts the results in one table
(unreal-ng, FUSE, MAME, ZEsarUX, ZXMAK2, xpeccy-plus, Xpeccy, SkoolKit). Every runner follows one contract:
machine names, `ok` / `wrong` / `error` / `skipped`, exit codes, discovery through environment variables. It
answers "is this our bug or a disagreement between emulators?" and shows which emulator agrees with the hardware
data we have.

## 7. The Scorpion

- **Question:** "does the Scorpion have contention at all?" Research by circuit, not by emulator
  ([contention-by-machine.md](contention-by-machine.md)): the Scorpion's own EPLD equations (SC15.1) show no
  contention, but a one-tick WAIT on opcode fetches from RAM that would start on an odd T-state ("Even M1").
- **Core:** Even M1 in `Z80Step`, before an instruction's first M1, only for RAM, never in turbo; the flag
  follows the memory model. Tests on the invariant (odd-T RAM fetch waits, ROM never, RAM at #0000 waits).
- **The probe broke:** its engine needs delays of every length, and Even M1 makes every delay even. First the
  probe detected Even M1 and said "not measured"; then (follow-up 1) it got a 2 T-step delay routine
  (`DELAYE`) and two corrections that were worked out on paper: the engine's four-pass vernier sees one phase
  only, so its frame measurement is exactly 2 T short whatever the phase; with that, code starts 2 T early and
  durations come out 4 T short. The start shift was located with the floating-bus case P-02.
- **Cross-check:** MAME and ZXMAK2 implement Even M1 independently and agree with every duration; xpeccy-plus
  differs on P-02 only, because it adds the tick before it samples the interrupt. Lesson: a correction derived
  on one emulator must be confirmed on another implementation before it is trusted.

## 8. Snow

- **Research first:** nine snow test programs, the hardware model from Weiv and TheMartian, and which tests are
  usable as references ([2026-09-29-ula-snow/research.md](../2026-09-29-ula-snow/research.md)). Most tests wait
  for the interrupt in a 4 T loop, so the same tape shows different pictures on the same machine; only Snow Hold
  (exact sync, photos from three real machines) and Weiv's tuning-table tests avoid that.
- **Anchoring:** the model's ticks ("the 3rd and 5th tick of the ULA's 8-tick cycle") map onto the phases of
  unreal-ng's floating bus, which Butler's hardware-measured tests already validated. The Snow Hold photos then
  pin the last tick of uncertainty.
- **Lost work:** the first research report existed only in the session transcript and had to be recovered.
  Lesson: research goes into `docs/inprogress/` the moment it is done.

## 9. What was learned, in one list

1. Baseline first; update fingerprints row by row, never wholesale.
2. A failing hardware test: trace against a second implementation to the first divergence.
3. Hardware data (photos, published measurements, circuit equations) over emulator consensus; emulator
   consensus over one emulator.
4. Test programs print through the ROM and are tried by a person on real software paths (tape, TR-DOS, +3 BASIC).
5. Every correction or constant gets a written derivation and an independent confirmation.
6. Write findings down in the repository immediately.
