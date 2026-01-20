# Phase 2 Execution Log: LUT-Based Coordinate Lookups

**Date**: 2026-01-11  
**Status**: ✅ Complete

---

## Summary

Implemented LUT-based coordinate lookups to optimize the `ScreenZX::Draw()` method by eliminating expensive runtime division/modulo operations. The optimization pre-computes all coordinate transformations during mode initialization.

## Implementation Details

### New Data Structure

Added `TstateCoordLUT` structure in [`screenzx.h`](core/src/emulator/video/zx/screenzx.h):

```cpp
struct TstateCoordLUT
{
    uint16_t framebufferX;     // Framebuffer X coordinate (UINT16_MAX if invisible)
    uint16_t framebufferY;     // Framebuffer Y coordinate
    uint16_t zxX;              // ZX screen X (UINT16_MAX if border/invisible)
    uint8_t  zxY;              // ZX screen Y (255 if border/invisible)
    uint8_t  symbolX;          // Pre-computed x / 8
    uint8_t  pixelXBit;        // Pre-computed x % 8
    RenderTypeEnum renderType; // RT_BLANK, RT_BORDER, or RT_SCREEN
    uint16_t screenOffset;     // Pre-computed _screenLineOffsets[y]
    uint16_t attrOffset;       // Pre-computed _attrLineOffsets[y]
};
```

**Memory Usage**: ~1.1 MB for LUT (`71680 entries × 16 bytes`)

### Modified Files

| File | Change |
|:---|:---|
| `screenzx.h` | Added `TstateCoordLUT` struct, `_tstateLUT[]` array, `CreateTstateLUT()` method, `SetVideoMode()` override, `DrawOriginal()` for comparison |
| `screenzx.cpp` | Implemented `CreateTstateLUT()`, new LUT-based `Draw()`, `DrawOriginal()` (kept for benchmarking), `SetVideoMode()` override |
| `screenzx_benchmark.cpp` | Added `BM_DrawFrame_Original` and `BM_DrawFrame_LUT` benchmarks |
| `screenzx_test.cpp` | Added 5 new LUT tests, fixed M_PMC → M_PENTAGON128K |

---

## Benchmark Results

```
-------------------------------------------------------------------------------
Benchmark                                     Time             CPU   Iterations
-------------------------------------------------------------------------------
BM_DrawFrame_Original/iterations:100     341922 ns       340900 ns          100
BM_DrawFrame_LUT/iterations:100          144715 ns       144110 ns          100
```

### Performance Improvement

| Metric | Original | LUT-Optimized | Improvement |
|:---|---:|---:|:---:|
| Frame Draw Time | 341 μs | 144 μs | **2.4x faster** |
| CPU Time per Frame | 340.9 μs | 144.1 μs | **-58%** |

---

## LUT Initialization Behavior

The LUT is regenerated automatically when:
1. **Mode change**: `SetVideoMode()` override calls `CreateTimingTable()` → `CreateTstateLUT()`
2. **Initial construction**: `ScreenZX` constructor calls `CreateTables()` → `CreateTimingTable()` → `CreateTstateLUT()`

LUT entry counts per video mode:
| Mode | BLANK | BORDER | SCREEN |
|:---|:---|:---|:---|
| M_ZX48 | 19,200 | 26,112 | 24,576 |
| M_ZX128 | 20,448 | 26,112 | 24,576 |
| M_PENTAGON128K | 20,992 | 26,112 | 24,576 |

---

## Unit Tests

All 5 new LUT tests passing:

| Test | Description |
|:---|:---|
| `TstateLUT_InitializedOnModeChange` | Verifies LUT contains BLANK, BORDER, and SCREEN entries |
| `TstateLUT_MatchesTransformTstateToFramebufferCoords` | Validates LUT framebuffer coords match original function |
| `TstateLUT_MatchesTransformTstateToZXCoords` | Validates LUT ZX coords match original function |
| `TstateLUT_DrawProducesSameOutputAsDrawOriginal` | **Critical**: Verifies identical framebuffer output |
| `TstateLUT_InitializedForAllModes` | Tests LUT initialization for all video modes |

---

## Technical Notes

### DrawOriginal (deprecated)
The original `Draw()` method is preserved as `DrawOriginal()` for:
- Benchmarking comparison
- Verification during testing
- Safe removal after production validation

### SetVideoMode Override
Added `ScreenZX::SetVideoMode()` override to ensure LUT regeneration on mode change:
```cpp
void ScreenZX::SetVideoMode(VideoModeEnum mode)
{
    Screen::SetVideoMode(mode);  // Base class updates _mode and _rasterState
    CreateTimingTable();         // Regenerate timing tables including LUT
}
```

---

## Verification Commands

```bash
# Run LUT unit tests
./build/bin/core-tests --gtest_filter="ScreenZX_Test.TstateLUT*"

# Run Draw benchmarks
./build/bin/core-benchmarks --benchmark_filter="BM_DrawFrame"

# Run all ScreenZX tests (14 tests)
./build/bin/core-tests --gtest_filter="ScreenZX_Test.*"
```
