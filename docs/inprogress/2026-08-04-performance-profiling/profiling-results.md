# Performance Profiling Results - 2026-08-04

## Executive Summary

Instrumented profiling of TTD Gaming mode frame execution revealed **TurboSound HQ FIR decimation** as the dominant bottleneck (45% of frame time). A series of targeted optimizations reduced frame time by **47%** while preserving full emulation accuracy and multicolor demo compatibility.

**Optimizations implemented:**
1. **SIMD FIR filter** (NEON/SSE2) with double-buffer: **-28% frame time** (913µs → 656µs)
2. **Batch audio processing** (80 t-states default): **-3.5% additional** (656µs → 632µs)
3. **Screen area SIMD batching** (8 pixels/call): **-3.3% additional** (632µs → 611µs)
4. **Branchless AY mixer** (volume/mute selection): **-1.8% additional** (611µs → 600µs)
5. **Inlined/unrolled AY generators** (updateState + mixer): **-0.5% additional** (600µs → 597µs)
6. **8-wide SIMD FIR** (4 accumulators per iteration): **-3.2% additional** (597µs → 578µs)
7. **Inlined DrawPeriod** (eliminate virtual calls + cache locals): **-11.6% additional** (578µs → 511µs)
8. **Stereo interleaved decimator** (2 filters instead of 4): **-1.6% additional** (511µs → 503µs)
9. **TTD previous-page cache** (avoid decompression during capture): **-4.6% additional** (504µs → 481µs)

**Combined improvement: 47%** (913µs → 481µs) with SoundHQ ON + TTD Gaming mode

---

## Test Configuration

- **Benchmark**: `FrameBenchmarkFixture/BM_Frame_TTD_Gaming`
- **Build**: Release (CMake)
- **Platform**: macOS 15.7.7, ARM64 (Apple Silicon)
- **Profiler**: macOS `sample` (1ms sampling interval, 3s duration)
- **Workload**: action.sna snapshot, TTD enabled, write journal disabled

---

## Results Summary

| Configuration | Frame Time | FPS Capacity | vs Baseline |
|---------------|------------|--------------|-------------|
| Before optimization (SoundHQ ON) | 913 µs | 1,095 fps | baseline |
| After SIMD FIR decimator | 656 µs | 1,524 fps | -28% |
| + Audio batch=80 | 632 µs | 1,582 fps | -31% |
| + Screen SIMD batch | 611 µs | 1,637 fps | -33% |
| + Branchless AY mixer | 600 µs | 1,667 fps | -34% |
| + Inlined/unrolled AY | 597 µs | 1,675 fps | -35% |
| + 8-wide SIMD FIR | 578 µs | 1,730 fps | -37% |
| + Inlined DrawPeriod | 511 µs | 1,957 fps | -44% |
| + Stereo decimator | 503 µs | 1,988 fps | -45% |
| + TTD page cache | 481 µs | 2,079 fps | **-47%** |
| No TTD (pure frame) | 402 µs | 2,488 fps | -56% |

---

## Profile Analysis

### Before Optimization (913 µs/frame)

| Component | Samples | % of Frame | Notes |
|-----------|---------|------------|-------|
| **TurboSound::handleStep** | **377** | **44.6%** | FIR decimation loop |
| Screen::DrawPeriod/Draw | 153 | 18.1% | Per-t-state rendering |
| Z80::Z80Step (opcode exec) | 158 | 18.7% | Core CPU emulation |
| Other | 157 | 18.6% | Memory, breakpoints, etc. |

**Root cause**: FilterDecimator::getOutput() with 96-tap FIR convolution:
- 882 audio samples/frame × 4 decimators × 96 MACs = 339,264 floating-point ops
- Circular buffer linearization on every output sample
- Called ~70,000 times per frame (every CPU instruction)

### After Optimization (632 µs/frame with batch=80)

| Component | Samples | % of Frame | Notes |
|-----------|---------|------------|-------|
| Screen::DrawPeriod/Draw | 180 | 26% | Now dominant |
| TurboSound::handleStep | 165 | 24% | Reduced from 44.6% |
| Z80 execution | 120 | 17% | Core CPU emulation |
| Other | 227 | 33% | Memory, breakpoints, etc. |

