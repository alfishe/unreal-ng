# GIF Encoder

## Overview

The GIF Encoder produces animated GIF files from emulator video frames. It provides multiple optimization paths for different use cases.

---

## Architecture

```
GIFEncoder → gif.h (direct)
     ↓
GifWriter, GifPalette, GifColorLookup
```

> **Note:** `GIFAnimationHelper` was removed (2026-01-11). `GIFEncoder` now directly calls `gif.h` functions for optimal performance.

---

## Optimization Techniques

### Performance Comparison (352×288 frame, same scenario)

| Approach | Time | Throughput | Speedup | Use Case |
|----------|------|------------|---------|----------|
| **Auto Mode** | 420 μs | 925 MB/s | 1.0× | Unknown content |
| **Fixed Palette (k-d tree)** | 314 μs | 1.20 GB/s | 1.3× | Known palette, approximate match |
| **Hash Lookup (exact)** | **284 μs** | **1.33 GB/s** | **1.5×** | Known palette, exact match |
| **DirectZX** | 270 μs | 1.40 GB/s | 1.6× | Standard ZX colors only |

### Optimization Levels

```
┌────────────────────────────────────────────────────────────────────────┐
│ Level 0: Auto Mode (baseline)                                          │
│ - Per-frame palette calculation via k-d tree                           │
│ - Best for: Unknown content, photos, gradients                         │
├────────────────────────────────────────────────────────────────────────┤
│ Level 1: Fixed Palette + k-d tree lookup                               │
│ - Pre-built palette, skip GifMakePalette                               │
│ - Best for: Known palette, approximate color matching OK               │
├────────────────────────────────────────────────────────────────────────┤
│ Level 2: Hash Lookup (RECOMMENDED)                                     │
│ - O(1) hash table lookup for exact color matching                      │
│ - Best for: Standard/custom ZX palettes, hi-color modes                │
├────────────────────────────────────────────────────────────────────────┤
│ Level 3: DirectZX (deprecated)                                         │
│ - Hardcoded bit manipulation for standard 0x00/0xCD/0xFF values        │
│ - Only works with exact ZX color values, inflexible                    │
└────────────────────────────────────────────────────────────────────────┘
```

---

## Color Lookup Methods

### Hash Table (GifColorLookup) - RECOMMENDED

| Property | Value |
|----------|-------|
| Lookup complexity | O(1) |
| Memory | ~5 KB for 256 colors |
| Build time | ~0.8 μs for 256 colors |
| Single lookup | ~3.2 ns |
| Exact matching | ✅ Yes |
| Any palette | ✅ Yes |

### K-d Tree (GifBuildPaletteTree)

| Property | Value |
|----------|-------|
| Lookup complexity | O(log n) |
| Memory | ~12 KB |
| Exact matching | ❌ Approximate |

> **Note:** K-d tree may return wrong index for similar colors (e.g., white 0xCACACA may map to bright white 0xFFFFFF).

### DirectZX (GifGetZXPaletteIndexDirect) - DEPRECATED

| Property | Value |
|----------|-------|
| Lookup complexity | O(1) bit manipulation |
| Exact matching | ❌ Only standard ZX colors |

> **Why deprecated:** Uses conditional branches (pipeline stalls). Hash lookup is now faster and more flexible.

---

## API Reference

### Low-Level Functions (gif.h)

```cpp
// Lifecycle
bool GifBegin(GifWriter*, const char* filename, uint32_t w, uint32_t h, uint32_t delay);
bool GifEnd(GifWriter*);

// Palette
void GifBuildPaletteTree(GifPalette*);
void GifBuildColorLookup(GifColorLookup*, const GifPalette*);

// Frame writing
bool GifWriteFrame(GifWriter*, const uint8_t* image, uint32_t w, uint32_t h, uint32_t delay);
bool GifWriteFrameFast(GifWriter*, const uint8_t* image, uint32_t w, uint32_t h, uint32_t delay, GifPalette*);
bool GifWriteFrameExact(GifWriter*, const uint8_t* image, uint32_t w, uint32_t h, uint32_t delay, GifPalette*, const GifColorLookup*);
bool GifWriteFrameZX(GifWriter*, const uint8_t* image, uint32_t w, uint32_t h, uint32_t delay, GifPalette*);  // Deprecated
```

---

## Files

| File | Description |
|------|-------------|
| `core/src/3rdparty/gif/gif.h` | GIF encoder API |
| `core/src/3rdparty/gif/gif.cpp` | Implementation |
| `core/src/emulator/recording/encoders/gif_encoder.h` | High-level encoder |
| `core/src/emulator/recording/encoders/gif_encoder.cpp` | Encoder implementation |
| `core/tests/emulator/recording/gif_encoder_test.cpp` | 51 unit tests |
| `core/benchmarks/emulator/recording/gif_encoder_benchmark.cpp` | Performance benchmarks |

---

## Changelog

### 2026-01-11

- **Removed `GIFAnimationHelper`** - Consolidated into `GIFEncoder`
- **Added Hash Lookup** - `GifColorLookup` for O(1) exact color matching
- **Added `GifWriteFrameExact`** - Optimized path using hash lookup
- **DirectZX deprecated** - Hash lookup is now faster due to branch-free execution
- **All tests pass** - 51 unit tests, 1 disabled (documents k-d tree limitation)

---

## See Also

- [Encoder Architecture](../encoder-architecture.md)
- [Recording System](../recording-system.md)
- [SIMD Optimization Opportunities](../../../inprogress/2026-01-10-performance-optimizations/gap-8-gif-encoder-simd-optimizations.md)
