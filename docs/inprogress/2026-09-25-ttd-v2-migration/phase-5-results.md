# Phase 5 results: the switch to the engine

Part of the [TTD v1 → v2 migration](README.md). Phase 5 design: [phase-5-switchover-tdd.md](phase-5-switchover-tdd.md). Quality bar: [engine decision 33](engine-decisions.md#g-quality-bar-after-every-phase). Phase 4: [phase-4-results.md](phase-4-results.md). Open work: [TODO.md](TODO.md).

Measured 2026-10-05 on branch `ttd-engine` (master merged at `730566fbe`), Release, on the shared development host.

## What changed for users

The application records with the engine (`GET /ttd/status` → `backend: "engine"`). v1 stays in the build as the
reference and the fallback: `UNREAL_TTD_BACKEND=v1` selects it for a run. `core-tests` and `unreal-qt-tests` run v1 by
default; the engine's tests select it per test.

| Behavior | v1 | The engine |
|---|---|---|
| Seek, step, find-last, reverse queries while recording | Refused (HTTP 409) | Pause the recording (`recording_paused`); resuming at its end, or running into it, continues it (D8) |
| A tool edit (memory, registers, paging; DeZog) while recording | A barrier; DeZog restarted the recording | An event with what it changed; replay reproduces it, the history stays (D9) |
| A snapshot load while recording | Refused | Part of the recording: at the next frame boundary, frame numbers go on; outside a recording the history stays (D10a) |
| The earliest position | `session_start_frame` only | Also `earliest`; a seek before it names it (D12) |
| A seek to `{frame: N}` without `tinframe` | Frame N's start | Frame N's end - its final state and picture; `tinframe: 0` is the start (D13) |
| Fast tape / fast disk traps while recording | Masked (refused) | Masked for an explicit recording; recorded as edits in a black box (Step 3a) |
| The black box (no UI yet) | - | A recording that keeps the last N minutes and never holds turbo or host speed: it stops before them, its history kept (Step 3a) |

## Gate (§6.1)

| Check | Result |
|---|---|
| D33 matrix, engine against v1 (bytes, memory, counted work; 600 frames) | 46 configurations, 0 failing a deterministic condition (`ttd_engine_d33.py`, `UNREAL_TTD_BENCH_SET=full UNREAL_TTD_BENCH_ENGINE=all`, 600 frames, no seeks; baseline `testdata/ttd/bench/engine-phase5-full.json`). Recorded bytes per frame 3-16 times below v1 (Sprinter 18,364 → 1,132) |
| PR-1, the engine alone in the frame | Measured 2026-10-05 before the merge: frame overhead 3-11 % on the default configurations; above the 15 % / 10 % limits where v1 is too (Pentagon *game* with coverage 25.6 %, v1 22.1 %) - [TODO.md](TODO.md) |
| Full test suite, macOS | All passed, 0 warnings (21 shards, the `test-parallel` target) on `f3e871de2` |
| Linux gcc (`docker/linux/build.sh --test`) | 7,835 passed, 0 warnings on the merge `730566fbe`; 3 failures also on master (`TSL-VDAC2` cannot be created in the Linux image: the model-state contract and two shadow-model cases). Re-run on the final commit: 7,836 passed, 0 warnings on `f3e871de2`, the same three known failures |
| ASan / UBSan on the TTD and DeZog tests | 1,485 passed (recoverable mode). Findings, all known and outside the engine: ATM710 / TS-Conf decoder teardown, `blip_buf` overflow, `modulelogger` index, misaligned binding in two v1 tests, `MainLoop_CUT` in `z80_test`, v1's `HistoryLimit_Test.ByteLimitHolds` use-after-free (also on master) |
| Live surface contract (WebAPI, Lua, CLI; Python where built) | 91 / 91 on the merged build |
| Recipes re-run (§4.6) | Re-run live on the merged build (WebAPI; MCP checked against its schema). Fixed here: a recording paused for browsing was not guarded (a tape / disk load or a media swap dropped it, the CD front panel played), WebAPI and CLI memory writes left a bare marker instead of a replayed edit, old text in five recipes, the MCP tool text, the `/state/registers` path. Pre-existing on both backends, on master next: a bookmark at the current position after `stop` ([BUGS.md](../BUGS.md) 2026-10-05 #3), the status trailing a running recording (#4), the Sprinter BIOS change accepted while recording (drops the session; sprinter.md describes it) |
| The engine's fixture corpus | Re-recorded after the merge (the slot set entered the configuration fingerprint); loads, restores and continues exactly (`TimeTravelControllerCorpus_Test`) |

## Moved to master (owner decision 2026-10-05: land first)

- Step 3b-3d: the black box's Qt settings and menu, its files (`~/.unreal-ng/ttd/`), automation arguments.
- Step 2b: branches (resume or edit in the past keeps the later history).
- D10 rest: RZX start snapshot, ROM reload / model transfer as linked sessions (D26), GS card switch (D38); media loads wait for the storage manager's change layer.
- Step 4: v1 into the verification tools; open question 4 (soak time) is asked before it.
- Known bugs found on the way: [BUGS.md](../BUGS.md) 2026-10-05 #1 (live border before `#FE` is written), #2 (TurboSound FM after a mid-frame baseline).
