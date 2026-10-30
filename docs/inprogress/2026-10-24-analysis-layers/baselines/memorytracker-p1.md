# P1: the cost of the tracker level `facts`

Phase P1 adds the L0 access facts ([design.md](../design.md) "L0 Access facts", module doc
[analysis-layers.md](../../../emulator/analysis/analysis-layers.md)). This page measures them against the budget
of design.md and checks that the levels that existed before did not get slower. Method and benchmark as in
[memorytracker-p0.md](memorytracker-p0.md); P1 adds two levels to `BM_MemoryTrackerFrame`:

| Level | Arg | What is on |
|-------|----:|-----------|
| facts-z80 | 4 | counters-z80 plus the facts (`TrackerLevel::Facts`, the Z80's access context attached) |
| facts-phys | 5 | counters-phys plus the facts per physical page |

## Method

- Release build, `tools/build/build.sh core-benchmarks`, two trees:
  - **base**: `c0995dcf1` (the P0 tree), built in a detached worktree of its own;
  - **new**: the P1 tree (`c0995dcf1` plus the P1 changes).
- Four rounds; in each round one base process and then one new process, 15 repetitions each, Google Benchmark's
  random interleaving inside a process. The machine was loaded (another session's builds); more rounds instead of
  waiting.
- `facts` vs `counters`: the median of the paired per-repetition ratios inside the new processes (60 pairs), ± the
  95 % half-width as in P0.
- new vs base: per round the ratio of the two processes' medians (they ran back to back), the median and the range
  over the four rounds; and the ratio of the pooled medians.

```bash
# scratch/analysis-p1/run_bench.sh: base and new alternating, 4 rounds
./core-benchmarks --benchmark_filter=BM_MemoryTrackerFrame --benchmark_repetitions=15 \
    --benchmark_enable_random_interleaving=true --benchmark_out=<side><round>.json --benchmark_out_format=json
```

| | |
|---|---|
| Host | Apple M1 Ultra, macOS 15.8.1, Apple clang 17.0.0 (clang-1700.6.4.2) |
| Date | 2026-10-26 00:32-00:37 |
| Load (1 / 5 / 15 min) | start 38.3 / 61.6 / 59.5, end 39.5 / 42.6 / 50.4 |

## Results: `facts` against the budget

Budget (design.md): `facts` at most 25 % slower than `counters` (1.25x).

| Machine | Level | median ms/frame | p10-p90 ms | vs off (paired) | vs counters, same mode (paired) |
|---|---|---:|---:|---:|---:|
| 48K garfield | counters-z80 | 0.275 | 0.269-0.334 | +90.0 ± 5.1 % | - |
| 48K garfield | facts-z80 | 0.314 | 0.311-0.371 | +119.6 ± 5.7 % | **1.149x ± 0.024** |
| 48K garfield | counters-phys | 0.265 | 0.262-0.321 | +84.5 ± 6.0 % | - |
| 48K garfield | facts-phys | 0.313 | 0.308-0.380 | +117.2 ± 5.2 % | **1.181x ± 0.032** |
| 128K greenberet | counters-z80 | 0.312 | 0.304-0.370 | +126.3 ± 6.4 % | - |
| 128K greenberet | facts-z80 | 0.344 | 0.338-0.402 | +151.5 ± 6.2 % | **1.106x ± 0.025** |
| 128K greenberet | counters-phys | 0.304 | 0.298-0.366 | +121.4 ± 6.3 % | - |
| 128K greenberet | facts-phys | 0.338 | 0.332-0.391 | +146.8 ± 5.5 % | **1.115x ± 0.024** |
| PENTAGON dizzyx | counters-z80 | 0.285 | 0.279-0.334 | +109.3 ± 5.6 % | - |
| PENTAGON dizzyx | facts-z80 | 0.325 | 0.319-0.385 | +139.2 ± 6.5 % | **1.141x ± 0.024** |
| PENTAGON dizzyx | counters-phys | 0.273 | 0.269-0.321 | +100.1 ± 5.2 % | - |
| PENTAGON dizzyx | facts-phys | 0.318 | 0.313-0.386 | +133.9 ± 6.2 % | **1.162x ± 0.032** |

facts-z80 / counters-z80 per round: 48K 1.150, 1.153, 1.145, 1.145; 128K 1.110, 1.098, 1.174, 1.092; Pentagon
1.135, 1.148, 1.133, 1.148.

**Within the budget**: +11 to +18 %, about 0.03-0.05 ms per turbo frame. The access context stays in the Z80
(design.md's fallback, stack and block kinds in the replay tracer, is not needed). Facts held:
1 MB in Z80 mode; in physical mode 1.8 MB on the Pentagon (the 1 MB Z80 table is allocated too, unused, plus
256 KB per touched page).

## Results: the levels that existed (new vs base)

| Machine | Level | base median ms | new median ms | new / base pooled | per round: median (min..max) |
|---|---|---:|---:|---:|---:|
| 48K | off | 0.143 | 0.144 | 1.010 | 1.006 (0.945..1.087) |
| 48K | debug | 0.216 | 0.221 | 1.023 | 1.023 (0.896..1.127) |
| 48K | counters-z80 | 0.268 | 0.275 | 1.024 | 1.028 (1.018..1.146) |
| 48K | counters-phys | 0.259 | 0.265 | 1.021 | 1.019 (0.985..1.026) |
| 128K | off | 0.136 | 0.137 | 1.007 | 1.006 (0.988..1.118) |
| 128K | debug | 0.251 | 0.256 | 1.020 | 1.021 (0.956..1.031) |
| 128K | counters-z80 | 0.303 | 0.312 | 1.028 | 1.022 (0.950..1.144) |
| 128K | counters-phys | 0.296 | 0.304 | 1.027 | 1.022 (0.982..1.093) |
| PENTAGON | off | 0.135 | 0.137 | 1.015 | 1.010 (0.937..1.134) |
| PENTAGON | debug | 0.204 | 0.209 | 1.025 | 1.020 (0.902..1.158) |
| PENTAGON | counters-z80 | 0.277 | 0.285 | 1.026 | 1.023 (0.971..1.157) |
| PENTAGON | counters-phys | 0.266 | 0.273 | 1.029 | 1.027 (0.999..1.090) |

- **off: +0.6 to +1.5 %**, inside the A/A noise of about 1.8 % (design.md). What off pays is one relaxed load
  of the null context pointer after each opcode fetch.
- **debug and counters: +2.0 to +2.9 %**, at the edge of the noise and the same sign on all three machines, so
  probably real: the tracker tests the level once per access (`_level == Facts`, `[[unlikely]]`) and the prefix
  fetch (`m1_cycle`) tests the context pointer too. A between-process comparison under this load has a wide
  per-round range (0.90..1.16); a quiet A/B with more rounds would pin it down. Both levels are debugger levels,
  not what a user runs.
