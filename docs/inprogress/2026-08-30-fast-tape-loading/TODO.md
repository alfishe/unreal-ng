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
- ~~**O4** ALEX_S hangs on 48K with fast loading on~~ — closed 2026-09-28, not an emulator fault: the
  release carries 128K data only. After its "1-cheat 2-normal" menu the program checks for 128K memory
  and calls LD-BYTES with different parameters per machine (registers at the call):

  | Machine | flag (A') | length (DE) | address (IX) | outcome |
  |:--|:--|:--|:--|:--|
  | Pentagon | #FF | #8400 = 33792 | #61A8 | block 3 loads whole, then the screen (#1B00 at #4000); the game runs |
  | 48K | #13 | #5CB9 = 23737 | #7D3C | waits for a flag #13 block |

  The tape's five blocks carry flags #00 #FF #FF #FF #FF: there is no #13 block, so LD-BYTES rejects
  every block by its flag, the tape ends and the program waits, as a real 48K would. Same on
  `a3e23d6f` and after P2/P3; the investigation's 48K runs (back to BASIC) agree. The sweep reports the
  48K cases of this tape as `128K-only` instead of `hang`.

## Plan (from the investigation, §9)

1. ~~P1: remove the ERR_NR stop~~ — done in `ac200bb8` (TTD tape-state byte 44 reserved; test
   `Tape_Test.ErrNrWriteDuringPlaybackKeepsTapeRolling`).
2. ~~P2: a stop never consumes a partly played block~~ — done 2026-09-28: the branch is gone,
   `TapeFastLoad_Test.StopKeepsThePartlyPlayedBlock` (inverted), design §9.4/§12.1-8 updated.
   Tests that used a mid-block stop to reach "end of tape" use `TapeCUT::EndOfTape()` now.
3. ~~P3: ROM restart after a freeze~~ — done 2026-09-28. T12 found B4 still open: a block frozen
   mid-data and restarted by the ROM anchor went on from the frozen pulse, because
   `StartPlaybackAtCursor()` kept the pulse position (`handleFrameStart()` rebuilds a block with
   its saved position, which a TTD restore needs). `StartPlaybackAtCursor()` now starts the
   cursor block from its first pulse. Tests in `TapeLoaderFollow_Test`: T12, park then ROM load
   (next block), freeze in a pilot then ROM load (pilot start), the "nothing moved it since"
   rule (block pick and rewind drop a frozen pulse).
4. ~~P4: the tape moves only while a loader listens~~ — done in `ac200bb8`
   ([loader-follow-design.md](loader-follow-design.md)). Requirement R (2026-09-27): a load that
   stops for a key prompt or for beeper/AY music resumes by itself when EAR polling returns.
   Verified live: EMELYANOV 48K through the trainer menu, SAN-SAN 48K and 128K, fast loading off.
   Tests T7, T12, T13 and T14 added 2026-09-28; T4 (an IM2 AY player over a whole load) is
   covered by the sweep's real tapes rather than a unit test.
5. Fixture sweep: `tools/verification/tape/tape-sweep.sh` runs
   `TapeLoadingSweep_Test` (`tapeloadingsweep_integration_test.cpp`, registered only with
   `UNREAL_TAPE_SWEEP` set): every tape in `testdata/loaders` on 48K and Pentagon, fast loading
   on and off, prompt keys Y / 1 / 0 every 10 s (never SPACE: it is BREAK for LD-BYTES) (and 1 s / 60 s for the Dizzy tapes), then
   a liveness check. It asserts that the ROM loader is not left waiting for EMELYANOV and SAN-SAN
   with fast loading off and for the control tape, and saves every final screen for a contact sheet.
   **Result 2026-09-28 (120 cases, all pass):** EMELYANOV and SAN-SAN reach the game in all 16
   combinations (48K / Pentagon, fast on / off, key after 1 s / 10 s / 60 s), checked on the sheet:
   the "dead" hints there are Dizzy standing in the first room, which does not react to 0 / 1 /
   ENTER / N. The only hang left is O4 (ALEX_S, 48K, fast loading on). The P4 thresholds held:
   no case parked or froze a loader that was listening. Lesson: SPACE is BREAK for LD-BYTES, never
   a "press any key" key in a sweep.
6. Close out; split O1–O3 into their own item if they survive. **Re-checked 2026-10-03** (headless sweep, 48K and Pentagon):
   - **O1 KID__DR, 48K: cause found and fixed.** After block 2 the program waits for Y / N in a loop, then its own
     loader reads the 6912-byte screen block. The loader, after the first pilot pulses, sits out `ld hl,#0415 / djnz`
     (1045 passes of 3349 T = 3.5 million T = **50.07 frames**) and only then checks that the pilot goes on. The tape
     paused after `TAPE_BLOCK_HOLD_FRAMES` = 50 silent frames and a pilot freeze rewinds the pilot, so after every delay
     the loader found a fresh pilot and never locked (the position inside the block jumped back to 0 every time). A
     pilot now waits `TAPE_PILOT_HOLD_FRAMES` = 100 frames (test `DelayInsidePilotLongerThanTheDataHoldDoesNotRewindIt`).
     The 48K cases now run the tape to its end or past block 4 (cursor 4-6 of 6 instead of stuck at 2). The screen stays
     black there: blocks 4 and 5 (41216 and 16384 bytes) do not fit a 48K, so this is most likely a 128K-only release like
     ALEX_S (not proven: the loader's 48K / 128K check was not read).
   - **O1 KID__DR, Pentagon: closed 2026-10-03 - copy/clone protection, not a defect.** The loader checks that ROM byte `#006D` is `#20`
     (original Sinclair 48K ROM; at `#986B` and in every interrupt at `#5ECA`). The Pentagon ROM set uses `rom/48for128.rom`
     with `#28` there, so the loader fills memory with `#15` (the runaway measured earlier). MAME's Pentagon has `#20` and
     loads the tape. Listing and details: `docs/disasm/software/dizzy-x-kid-dr-loader/`. Decision (owner, 2026-10-03):
     no ROM change; the protection is behaving as its authors intended and is documented as such.
   - **O2 TIMOFEY, 48K:** unchanged (back to BASIC with the tape at block 2 of 5; blocks 3-5 hold 45568 bytes, more than a
     48K takes, and it runs on Pentagon); very likely 128K-only, not proven.
   - **O3 HACKER_SHURIK, Pentagon, fast off:** no longer reproduces: all Pentagon cases reach the end of the tape. The one
     "dead" verdict (48K, signal, key every 500 frames) is the liveness check of the sweep (a screen that does not react to
     0 / 1 / ENTER / SPACE / N), not a loader failure.
   - Sweep before / after the threshold change (120 cases): only the four KID__DR 48K cases differ (better); `lphp`
     48K signal flips OK to dead only through one stray pixel in the liveness check (final screens identical: "Bytes: main.tap").

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
