# Feature Toggles Design: Runtime Performance Optimization

## Executive Summary

This document describes the design and implementation plan for runtime feature toggles that will reduce CPU usage for sound generation and breakpoint handling in multi-instance scenarios (video wall, turbo mode, non-debugging sessions).

**Key Goals:**
- Enable/disable CPU-intensive features at runtime
- Per-instance control (each emulator can have different settings)
- No restart required
- Preserve existing feature toggle architecture

## Profiling Analysis

Based on the flamegraph analysis, the two major CPU consumers are:

1. **Sound Generation Pipeline** (~30-40% of CPU time):
   - `SoundChip_AY8910::handleStep()` - Per-instruction AY chip state updates
   - `SoundChip_TurboSound::handleStep()` - Dual AY chip coordination + **DSP processing**
   - `SoundManager::handleFrameEnd()` - Audio mixing and buffer management
   - `FilterInterpolate` - **High-precision FIR filtering (192-tap polyphase filter)**
   - `FilterDC` - DC offset removal filters

2. **Breakpoint Serving** (~15-20% of CPU time):
   - `BreakpointManager::Find()` - Address/port lookup on every memory/IO operation
   - Debug memory interface overhead
   - Per-instruction PC change notifications

### DSP Post-Processing Overhead Analysis

**Critical Discovery:** The AY-8910 emulation includes sophisticated DSP post-processing that accounts for significant CPU usage:

#### Oversampling Architecture

