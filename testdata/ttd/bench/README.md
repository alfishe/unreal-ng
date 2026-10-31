# TTD benchmark baselines

These are the stored results of the TTD benchmark matrix (PLAN #40 Phase 0, Step 2, requirement BR-9). How to run, compare and regenerate them: [`tools/verification/ttd-bench/`](../../../tools/verification/ttd-bench/README.md).

| File | What | Read by |
|---|---|---|
| `ci-gate.txt` | Byte metrics of the `ci` set (engine `controller`), one line per metric, each with a tolerance | core-tests `TTDBench_Test` (the CI gate) |
| `engine-phase1-full.json` | The `full` set with both engines (`UNREAL_TTD_BENCH_ENGINE=all`), 600 frames, no seeks: the engine's Phase 1 baseline, bytes, memory and counted work (no timings) | history (`ttd_engine_d33.py`, gone with v1) |
| `engine-phase2-full.json` | The same run after Phase 2 (device state with history, time fields, changed ranges): the engine's Phase 2 baseline ([phase-2-results.md](../../../docs/inprogress/2026-09-25-ttd-v2-migration/phase-2-results.md)) | history (`ttd_engine_d33.py`, gone with v1) |
| `engine-phase3-full.json` | The same run after Phase 3 (event stream, bus, media and write journals in the engine, RZX playback): the engine's Phase 3 baseline ([phase-3-results.md](../../../docs/inprogress/2026-09-25-ttd-v2-migration/phase-3-results.md)) | history (`ttd_engine_d33.py`, gone with v1) |
| `engine-phase4-full.json` | The same run after Phase 4, with the session file (`bm7_file_bpf` for both engines): the engine's Phase 4 baseline ([phase-4-results.md](../../../docs/inprogress/2026-09-25-ttd-v2-migration/phase-4-results.md)) | history (`ttd_engine_d33.py`, gone with v1) |

The byte metrics in these files are exact for the recorded commit. The timings describe the host named in each JSON file's `context` block, under that host's load at the time of the run. Use the timings as a reference point, not as a pass/fail limit.

The `engine-phase*` files hold v1 and the shadow engine next to the engine (v1 went with Phase 6, 2026-10-30); `engine-phase5-full.json` also holds `controller`, the recorder the application runs: compare a new run against it with `--base-engine controller`. v1's own reference runs (`v1-ci.json`, `v1-turbo.json`, `v1-full.json`) were deleted with v1; their timings are in [v0b-benchmark-results.md](../../../docs/inprogress/2026-09-25-ttd-v2-migration/v0b-benchmark-results.md).
