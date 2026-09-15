# TTD V1 vs V2 Benchmark Report

**Date:** 2026-09-11  
**Objective:** Compare TTD V1 (full frame snapshots) vs V2 (page-granular XOR with back-references) for storage efficiency and restore latency.

---

## Executive Summary

V2 with I-frame every 50 frames achieves **40x smaller storage** than V1 with only **155 µs restore latency** (2.6% of 6ms frame budget). This is the recommended configuration.

| Metric | V1 | V2 @50 | Improvement |
|--------|-----|--------|-------------|
| Storage (10s) | 64 MB | 1.6 MB | **40x smaller** |
| Restore 1 frame | 3.6 µs | 155 µs | 43x slower |
| % of 6ms budget | 0.06% | 2.6% | Acceptable |

---

## Problem Statement

TTD (Time Travel Debugging) requires efficient storage and fast restore of emulator state snapshots. Two encoding strategies were evaluated:

- **V1:** Store full frame snapshot every frame (128KB compressed per frame)
- **V2:** Store page-granular XOR deltas with periodic I-frames (keyframes)

Key questions:
1. What are the storage/restore trade-offs?
2. Does batch compression help V2?
3. What is the optimal I-frame interval?

---

## Test Configuration

- **RAM model:** 128KB (32 × 4KB pages)
- **Frame rate:** 50 FPS
- **Static pages:** 60% (ROM shadow, unused RAM) — never change
- **Active pages:** 40% with ~15% change probability per frame
- **Bytes changed per modified page:** ~1.5%
- **Test duration:** 10 seconds (500 frames)

---

## Methodology

### Benchmarks Created

1. `ttd_v1_vs_v2_bench.cpp` — End-to-end V1 vs V2 comparison
2. `batch_size_bench.cpp` — Page batching impact on compression and restore
3. `v1_v2_batch_compare.cpp` — V2 batch=1 vs batch=4 vs batch=8
4. `v2_restore_breakdown.cpp` — Profile individual operations
5. `iframe_interval_tradeoff.cpp` — I-frame interval vs storage/restore

### Metrics Measured

- **Storage:** Total compressed bytes for 10 seconds of recording
- **Restore:** Time to unpack + unroll 1 frame to RAM cache (random position)
- **Capture:** Time to encode 1 second of frames

---

## Results

### 1. V1 vs V2 Basic Comparison

Initial benchmark with I-frame every 50 frames:

| Metric | V1 (Full Snapshot) | V2 (Page XOR) |
|--------|-------------------|---------------|
| Storage (10s) | 65.5 MB | 328 KB |
| Compression ratio | 1x | **199x** |
| Capture latency | 3.6 ms/sec | 5.3 ms/sec |
| Restore 1 frame | 3.7 µs | 160 µs |

**Initial finding:** V2 achieves 199x compression but 43x slower restore.

### 2. Batch Compression Impact

Tested whether batching multiple pages together improves V2:

| Batch Size | Storage | Restore | vs batch=1 |
|------------|---------|---------|------------|
| 1 | 314 KB | 160 µs | baseline |
| 4 | 290 KB | 110 µs | 8% smaller, **31% faster** |
| 8 | 289 KB | 108 µs | 8% smaller, 32% faster |

**Finding:** Batching makes V2 **both smaller AND faster** by amortizing zstd per-call overhead. batch=4 is optimal.

### 3. Restore Latency Breakdown

Profiled individual operations to find the bottleneck:

| Operation | Time |
|-----------|------|
| memcpy 128KB keyframe | 2.2 µs |
| Decompress 1 page (4KB) | 3.2 µs |
| XOR apply 1 page | 62 ns |
| Decompress + XOR 1 page | 3.2 µs |

**Root cause identified:** The delta chain length, not compression or XOR.

V2 restore formula:
```
restore_time = memcpy_keyframe + (delta_frames × pages_per_frame × 3.2µs)
```

With I-frame every 50 frames and ~2 pages changed per frame:
- Average delta chain: 25 frames
- Decompressions: 25 × 2 = 50
- Time: 2.2 + (50 × 3.2) = **162 µs** ✓

### 4. I-frame Interval Trade-off

Tested different I-frame intervals to find optimal balance:

| I-frame Interval | Storage | Restore | vs V1 Storage | vs V1 Restore |
|------------------|---------|---------|---------------|---------------|
| V1 (every frame) | 64 MB | 3.6 µs | 1x | 1x |
| V2 @5 frames | 13.1 MB | 16 µs | 4.9x smaller | 4.4x slower |
| V2 @10 frames | 6.7 MB | 31 µs | 9.5x smaller | 8.6x slower |
| V2 @25 frames | 2.9 MB | 78 µs | 22x smaller | 22x slower |
| **V2 @50 frames** | **1.6 MB** | **155 µs** | **40x smaller** | 43x slower |

