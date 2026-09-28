> Feature r4 (ROM-trap fast loading) is **done** — its summary is kept at the bottom. Restore DONE.md once PLAN #5 is fixed.

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

## Status after the 2026-09-27 investigation

Full write-up: [nonstandard-loader-investigation.md](nonstandard-loader-investigation.md).
The single-cause hypothesis below was only partly right:

- **B1: confirmed.** The ERR_NR watchdog false-stops on custom-loader writes to `$5C3A`. It broke
  EMELYANOV (fast loading off, 48K and 128K); elsewhere it fired harmlessly after the data.
- **B2: confirmed.** The stop consumes the in-flight block. Removing either B1 or B2 fixes
  EMELYANOV.
- **B3: new.** The deck does not wait for a busy loader between blocks. SAN-SAN hangs with fast
  loading off; a 50-frame freeze fixes it.
- **B4: new, from the code.** The ROM anchor restart after a freeze replays the frozen block from
  its start.
- **Correction:** several 2026-09-16 "failures" (SAN-SAN "dead loop at `0x8ABD`", KID__DR,
  HACKER_SHURIK) were games waiting for a key at a prompt. The sweep never pressed one.
- **Open, not explained by B1–B4:** O1 KID__DR crashes on Pentagon (all modes); O2 TIMOFEY falls
  back to BASIC on 48K (all modes); O3 HACKER_SHURIK hangs on Pentagon with fast loading off.

## Plan (from the investigation, §9)

1. ~~P1: remove the ERR_NR stop~~ — done in `ac200bb8` (TTD tape-state byte 44 reserved; test
   `Tape_Test.ErrNrWriteDuringPlaybackKeepsTapeRolling`).
2. P2: `stopPlayback()` still has the "skip the partly played block" branch, but after P1/P4 no
   caller reaches it with a block in flight (end of tape, park after the last block and the
   empty-cursor case all pass a null block). Remove the branch, invert
   `TapeFastLoad_Test.PartialBlockConsumedOnStop`, update design §9.4/§12.1-8.
3. P3: mostly delivered by P4 — a park moves the cursor to the next block, a freeze mid-data
   keeps the cursor on the block so the ROM anchor restarts it from its pilot, a freeze in a
   pilot rewinds it. Still to do: test T12 (ROM restart after a freeze mid-data) and the
   "nothing moved since" rule for user actions (design §5.1).
4. ~~P4: the tape moves only while a loader listens~~ — done in `ac200bb8`
   ([loader-follow-design.md](loader-follow-design.md)). Requirement R (2026-09-27): a load that
   stops for a key prompt or for beeper/AY music resumes by itself when EAR polling returns.
   Verified live: EMELYANOV 48K through the trainer menu, SAN-SAN 48K and 128K, fast loading off.
   Open tests: T4, T12, T13, T14.
5. Re-run the fixture matrix (probe in `scratch/tape-errnr/`) as a `tools/verification/` script;
   EMELYANOV and SAN-SAN must pass with fast loading off on both models; tapes with prompts must
   load with the key pressed after 1 s, 10 s and 60 s. Confirm the P4 thresholds from it.
6. Close out; split O1–O3 into their own item if they survive.

<details><summary>Original 2026-09-16 hypothesis (kept for history)</summary>

### Root-cause hypothesis (high confidence, confirmation pending)

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

### Next steps

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

</details>

## Repro assets

`scratch/nonstd-loader-repro/` (no longer present on 2026-09-27; superseded by `scratch/tape-errnr/`) — `run.py` (sweep), `watch.py` (transition
timeline + OCR), `ocr.py` (screen decode), `*.timeline.json`, sweep log and
instance-id files. App running on port 8090 during the session.

---

# DONE — Fast tape loading (ROM traps) (2026-08-30)

**Status:** complete (r4).

## What landed
- LD-BYTES trap-based instant loading per `design.md` r4: standard ROM loads complete
  near-instantly, decline matrix for everything else, custom-loader pause/resume
  lifecycle (insult.tap fix), feature `fasttape` with UI/CLI/WebAPI/MCP toggles
  (`fast_tape` io-acceleration setting).

## Evidence
- `core/src/emulator/io/tape/tapefastload.cpp`; `fasttape` feature in `FeatureManager`;
  `fast_tape` in `/settings` (cited by the 2026-09-14 gap analysis).

## Follow-ups
- Headerless blocks are served by turbo-tape ([2026-09-04-turbo-tape-loading](../2026-09-04-turbo-tape-loading/), done).
