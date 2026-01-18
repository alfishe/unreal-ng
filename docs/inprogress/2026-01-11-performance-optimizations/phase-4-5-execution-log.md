# Phase 4-5 Execution Log: Batch 8-Pixel Draw with SIMD

**Date**: 2026-01-11  
**Status**: ✅ Complete (Implementation + Benchmarks + Tests)

---

## Summary

Implemented batch 8-pixel rendering with ARM NEON SIMD for ScreenHQ=OFF mode. This optimization
batches 8 pixels from a single byte together, which breaks demo multicolor compatibility but
provides **massive performance gains** for non-demo usage.

## ⚠️ Demo Compatibility Warning

> **Racing the Beam**: These batch methods read the attribute once per 8 pixels. Demos that
> modify attributes mid-scanline ("multicolor" effects) will render incorrectly.
>
> **Only enable when ScreenHQ feature is OFF.**

---

## Implementation

### Files Modified

| File | Changes |
|:---|:---|
| [`screenzx.h`](core/src/emulator/video/zx/screenzx.h) | Added `DrawBatch8_Scalar`, `DrawBatch8_NEON`, `RenderScreen_Batch8` declarations |
| [`screenzx.cpp`](core/src/emulator/video/zx/screenzx.cpp) | Implemented batch methods with NEON SIMD |
| [`screenzx_benchmark.cpp`](core/benchmarks/emulator/video/screenzx_benchmark.cpp) | Added 4 new benchmarks |
| [`screenzx_test.cpp`](core/tests/emulator/video/screenzx_test.cpp) | Added 3 new unit tests |

### Key Methods

```cpp
/// Scalar version - branch-free 8-pixel rendering
void DrawBatch8_Scalar(uint8_t zxY, uint8_t symbolX, uint32_t* destPtr);

/// ARM NEON version - 4 pixels per instruction
void DrawBatch8_NEON(uint8_t zxY, uint8_t symbolX, uint32_t* destPtr);

/// Renders entire 256x192 screen using batch method
void RenderScreen_Batch8();
```

---

## Benchmark Results

```
---------------------------------------------------------------------------------------
Benchmark                                             Time             CPU   Iterations
---------------------------------------------------------------------------------------
BM_DrawFrame_Original/iterations:100_mean        345263 ns       343713 ns            3
BM_DrawFrame_LUT/iterations:100_mean             140295 ns       139457 ns            3
BM_RenderScreen_PerPixel/iterations:100_mean      17813 ns        17820 ns            3
BM_RenderScreen_Batch8_NEON/iterations:100_mean   16727 ns        16620 ns            3
BM_RenderScreen_Batch8_Scalar/iterations:100_mean 14504 ns        14460 ns            3
BM_RenderScreen_Batch8/iterations:100_mean        13656 ns        13647 ns            3
```

### Performance Comparison

| Method | Time | vs Original | vs LUT (Phase 2) | Notes |
|:---|---:|:---:|:---:|:---|
| DrawFrame_Original | 343.7 μs | 1.0x | - | Per-t-state, runtime calculation |
| DrawFrame_LUT (Phase 2) | 139.5 μs | 2.5x | 1.0x | Per-t-state with LUT |
| RenderScreen_PerPixel | 17.8 μs | 19.3x | 7.8x | Legacy per-pixel batch |
| RenderScreen_Batch8_NEON | 16.6 μs | 20.7x | 8.4x | ARM NEON SIMD |
| RenderScreen_Batch8_Scalar | 14.5 μs | 23.7x | 9.6x | Scalar loop |
| **RenderScreen_Batch8** | **13.6 μs** | **25.3x** | **10.2x** | Auto-selects best |

### Surprising Finding: Scalar > NEON

The scalar version outperforms NEON by ~15%! Likely reasons:
1. **NEON mask generation overhead** - Creating 4 separate masks per vector adds latency
2. **Apple M-series scalar efficiency** - Modern ARM cores have very fast scalar pipelines
3. **Short loop penalty** - 8-iteration loop doesn't amortize SIMD setup costs

> **Recommendation**: For ScreenHQ=OFF, consider using scalar-only implementation.
> NEON may provide benefit on older/simpler ARM cores.

---

## Unit Tests

All 3 new tests passing:

| Test | Description |
|:---|:---|
| `Batch8_ScalarProducesCorrectOutput` | Validates known pixel pattern (0xAA = alternating) |
| `Batch8_NEONMatchesScalar` | Confirms NEON output matches scalar across screen |
| `Batch8_RenderScreenMatchesPerPixel` | Validates batch output matches legacy per-pixel |

