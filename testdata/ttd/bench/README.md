# TTD benchmark baselines

These are the stored results of the TTD benchmark matrix (PLAN #40 Phase 0, Step 2, requirement BR-9). How to run, compare and regenerate them: [`tools/verification/ttd-bench/`](../../../tools/verification/ttd-bench/README.md).

| File | What | Read by |
|---|---|---|
| `v1-ci-gate.txt` | Byte metrics of the `ci` set, one line per metric, each with a tolerance | core-tests `TTDBench_Test` (the CI gate) |
| `v1-ci.json` | The `ci` set, Google Benchmark JSON | `ttd_bench_compare.py` |
| `v1-turbo.json` | The `turbo` set: the configurations that decided V1b | `ttd_bench_compare.py` |
| `v1-full.json` | The `full` set: all 13 base models, 8 workloads, and 9 peripheral sets on Pentagon and ZX-Evo | `ttd_bench_compare.py` |

The byte metrics in these files are exact for the recorded commit. The timings describe the host named in each JSON file's `context` block, under that host's load at the time of the run. Use the timings as a reference point, not as a pass/fail limit.

`v1-full.json` was run at load average 45–70 (a shared host kept busy by other sessions), so its timings are inflated. Its bytes are exact. The reference timings are in [v0b-benchmark-results.md](../../../docs/inprogress/2026-09-25-ttd-v2-migration/v0b-benchmark-results.md).
