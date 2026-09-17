# TODO — reopened 2026-09-16: non-standard loader tapes fail deterministically

The feature itself (r4) stays done; this file tracks one open defect found while
re-testing custom-loader tapes on 2026-09-16 ("here we still have issue with
non-standard loaders").

## Symptom

Sweep of 18 fixtures (fresh 48K instance per tape, `LOAD ""` typed via keyboard
injection, WebAPI `/tape` polling — harness in `scratch/nonstd-loader-repro/`):

- **Pass:** DIZZY CHEFRANOV, DIZZY ALEX_S, earshaver, greenberet, lphp, all 6 TZX demos.
- **Fail (deterministic, same block every run):** DIZZY SAN-SAN (dies at block 6,
  ~13.3 s warp), DIZZY TIMOFEY, DIZZY KID__DR (block 2), DIZZY EMELYANOV,
  DIZZY HACKER_SHURIK, echology. `insult.tap` needs a re-run on a 128K model
  (it is a 128K demo — the 48K run was an invalid test).

## What is ruled out

- **Not the fasttape trap, not turbotape.** Failures reproduce identically (same
  outcome class, same death blocks) at real speed with both features verified
  off. Tooling hazard fixed along the way: bulk `PUT /emulator/{id}/settings`
  is **silently ignored** — features must be set per key via
  `PUT /settings/{name} {"value": false}`; the first "features off" differential
  was invalid until this was corrected.
- **Not a signal-encoding issue** (all failing blocks are consumed and decoded
  at least partially before the death point).

## Death-point evidence

- SAN-SAN: machine ends in a dead key-wait loop at `PC=0x8ABD`
  (`call #72CC; and #09; jr z`) polling a RAM mirror at `#728D` that nothing
  updates — the game derailed during load. Only the ROM keyboard-scan ISR runs.
- KID__DR: dies the instant block 2 (first custom-loader block) playback goes
  `playing → idle`; `PC=0x70E1` sits inside an LZ-style unpacker
  (`ex af,af'; exx; ret` tail) with a partially drawn loading screen.

## Root-cause hypothesis (high confidence, confirmation pending)

`Tape::handleFrameEnd` (`core/src/emulator/io/tape/tape.cpp`, ERR_NR watchdog)
false-fires when a custom loader/unpacker writes the `$5C3A` sysvar area as
scratch RAM (games abandon BASIC and reuse sysvars):

1. `errNr != _initialErrNr` → `stopPlayback()` — terminal stop at a moment no
   ROM report happened.
2. `stopPlayback()` **consumes the in-flight block** (cursor++), so the later
   sustained-poll resume restarts signal playback at the *wrong* block.
3. The loader decodes wrong data → deterministic derail into dead RAM loops.

This contradicts the turbo-tape design r2 assumption that the ERR_NR detector
only ever sees ROM report writes ("a successful `0 OK` leaves ERR_NR at $FF") —
true for ROM-driven reports, false for loader code writing the byte directly.

Explains: per-tape determinism, failure independent of warp/real speed, machines
ending alive-but-derailed in RAM code, failures at various block indices.

## Next steps

1. Confirm empirically: re-run KID__DR at real speed polling `$5C3A` (use
   `size>1` — single-byte raw reads return `{}`) alongside `/tape` state; expect
   the byte to flip off `0xFF` exactly when state → `idle`.
2. Fix in `Tape::handleFrameEnd`: stop treating any ERR_NR delta as terminal
   while the load is driven by non-ROM code (e.g. gate on the poll-resume
   lifecycle, or require the delta to look like a ROM report semantics), and
   revisit whether this stop path should consume the in-flight block at all.
3. Regression tests with the failing fixtures (deterministic death blocks make
   tight assertions possible); re-test `insult.tap` on 128K.
4. Update turbo-tape design r2 note (ERR_NR assumption) and this file.

## Repro assets

`scratch/nonstd-loader-repro/` — `run.py` (sweep), `watch.py` (transition
timeline + OCR), `ocr.py` (screen decode), `*.timeline.json`, sweep log and
instance-id files. App running on port 8090 during the session.