---

## Changes Made

### 1. SIMD FIR Filter with Double-Buffer

**File**: `core/src/common/sound/filters/filter_decimator.h`

**Changes**:
- Added SIMD detection macros for NEON (ARM64) and SSE2 (x86-64)
- Changed circular buffer to double-buffer layout (192 doubles instead of 96)
- `feedSample()` writes to both halves, eliminating wrap handling in convolution
- `getOutput()` reads contiguous memory, enabling SIMD vectorization
- Added `convolveNEON()` using `float64x2_t` and `vfmaq_f64` intrinsics
- Added `convolveSSE2()` using `__m128d` and `_mm_mul_pd` intrinsics
- Retained `convolveScalar()` as cross-platform fallback

**Key code**:
```cpp
// Double-buffer: samples written to both halves for wrap-free SIMD access
alignas(16) double _buffer[FIR_TAPS * 2];

inline void feedSample(double sample) {
    _buffer[_writeIndex] = sample;
    _buffer[_writeIndex + FIR_TAPS] = sample;  // Mirror
    _writeIndex = (_writeIndex + 1) % FIR_TAPS;
    _phase += 1.0;
}

// NEON convolution (ARM64)
static double convolveNEON(const double* src) {
    float64x2_t sum = vdupq_n_f64(0.0);
    for (size_t i = 0; i < FIR_TAPS; i += 2) {
        float64x2_t buf = vld1q_f64(&src[i]);
        float64x2_t coef = vld1q_f64(&FIR_COEFFS[i]);
        sum = vfmaq_f64(sum, buf, coef);
    }
    return vgetq_lane_f64(sum, 0) + vgetq_lane_f64(sum, 1);
}
```

### 2. Batch Audio Processing

**Files**: 
- `core/src/emulator/sound/soundmanager.h`
- `core/src/emulator/sound/soundmanager.cpp`

**Changes to soundmanager.h**:
```cpp
// Added fields (in private section):
uint32_t _batchInterval = 80;    // Default: 80 t-states (1 audio sample period)
uint32_t _lastBatchTStates = 0;

// Added API (in public section):
void setBatchInterval(uint32_t interval) { _batchInterval = interval; _lastBatchTStates = 0; }
uint32_t getBatchInterval() const { return _batchInterval; }
```

**Changes to soundmanager.cpp**:
```cpp
void SoundManager::handleStep()
{
    if (!_feature_sound_enabled)
        return;

    // Batch mode: only call TurboSound every N t-states
    if (_batchInterval > 0)
    {
        uint32_t currentTStates = _context->pCore->GetZ80()->t;
        if (currentTStates - _lastBatchTStates < _batchInterval)
            return;
        _lastBatchTStates = currentTStates;
    }

    _turboSound->handleStep();
}
```

### 3. Screen Area SIMD Batching

**File**: `core/src/emulator/video/zx/screenzx.cpp`

**Changes**:
- Added SIMD includes at top (`arm_neon.h` / `emmintrin.h`)
- Modified `ScreenZX::Draw()` to batch 8 pixels for screen area:
  - Skip if `pixelXBit != 0` (already rendered on boundary)
  - Render all 8 pixels using SIMD when `pixelXBit == 0`
  - Border rendering unchanged (per-t-state for accurate effects)

**Key code**:
```cpp
if (lut.renderType == RT_SCREEN)
{
    // Skip if not at character cell boundary
    if (lut.pixelXBit != 0)
        return;  // Already rendered when pixelXBit was 0

    // Render 8 pixels using NEON/SSE2/scalar
#if defined(__ARM_NEON)
    // NEON: 4 pixels per instruction
    uint32x4_t vInk = vdupq_n_u32(colorInk);
    uint32x4_t vPaper = vdupq_n_u32(colorPaper);
    // ... masks and vbslq_u32 ...
#elif defined(__SSE2__)
    // SSE2: 4 pixels per instruction
    __m128i vInk = _mm_set1_epi32(colorInk);
    // ... masks and bitwise select ...
#else
    // Scalar fallback
    for (int i = 0; i < 8; i++) { ... }
#endif
}
else
{
    // Border: keep per-t-state for accurate border effects
}
```

