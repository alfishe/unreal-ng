# Snapshot Loading Optimizations

> **Date**: 2026-01-13 | **Status**: ✅ Completed

## Overall Performance Summary

| Format | Baseline | After Optimizations | **Improvement** |
|--------|----------|---------------------|-----------------|
| SNA 48K | ~100 μs | ~82 μs | **18% faster** |
| SNA 128K | ~109 μs | ~81 μs | **26% faster** |
| Z80 | ~106 μs | ~74 μs | **30% faster** |

---

# 1. SNA Loader

## Problem
SNA loader was dominated by screen rendering operations during snapshot restoration, not by format-specific code.

## Optimization Applied
All improvements came from **Common Optimizations** section below (screen rendering).

## Results
| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| SNA 48K Reload | 100 μs | 82 μs | **18%** |
| SNA 128K Reload | 109 μs | 81 μs | **26%** |

---

# 2. Z80 Loader

## Problem (What Was)
Profiling identified `decompressPage` as hotspot (38% of samples):
- Byte-by-byte RLE loop instead of bulk fill
- Wasteful initial `memset(dst, 0, dstLen)` that gets overwritten

## Optimization Alternatives

| Approach | Description | Expected Speedup | Complexity |
|----------|-------------|------------------|------------|
| **memset for RLE** | Replace byte loop with memset call | 2-5x | Low |
| memcpy for literals | Coalesce consecutive literals | 1.5-2x | Medium |
| SIMD pattern detection | NEON scan for ED ED patterns | 2-3x | High |
| Prefetch hints | `__builtin_prefetch` for cache warming | 1.1x | Low |

## Selected Approach
**memset for RLE sequences + deferred zero-fill**
- **Rationale**: Simple, portable, high impact
- **Trade-off**: Doesn't optimize literal sequences (future work)

## Measurement
```
BM_DecompressPage_Original/median   2353 ns   (6.48 GB/s)
BM_DecompressPage_Optimized/median  1045 ns  (14.60 GB/s)  ← 2.25x faster
```

## Conclusion
| Metric | Original | Optimized | Improvement |
|--------|----------|-----------|-------------|
| Time per 16K page | 2.35 μs | 1.05 μs | **2.25x** |
| Z80 reload total | 106 μs | 74 μs | **30%** |

**Files Changed**:
- [loaders/snapshot/loader_z80.h](../../../core/src/loaders/snapshot/loader_z80.h)
- [loaders/snapshot/loader_z80.cpp](../../../core/src/loaders/snapshot/loader_z80.cpp)
- [benchmarks/loaders/snapshot_benchmark.cpp](../../../core/benchmarks/loaders/snapshot_benchmark.cpp)

## Post-Optimization Profile Analysis

| Method | Before | After | Change |
|--------|--------|-------|--------|
| `decompressPage` | 38 (38%) | **0** | ✅ **Eliminated** |
| `RenderOnlyMainScreen` | 34 (34%) | 30 (32%) | Irreducible (NEON) |
| `FillBorderWithColor` | 15 (15%) | 21 (22%) | Irreducible (fill_n) |
| File I/O + framework | 11 (11%) | 43 (46%) | Dominates now |

> **Analysis**: `decompressPage` no longer appears as hotspot. Remaining time is dominated by screen rendering (already optimized) and benchmark framework overhead.

## Future Work
- Streaming literal copy with memcpy
- SIMD pattern detection for ED ED sequences
- Prefetch hints

---

# 3. Common Optimizations (Screen Rendering)

## Problem (What Was)
Both SNA and Z80 loaders call screen rendering during snapshot restoration:

| Method | Samples | Issue |
|--------|---------|-------|
| `FillBorderWithColor()` | 23 (4.4%) | 4 nested loops, pixel-by-pixel writes |
| `RenderOnlyMainScreen()` | 52 (10%) | Triple nested loop, per-pixel offset calc |

## Optimization Alternatives

### FillBorderWithColor
| Approach | Description | Expected Speedup |
|----------|-------------|------------------|
| **Row-based std::fill_n** | Fill entire rows at once | 5-10x |
| SIMD NEON vst1q | NEON to fill 4 pixels at once | 8-15x |
| Platform memset | Platform-specific 32-bit fills | 3-5x |

### RenderOnlyMainScreen
| Approach | Description | Expected Speedup |
|----------|-------------|------------------|
| **Reuse RenderScreen_Batch8()** | Existing NEON batch renderer | 1.5-2x |
| Custom NEON blit | Dedicated NEON screen blit | 2-3x |
| Multithreaded rows | Parallelize row rendering | 1.5x |

## Selected Approach
- **FillBorderWithColor**: Row-based `std::fill_n` (simple, compiler optimizes to SIMD)
- **RenderOnlyMainScreen**: Reuse `RenderScreen_Batch8()` (zero additional complexity)

## Measurement
```
BM_FillBorderWithColor_Original/median    172342 ns
BM_FillBorderWithColor_Optimized/median    24059 ns  ← 7.2x faster

BM_RenderOnlyMainScreen_Original/median    18739 ns
BM_RenderOnlyMainScreen_Optimized/median   14121 ns  ← 1.3x faster
```

## Conclusion
| Method | Original | Optimized | Speedup |
|--------|----------|-----------|---------|
| `FillBorderWithColor` | 171 μs | 24 μs | **7.2x** |
| `RenderOnlyMainScreen` | 18 μs | 14 μs | **1.3x** |

**Files Changed**:
- [emulator/video/zx/screenzx.h](../../../core/src/emulator/video/zx/screenzx.h)
- [emulator/video/zx/screenzx.cpp](../../../core/src/emulator/video/zx/screenzx.cpp)
- [benchmarks/emulator/video/screenzx_benchmark.cpp](../../../core/benchmarks/emulator/video/screenzx_benchmark.cpp)
