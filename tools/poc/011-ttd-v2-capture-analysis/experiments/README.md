# TTD v2 Phase 1 experiments

These experiments measure, on real recordings, the parameters that the [Phase 1 technical design](../../../../docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md) left open (roadmap: [TTD v1 → v2 migration](../../../../docs/inprogress/2026-09-25-ttd-v2-migration/README.md)). They were added on 2026-10-01 next to the original POC 011 material, which is unchanged.

Each experiment is its own folder, with:
- a goal;
- the method and how to run it;
- the results, the analysis and the conclusions;
- its script and its generated `results.md`.

They share the input data and two small modules in [`common/`](common/).

## Experiments

| Experiment | Question | Result |
|---|---|---|
| [E1 — Chain length limit](e1-chain-limit/README.md) | Which per-piece chain limit K, and are staggered limits needed? | K = 50 halves stored bytes against the v1 key frame and removes the spike (≤ 4 forced Full stores per frame, against up to 30). Staggering is not needed |
| [E2 — Encode once](e2-encode-once/README.md) | When is the second (full) compression of a changed piece worth it? | The full piece wins in 5% of changes. Skipping it when the XOR is ≤ 128 B makes encoding 3.5× faster for +0.07% bytes |
| [E3 — Reference blocks](e3-reference-blocks/README.md) | Block size of the copy-on-write reference table; one or two levels? | 8-page blocks; table bytes 0.21 of v1 (ZX-Evo: 4,096 → 520 B per frame). Two levels are needed for idle device memory (PR-10) |
| [E4 — Restore differences](e4-restore-differences/README.md) | What does restoring only differing pieces save on a seek? | ZX-Evo memory restore from ~760 to ~145 µs. Most of a full restore writes zeros into untouched memory |
| [E5 — Heap split](e5-heap-split/README.md) | Where does v1's recording memory go? | Mostly unused allocation: every stored piece holds ~4 KB of heap whatever it compressed to (`ZSTD_compressBound`). Then the write journal |
| [E6 — v1 / v2 data model](e6-v1-v2-model/README.md) | How much memory and file will a recording take with v2, on real use, and why? | Memory 3–16× smaller. The model matches measured v1 within 0.6% (ZX-Evo 2%). The write journal becomes the largest stream; idle cards still cost 0.8–0.9 MB per minute |
| [E8 — device fields](e8-device-fields/README.md) | Which device-state bytes change from frame to frame, and which only count time? (Phase 2) | Storing only changes: 4–13× smaller than v1. Deriving the time fields (MoonSound, NeoGS, TSFM's own) halves it again: 0.25–0.4 MB per minute, ATM710 0.52 (its keyboard controller) |

## What changes in the design

| TDD item | Was | Now |
|---|---|---|
| Step 2, chain limit | K = 50 with staggered start depths | K = 50, **no staggering** (E1) |
| Step 7, full-compression threshold | 1 KB, to be tuned | **128 B** (E2) |
| Step 4, block size | 16 pages | **8 pages**, two levels (E3) |
| Step 6, why | seek decodes all memory | a full restore mostly **writes zeros** into untouched memory; ~5× faster memory restore on ZX-Evo (E4) |
| Before Phase 1 | — | **exact-size payloads** in v1: half or more of a recording's memory (E5) |
| Phase 2 | device blobs versioned | counters that advance with time are **derived from time**, not stored (E6) |
| Before Phase 4 | journal ring | decide what to keep of the **write journal**: the largest stream on active content (E6) |

## Data

Every experiment reads the same 17 sessions:

| Source | Sessions | Frames | What they cover |
|---|---|---|---|
| Fixture corpus [`testdata/ttd/`](../../../../testdata/ttd/README.md) | 5 | 300 each | Pentagon 128K idle, Dizzy Y, 7th Reality, Across the Edge, TSFM music; with a classic GS card |
| Benchmark matrix cases, recorded by [`common/record-datasets.sh`](common/record-datasets.sh) | 12 | 1,500 each | ZX-Evo idle and with GS512 + MoonSound + TSFM; ATM710 turbo; Scorpion; Pentagon demo, demo 2, game, GS upload, MoonSound upload, disk loading; Pentagon 1024; Profi |

- The matrix cases come from the [TTD benchmark harness](../../../../tools/verification/ttd-bench/README.md). The workloads are replayable — fixed start state, scripted input, frozen RTC, zeroed power-on RAM — so a re-recording gives byte-identical files.
- `record-datasets.sh` keeps their saved sessions, through `UNREAL_TTD_BENCH_KEEP_SESSIONS`, in `<repo>/scratch/ttd-experiments/sessions/`. That folder is git-ignored and takes about 1.5 minutes to fill.
- **Real-use sessions (E5, E6):** [`common/record-real-sessions.sh`](common/record-real-sessions.sh) records six sessions, 1 and 5 minutes each, into `<repo>/scratch/ttd-experiments/real/` with the benchmark's metrics: Pentagon 128 at the BASIC prompt, a game, 7th Reality, Across the Edge, Eye Ache, and ZX-Evo at the BASIC prompt (about 15 minutes, 1 GB).
- **Regions analyzed:** machine RAM, and the classic General Sound RAM, which v1 keeps inside the GS device blob. MoonSound wave memory, NeoGS memory and the EEPROMs are not recorded by v1 and so cannot be analyzed.

## Common code

| File | What |
|---|---|
| [`common/ttdhistory.py`](common/ttdhistory.py) | Reads a `.ttd` file with the project's analyzer (`tools/verification/ttd-analyzer`) and finds, per checkpoint, the pieces whose **content** changed. A v1 key frame gives unchanged pieces new slot ids, so slot ids alone would overstate change |
| [`common/piecestats.py`](common/piecestats.py) | For every change: zstd level 1 size of the XOR difference and of the full piece. Also the full size of every piece at each v1 key frame. Its sizes match the payloads the emulator stored, byte for byte. Cached in `<repo>/scratch/ttd-experiments/cache/` |
| [`common/datasets.py`](common/datasets.py) | The list of input sessions |

## Running everything

Requires a build configured with `-DBENCHMARKS=ON` (for `core-benchmarks` and the static zstd), Python 3 with `numpy` and `zstandard`:

```bash
JOBS=$(( $(sysctl -n hw.ncpu 2>/dev/null || nproc) / 2 )); JOBS=$(( JOBS < 1 ? 1 : JOBS ))
cmake -S . -B cmake-build-agent-release -G Ninja -DBENCHMARKS=ON
ninja -C cmake-build-agent-release -j "$JOBS" core-benchmarks

cd tools/poc/011-ttd-v2-capture-analysis/experiments
common/record-datasets.sh
(cd e1-chain-limit && python3 run.py)
(cd e2-encode-once && python3 run.py)          # E4 needs its timing.json
(cd e3-reference-blocks && python3 run.py)
(cd e4-restore-differences && python3 run.py)
common/record-real-sessions.sh                # E5 (its bench.json) and E6
(cd e6-v1-v2-model && python3 run.py)
```

Byte counts and chain statistics repeat exactly. Times depend on the host (E2 keeps the minimum of 15 runs per call).