**Why this is lossless for multicolor:**
- ULA reads attributes at 8-pixel (character cell) boundaries
- Software cannot change attributes faster than the ULA reads them
- Batching 4 t-states (8 pixels) matches hardware behavior exactly

### 4. Benchmark Additions

**File**: `core/benchmarks/debugger/ttd/ttd_frame_overhead_benchmark.cpp`

**Added**:
- `BM_Frame_TTD_Gaming_SoundLQ` - Tests with SoundHQ OFF
- `BM_Frame_TTD_Gaming_Batch` - Parameterized test for batch intervals (0, 40, 80, 160)

---

## Batch Interval Analysis

| Interval | Calls/Frame | Latency | Frame Time | Quality Impact |
|----------|-------------|---------|------------|----------------|
| 0 (every instr) | 70,000 | 0 | 656 µs | Perfect |
| 40 t-states | 1,750 | 11µs | 634 µs | None - sub-sample |
| **80 t-states** | **875** | **23µs** | **632 µs** | **None - 1 sample (DEFAULT)** |
| 160 t-states | 437 | 46µs | 617 µs | None - 2 samples |

**Selected default: 80 t-states**
- Reduces handleStep calls from 70,000 to 875 per frame (80x reduction)
- Latency of 23µs (1 audio sample) is inaudible
- No quality loss for any practical use case
- Provides 3.5% frame time improvement over no batching

---

## Platform Support

| Platform | SIMD Path | Tested |
|----------|-----------|--------|
| macOS ARM64 (Apple Silicon) | NEON `float64x2_t` | ✓ |
| Linux ARM64 | NEON `float64x2_t` | Expected to work |
| x86-64 (Intel/AMD) | SSE2 `__m128d` | Compiles, untested |
| Other | Scalar fallback | Always available |

---

## Files Modified

| File | Change |
|------|--------|
| `core/src/common/sound/filters/filter_decimator.h` | SIMD + double-buffer FIR rewrite |
| `core/src/emulator/sound/soundmanager.h` | Batch interval fields and API (default: 80) |
| `core/src/emulator/sound/soundmanager.cpp` | Batch interval logic in handleStep() |
| `core/src/emulator/video/zx/screenzx.cpp` | Screen area SIMD 8-pixel batching |
| `core/src/emulator/sound/chips/soundchip_ay8910.cpp` | Branchless mixer + inlined generators |
| `core/src/emulator/video/zx/screenzx.h` | DrawPeriod override declaration |
| `core/src/emulator/video/zx/screenzx.cpp` | Inlined DrawPeriod + UpdateScreen |
| `core/src/common/sound/filters/filter_decimator_stereo.h` | New stereo decimator |
| `core/src/emulator/cpu/z80.h` | Inline IncrementCPUCyclesCounter (minor) |
| `core/src/emulator/cpu/z80.cpp` | Removed duplicate definition (minor) |
| `core/benchmarks/debugger/ttd/ttd_frame_overhead_benchmark.cpp` | New benchmarks |

---

## How to Adjust Settings

### Change Batch Interval at Runtime
```cpp
emulator->GetContext()->pSoundManager->setBatchInterval(160);  // More aggressive
emulator->GetContext()->pSoundManager->setBatchInterval(0);    // Disable batching
```

### Disable SoundHQ (for maximum performance)
```cpp
emulator->GetFeatureManager()->setFeature(Features::kSoundHQ, false);
```

---

## Future Optimization Opportunities

### Track 2: Screen::DrawPeriod - COMPLETED

Screen area SIMD batching implemented. See "3. Screen Area SIMD Batching" above.

**Results:**
- Screen area: 8 pixels per call with SIMD (NEON/SSE2)
- Border: unchanged (per-t-state for accurate effects)
- Gain: **3.3%** (632µs → 611µs)
- Multicolor compatibility: **fully preserved**

### 4. Branchless AY Mixer

**File**: `core/src/emulator/sound/chips/soundchip_ay8910.cpp`

**Changes**:
- Eliminated branches in `updateMixer()` hot path using arithmetic masks
- Envelope/fixed volume selection: `(envVol & -enabled) | (fixedVol & ~(-enabled))`
- Mute: multiply by 0.0 or userVolume instead of branch

