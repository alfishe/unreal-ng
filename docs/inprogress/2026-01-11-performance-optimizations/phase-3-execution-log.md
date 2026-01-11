# Phase 3 Execution Log: Branch-Free Color Selection

**Date**: 2026-01-11  
**Status**: ✅ Complete

---

## Summary

Replaced conditional (ternary) color selection in `Draw()` with branch-free arithmetic operations to eliminate pipeline stalls from branch misprediction.

## Demo Compatibility Consideration

> **Racing the Beam**: ZX-Spectrum demos frequently modify attribute memory while the ULA 
> is actively rendering, achieving up to 8 colors per character cell. This means we 
> **cannot batch 8 pixels** together - attributes must be read at each 2-pixel t-state.
>
> This constraint required abandoning the originally planned SIMD 8-pixel optimization
> in favor of this targeted 2-pixel branch-free approach.

---

## Implementation

### Before (Conditional Branching)
```cpp
// Ternary causes pipeline stalls on misprediction
framebufferARGB[framebufferOffset] = ((pixels << lut.pixelXBit) & 0x80) ? colorInk : colorPaper;
```

### After (Branch-Free Arithmetic)
```cpp
// Branch-free: no pipeline stalls, constant execution time
uint32_t bit0 = (pixels << lut.pixelXBit) & 0x80;
uint32_t mask0 = static_cast<uint32_t>(-static_cast<int32_t>(bit0 >> 7));
framebufferARGB[framebufferOffset] = (colorInk & mask0) | (colorPaper & ~mask0);
```

### How It Works

1. `(pixels << lut.pixelXBit) & 0x80` → extracts pixel bit at position (0x80 or 0x00)
2. `bit >> 7` → shifts to bit 0 (0x01 or 0x00)
3. `-static_cast<int32_t>(...)` → negation creates mask (0xFFFFFFFF or 0x00000000)
4. Bitwise AND/OR selects ink or paper without branching

---

## Benchmark Results

```
----------------------------------------------------------------------------------
Benchmark                                        Time             CPU   Iterations
----------------------------------------------------------------------------------
BM_DrawFrame_Original/iterations:100        346944 ns       345350 ns          100
BM_DrawFrame_LUT/iterations:100             141167 ns       140340 ns          100
BM_DrawFrame_LUT_Ternary/iterations:100     139407 ns       138780 ns          100
```

### Performance Comparison

| Version | Time per Frame | vs Original |
|:---|---:|:---:|
| Original (ternary, no LUT) | 345.4 μs | 1.0x |
| Phase 2 (LUT + ternary) | 138.8 μs | 2.49x |
| Phase 3 (LUT + branch-free) | 140.3 μs | 2.46x |

**Phase 3 Finding**: No measurable improvement from branch-free optimization on Apple M-series ARM.
The modern branch predictor handles the simple ternary pattern efficiently. The ~1-2% difference
is within noise margin.

> **Conclusion**: Branch-free optimization is kept for compatibility with older/simpler CPUs
> but provides no benefit on modern Apple Silicon. The main optimization win remains the LUT
> (Phase 2) which provides **2.5x speedup**.

---

## Files Modified

| File | Change |
|:---|:---|
| [`screenzx.cpp`](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/video/zx/screenzx.cpp) | Branch-free color selection in `Draw()` |
| [`02-optimization-roadmap.md`](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/docs/inprogress/2026-01-11-performance-optimizations/02-optimization-roadmap.md) | Updated phases 3-4 to reflect demo constraint |

---

## Test Results

All 14 ScreenZX tests passing:
- `TstateLUT_DrawProducesSameOutputAsDrawOriginal` confirms identical framebuffer output
- No visual artifacts or compatibility regressions

---

## Verification Commands

```bash
# Run framebuffer comparison test
./build/bin/core-tests --gtest_filter="ScreenZX_Test.TstateLUT_DrawProducesSameOutputAsDrawOriginal"

# Run all ScreenZX tests
./build/bin/core-tests --gtest_filter="ScreenZX_Test.*"

# Run benchmarks
./build/bin/core-benchmarks --benchmark_filter="BM_DrawFrame"
```

---

## Next Steps

**Phase 4**: Qt opaque rendering optimization (estimated ~12% → ~2% improvement)
