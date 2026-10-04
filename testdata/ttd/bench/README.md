# TTD benchmark baselines

These are the stored results of the TTD benchmark matrix (PLAN #40 Phase 0, Step 2, requirement BR-9). How to run, compare and regenerate them: [`tools/verification/ttd-bench/`](../../../tools/verification/ttd-bench/README.md).

| File | What | Read by |
|---|---|---|
| `v1-ci-gate.txt` | Byte metrics of the `ci` set, one line per metric, each with a tolerance | core-tests `TTDBench_Test` (the CI gate) |
| `v1-ci.json` | The `ci` set, Google Benchmark JSON | `ttd_bench_compare.py` |
| `v1-turbo.json` | The `turbo` set: the configurations that decided V1b | `ttd_bench_compare.py` |
| `v1-full.json` | The `full` set: all 13 base models, 8 workloads, and 9 peripheral sets on Pentagon and ZX-Evo | `ttd_bench_compare.py` |
| `engine-phase1-full.json` | The `full` set with both engines (`UNREAL_TTD_BENCH_ENGINE=all`), 600 frames, no seeks: the engine's Phase 1 baseline, bytes, memory and counted work (no timings) | `ttd_engine_d33.py` |
| `engine-phase2-full.json` | The same run after Phase 2 (device state with history, time fields, changed ranges): the engine's Phase 2 baseline ([phase-2-results.md](../../../docs/inprogress/2026-09-25-ttd-v2-migration/phase-2-results.md)) | `ttd_engine_d33.py` |
| `engine-phase3-full.json` | The same run after Phase 3 (event stream, bus, media and write journals in the engine, RZX playback): the engine's Phase 3 baseline ([phase-3-results.md](../../../docs/inprogress/2026-09-25-ttd-v2-migration/phase-3-results.md)) | `ttd_engine_d33.py` |
| `engine-phase4-full.json` | The same run after Phase 4, with the session file (`bm7_file_bpf` for both engines): the engine's Phase 4 baseline ([phase-4-results.md](../../../docs/inprogress/2026-09-25-ttd-v2-migration/phase-4-results.md)) | `ttd_engine_d33.py` |

The byte metrics in these files are exact for the recorded commit. The timings describe the host named in each JSON file's `context` block, under that host's load at the time of the run. Use the timings as a reference point, not as a pass/fail limit.

`v1-full.json` was run at load average 45–70 (a shared host kept busy by other sessions), so its timings are inflated. Its bytes are exact. The reference timings are in [v0b-benchmark-results.md](../../../docs/inprogress/2026-09-25-ttd-v2-migration/v0b-benchmark-results.md).