**Key insight:** Deltas are constant at 314 KB — keyframes dominate storage. More frequent I-frames = faster restore but larger storage.

---

## Analysis

### Why V2 is 40x smaller

1. **Static page back-references:** 60% of RAM pages (ROM shadow, unused memory) never change. V2 stores them once and references forever.

2. **Sparse XOR deltas:** Changed pages have only ~1.5% nonzero bytes after XOR. zstd compresses these to ~50-80 bytes per 4KB page.

3. **Skip unchanged pages:** V2 only stores deltas for pages that actually changed (~2 pages per frame).

### Why V2 restore takes 155 µs

1. **Delta chain:** Must apply deltas from nearest I-frame to target frame (avg 25 frames with I-frame @50).

2. **Per-delta cost:** Each page delta requires one zstd decompress (3.2 µs) + XOR (62 ns).

3. **Total:** 2.2 µs keyframe copy + 50 decompressions × 3.2 µs ≈ 162 µs

### Why 155 µs is acceptable

- 6ms frame budget for TTD restore
- 155 µs = 2.6% of budget
- Leaves 97.4% headroom for other operations
- Timeline scrubbing at 50 FPS only needs 20ms per frame

---

## Decision Matrix

| Priority | Recommended Config | Trade-off |
|----------|-------------------|-----------|
| **Maximum storage efficiency** | V2 @50 | 40x smaller, 155 µs restore |
| Balanced | V2 @25 | 22x smaller, 78 µs restore |
| Fast scrubbing | V2 @10 | 9.5x smaller, 31 µs restore |
| Instant seeks | V1 | 1x storage, 3.6 µs restore |

---

## Recommendations

### Primary Recommendation

**Use V2 with I-frame every 50 frames (1/sec) + batch=4**

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| I-frame interval | 50 frames | 40x smaller than V1 |
| Page batching | **batch=4** | 8% smaller + 31% faster than batch=1 |
| Restore latency | ~110 µs | 1.8% of 6ms budget |

**Why batch=4:** Batching amortizes zstd per-call overhead (~3 µs). Fewer decompress calls = faster restore + better compression.

### Implementation Notes

1. **I-frame interval:** 50 frames (1 per second at 50 FPS)
2. **Page batching:** batch=4 pages together for compression
3. **Static page detection:** Hash pages to detect unchanged content for back-references
4. **Keyframe storage:** Store compressed keyframes for random access base

### Future Optimization Opportunities

1. **Adaptive I-frame interval:** More frequent I-frames during high-activity periods
2. **Page prediction:** Pre-cache likely-needed pages based on access patterns
3. **Parallel decompression:** Decompress multiple page deltas concurrently

---

---

## C++ Benchmark Results — REAL TTD FILES

| File | Frames | V1 Storage | V2 Storage | Ratio |
|------|--------|------------|------------|-------|
| demo_7threality | 303 | 25.7 MB | 707 KB | **36x** |
| demo_across-the-edge | 301 | 21.8 MB | 716 KB | **31x** |
| active_demo | 303 | 5.2 MB | 153 KB | **34x** |
| idle_session | 302 | 2.8 MB | 271 KB | **10x** |
| **TOTAL** | 1209 | **55.5 MB** | **1.85 MB** | **30x** |

### Restore Latency (C++, demo_7threality)

| Metric | V1 | V2 (I@50 + batch=4) | Ratio |
|--------|-----|-----|-------|
| **Restore** | 98 µs | 161 µs | **1.6x slower** |
| **% of 6ms** | 1.6% | **2.7%** | Acceptable |

**V2 achieves 96.7% storage savings with only 1.6x slower restore on real data.**

---

## Appendix: Benchmark Code

All benchmarks are in:
```
tools/poc/011-ttd-v2-capture-analysis/benchmarks/
├── ttd_v1_vs_v2_bench.cpp
├── batch_size_bench.cpp
├── v1_v2_batch_compare.cpp
├── v2_restore_breakdown.cpp
└── iframe_interval_tradeoff.cpp
```

Build and run:
```bash
cd tools/poc/011-ttd-v2-capture-analysis/build
cmake .. && cmake --build . --target ttd_v2_benchmarks
./bin/ttd_v2_benchmarks --benchmark_filter="BM_V2"
```