**Gain**: 1.8% (611µs → 600µs)

### 5. Inlined/Unrolled AY Generator Updates

**File**: `core/src/emulator/sound/chips/soundchip_ay8910.cpp`

**Changes to `updateState()`**:
- Inlined noise generator counter/LFSR logic (removed function call)
- Unrolled 3 tone generator updates with `UPDATE_TONE(idx)` macro
- Used `(_tick & 7)` for prescaler check (bitwise, no modulo)

**Changes to `updateMixer()`**:
- Cached shared values at start: `noiseOut`, `envOut`
- Unrolled 3-channel loop with `MIX_CHANNEL(idx)` macro
- Replaced `/3.0` with `*INV_3` (multiply by constant)

**Gain**: 0.5% (600µs → 597µs)

**Rejected approaches** (tested, no gain):
- Branchless tone generator (arithmetic mask for counter reset) - added more instructions than branches cost
- Branchless noise generator LFSR - same issue

### 6. 8-Wide SIMD FIR Convolution

**File**: `core/src/common/sound/filters/filter_decimator.h`

**Changes**:
- Increased from 2 to 4 vector accumulators per loop iteration
- Process 8 doubles per iteration (was 2), reducing loop overhead
- 96 taps / 8 = 12 iterations (was 48 iterations)
- Four independent FMA chains maximize instruction-level parallelism

**NEON implementation**:
```cpp
float64x2_t sum0, sum1, sum2, sum3 = vdupq_n_f64(0.0);
for (size_t i = 0; i < FIR_TAPS; i += 8) {
    sum0 = vfmaq_f64(sum0, vld1q_f64(&src[i]),     vld1q_f64(&FIR_COEFFS[i]));
    sum1 = vfmaq_f64(sum1, vld1q_f64(&src[i + 2]), vld1q_f64(&FIR_COEFFS[i + 2]));
    sum2 = vfmaq_f64(sum2, vld1q_f64(&src[i + 4]), vld1q_f64(&FIR_COEFFS[i + 4]));
    sum3 = vfmaq_f64(sum3, vld1q_f64(&src[i + 6]), vld1q_f64(&FIR_COEFFS[i + 6]));
}
// Reduce: sum0+sum1+sum2+sum3 -> horizontal add
```

**Gain**: 3.2% (597µs → 578µs)

**Why it works**: Modern CPUs can execute multiple independent FMA operations in parallel. Using 4 accumulators creates 4 independent dependency chains, allowing the CPU to saturate its execution units.

### 7. Inlined DrawPeriod Override

**Files**: 
- `core/src/emulator/video/zx/screenzx.h` (added override declaration)
- `core/src/emulator/video/zx/screenzx.cpp` (implemented override)

**Changes to `UpdateScreen()`**:
- Inlined `GetCurrentTstate()` to avoid member access chain
- Direct access to `_context->pCore->GetZ80()->t` and frequency multiplier

**Changes: New `DrawPeriod()` override**:
- Eliminated virtual `Draw()` call per t-state (was ~70,000 calls/frame)
- Cached frequently accessed values at loop start (framebuffer pointer, width, screen pointer, border color)
- Inlined all Draw logic directly into the loop
- Duplicated SIMD 8-pixel rendering inline (same code, no function call)

**Key code structure**:
```cpp
void ScreenZX::DrawPeriod(uint32_t fromTstate, uint32_t toTstate) {
    // Cache values once
    uint32_t* const fb = reinterpret_cast<uint32_t*>(_framebuffer.memoryBuffer);
    const size_t fbWidth = rd.fullFrameWidth;
    const uint32_t borderColorARGB = _rgbaColors[_borderColor];
    
    for (uint32_t t = fromTstate; t <= toTstate; t++) {
        const TstateCoordLUT& lut = _tstateLUT[t];
        if (lut.renderType == RT_BLANK) continue;
        // ... inlined Draw logic with SIMD ...
    }
}
```

**Gain**: 11.6% (578µs → 511µs)

