# GIF Encoder Extreme Optimization Opportunities

## Overview

This document captures potential extreme optimizations for `gif.cpp` that could further improve GIF encoding performance beyond the optimizations already implemented.

> [!IMPORTANT]
> **Cross-Platform Requirement:** All optimizations MUST be cross-platform compatible. Any platform-specific optimization (SIMD, intrinsics) MUST have a safe scalar fallback that produces identical results.

**Current State (2026-01-11):**
- `GIFAnimationHelper` removed - `GIFEncoder` now directly uses `gif.h`
- Hash lookup (`GifColorLookup`) provides O(1) exact color matching (1.5× speedup)
- Fixed palette mode (`FixedZX16`) provides 1.3× speedup via k-d tree
- DirectZX deprecated - hash lookup is faster due to branch-free execution
- 51 unit tests pass, all benchmarks work

**Completed Optimizations:**
| OPT | Name | Status | Speedup |
|-----|------|--------|---------|
| OPT-1 | Direct ZX Index | ✅ Implemented | 1.6× |
| OPT-7 | Hash Lookup | ✅ Implemented | 1.5× |

**Goal:**
- Apply SIMD and algorithmic optimizations to further reduce encoding time
- Maintain correctness via unit tests
- Measure each optimization individually and combined
- **Ensure all platforms compile and run correctly**

---

## Hot Path Analysis

### Current Performance Profile

| Function | Time Share | Called Per | Description |
|----------|------------|------------|-------------|
| `GifGetClosestPaletteColor` | 40-60% | Every changed pixel | k-d tree traversal |
| `GifThresholdImage` | 20-30% | Every frame | Pixel loop + color lookup |
| `GifWriteLzwImage` | 15-25% | Every frame | LZW compression |
| `GifDitherImage` | 10-20% | Optional | Floyd-Steinberg error propagation |

### Code Locations

| Function | File | Line Range |
|----------|------|------------|
| `GifGetClosestPaletteColor` | `gif.cpp` | 22-72 |
| `GifThresholdImage` | `gif.cpp` | 437-472 |
| `GifDitherImage` | `gif.cpp` | 330-435 |
| `GifWriteLzwImage` | `gif.cpp` | 537-660 |

---

## Optimization Opportunities

### OPT-1: Direct ZX Palette Index Lookup

**Impact: HIGH (20-50× faster for color lookup)**

Replace k-d tree traversal with direct calculation for ZX Spectrum colors:

```cpp
// ZX Spectrum colors are deterministic: bits encode RGB presence
uint8_t GetZXPaletteIndexDirect(uint8_t r, uint8_t g, uint8_t b)
{
    // Detect intensity level
    bool isBright = (r == 0xFF || g == 0xFF || b == 0xFF);
    uint8_t base = isBright ? 8 : 0;
    
    // Encode color bits (threshold at 0x80)
    uint8_t color = 0;
    if (r >= 0x80) color |= 0x02;  // Bit 1 = Red
    if (g >= 0x80) color |= 0x04;  // Bit 2 = Green
    if (b >= 0x80) color |= 0x01;  // Bit 0 = Blue
    
    return base + color;
}
```

**Complexity:** O(1) vs O(log n) for k-d tree
**Applicability:** Only valid for exact ZX palette colors
**Risk:** Low - simple conditional logic
**Status:** ✅ Implemented in `gif.cpp` as `GifGetZXPaletteIndexDirect`

---

### OPT-7: Hash Table Exact Lookup (IMPLEMENTED)

**Impact: HIGH (1.5× end-to-end speedup)**

Hash table for O(1) exact color matching with any palette:

```cpp
// ~5KB for 256 colors (FNV-1a hash, linear probing)
struct GifColorLookup {
    uint32_t keys[1024];      // ABGR color values
    uint8_t values[1024];     // Palette indices
    bool occupied[1024];      // Slot occupied flags
    int numColors;
    bool valid;
};

void GifBuildColorLookup(GifColorLookup* lookup, const GifPalette* pPal);
int32_t GifGetColorIndex(const GifColorLookup* lookup, uint32_t abgrColor);
```

**Benefits over OPT-1 (DirectZX):**
- Works with ANY palette (not just standard ZX colors)
- Branch-free execution (no pipeline stalls)
- Supports custom INI palettes

**Benchmark Results:**
| Approach | Time | Speedup |
|----------|------|---------|
| Auto Mode | 420 μs | 1.0× |
| Hash Lookup | 284 μs | 1.5× |
| DirectZX | 270 μs | 1.6× |

**Status:** ✅ Implemented
- `GifBuildColorLookup()` - Build hash table from palette
- `GifGetColorIndex()` - O(1) lookup
- `GifWriteFrameExact()` - Full frame encoding with hash lookup
- `GIFEncoder` uses hash lookup for fixed palettes

