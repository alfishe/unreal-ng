# TTD benchmark matrix

This directory holds the tools for running, comparing and baselining the time-travel (TTD) benchmark matrix (PLAN #40 Phase 0, Step 2, [TTD v2 requirements §5](../../../docs/inprogress/2026-09-25-ttd-v2-migration/requirements.md)).

The same emulation runs under every TTD engine (today only `v1`, the current `TimeTravelManager`), so the numbers compare engine against engine and commit against commit.

| Piece | Where |
|---|---|
| Harness: engines, configurations, workloads, metrics | `core/src/debugger/ttd/bench/ttdbench.h` |
| Matrix benchmarks (`TTDMatrix/<engine>/<configuration>/<workload>`) | `core/benchmarks/debugger/ttd/ttd_matrix_benchmark.cpp` |
| CI gate, core-tests `*TTDBench*` | `core/tests/debugger/ttd/bench/ttdbench_test.cpp` |
| Stored baselines | [`testdata/ttd/bench/`](../../../testdata/ttd/bench/README.md) |
| Compare / summarize / export | `ttd_bench_compare.py` (this directory) |

## Glossary

- **Configuration**: a machine model plus a set of peripherals, for example `ATM3+gs512+moon+tsfm`. Some configurations also switch on the hardware turbo (`ATM710-turbo`); ZX-Evo `ATM3` and the Scorpions (`SCORPION`, `PROFSCORP`) boot with their 2x turbo already on, and `SCORPION-3.5MHz` switches it off.
- **Workload**: what the machine does while it is recorded. Each workload is replayable, meaning every run produces exactly the same machine states:
  - it starts from a fixed state (cold boot, a snapshot, or an autostarted disk);
  - it runs a number of settle frames before recording starts;
  - it records a fixed number of frames, with scripted key presses at fixed frames;
  - the RTC clock is frozen, so the firmware never reads the host time;
  - every machine is created with zeroed RAM (`RAMPowerOn=ZERO`) instead of the default power-on noise from the process-wide `rand()`, so a case does not depend on the cases that ran before it.
- **Byte metric** (deterministic metric): a size or a count, which is identical on every run. Names end in `_bytes`, `_bpf` (bytes per frame) or `_opf` (operations per frame), plus `frames` and `checkpoints`.
- **Timing metric**: a duration. Timings vary with host load and are reported as percentiles.

## Running

Benchmarks are opt-in at configure time:

```bash
cmake -S . -B cmake-build-agent-release -G Ninja -DBENCHMARKS=ON
ninja -C cmake-build-agent-release core-benchmarks

# The CI-sized set (4 cases, ~10 s)
./cmake-build-agent-release/bin/core-benchmarks --benchmark_filter='TTDMatrix/' \
    --benchmark_format=json --benchmark_out=scratch/ttd-ci.json

# The configurations that decide whether V1b is needed (turbo, heavy peripherals)
UNREAL_TTD_BENCH_SET=turbo ./cmake-build-agent-release/bin/core-benchmarks \
    --benchmark_filter='TTDMatrix/' --benchmark_format=json --benchmark_out=scratch/ttd-turbo.json

# Everything: 17 base models, 9 workloads, 9 peripheral sets on two models
UNREAL_TTD_BENCH_SET=full UNREAL_TTD_BENCH_DIRTY=1 ./cmake-build-agent-release/bin/core-benchmarks \
    --benchmark_filter='TTDMatrix/' --benchmark_format=json --benchmark_out=scratch/ttd-full.json
```

The following environment variables control a run:

| Variable | Values | Default |
|---|---|---|
| `UNREAL_TTD_BENCH_SET` | `ci`, `turbo`, `full` | `ci` |
| `UNREAL_TTD_BENCH_ENGINE` | `v1`, `all`, or a comma-separated list | `v1` |
| `UNREAL_TTD_BENCH_PERIPHERALS` | an overlay applied to every configuration: `none`, or a list such as `ay+gs512+moon` (items: `ay`/`ts`, `tsfm`, `gs128`, `gs512`, `moon`, `covox`, `beta`, `mouse`) | the matrix's own sets |
| `UNREAL_TTD_BENCH_FRAMES` | the number of recorded frames, replacing every workload's own count | the workload's count |
| `UNREAL_TTD_BENCH_SEEKS` | the number of random seek positions (BM-5) | 40 for `ci`, 200 otherwise |
| `UNREAL_TTD_BENCH_OVERHEAD` | `0` skips BM-1 (BM-1 costs four extra runs per case) | on |
| `UNREAL_TTD_BENCH_DIRTY` | `1` adds BM-8 | off |
| `UNREAL_TTD_BENCH_KEEP_SESSIONS` | a folder: keep each case's saved `.ttd` session there instead of deleting it (input data for the [TTD v2 experiments](../../poc/011-ttd-v2-capture-analysis/experiments/README.md)) | off |

Other sessions running on the same machine inflate timings. Check `uptime` before running, and compare only runs made at a similar load.

## Metrics

| Id | Metric names | Meaning |
|---|---|---|
| BM-1 | `bm1_frame_off_us_p50`, `bm1_overhead_{nojournal,journal,journal_cov}_pct` | Median frame time with TTD off, and the extra time per frame in each recording mode |
| BM-2 | `bm2_capture_us_{p50,p95,p99,max}`, `bm2_capture_p99_over_p50`, `bm2_capture_share_pct` | Time to capture one checkpoint at the end of a frame |
| BM-2 work | `bm2_work_{pages_visited,compress_calls,decoded}_opf`, `bm2_work_{delta_base,device_blobs,scanned,compress_input}_bpf` | What the capture did, counted per frame: RAM pages walked, bytes copied into the delta base, device-state bytes, bytes XOR'd and zero-checked, zstd calls and their input, chain links decoded. Deterministic: the CI gate checks capture cost with these, not with a clock |
| BM-3 | `bm3_{ram_payload,page_refs,device_blobs,checkpoint_core,write_journal,input_journal,coverage,total}_bpf` | Recording size per frame, split by stream |
| BM-3 writes | `bm3_journal_writes_opf` | Memory writes per frame over the whole session. `bm3_write_journal_bpf` stops growing once the journal's ring is full; this count does not |
| BM-4 | `bm4_resident_bytes`, `bm4_resident_bpf` | Memory held by the whole session |
| BM-4 split | `bm4_heap_<part>_bpf`: `page_store_table`, `ram_payload`, `checkpoints`, `page_refs`, `device_blobs`, `input_journals`, `write_journal`, `coverage`, `port_reads`, `port_writes`, `frame_cache`, and the unused allocation inside them, `ram_payload_slack`, `write_journal_slack`, `coverage_slack`, `port_journal_slack` | Where the session memory goes. The parts without `_slack`, plus `ram_payload_slack`, sum to `bm4_resident_bpf`; the other `_slack` parts are the unused allocation inside the part they name. Allocator-dependent, so compared with the heap tolerance and not stored in the CI gate baseline |
| BM-5 | `bm5_{aligned,offset}_us_*`, `..._nopresent_us_*`, `..._present_us_*`, `bm5_offset_{restore,replay}_us_*` | Seek time, to a frame start (`aligned`) or to a point inside a frame (`offset`). `nopresent` is the time to reach the machine state; `present` is the extra time to build the picture of that position |
| BM-6 | `bm6_restore_{cpu_chipset,devices,memory,screen}_us_p50` | Checkpoint restore time, split by component |
| BM-7 | `bm7_file_bytes`, `bm7_file_bpf`, `bm7_{save,load}_s_per_gb`, `bm7_first_seek_ms` | Session file size, save and load speed, and time to the first seek after loading |
| BM-8 | `bm8_capture_us_dirty{0,1,4,16,64}` | Capture time after writing 0 to 64 pieces of 4 KB each |
| - | `turbo_ratio` | The hardware clock ratio (1 = base clock) in effect at the end of the run: proof that the configuration really ran as named |

Each seek position is measured three times (`seekRepeats`) and the fastest run is kept. The spread between positions (how far the target is from its checkpoint, where it lies in the frame) stays in the data, while a single measurement slowed by the scheduler does not.

## Comparing

```bash
T=tools/verification/ttd-bench/ttd_bench_compare.py

python3 $T compare testdata/ttd/bench/v1-full.json scratch/ttd-full.json   # baseline vs now
python3 $T compare run.json run.json --base-engine v1 --new-engine v2     # two engines, one file
python3 $T summary scratch/ttd-turbo.json --markdown                        # one row per case
```

`compare` prints only the flagged metrics (`--all` prints every metric):

- `BYTES`: a byte metric changed. This always fails the comparison.
- `slower` / `faster`: a timing moved by more than `--threshold` (default 25%). A timing fails the comparison only with `--fail-on-time`.

Exit codes:

| Code | Meaning |
|---|---|
| 0 | clean |
| 1 | a byte difference, a failed case or a case present on one side only (or, with `--fail-on-time`, a slower timing) |
| 2 | bad input |

Byte metrics measured as container capacity (`bm3_coverage_bpf`, `bm3_total_bpf`, `bm4_*`) follow the allocator's growth policy. Comparing runs from different platforms therefore needs `--heap-tolerance 25`.

## Updating the baselines

Update a baseline when a change to capture or to the file format is intended, and include the reason in the commit message:

```bash
# CI gate (core-tests reads it)
UNREAL_TTD_BENCH_OVERHEAD=0 ./cmake-build-agent-release/bin/core-benchmarks \
    --benchmark_filter='TTDMatrix/v1/' --benchmark_format=json --benchmark_out=scratch/ttd-ci.json
python3 tools/verification/ttd-bench/ttd_bench_compare.py export-gate scratch/ttd-ci.json \
    testdata/ttd/bench/v1-ci-gate.txt

# Reference runs (drops the local executable path from the context)
for set in ci turbo full; do
    python3 tools/verification/ttd-bench/ttd_bench_compare.py export-baseline scratch/ttd-v1-$set.json \
        testdata/ttd/bench/v1-$set.json
done
```
