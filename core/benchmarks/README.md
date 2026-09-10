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