---

### OPT-2: RGB565 Lookup Table

**Impact: MEDIUM-HIGH (10-20× faster for general case)**

Pre-compute color mapping for all possible 565-quantized colors:

```cpp
// 32×64×32 = 65,536 entries × 1 byte = 64KB LUT
struct ColorLUT565 {
    uint8_t table[32][64][32];  // [R5][G6][B5] → palette index
    
    void Build(const GifPalette* pal);
    
    inline uint8_t Lookup(uint8_t r, uint8_t g, uint8_t b) const {
        return table[r >> 3][g >> 2][b >> 3];
    }
};
```

**Tradeoff:**
- Memory: 64KB per palette
- Build time: ~65K color comparisons (one-time)
- Lookup: O(1) with cache-friendly access

---

### OPT-3: SIMD Delta Detection

**Impact: MEDIUM (2-3× faster for delta detection)**

Vectorized comparison for unchanged pixels:

```cpp
#ifdef __ARM_NEON__
#include <arm_neon.h>

bool ArePixelsIdentical_NEON(const uint8_t* last, const uint8_t* next, size_t count)
{
    for (size_t i = 0; i < count; i += 16)
    {
        uint8x16_t a = vld1q_u8(last + i);
        uint8x16_t b = vld1q_u8(next + i);
        uint8x16_t diff = veorq_u8(a, b);
        
        // Check if all bytes are zero
        if (vmaxvq_u8(diff) != 0) return false;
    }
    return true;
}
#endif

#ifdef __SSE2__
#include <emmintrin.h>

bool ArePixelsIdentical_SSE2(const uint8_t* last, const uint8_t* next, size_t count)
{
    for (size_t i = 0; i < count; i += 16)
    {
        __m128i a = _mm_loadu_si128((__m128i*)(last + i));
        __m128i b = _mm_loadu_si128((__m128i*)(next + i));
        __m128i diff = _mm_xor_si128(a, b);
        
        if (_mm_movemask_epi8(_mm_cmpeq_epi8(diff, _mm_setzero_si128())) != 0xFFFF)
            return false;
    }
    return true;
}
#endif
```

---

### OPT-4: Batch Pixel Processing

**Impact: MEDIUM (2× faster for threshold pass)**

Process 4 pixels at once with SIMD deinterleave:

```cpp
#ifdef __ARM_NEON__
void ThresholdBatch4_NEON(const uint8_t* next, uint8_t* out, 
                          const ColorLUT565& lut)
{
    // Load 4 RGBA pixels (16 bytes)
    uint8x16x4_t rgba = vld4q_u8(next);
    
    // Quantize to 565
    uint8x16_t r5 = vshrq_n_u8(rgba.val[0], 3);
    uint8x16_t g6 = vshrq_n_u8(rgba.val[1], 2);
    uint8x16_t b5 = vshrq_n_u8(rgba.val[2], 3);
    
    // LUT lookup (scalar - NEON lacks gather)
    for (int i = 0; i < 4; i++)
    {
        out[i*4+3] = lut.Lookup(
            vgetq_lane_u8(r5, i),
            vgetq_lane_u8(g6, i),
            vgetq_lane_u8(b5, i));
    }
}
#endif
```

**Limitation:** SIMD lacks gather instructions; scalar fallback for LUT

---

### OPT-5: LZW Optimization (Limited Potential)

**Impact: LOW (inherently serial algorithm)**

LZW is inherently serial (each code depends on previous). Minor optimizations:

- Pre-size code dictionary to avoid reallocs
- Use `memset` intrinsics for dictionary clear
- Batch file writes

**Not recommended** due to low ROI.

---

## Implementation Priority Matrix

| Optimization | Impact | Effort | Risk | Priority |
|-------------|--------|--------|------|----------|
| OPT-1: Direct ZX Index | Very High | Low | Low | **P0** |
| OPT-2: RGB565 LUT | High | Medium | Low | **P1** |
| OPT-3: SIMD Delta | Medium | Medium | Low | P2 |
| OPT-4: Batch Pixels | Medium | High | Medium | P3 |
| OPT-5: LZW | Low | High | High | Skip |

---

## Testing Strategy

### Existing Tests (Must Continue Passing)

```bash
./build/bin/core-tests --gtest_filter="*GIFEncoder*"
```

| Test | Description |
|------|-------------|
| `AutoModeDefault` | Default palette mode works |
| `FixedZX16Mode` | Fixed palette encodes correctly |
| `FixedZX256Mode` | 256-color mode works |
| `FixedModeCreatesValidGIF` | Output is valid GIF file |
| `OnVideoFrameIncreasesCount` | Frame counting works |

---