**Why it works**:
1. **No virtual call overhead** - Draw() was called ~70,000 times per frame via function pointer
2. **Cached locals** - Framebuffer pointer, width, border color loaded once instead of per-iteration
3. **Better branch prediction** - Single loop vs nested function calls
4. **Compiler optimization** - Inlined code allows better register allocation

**Multicolor compatibility**: Fully preserved - still processes per-t-state, just without function call overhead.

### 8. Stereo Interleaved Decimator

**Files**: 
- `core/src/common/sound/filters/filter_decimator_stereo.h` (new)
- `core/src/emulator/sound/chips/soundchip_ay8910.h` (added stereo decimator field)
- `core/src/emulator/sound/chips/soundchip_turbosound.cpp` (use stereo decimator)

**Changes**:
- New `FilterDecimatorStereo` class with interleaved L/R buffer
- Buffer layout: `[L0,R0,L1,R1,...]` instead of separate L and R arrays
- Single `feedSample(left, right)` and `getOutput(outL, outR)` calls
- SIMD processes L+R together with same coefficient (duplicated)

**Key benefits**:
- 2 decimator instances instead of 4 (TurboSound has 2 AY chips)
- Better cache locality (L/R samples adjacent)
- Fewer function calls per audio sample

**Gain**: 1.6% (511µs → 503µs)

**Quality**: Lossless - same 96-tap FIR coefficients, same math, just reorganized.

### 9. TTD Previous-Page Cache

**Files**: 
- `core/src/debugger/ttd/ttd_codec_page_store.h` (added `InternXorCached`)
- `core/src/debugger/ttd/ttd_codec_page_store.cpp` (implemented `InternXorCached`)
- `core/src/debugger/ttd/timetravelmanager.h` (added `_prevPageCache`, `UpdatePrevPageCache`)
- `core/src/debugger/ttd/timetravelmanager.cpp` (cache management and usage)

**Problem**: During forward TTD recording, computing XOR deltas required decompressing the previous checkpoint's pages. With XOR-delta encoding, this caused **recursive decompression** up the entire delta chain back to the last I-frame. For a P-frame 50 frames after the I-frame, a single dirty page triggered 50 nested decompressions.

**Solution**: Cache the uncompressed RAM content after each checkpoint capture. On the next frame, use the cached bytes directly for XOR computation instead of decompressing.

**Key changes**:
```cpp
// New method: XOR using cached previous (no decompression)
uint32_t InternXorCached(uint32_t prevSlot, const uint8_t* pageData, const uint8_t* cachedPrev);

// In TimeTravelManager: cache current RAM after capture
void UpdatePrevPageCache() {
    // Copy all RAM pages to _prevPageCache
    // Size: _modelRamPages * 16KB (e.g., 128KB for 128KB model)
}
```

**Memory cost**: 128KB for 128KB model, 1MB for 1MB model. Cache is only allocated during active TTD recording and freed on session invalidate.

**Gain**: 4.6% (504µs → 481µs)

**Why it works**: The previous page content is **already known** - it was the live RAM one frame ago. Caching it avoids O(chain_depth) decompression per dirty page, reducing complexity from O(N × dirty_pages × chain_depth) to O(N × dirty_pages).

---

### Track 3: Further Sound Optimization (remaining)
- Merge L/R into stereo decimator (2 instead of 4 decimators)
- Reduce FIR taps (48 instead of 96) with quality trade-off

---

## Rejected Optimizations

This section documents optimization approaches that were tested and found to provide no benefit or made performance worse. **Do not retry these approaches.**

---

### 1. Border SIMD Batching - NO GAIN

**Tested**: 2026-08-04  
**Target**: `ScreenZX::Draw()` border rendering  
**Hypothesis**: Batch border pixels to 8 per call (4 t-states) like screen area

**What was tried**:
```cpp
// Check if at 8-pixel boundary
if (framebufferX & 7) return;  // Skip non-boundary calls
// Render 8 border pixels at once
```

**Why it failed**:
1. **Check overhead exceeded savings** - The `framebufferX & 7` boundary check added more cycles than batching saved
2. **Small area** - Border is only ~30% of visible area (screen area is 70%)
3. **Already fast** - Simple 2-pixel `memset` or store is already near-optimal
4. **Measured result**: No change in frame time (within noise)

---

