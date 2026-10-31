# Core Benchmarks

Performance benchmarks for critical emulator subsystems using Google Benchmark.

## Building

```bash
cd core/benchmarks
mkdir build && cd build
cmake ..
cmake --build . -j8
```

## Running

```bash
./build/bin/core-benchmarks                    # Run all
./build/bin/core-benchmarks --benchmark_filter="BM_Frame"  # Filter by name
```

## Frame Profiles

Located in `emulator/profiles/`. `BM_Prof_<machine>_<profile>` measures one whole frame of a machine built from its
shipped config, in several run profiles: 1x, turbo, turbo with the host-side switches off, debugging, the TTD worst
case, and the shipped cards removed or fitted one at a time. Guide (running, comparing two commits, profiling a row
with callgrind / perf / xctrace) and recorded results: [emulator/profiles/README.md](emulator/profiles/README.md).

## TTD Benchmark Matrix

Located in `debugger/ttd/`. `ttd_matrix_benchmark.cpp` registers one benchmark per TTD engine, configuration and workload: `TTDMatrix/<engine>/<configuration>/<workload>`, with the metrics BM-1 to BM-8 as counters. The environment variables `UNREAL_TTD_BENCH_*` pick the set (`ci`, `turbo` or `full`) and the engines. Running, comparing and baselines: [`tools/verification/ttd-bench/`](../../tools/verification/ttd-bench/README.md).

## Sprinter Demo Benchmarks

Located in `emulator/machines/`. `BM_SprinterDemo_<demo>_{Bare,Shipped}` measures the host CPU per frame of a demo
from the Sprinter's DSS system disk (DNTBLINK, ROTOZOOM, PLASMA2, BADAPPLE).
- **Bare**: without the sound cards.
- **Shipped**: with the shipped config's NeoGS and AY.

They need `UNREAL_SPRINTER_HDD` (the raw `sp_hdd_sys.img` from the MAME pack); without it they are skipped with
an error. Details, method and results: [emulator/machines/README.md](emulator/machines/README.md).

## Memory Tracker Frame Benchmark

`emulator/memory/memorytracker_frame_benchmark.cpp`: `BM_MemoryTrackerFrame/machine:<m>/level:<l>`. It measures one
turbo frame of a real game (48K Garfield, 128K Green Beret, Pentagon Dizzy X, from `testdata/`) at each
`MemoryAccessTracker` level:
- 0: off;
- 1: debug mode without tracking;
- 2: counters in Z80 mode;
- 3: counters in physical mode.

Run it with `--benchmark_repetitions=15 --benchmark_enable_random_interleaving=true`. Baseline and method:
[memorytracker-p0.md](../../docs/inprogress/2026-10-24-analysis-layers/baselines/memorytracker-p0.md).

## Next Layer Capture Benchmark

`emulator/video/nextlayercapture_benchmark.cpp`: what recording the layers of a ZX Spectrum Next frame costs (design: `docs/inprogress/2026-10-07-zx-next/design-layer-capture.md`
section 3.2 and 7). `BM_NextFrame_LayerCapture/{0 full emulated frame, 1 bare ScreenNext render}` runs interleaved A/B rounds (capture off, capture on, the order flipped every
round, thread CPU time) over a scene with every layer, 64 sprites and a copper list, and reports the median overhead with its 95 % interval (`overhead_pct`, `ci95_pct`, `p10_pct`,
`p90_pct`). `BM_NextFrame_Plain/{0|1}/{0 off|1 on}` is one configuration per run, for comparing two builds (`-DLAYER_CAPTURE_BASELINE` builds it against a tree without the record).
Do not wait for a quiet machine: more rounds and interleaving, and report the spread.

## Message Center Benchmarks

Located in `emulator/messagecenter/`. Tests the pub/sub notification system used for frame sync, debug events, and inter-component communication.

### Benchmarks

| Benchmark | Description |
|-----------|-------------|
| `BM_FrameRefreshPattern` | Video frame notification (~50Hz hot path) |
| `BM_MultiInstancePattern` | Multiple emulators posting to shared topic (videowall) |
| `BM_ObserverFanout` | Single post dispatched to N observers |
| `BM_DebugStepBurst` | Rapid CPU step notifications (instruction stepping) |
| `BM_PayloadAllocation_None` | Post with no payload |
| `BM_PayloadAllocation_Number` | Post with 32-bit number payload |
| `BM_PayloadAllocation_Text` | Post with string payload |
| `BM_PayloadAllocation_Frame` | Post with frame payload (string + uint32) |
| `BM_TopicRegistration` | Topic lookup/registration overhead |
| `BM_FullFrameCycle` | Combined video + audio frame notifications |

### Results (Apple M3 Max, 2026-08-20)

```
Benchmark                                Time        Throughput
----------------------------------------------------------------
BM_FrameRefreshPattern                 22.4 ns      44.6 M/s
BM_MultiInstancePattern/1              27.9 ns      36.3 M/s
BM_MultiInstancePattern/4               116 ns      34.7 M/s
BM_MultiInstancePattern/16              451 ns      35.8 M/s
BM_ObserverFanout/1                    18.7 ns      53.7 M/s
BM_ObserverFanout/4                    32.3 ns     124.6 M/s
BM_ObserverFanout/16                    117 ns     138.0 M/s
BM_DebugStepBurst                       8.0 ns     125.2 M/s
BM_PayloadAllocation_None               7.7 ns     129.9 M/s
BM_PayloadAllocation_Number            19.3 ns      51.7 M/s
BM_PayloadAllocation_Text              28.5 ns      35.1 M/s
BM_PayloadAllocation_Frame             30.2 ns      33.1 M/s
BM_TopicRegistration (6 topics)        ~40 ns     150.0 M/s
BM_FullFrameCycle                      ~30 ns      65.0 M/s
```

### Key Observations

- **Frame refresh at 22ns** - supports 45M frames/sec, far exceeding 50Hz requirement
- **No-payload post at 7.7ns** - pure dispatch overhead, essentially function call cost
- **Observer fanout scales linearly** - ~7ns per additional observer
- **Payload allocation dominates** - string allocation (Text/Frame) adds ~20ns vs bare post

### Implementation

Uses `FastEventQueue` from `3rdparty/message-center/eventqueue_fast.h`:
- Inline synchronous dispatch (no worker thread)
- Fixed-size observer arrays (no heap allocation on dispatch)
- Topic IDs resolved once at registration
- Cache-line aligned atomics for future async support

## Adding New Benchmarks

1. Create `emulator/<subsystem>/<name>_benchmark.cpp`
2. Include `<benchmark/benchmark.h>`
3. Use `BENCHMARK()` macro to register
4. Files are auto-discovered by CMake glob