### OPT-1: Direct ZX Palette Index - Unit Tests

| Test Name | Description | Inputs | Expected |
|-----------|-------------|--------|----------|
| `DirectIndex_Black` | Black maps to index 0 | `(0,0,0)` | `0` |
| `DirectIndex_Blue` | Blue maps correctly | `(0,0,0xCD)` | `1` |
| `DirectIndex_Red` | Red maps correctly | `(0xCD,0,0)` | `2` |
| `DirectIndex_Magenta` | Magenta maps correctly | `(0xCD,0,0xCD)` | `3` |
| `DirectIndex_Green` | Green maps correctly | `(0,0xCD,0)` | `4` |
| `DirectIndex_Cyan` | Cyan maps correctly | `(0,0xCD,0xCD)` | `5` |
| `DirectIndex_Yellow` | Yellow maps correctly | `(0xCD,0xCD,0)` | `6` |
| `DirectIndex_White` | White maps correctly | `(0xCD,0xCD,0xCD)` | `7` |
| `DirectIndex_BrightBlue` | Bright blue maps to 9 | `(0,0,0xFF)` | `9` |
| `DirectIndex_BrightWhite` | Bright white maps to 15 | `(0xFF,0xFF,0xFF)` | `15` |
| `DirectIndex_MatchesKDTree` | Compare full palette lookup | All 16 ZX colors | Matches k-d tree |
| `DirectIndex_FullFrameIdentical` | Encode frame with both methods | 256×192 frame | Byte-identical output |

---

### OPT-2: RGB565 LUT - Unit Tests

| Test Name | Description | Inputs | Expected |
|-----------|-------------|--------|----------|
| `LUT565_Build` | LUT builds without crash | ZX16 palette | 64KB table filled |
| `LUT565_BlackLookup` | Black returns correct index | `(0,0,0)` | Same as k-d tree |
| `LUT565_WhiteLookup` | White returns correct index | `(255,255,255)` | Same as k-d tree |
| `LUT565_QuantizationBoundary` | Test 565 quantization edges | `(7,3,7)` vs `(8,4,8)` | Different buckets |
| `LUT565_AllColors` | Compare all 65536 LUT entries | Full LUT | Matches k-d tree |
| `LUT565_FrameIdentical` | Encode frame with both methods | 256×192 frame | Byte-identical output |
| `LUT565_PerformanceImprovement` | LUT faster than k-d tree | 1000 lookups | LUT < k-d tree time |

---

### OPT-3: SIMD Delta Detection - Unit Tests

| Test Name | Description | Inputs | Expected |
|-----------|-------------|--------|----------|
| `SIMDDelta_AllIdentical` | Identical frames detected | Same 2 frames | `true` |
| `SIMDDelta_FirstPixelDifferent` | First pixel change detected | 1 pixel diff | `false` |
| `SIMDDelta_LastPixelDifferent` | Last pixel change detected | 1 pixel diff | `false` |
| `SIMDDelta_MiddlePixelDifferent` | Middle pixel change detected | 1 pixel diff | `false` |
| `SIMDDelta_SingleBitDifference` | Single bit difference detected | 1 bit diff | `false` |
| `SIMDDelta_MatchesScalar` | SIMD matches scalar result | Random frames | Identical result |
| `SIMDDelta_UnalignedInput` | Works with non-16-byte aligned | Unaligned ptrs | Correct result |
| `SIMDDelta_OddSize` | Works with non-multiple of 16 | 253 pixels | Correct result |

---

### OPT-4: Batch Pixel Processing - Unit Tests

| Test Name | Description | Inputs | Expected |
|-----------|-------------|--------|----------|
| `Batch_4PixelsMatchScalar` | 4 pixels match scalar path | 4 pixels | Identical indices |
| `Batch_16PixelsMatchScalar` | 16 pixels match scalar path | 16 pixels | Identical indices |
| `Batch_FullRowMatchScalar` | Full row (256 px) matches | 256 pixels | Identical indices |
| `Batch_FullFrameMatchScalar` | Full frame matches | 256×192 | Byte-identical |
| `Batch_AllBlack` | All black pixels | All 0s | All index 0 |
| `Batch_AllWhite` | All white pixels | All 255s | All index 15 |
| `Batch_Alternating` | Alternating colors | Checkerboard | Correct pattern |
| `Batch_UnalignedStart` | Non-aligned start address | Offset +1 | Correct result |

---

### Cross-Platform Validation Tests

| Test Name | Description | Platforms |
|-----------|-------------|-----------|
| `Fallback_CompilesWithoutSIMD` | Scalar fallback compiles | All |
| `Fallback_MatchesSIMD_ARM` | NEON matches scalar | macOS ARM, Linux ARM |
| `Fallback_MatchesSIMD_x86` | SSE2 matches scalar | macOS x86, Linux x86, Windows |
| `Fallback_RuntimeDetection` | Correct path selected | All |