---

## Integration Notes

**Feature Manager Integration: ✅ COMPLETE**

The `ScreenHQ` feature is now fully integrated:

| File | Changes |
|:---|:---|
| [`featuremanager.h`](core/src/base/featuremanager.h) | Added `kScreenHQ`, `kScreenHQAlias`, `kScreenHQDesc` constants |
| [`featuremanager.cpp`](core/src/base/featuremanager.cpp) | Registered feature (ON by default), added Screen::UpdateFeatureCache callback |
| [`screen.h`](core/src/emulator/video/screen.h) | Added `UpdateFeatureCache()`, `IsScreenHQEnabled()`, `_feature_screenhq_enabled` |
| [`screen.cpp`](core/src/emulator/video/screen.cpp) | Implemented `UpdateFeatureCache()` |
| [`VideoWallWindow.cpp`](unreal-videowall/src/videowall/VideoWallWindow.cpp) | Disables ScreenHQ for all videowall instances |
| [`command-interface.md`](docs/emulator/design/control-interfaces/command-interface.md) | Documented ScreenHQ feature |

**Feature Details**:
- **ID**: `screenhq`
- **Alias**: `vhq`
- **Default**: ON (demo-compatible, per-t-state rendering)
- **Category**: Performance
- **Description**: Controls video rendering mode (per-t-state vs batch 8-pixel)

**Usage**:
```bash
# List features (CLI or Python)
feature

# Enable/disable via CLI
feature screenhq off   # 25x faster, breaks demo multicolor
feature screenhq on    # Demo-compatible (default)

# Via WebAPI
PUT /api/v1/emulator/{id}/feature/screenhq {"state": "off"}
```

**Cleanup TODO**:
- Consider removing NEON version if scalar consistently outperforms
- Add x86 SSE2 version if cross-platform batch support needed

---

## Render Pipeline Integration

The ScreenHQ optimization is now integrated into the full render pipeline with bypass logic
at two key points:

### Bypass Point 1: Screen::DrawPeriod()

First bypass - skips per-t-state rendering when ScreenHQ=OFF:

| Location | [screen.cpp:705](core/src/emulator/video/screen.cpp#L705-L730) |
|:---|:---|
| **Bypassed** | `for` loop calling `Draw(tstate)` for ~70,000 t-states per frame |
| **Condition** | `!_feature_screenhq_enabled` |
| **Action** | Early return, no per-t-state rendering |

### Bypass Point 2: MainLoop::OnFrameEnd()

Second bypass point - renders batch at frame end:

| Location | [mainloop.cpp:250](core/src/emulator/mainloop.cpp#L250-L265) |
|:---|:---|
| **Condition** | `!_context->pScreen->IsScreenHQEnabled()` |
| **Action** | Calls `pScreen->RenderFrameBatch()` |
| **Dispatch** | `ScreenZX::RenderScreen_Batch8()` for 25x faster batch render |

### Data Flow Diagram

```
ScreenHQ=ON (default):
┌─────────────┐   per-tstate   ┌────────────┐   2 pixels   ┌─────────────┐
│ OnCPUStep() │ ─────────────► │ DrawPeriod │ ───────────► │ Framebuffer │
└─────────────┘    ~70,000x    └────────────┘   per call   └─────────────┘

ScreenHQ=OFF (optimized):
┌─────────────┐   bypassed   ┌────────────┐
│ OnCPUStep() │ ──────────►  │ DrawPeriod │ (returns immediately)
└─────────────┘              └────────────┘

┌─────────────┐   1 call    ┌───────────────────┐   768 symbols   ┌─────────────┐
│ OnFrameEnd()│ ──────────► │ RenderFrameBatch()│ ─────────────►  │ Framebuffer │
└─────────────┘             └───────────────────┘    8 px each    └─────────────┘
```

### Full Documentation

See [Screen Rendering Pipeline](docs/emulator/design/video/screen-rendering-pipeline.md)
for comprehensive architecture, sequence diagrams, and performance analysis.

## Verification Commands

```bash
# Run batch unit tests
./build/bin/core-tests --gtest_filter="ScreenZX_Test.Batch8*"

# Run batch benchmarks
./build/bin/core-benchmarks --benchmark_filter="BM_RenderScreen"

# Full comparison
./build/bin/core-benchmarks --benchmark_filter="BM_DrawFrame|BM_RenderScreen"
```