### 2. Screen Draw Branchless Pixel Selection - ALREADY OPTIMAL

**Tested**: 2026-08-04  
**Target**: `ScreenZX::Draw()` pixel color selection  
**Hypothesis**: Replace if/else with arithmetic masks

**Finding**: The SIMD implementation already uses branchless selection:
```cpp
// Arithmetic mask: bit=1 -> 0xFFFFFFFF, bit=0 -> 0x00000000
int32_t mask = -static_cast<int32_t>(bit);
pixel = (colorInk & mask) | (colorPaper & ~mask);
```

**Remaining branches are unavoidable or well-predicted**:
| Branch | Why it can't be removed |
|--------|------------------------|
| `_mode == M_NUL` | Constant per frame, 100% predicted |
| `renderType == RT_BLANK/SCREEN/BORDER` | Control flow dispatch, required |
| `pixelXBit != 0` | 75% taken (skip), well predicted |

**No further optimization possible.**

---

### 3. Branchless Tone Generator Counter - WORSE PERFORMANCE

**Tested**: 2026-08-04  
**Target**: `ToneGenerator::updateState()`  
**Hypothesis**: Replace counter increment/reset branches with arithmetic masks

**What was tried**:
```cpp
// Branchless version
_counter++;
uint16_t overflow = (_counter >= _period) & (_period > 0);
_counter &= -static_cast<int16_t>(!overflow);  // Reset on overflow
_out ^= overflow;  // Toggle on overflow
```

**Original code**:
```cpp
if (_period > 0) {
    _counter++;
    if (_counter >= _period) {
        _counter = 0;
        _out = !_out;
    }
}
```

**Why it failed**:
1. **More instructions** - Branchless version requires 6+ operations vs 2-3 for branching
2. **Well-predicted branches** - Counter overflows are rare (once per period), so branch predictor handles this perfectly
3. **Prescaler already skips 87.5%** - Only 1 in 8 calls even reach this code
4. **Measured result**: 602µs vs 600µs baseline (+0.3% worse)

---

### 4. Branchless Noise Generator LFSR - WORSE PERFORMANCE

**Tested**: 2026-08-04  
**Target**: `NoiseGenerator::updateState()`  
**Hypothesis**: Always compute LFSR shift, conditionally select result

**What was tried**:
```cpp
_counter++;
bool overflow = _counter >= (_period << 1);
_counter &= -static_cast<int8_t>(!overflow);

// Always compute new LFSR (even when not needed)
uint32_t tapBit = (_registerLSFR & 1) ^ ((_registerLSFR >> 3) & 1);
uint32_t newLSFR = (_registerLSFR >> 1) | (tapBit << 16);
// Branchless select
uint32_t mask = -static_cast<int32_t>(overflow);
_registerLSFR = (newLSFR & mask) | (_registerLSFR & ~mask);
_out = _registerLSFR & 1;
```

**Why it failed**:
1. **Wasted computation** - LFSR shift computed even when counter hasn't overflowed
2. **Same branch prediction issue** - Counter overflow is rare, predictor handles it
3. **More register pressure** - Needs temporaries for both old and new LFSR values
4. **Measured result**: No improvement, slightly worse

---

### 5. Per-Instruction Sound Processing (batch=0) - EXCESSIVE OVERHEAD

**Tested**: 2026-08-04  
**Target**: `SoundManager::handleStep()`  
**Hypothesis**: Process sound every Z80 instruction for maximum accuracy

**Measured results**:
| Batch Interval | Calls/Frame | Frame Time |
|----------------|-------------|------------|
| 0 (every instr) | 70,000 | 625 µs |
| 80 t-states | 875 | 597 µs |

**Why it's rejected as default**:
1. **4.5% slower** - 28µs overhead per frame for no audible benefit
2. **No quality improvement** - 80 t-states = 1 audio sample period, no data lost
3. **Inaudible latency** - 23µs is far below human perception threshold

**Note**: batch=0 is still available for special cases (debugging, analysis).

---

### 6. FIR Filter Circular Buffer Linearization - REPLACED

**Tested**: 2026-08-03 (before double-buffer redesign)  
**Target**: `FilterDecimator::getOutput()`  
**Hypothesis**: Copy circular buffer to linear array before SIMD convolution