---

## Benchmark Strategy

### Baseline Benchmarks (Existing)

```bash
./build/bin/core-benchmarks --benchmark_filter=GIF
```

Current benchmarks:
| Benchmark | Mode | Description |
|-----------|------|-------------|
| `BM_GIFWriteFrame_MainScreen` | Auto | Baseline |
| `BM_GIFWriteFrame_FixedZX16_MainScreen` | Fixed | Current optimization |
| `BM_GIFWriteFrame_MenuScroll` | Both | Typical use case |
| `BM_GIFWriteFrame_HighChange` | Both | Worst case |

### New Benchmarks Required

For each optimization, add corresponding benchmark:

```cpp
// OPT-1: Direct ZX Index
BENCHMARK(BM_GIFWriteFrame_DirectIndex)->Iterations(250);

// OPT-2: RGB565 LUT
BENCHMARK(BM_GIFWriteFrame_LUT565)->Iterations(250);

// OPT-3: SIMD Delta
BENCHMARK(BM_DeltaDetection_Scalar)->Iterations(1000);
BENCHMARK(BM_DeltaDetection_SIMD)->Iterations(1000);

// Combined optimizations
BENCHMARK(BM_GIFWriteFrame_AllOptimizations)->Iterations(250);
```

### Comparison Matrix

After implementation, generate comparison table:

| Scenario | Auto | FixedZX16 | +DirectIdx | +LUT565 | +SIMD | All |
|----------|------|-----------|------------|---------|-------|-----|
| Static | 343μs | 252μs | ? | ? | ? | ? |
| Menu | 1.17ms | 593μs | ? | ? | ? | ? |
| High-change | 13.9ms | 1.8ms | ? | ? | ? | ? |

---

## Verification Checklist

Before merging each optimization:

- [ ] All existing GIF encoder tests pass
- [ ] New comparison tests pass (optimized matches original)
- [ ] Benchmark shows measurable improvement
- [ ] Generated GIF files are visually identical
- [ ] No memory leaks (Valgrind/ASAN)
- [ ] Platform-specific code has fallback (NEON/SSE2 → scalar)

---

## Files to Modify

| File | Changes |
|------|---------|
| `core/src/3rdparty/gif/gif.h` | Add new function declarations |
| `core/src/3rdparty/gif/gif.cpp` | Implement optimizations |
| `core/src/emulator/recording/encoders/gif_encoder.cpp` | Use optimized paths |
| `core/tests/emulator/recording/gif_encoder_test.cpp` | Add comparison tests |
| `core/benchmarks/emulator/recording/gif_encoder_benchmark.cpp` | Add benchmarks |

---

## Platform Considerations

| Platform | SIMD | Notes |
|----------|------|-------|
| macOS ARM64 | NEON | Full support, use `arm_neon.h` |
| macOS x86_64 | SSE2/AVX | Use `immintrin.h` |
| Linux ARM | NEON | Same as macOS ARM64 |
| Linux x86_64 | SSE2/AVX | Same as macOS x86_64 |
| Windows | SSE2/AVX | Same includes |

### Mandatory Fallback Pattern

> [!CAUTION]
> Every SIMD optimization MUST follow this pattern to ensure cross-platform compatibility:

```cpp
// Scalar fallback (ALWAYS implemented first)
void ProcessPixels_Scalar(const uint8_t* src, uint8_t* dst, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        // Scalar implementation
    }
}

// Platform-specific optimizations
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
void ProcessPixels_NEON(const uint8_t* src, uint8_t* dst, size_t count)
{
    // NEON implementation (identical output to scalar)
}
#define ProcessPixels ProcessPixels_NEON

#elif defined(__SSE2__)
void ProcessPixels_SSE2(const uint8_t* src, uint8_t* dst, size_t count)
{
    // SSE2 implementation (identical output to scalar)
}
#define ProcessPixels ProcessPixels_SSE2

#else
// Safe fallback for all other platforms
#define ProcessPixels ProcessPixels_Scalar
#endif
```

**Requirements:**
1. **Scalar first** - Always implement scalar version first
2. **Identical output** - SIMD must produce byte-identical output
3. **Compile on all platforms** - No #error if SIMD unavailable
4. **Runtime fallback** - Consider runtime detection if needed

---

## References

- [Current GIF encoder benchmarks](../../../core/benchmarks/emulator/recording/gif_encoder_benchmark.cpp)
- [GIF encoder documentation](../../emulator/design/recording/encoders/gif-encoder.md)
- [Fixed palette implementation plan](gap-7-gif-encoder-fixed-palette.md)