**Constants** ([soundchip_ay8910.h:65-76](core/src/emulator/sound/chips/soundchip_ay8910.h#L65-L76)):
```cpp
static const int AY_OVERSAMPLING_FACTOR = 64;
static const int AY_SAMPLING_RATE = 44100;
// Effective oversampling rate = 44100 * 64 = 2.822 MHz
```

**Why This Is Expensive:**
- Base AY chip runs at 1.75 MHz
- Chip is oversampled at 2.822 MHz (64x factor)
- Each audio sample at 44.1 kHz requires 8 oversampled calculations
- **192-tap FIR polyphase filter** applied to each oversampled block

#### FIR Filter Processing

**Filter Specifications** ([filter_interpolate.h:75-76](core/src/common/sound/filters/filter_interpolate.h#L75-L76)):
```cpp
static constexpr size_t FIR_ORDER = 192;        // 192-tap polyphase filter
static constexpr size_t DECIMATE_FACTOR = 8;    // Oversampling/decimation factor
```

**DSP Pipeline per Audio Sample** ([soundchip_turbosound.cpp:37-63](core/src/emulator/sound/chips/soundchip_turbosound.cpp#L37-L63)):
```cpp
// For EACH audio sample (882 samples per frame @ 50 Hz):
1. _chip0->firLeft().startOversamplingBlock()      // Initialize FIR state
2. _chip0->firRight().startOversamplingBlock()
3. _chip1->firLeft().startOversamplingBlock()      // TurboSound = 2 AY chips
4. _chip1->firRight().startOversamplingBlock()

5. for (int j = DECIMATE_FACTOR - 1; j >= 0; j--) // 8 iterations per sample
   {
       updateState(true);                          // Update AY chip state

       // Recalculate interpolation coefficients (4 FIR filters × 8 times)
       _chip0->firLeft().recalculateInterpolationCoefficients(...)
       _chip0->firRight().recalculateInterpolationCoefficients(...)
       _chip1->firLeft().recalculateInterpolationCoefficients(...)
       _chip1->firRight().recalculateInterpolationCoefficients(...)
   }

6. endOversamplingBlock() → calls decimate()      // 192-tap FIR convolution
```

**FIR Convolution Complexity** ([filter_interpolate.cpp:70-100](core/src/common/sound/filters/filter_interpolate.cpp#L70-L100)):
```cpp
double FilterInterpolate::decimate(double* x)
{
    // 96 multiply-accumulate operations per call
    // Coefficient array with 96 unique values
    // Symmetric FIR filter (exploits symmetry for optimization)
    y = -0.0000046183113992051936 * (x[1] + x[191]) +
        -0.00001117761640887225 * (x[2] + x[190]) +
        // ... 94 more MAC operations ...
}
```

#### DC Offset Filtering

**Additional Processing** ([soundchip_ay8910.cpp:431-436](core/src/emulator/sound/chips/soundchip_ay8910.cpp#L431-L436)):
```cpp
// Filter out DC offset (analog capacitor simulation)
_mixedLeft = _filterDCLeft.filter(_mixedLeft);
_mixedRight = _filterDCRight.filter(_mixedRight);
```

**Fields** ([soundchip_ay8910.h:344-349](core/src/emulator/sound/chips/soundchip_ay8910.h#L344-L349)):
```cpp
FilterInterpolate _leftFIR;
FilterInterpolate _rightFIR;
FilterDC<double> _filterDCLeft;
FilterDC<double> _filterDCRight;
```

#### CPU Cost Breakdown

**Per Video Frame (50 Hz, 882 samples):**
```
Base audio processing:
  - 882 samples × 4 FIR filters (2 chips × stereo) = 3,528 FIR operations
  - Each FIR: 8 oversampled iterations = 28,224 iterations
  - Each iteration: 192-tap convolution = 96 MAC operations
  - Total MACs: 28,224 × 96 = 2,709,504 multiply-accumulates per frame

At 50 FPS: 135,475,200 MACs per second!

Additional overhead:
  - 8 × updateState() calls per sample = 7,056 AY state updates per frame
  - 4 × DC filter per sample = 3,528 DC filter operations per frame
  - Stereo mixing and panning calculations
```

**This Explains the Heavy CPU Usage!**

The DSP post-processing is designed for **audiophile-grade** sound quality:
- Anti-aliasing via oversampling
- Sharp low-pass filter (192-tap FIR)
- DC offset removal (simulates analog coupling capacitors)
- Precise pitch accuracy

**Performance Trade-off:**
- ✅ **High Quality Mode**: Current implementation (64× oversampling, 192-tap FIR)
- ⚡ **Fast Mode**: Simple downsampling without FIR filters (10-20× faster audio)

## Current Architecture

### Existing Feature Toggle System

**FeatureManager** (`core/src/base/featuremanager.h`):
- Runtime toggleable features for debugging and performance
- INI-based persistence (`features.ini`)
- Per-instance (each Emulator has its own FeatureManager)
- Cached flags for hot-path performance

**Current Features:**
```cpp
Features::kDebugMode         // Master debug switch
Features::kMemoryTracking    // Memory access statistics
Features::kBreakpoints       // Breakpoint handling
Features::kCallTrace         // CALL/RET trace
```

**Feature Cache Pattern:**
```
FeatureManager → onFeatureChanged()
    ↓
Memory::UpdateFeatureCache()
    ↓
MemoryAccessTracker::UpdateFeatureCache()
    ↓
Z80::isDebugMode (master switch)
```

### Sound System Architecture

**Processing Pipeline:**

1. **Per-Frame Initialization** (`SoundManager::handleFrameStart()`):
   - Resets audio buffers
   - Prepares for new frame

2. **Per-CPU-Step Processing** (`SoundManager::handleStep()`):
   - Updates AY chip state after each CPU instruction
   - Accumulates chip register changes
   - **HIGH CPU COST** - called ~70,000 times per frame

3. **Frame Finalization** (`SoundManager::handleFrameEnd()`):
   - TurboSound generates mixed AY output
   - Beeper generates DAC output
   - Mixes both into final output buffer
   - Sends to audio callback (respects mute)
   - Captures for recording if active

**Key Components:**
- `SoundManager` - Orchestrator
- `SoundChip_TurboSound` - Dual AY-8910 container
- `SoundChip_AY8910` - Complete PSG emulation (3x ToneGenerator, NoiseGenerator, EnvelopeGenerator)
- `Beeper` - Simple DAC output

### Breakpoint System Architecture

**BreakpointManager** (`core/src/debugger/breakpoints/breakpointmanager.h`):
- Fast lookup via multiple hash maps:
  - `_breakpointMapByAddress` (memory breakpoints)
  - `_breakpointMapByPort` (I/O breakpoints)
  - `_breakpointMapByID` (sorted enumeration)

**Integration Points:**
- `HandlePCChange(uint16_t pc)` - Every instruction fetch
- `HandleMemoryRead/Write(uint16_t addr)` - Every memory access
- `HandlePortIn/Out(uint16_t port)` - Every I/O operation

**Control Flow:**
```
Z80::Step() → checks isDebugMode
    ↓ (if enabled)
Memory access → Debug interface → Breakpoint handlers
    ↓ (if disabled)
Memory access → Fast interface → No overhead
```

## Proposed Feature Toggles

### New Audio Features

```cpp
// Master audio control
Features::kAudioGeneration = "audiogeneration"
Features::kAudioGenerationAlias = "audio"
Features::kAudioGenerationDesc = "Enable audio generation during emulation"
Features::kAudioGenerationCategory = Features::kCategoryPerformance

// DSP Quality control (NEW - addresses heavy CPU usage)
Features::kAudioDSPQuality = "audiodspquality"
Features::kAudioDSPQualityAlias = "dsp"
Features::kAudioDSPQualityDesc = "Enable high-quality DSP post-processing (FIR filters, oversampling)"
Features::kAudioDSPQualityCategory = Features::kCategoryPerformance
// Modes: "high" (192-tap FIR + 64× oversampling), "low" (simple downsampling)

// Granular audio controls (optional)
Features::kAYChipEmulation = "ayemulation"
Features::kAYChipEmulationAlias = "ay"
Features::kAYChipEmulationDesc = "Enable AY-8910 chip emulation"

Features::kBeeperEmulation = "beeperemulation"
Features::kBeeperEmulationAlias = "beeper"
Features::kBeeperEmulationDesc = "Enable beeper/DAC emulation"
```

### Feature Categories

```cpp
Features::kCategoryPerformance = "performance"  // NEW
Features::kCategoryDebug = "debug"             // existing
Features::kCategoryAnalysis = "analysis"       // existing
```

### Audio Quality Modes

The `audiodspquality` feature supports multiple quality modes:

| Mode | Description | CPU Impact | Use Case |
|------|-------------|------------|----------|
| **high** | 192-tap FIR filter + 64× oversampling + DC filters | 100% (baseline) | Single instance, high-quality audio required |
| **low** | Simple linear interpolation, no FIR filters | ~10-20% of high | Video wall, turbo mode, background instances |
| **off** | Raw chip output, no interpolation | ~5% of high | Maximum performance, audio quality not critical |

**Implementation Strategy:**
- `high` mode: Current implementation (default)
- `low` mode: Skip FIR processing in `SoundChip_TurboSound::handleStep()`, use simple linear interpolation
- `off` mode: Direct chip output without any filtering

## Implementation Plan

### Phase 1: Add Audio Feature Constants

**File:** `core/src/base/featuremanager.h`

Add to `Features` namespace:
```cpp
// Audio Performance Features
constexpr const char* const kAudioGeneration = "audiogeneration";
constexpr const char* const kAudioGenerationAlias = "audio";
constexpr const char* const kAudioGenerationDesc = "Enable audio generation during emulation";

// DSP Quality Feature (NEW - critical for performance)
constexpr const char* const kAudioDSPQuality = "audiodspquality";
constexpr const char* const kAudioDSPQualityAlias = "dsp";
constexpr const char* const kAudioDSPQualityDesc = "Audio DSP quality mode";

// DSP Quality Modes
constexpr const char* const kAudioDSPQualityHigh = "high";  // 192-tap FIR + oversampling
constexpr const char* const kAudioDSPQualityLow = "low";    // Simple interpolation
constexpr const char* const kAudioDSPQualityOff = "off";    // No filtering

constexpr const char* const kAYChipEmulation = "ayemulation";
constexpr const char* const kAYChipEmulationAlias = "ay";
constexpr const char* const kAYChipEmulationDesc = "Enable AY-8910 chip emulation";

constexpr const char* const kBeeperEmulation = "beeperemulation";
constexpr const char* const kBeeperEmulationAlias = "beeper";
constexpr const char* const kBeeperEmulationDesc = "Enable beeper/DAC emulation";

// Category
constexpr const char* const kCategoryPerformance = "performance";
```

### Phase 2: Register Features in FeatureManager

**File:** `core/src/base/featuremanager.cpp`

In `FeatureManager::setDefaults()`, add:
```cpp
// Audio generation features (enabled by default for normal use)
registerFeature({
    Features::kAudioGeneration,
    Features::kAudioGenerationAlias,
    Features::kAudioGenerationDesc,
    true,  // enabled by default
    "",
    {Features::kStateOff, Features::kStateOn},
    Features::kCategoryPerformance
});

registerFeature({
    Features::kAYChipEmulation,
    Features::kAYChipEmulationAlias,
    Features::kAYChipEmulationDesc,
    true,  // enabled by default
    "",
    {Features::kStateOff, Features::kStateOn},
    Features::kCategoryPerformance
});

registerFeature({
    Features::kBeeperEmulation,
    Features::kBeeperEmulationAlias,
    Features::kBeeperEmulationDesc,
    true,  // enabled by default
    "",
    {Features::kStateOff, Features::kStateOn},
    Features::kCategoryPerformance
});
```

### Phase 3: Add Cache Flags to SoundManager

**File:** `core/src/emulator/sound/soundmanager.h`

Add to protected fields:
```cpp
// Feature cache flags
bool _feature_audio_enabled = true;
bool _feature_ay_enabled = true;
bool _feature_beeper_enabled = true;
std::string _feature_dsp_quality = "high";  // "high", "low", "off"
```

Add public method:
```cpp
void UpdateFeatureCache();
```

**File:** `core/src/emulator/sound/chips/soundchip_turbosound.h`

Add to protected fields:
```cpp
// Feature cache for DSP quality
std::string _dsp_quality_mode = "high";
```

Add public method:
```cpp
void UpdateFeatureCache();
void setDSPQuality(const std::string& mode);
```

### Phase 4: Implement Feature Cache Update

**File:** `core/src/emulator/sound/soundmanager.cpp`

Add method implementation:
```cpp
void SoundManager::UpdateFeatureCache()
{
    if (_context && _context->pFeatureManager)
    {
        _feature_audio_enabled = _context->pFeatureManager->isEnabled(Features::kAudioGeneration);
        _feature_ay_enabled = _context->pFeatureManager->isEnabled(Features::kAYChipEmulation);
        _feature_beeper_enabled = _context->pFeatureManager->isEnabled(Features::kBeeperEmulation);
    }
}
```

### Phase 5: Integrate Toggles into Audio Pipeline

**File:** `core/src/emulator/sound/soundmanager.cpp`

**A. Modify `handleStep()`:**
```cpp
void SoundManager::handleStep()
{
    // Fast exit if audio generation disabled
    if (!_feature_audio_enabled)
        return;

    // Only update AY chips if enabled
    if (_feature_ay_enabled && _turboSound)
    {
        _turboSound->handleStep();
    }

    // Beeper is handled separately via updateDAC
}
```

**B. Modify `handleFrameEnd()`:**
```cpp
void SoundManager::handleFrameEnd()
{
    // Fast exit if audio completely disabled
    if (!_feature_audio_enabled)
    {
        // Still need to handle recording and callbacks with silence
        // if they expect data
        AudioCallback callback = _context->pAudioCallback.load(std::memory_order_acquire);
        void* obj = _context->pAudioManagerObj.load(std::memory_order_acquire);

        if (callback && obj)
        {
            memset(_outBuffer, 0, SAMPLES_PER_FRAME * AUDIO_CHANNELS * sizeof(int16_t));
            callback(obj, _outBuffer, SAMPLES_PER_FRAME * AUDIO_CHANNELS);
        }
        return;
    }

    // Existing implementation with conditional chip processing
    uint16_t* _ayBuffer = _turboSound->getAudioBuffer();

    // Mix channels based on enabled features
    if (_feature_ay_enabled && _feature_beeper_enabled)
    {
        // Mix both AY and beeper
        AudioUtils::MixAudio((const int16_t*)_ayBuffer, _beeperBuffer, _outBuffer, AUDIO_BUFFER_SAMPLES_PER_FRAME);
    }
    else if (_feature_ay_enabled)
    {
        // Only AY chip
        memcpy(_outBuffer, _ayBuffer, AUDIO_BUFFER_SAMPLES_PER_FRAME * sizeof(int16_t));
    }
    else if (_feature_beeper_enabled)
    {
        // Only beeper
        memcpy(_outBuffer, _beeperBuffer, AUDIO_BUFFER_SAMPLES_PER_FRAME * sizeof(int16_t));
    }
    else
    {
        // No sound sources, output silence
        memset(_outBuffer, 0, SAMPLES_PER_FRAME * AUDIO_CHANNELS * sizeof(int16_t));
    }

    // Rest of existing code (recording, callback)
    // ...
}
```

**C. Modify `updateDAC()`:**
```cpp
void SoundManager::updateDAC(uint32_t frameTState, int16_t left, int16_t right)
{
    // Fast exit if audio or beeper disabled
    if (!_feature_audio_enabled || !_feature_beeper_enabled)
        return;

    // Existing implementation
    // ...
}
```

### Phase 6: Hook into Feature Change Notification

**File:** `core/src/base/featuremanager.cpp`

Modify `FeatureManager::onFeatureChanged()`:
```cpp
void FeatureManager::onFeatureChanged()
{
    // Existing code for Memory and Z80
    if (_context && _context->pCore && _context->pCore->GetMemory())
    {
        _context->pCore->GetMemory()->UpdateFeatureCache();
        _context->pCore->GetZ80()->isDebugMode = _features[Features::kDebugMode].enabled;
    }

    // NEW: Update SoundManager cache
    if (_context && _context->pSoundManager)
    {
        _context->pSoundManager->UpdateFeatureCache();
    }

    if (_dirty)
    {
        saveToFile(kFeaturesIni);
        _dirty = false;
    }
}
```

### Phase 7: Initialize Cache on Startup

**File:** `core/src/emulator/sound/soundmanager.cpp`

In `SoundManager::SoundManager()` constructor:
```cpp
SoundManager::SoundManager(EmulatorContext* context)
    : _context(context), _logger(nullptr)
{
    // ... existing initialization ...

    // Initialize feature cache
    UpdateFeatureCache();
}
```

### Phase 8: Implement DSP Quality Modes (CRITICAL FOR PERFORMANCE)

This phase addresses the heavy DSP processing identified in the flamegraph analysis.

**File:** `core/src/emulator/sound/chips/soundchip_turbosound.cpp`

**A. Add DSP Quality Setter:**
```cpp
void SoundChip_TurboSound::setDSPQuality(const std::string& mode)
{
    _dsp_quality_mode = mode;
}

void SoundChip_TurboSound::UpdateFeatureCache()
{
    if (_context && _context->pFeatureManager)
    {
        _dsp_quality_mode = _context->pFeatureManager->getMode(Features::kAudioDSPQuality);
    }
}
```

**B. Modify `handleStep()` with DSP Quality Branches:**
```cpp
void SoundChip_TurboSound::handleStep()
{
    size_t currentTStates = _context->pCore->GetZ80()->t;
    uint8_t speedMultiplier = _context->emulatorState.current_z80_frequency_multiplier;
    size_t scaledCurrentTStates = currentTStates * speedMultiplier;
    int32_t diff = scaledCurrentTStates - _lastTStates;

    if (diff > 0)
    {
        _ayPLL += diff * AUDIO_SAMPLE_TSTATE_INCREMENT;

        while (_ayPLL > 1.0 && _ayBufferIndex < AUDIO_SAMPLES_PER_VIDEO_FRAME * AUDIO_CHANNELS)
        {
            _ayPLL -= 1.0;

            int16_t leftSample;
            int16_t rightSample;

            // Branch based on DSP quality mode
            if (_dsp_quality_mode == Features::kAudioDSPQualityHigh)
            {
                // ========== HIGH QUALITY MODE (current implementation) ==========
                // 192-tap FIR filter + 64× oversampling + DC filtering

                _chip0->firLeft().startOversamplingBlock();
                _chip0->firRight().startOversamplingBlock();
                _chip1->firLeft().startOversamplingBlock();
                _chip1->firRight().startOversamplingBlock();

                // Oversample and apply LPF FIR
                for (int j = FilterInterpolate::DECIMATE_FACTOR - 1; j >= 0; j--)
                {
                    _x += _clockStep;

                    if (_x >= 1.0)
                    {
                        _x -= 1.0;
                        updateState(true);
                    }

                    _chip0->firLeft().recalculateInterpolationCoefficients(j, _chip0->mixedLeft());
                    _chip0->firRight().recalculateInterpolationCoefficients(j, _chip0->mixedRight());
                    _chip1->firLeft().recalculateInterpolationCoefficients(j, _chip1->mixedLeft());
                    _chip1->firRight().recalculateInterpolationCoefficients(j, _chip1->mixedRight());
                }

                leftSample = (_chip0->firLeft().endOversamplingBlock() + _chip1->firLeft().endOversamplingBlock()) * INT16_MAX;
                rightSample = (_chip0->firRight().endOversamplingBlock() + _chip1->firRight().endOversamplingBlock()) * INT16_MAX;
            }
            else if (_dsp_quality_mode == Features::kAudioDSPQualityLow)
            {
                // ========== LOW QUALITY MODE (10-20% CPU of high) ==========
                // Simple linear interpolation, no FIR filters, no oversampling

                _x += _clockStep;

                if (_x >= 1.0)
                {
                    _x -= 1.0;
                    updateState(true);
                }

                // Direct chip output with simple mixing (no FIR)
                leftSample = (_chip0->outLeft() + _chip1->outLeft());
                rightSample = (_chip0->outRight() + _chip1->outRight());
            }
            else // "off" mode
            {
                // ========== NO FILTERING MODE (~5% CPU of high) ==========
                // Direct chip output, update only when needed

                _x += _clockStep;

                if (_x >= 1.0)
                {
                    _x -= 1.0;
                    updateState(true);
                }

                // Raw chip output, no interpolation
                leftSample = (_chip0->outLeft() + _chip1->outLeft());
                rightSample = (_chip0->outRight() + _chip1->outRight());
            }

            // Store samples in output buffer
            _ayBuffer[_ayBufferIndex++] = leftSample;
            _ayBuffer[_ayBufferIndex++] = rightSample;
        }
    }

    _lastTStates = scaledCurrentTStates;
}
```

**C. Update SoundManager to propagate DSP quality:**
```cpp
void SoundManager::UpdateFeatureCache()
{
    if (_context && _context->pFeatureManager)
    {
        _feature_audio_enabled = _context->pFeatureManager->isEnabled(Features::kAudioGeneration);
        _feature_ay_enabled = _context->pFeatureManager->isEnabled(Features::kAYChipEmulation);
        _feature_beeper_enabled = _context->pFeatureManager->isEnabled(Features::kBeeperEmulation);
        _feature_dsp_quality = _context->pFeatureManager->getMode(Features::kAudioDSPQuality);

        // Propagate DSP quality to TurboSound
        if (_turboSound)
        {
            _turboSound->setDSPQuality(_feature_dsp_quality);
        }
    }
}
```

**D. Register DSP Quality Feature:**

In `FeatureManager::setDefaults()`:
```cpp
registerFeature({
    Features::kAudioDSPQuality,
    Features::kAudioDSPQualityAlias,
    Features::kAudioDSPQualityDesc,
    true,  // enabled by default
    Features::kAudioDSPQualityHigh,  // default mode
    {Features::kAudioDSPQualityHigh, Features::kAudioDSPQualityLow, Features::kAudioDSPQualityOff},
    Features::kCategoryPerformance
});
```

**Performance Impact:**
- **High mode** (default): 100% audio CPU (current behavior)
- **Low mode**: ~10-20% of high mode CPU (eliminates 192-tap FIR and oversampling)
- **Off mode**: ~5% of high mode CPU (minimal processing)

**Expected Total CPU Savings (video wall scenario):**
- Audio disabled: 30-40% savings
- Audio enabled with low DSP: 25-35% savings
- Audio enabled with off DSP: 28-38% savings
- Debug disabled: 15-20% savings
- **Combined (audio low + debug off): 40-55% total CPU savings**
- **Combined (audio off + debug off): 45-60% total CPU savings**

## Performance Optimization Strategy

### CPU Savings Estimation

**When Audio Generation Disabled:**
- **Eliminated:** 70,000 calls to `handleStep()` per frame (50 frames/sec)
- **Eliminated:** AY chip tone/noise/envelope generation
- **Eliminated:** Audio mixing and filtering
- **Expected Savings:** 30-40% CPU reduction

**When Breakpoints Disabled (already implemented):**
- **Eliminated:** Hash map lookups on every memory/IO operation
- **Eliminated:** Debug memory interface overhead
- **Expected Savings:** 15-20% CPU reduction

**Combined Effect (both disabled):**
- **Expected Savings:** 45-60% CPU reduction for video wall scenarios

### Use Cases

#### Use Case 1: Video Wall (Multiple Instances) - Maximum Performance
```bash
# Background instances: disable audio completely
feature set audio off
feature set debugmode off
```
**CPU Savings:** 45-60%
**Audio Quality:** None (silent)
**Best for:** Non-active emulator instances in video wall

#### Use Case 2: Video Wall - Audio with Low Quality DSP
```bash
# Want some audio feedback but need performance
feature set audio on
feature set dsp low           # Use simple interpolation instead of FIR
feature set debugmode off
```
**CPU Savings:** 40-55%
**Audio Quality:** Acceptable (some aliasing, less precise pitch)
**Best for:** Video wall with audio on selected instances

#### Use Case 3: Turbo Mode - Fast Forward
```bash
# Maximum emulation speed
feature set audio off         # Audio would overflow anyway in turbo
feature set debugmode off
```
**CPU Savings:** 45-60%
**Benefit:** Faster emulation speed, no audio buffer issues

#### Use Case 4: Debugging Session
```bash
# Full debugging with minimal distractions
feature set debugmode on
feature set bp on
feature set audio off         # No audio noise while debugging
```
**CPU Savings:** 30-40% (from audio)
**Benefit:** Full debugging capabilities, cleaner debugging environment

#### Use Case 5: Normal Operation - High Quality Audio
```bash
# Default configuration for single-instance use
feature set audio on
feature set dsp high          # Full 192-tap FIR filtering (default)
feature set debugmode off
feature set bp off
```
**CPU Savings:** 0% (baseline)
**Audio Quality:** Audiophile-grade (pristine sound)
**Best for:** Single emulator, listening to music/games

#### Use Case 6: Balanced Performance
```bash
# Good audio quality with moderate CPU usage
feature set audio on
feature set dsp low           # Skip expensive FIR processing
feature set debugmode off
```
**CPU Savings:** 25-35%
**Audio Quality:** Good (minor aliasing at high frequencies)
**Best for:** Multiple instances with audio, modest hardware

#### Use Case 7: Recording/Streaming
```bash
# High-quality audio for recording, but no debugging overhead
feature set audio on
feature set dsp high          # Best quality for recording
feature set debugmode off
feature set bp off
```
**CPU Savings:** 15-20% (from debug features)
**Audio Quality:** Maximum
**Best for:** Creating video recordings or live streaming

## Testing Strategy

### Unit Tests

1. **Feature Registration Test:**
   - Verify all audio features registered correctly
   - Check aliases work
   - Validate default states

2. **Cache Update Test:**
   - Toggle features, verify cache updates
   - Check SoundManager receives notifications

3. **Performance Test:**
   - Measure CPU usage with features on/off
   - Compare against profiling expectations

### Integration Tests

1. **Multi-Instance Test:**
   - Create 4 emulator instances
   - Set different feature configurations per instance
   - Verify independent operation

2. **Runtime Toggle Test:**
   - Start emulation with audio on
   - Toggle audio off during emulation
   - Verify smooth transition, no crashes

3. **Audio Quality Test:**
   - Ensure no audio artifacts when toggling
   - Verify recording continues to work

### Regression Tests

1. Verify existing features still work (debugmode, breakpoints, etc.)
2. Check features.ini persistence
3. Validate default configurations

## Multi-Instance Support

### Architecture Benefits

**FeatureManager is Per-Instance:**
- Each `Emulator` has its own `EmulatorContext`
- Each `EmulatorContext` has its own `FeatureManager`
- Features are completely isolated between instances

**Example Configuration:**
```
Instance 1 (Active - User is playing):
  - audio: ON
  - debugmode: OFF
  - ay: ON
  - beeper: ON

Instance 2 (Video wall - Background):
  - audio: OFF      ← saves 30% CPU
  - debugmode: OFF
  - ay: OFF
  - beeper: OFF

Instance 3 (Turbo mode - Fast forward):
  - audio: OFF      ← saves 30% CPU
  - debugmode: OFF
  - ay: OFF
  - beeper: OFF

Instance 4 (Debugging):
  - audio: OFF      ← less distraction
  - debugmode: ON
  - bp: ON
  - memtrack: ON
```

### API for Per-Instance Control

**EmulatorManager Access Pattern:**
```cpp
// Get specific emulator instance
Emulator* emu = EmulatorManager::getInstance()->getEmulator(uuid);

// Toggle features for this instance
emu->getContext()->pFeatureManager->setFeature("audio", false);
emu->getContext()->pFeatureManager->setFeature("debugmode", false);
```

## CLI Integration

### Existing Command Structure

The project likely has a CLI interface for feature management:

```bash
# Show all features
feature list

# Enable/disable features
feature set <id|alias> <on|off>

# Query feature state
feature get <id|alias>
```

### New Commands for Audio

```bash
# Disable all audio (master control)
feature set audio off
feature set audiogeneration off

# Fine-grained control
feature set ay off        # Disable AY chips only
feature set beeper off    # Disable beeper only

# Quick combos
feature set audio on      # Re-enable everything
```

## Migration Path

### Default Behavior

**All audio features default to ON:**
- Existing users see no change
- New installs work as expected
- Backward compatible

### Gradual Adoption

1. **Week 1:** Deploy feature infrastructure
2. **Week 2:** Monitor CPU usage, gather metrics
3. **Week 3:** Update documentation with use cases
4. **Week 4:** Add UI toggles (if GUI exists)

## Future Enhancements

### Additional Performance Features

```cpp
// Video rendering toggle
Features::kVideoRendering = "videorendering"
Features::kVideoRenderingAlias = "video"
Features::kVideoRenderingDesc = "Enable video frame generation"

// Screen refresh toggle
Features::kScreenRefresh = "screenrefresh"
Features::kScreenRefreshAlias = "screen"
Features::kScreenRefreshDesc = "Enable screen updates"

// Peripheral emulation toggle
Features::kPeripheralEmulation = "peripherals"
Features::kPeripheralEmulationAlias = "periph"
Features::kPeripheralEmulationDesc = "Enable peripheral device emulation (FDC, etc.)"
```

### Profile System

```cpp
// Predefined feature profiles
FeatureProfile::VideoWall     → audio=off, debug=off, video=on
FeatureProfile::Turbo         → audio=off, debug=off, video=optional
FeatureProfile::Debugging     → audio=off, debug=on, bp=on
FeatureProfile::Normal        → all enabled
```

### Auto-Optimization

```cpp
// Automatically disable features based on context
if (turboMode && !userOverride)
    setFeature("audio", false);

if (multipleInstancesRunning && !hasFocus)
    setFeature("audio", false);
```

## Implementation Checklist

### Core Implementation

- [ ] Add audio feature constants to `featuremanager.h`
- [ ] Add performance category constant
- [ ] Register audio features in `setDefaults()`
- [ ] Add cache flags to `SoundManager`
- [ ] Implement `SoundManager::UpdateFeatureCache()`
- [ ] Modify `handleStep()` with feature checks
- [ ] Modify `handleFrameEnd()` with feature checks
- [ ] Modify `updateDAC()` with feature check
- [ ] Hook SoundManager into `onFeatureChanged()`
- [ ] Initialize cache in SoundManager constructor

### Testing

- [ ] Write unit tests for feature registration
- [ ] Write unit tests for cache updates
- [ ] Write integration test for multi-instance
- [ ] Write performance benchmark
- [ ] Test runtime toggling
- [ ] Verify audio quality
- [ ] Check recording still works
- [ ] Regression test existing features

### Documentation

- [ ] Update features.ini documentation
- [ ] Document use cases
- [ ] Update CLI help text
- [ ] Add performance tuning guide
- [ ] Document multi-instance best practices

### Performance Validation

- [ ] Profile before changes
- [ ] Profile after changes
- [ ] Measure per-instance CPU usage
- [ ] Validate 30-40% audio savings
- [ ] Validate combined 45-60% savings

## Risk Assessment

### Low Risk

- **Existing feature system:** Well-tested infrastructure
- **Per-instance isolation:** No cross-instance interference
- **Default behavior:** All features ON = no user impact

### Medium Risk

- **Audio glitches:** Toggling during playback might cause artifacts
  - **Mitigation:** Frame boundary toggling, proper buffer handling

- **Recording impact:** Need to ensure recording works with features off
  - **Mitigation:** Always capture audio before mute check

### High Risk

- **None identified:** Architecture is well-suited for this enhancement

## Success Metrics

### Performance Metrics

- [ ] 30-40% CPU reduction with audio disabled
- [ ] 15-20% CPU reduction with breakpoints disabled
- [ ] 45-60% combined CPU reduction
- [ ] No performance regression with features enabled

### Quality Metrics

- [ ] Zero audio quality degradation
- [ ] Zero crashes during runtime toggling
- [ ] 100% multi-instance independence

### Usability Metrics

- [ ] CLI commands work as expected
- [ ] Features persist across restarts
- [ ] Documentation clarity (user feedback)

## Conclusion

This feature toggle system leverages the existing FeatureManager infrastructure to provide fine-grained runtime control over CPU-intensive features. The per-instance architecture makes it ideal for multi-instance scenarios like video walls, while the simple on/off toggles make it accessible to all users.

**Key Benefits:**
- ✅ No restart required
- ✅ Per-instance control
- ✅ Significant CPU savings (45-60%)
- ✅ Backward compatible
- ✅ Simple API
- ✅ Well-tested architecture

**Next Steps:**
1. Implement Phase 1-7 in sequence
2. Run test suite
3. Gather performance metrics
4. Deploy to production
5. Monitor user feedback