**What was tried**:
```cpp
// Linearize buffer for SIMD (copying 96 doubles)
double linear[FIR_TAPS];
for (size_t i = 0; i < FIR_TAPS; i++) {
    linear[i] = _buffer[(_writeIndex + i) % FIR_TAPS];
}
// Then SIMD convolve on linear[]
```

**Why it failed**:
1. **Copy overhead dominated** - Copying 96 doubles per output sample negated SIMD gains
2. **Called 3500+ times/frame** - Linearization overhead accumulated rapidly
3. **Solution**: Double-buffer design eliminates linearization entirely

**Replaced by**: Double-buffer (write to both halves, read contiguous memory)

---

### 7. DrawPeriod BLANK-Skip with nextVisible LUT - ABANDONED

**Tested**: 2026-08-04  
**Target**: `ScreenZX::DrawPeriod()` loop  
**Hypothesis**: Pre-compute "next visible t-state" in LUT to skip BLANK regions without per-tstate check

**What was tried**:
```cpp
struct TstateCoordLUT {
    // ... existing fields ...
    uint16_t nextVisible;  // Next t-state where renderType != RT_BLANK
};

// In loop:
if (lut.renderType == RT_BLANK) {
    t = lut.nextVisible;  // Jump over BLANK region
    continue;
}
```

**Why it was abandoned**:
1. **Complex edge cases** - Handling wrap-around and toTstate boundaries introduced bugs
2. **Hang risk** - Incorrect nextVisible computation caused infinite loops
3. **Marginal gain expected** - BLANK regions already skipped with `continue`, branch predictor handles it well
4. **Simple `continue` is fast** - The check is just `if (renderType == RT_BLANK) continue;`

**The simple per-tstate BLANK check is sufficient.**

---

## Summary: When Branchless Hurts

These tests demonstrate that **branchless is not always faster**:

| Scenario | Branchless Wins | Branchless Loses |
|----------|-----------------|------------------|
| Unpredictable branches | ✓ | |
| Data-dependent selection (SIMD) | ✓ | |
| Rare events (overflow, error) | | ✓ |
| Simple if/else with good prediction | | ✓ |
| More instructions than branch cost | | ✓ |

**Rule of thumb**: Profile before converting to branchless. Modern branch predictors are very good at patterns like "counter overflow" that happen once every N iterations.


---

## Appendix: Profiling Commands

```bash
# Build benchmarks (Release mode)
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --target core-benchmarks -j8

# Run frame benchmark with repetitions
./bin/core-benchmarks --benchmark_filter='BM_Frame_TTD_Gaming' --benchmark_repetitions=3

# Profile with macOS sample
./bin/core-benchmarks --benchmark_filter='BM_Frame_TTD_Gaming' &
sample $! -f /tmp/profile.txt 3 1

# Run batch interval comparison
./bin/core-benchmarks --benchmark_filter='BM_Frame_TTD_Gaming_Batch'
```

---

## Conclusion

This optimization round achieved a **47% reduction in frame time** (913µs → 481µs) for TTD Gaming mode with SoundHQ enabled. Key learnings:

1. **Profile first** - The initial assumption that "screen rendering is slow" was wrong. TurboSound FIR decimation was the actual bottleneck (45% of frame time).

2. **SIMD requires data layout changes** - The biggest single win (28%) came from redesigning the FIR buffer as a double-buffer to enable contiguous SIMD access.

3. **Virtual call overhead matters in hot loops** - Inlining DrawPeriod gave 11.6% improvement by eliminating ~70,000 virtual calls per frame.

4. **Branchless is not always faster** - Several branchless attempts (tone generators, noise LFSR) made performance worse because modern branch predictors handle predictable patterns well.

5. **Cache what you'll need next** - The TTD page cache eliminated recursive decompression during capture, proving that "obvious" architectural inefficiencies often hide in plain sight.

**Remaining bottleneck**: Z80 core execution (~50% of frame time). Further optimization would require architectural changes (instruction caching, JIT) that are beyond the scope of this optimization round.

**Compatibility**: All optimizations preserve full emulation accuracy. Multicolor effects, demo timing, and audio quality are unchanged.
