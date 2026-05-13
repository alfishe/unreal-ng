# Speed Control System

## Overview

The emulator supports two forms of speed control:

1. **Speed Multiplier** - Fixed speed multipliers (1x, 2x, 4x, 8x, 16x) with audio synchronization
2. **Turbo Mode** - Maximum speed without audio synchronization

## Architecture

### Speed Multiplier

**Mechanism**: Scales the number of t-states executed per video frame

**Implementation**: `core/src/emulator/cpu/z80.cpp:285-321`

```cpp
void Z80::Z80FrameCycle()
{
    // Calculate scaled frame limit based on speed multiplier
    uint32_t scaledFrameLimit = config.frame * state.current_z80_frequency_multiplier;
    
    // Execute CPU cycles until frame complete
    while (cpu.t < scaledFrameLimit)
    {
        ProcessInterrupts(...);
        Z80Step();
        OnCPUStep();
    }
}
```

**Supported Multipliers**:
- 1x: 3.5 MHz (69,888 t-states/frame)
- 2x: 7 MHz (139,776 t-states/frame)
- 4x: 14 MHz (279,552 t-states/frame)
- 8x: 28 MHz (559,104 t-states/frame)
- 16x: 56 MHz (1,118,208 t-states/frame)

**Frame Rate**: Fixed at ~50 FPS (synchronized to audio callback)

**Audio Behavior**: Pitch increases proportionally to speed multiplier

### Turbo Mode

**Mechanism**: Bypasses audio synchronization, allowing unlimited frame rate

**Implementation**: `core/src/emulator/mainloop.cpp:115-133`

```cpp
void MainLoop::Run(volatile bool& stopRequested)
{
    while (!stopRequested)
    {
        RunFrame();
        
        if (!config.turbo_mode)
        {
            // Normal: Wait for audio buffer drain
            std::unique_lock<std::mutex> lock(_audioBufferMutex);
            _cv.wait_for(lock, timeout, [&]{ 
                return _moreAudioDataRequested.load(); 
            });
        }
        // Turbo: No wait, immediate next frame
    }
}
```

**Frame Rate**: Unlimited (100s-1000s FPS, depends on host CPU)

**Audio Options**:
- `turbo_mode_audio = false`: No audio generation (silent)
- `turbo_mode_audio = true`: Audio generation continues (high pitch)

## Configuration

### CONFIG Structure

**File**: `core/src/emulator/platform.h:386-400`

```cpp
struct CONFIG
{
    /// Speed multiplier: 1x (default), 2x, 4x, 8x, or 16x
    uint8_t speed_multiplier = 1;
    
    /// Turbo/Max speed mode
    bool turbo_mode = false;
    
    /// Generate audio in turbo mode
    bool turbo_mode_audio = false;
};
```

### Persistence

Speed settings can be saved to INI file:

```ini
[emulator]
speed_multiplier=1
turbo_mode=0
turbo_mode_audio=0
```

## API

### Core Class

**File**: `core/src/emulator/cpu/core.h`

```cpp
class Core
{
public:
    // Speed multiplier control
    void SetSpeedMultiplier(uint8_t multiplier);  // 1, 2, 4, 8, 16
    uint8_t GetSpeedMultiplier() const;
    
    // Turbo mode control
    void EnableTurboMode(bool withAudio = false);
    void DisableTurboMode();
    bool IsTurboMode() const;
};
```

### Emulator Class

**File**: `core/src/emulator/emulator.h`

```cpp
class Emulator
{
public:
    // Speed control
    void SetSpeed(BaseFrequency_t speed);
    BaseFrequency_t GetSpeed();
    
    // Turbo mode control
    void EnableTurboMode(bool withAudio = false);
    void DisableTurboMode();
    bool IsTurboMode() const;
};
```

## Audio System Integration

### Audio Generation

**File**: `core/src/emulator/sound/soundmanager.cpp:75-151`

Audio is generated based on t-state position within the frame:

```cpp
void SoundManager::updateDAC(uint32_t frameTState, int16_t left, int16_t right)
{
    size_t sampleIndex = (frameTState * SAMPLES_PER_FRAME) / config.frame;
    _beeperBuffer[sampleIndex * 2] = left;
    _beeperBuffer[sampleIndex * 2 + 1] = right;
}
```

**Key Constants** (`core/src/emulator/sound/audio.h`):
- `AUDIO_SAMPLING_RATE = 44100` Hz
- `SAMPLES_PER_FRAME = 882` (@ 50 Hz)
- `AUDIO_CHANNELS = 2` (stereo)

### Synchronization

**Normal Mode**: Main loop waits for audio buffer half-full notification

```cpp
// MainLoop::Run()
_cv.wait_for(lock, timeout, [&]{ 
    return _moreAudioDataRequested.load(); 
});
```

**Turbo Mode**: Skip the wait, run immediately

### Audio Pitch Shift

When speed multiplier increases:

1. More t-states executed per frame → CPU runs faster
2. Audio events (beeper toggles, AY writes) occur at higher rate
3. Sample generation captures faster events
4. Result: Audio frequency content scales with multiplier

**Example** (4x speed):
- Beeper tone at 440 Hz becomes 1760 Hz
- Music plays 4× faster
- This is expected behavior

## Performance Characteristics

### Speed Multiplier Modes

| Mode | Effective CPU | Real FPS | Frame Time | Audio Pitch | Use Case |
|------|---------------|----------|------------|-------------|----------|
| 1x | 3.5 MHz | ~50 | 20ms | Normal | Normal gameplay |
| 2x | 7 MHz | ~50 | 20ms | 2× | Slightly faster |
| 4x | 14 MHz | ~50 | 20ms | 4× | Fast testing |
| 8x | 28 MHz | ~50 | 20ms | 8× | Very fast |
| 16x | 56 MHz | ~50 | 20ms | 16× | Extreme speed |

**Frame rate remains constant** due to audio synchronization.

### Turbo Mode

| Hardware | Turbo FPS | Frame Time | CPU Usage |
|----------|-----------|------------|-----------|
| Apple M1 | 500-800 | 1.2-2ms | ~100% (1 core) |
| Intel i7 | 300-500 | 2-3.3ms | ~100% (1 core) |

**Use cases**:
- Fast-forward tape loading
- Quick save state navigation
- Automated testing
- TAS (Tool-Assisted Speedrun) recording

## Implementation Details

### Frame Execution Flow

#### Normal Mode (1x multiplier)
```
MainLoop::Run()
  ├─ RunFrame()
  │  ├─ OnFrameStart()
  │  ├─ ExecuteCPUFrameCycle()
  │  │  └─ Z80FrameCycle()
  │  │     └─ Execute 69,888 t-states
  │  ├─ OnFrameEnd()
  │  │  └─ Generate 882 audio samples
  │  └─ Post NC_VIDEO_FRAME_REFRESH
  └─ Wait for audio buffer half-full (~20ms)
```

#### 4x Multiplier Mode
```
MainLoop::Run()
  ├─ RunFrame()
  │  ├─ ExecuteCPUFrameCycle()
  │  │  └─ Z80FrameCycle()
  │  │     └─ Execute 279,552 t-states (4x)
  │  ├─ OnFrameEnd()
  │  │  └─ Generate 882 audio samples (4x pitch)
  │  └─ Post NC_VIDEO_FRAME_REFRESH
  └─ Wait for audio buffer half-full (~20ms)
```

#### Turbo Mode
```
MainLoop::Run()
  ├─ RunFrame()
  │  ├─ ExecuteCPUFrameCycle()
  │  │  └─ Execute t-states
  │  ├─ OnFrameEnd()
  │  │  └─ [Optional] Generate audio
  │  └─ Post NC_VIDEO_FRAME_REFRESH
  └─ No wait → immediate next frame
```

### CPU Frequency Calculation

```cpp
// EmulatorState
state.base_z80_frequency = 3'500'000;  // 3.5 MHz
state.current_z80_frequency_multiplier = 4;
state.current_z80_frequency = 3'500'000 * 4 = 14'000'000;  // 14 MHz
```

### Frame T-States Calculation

```cpp
config.frame = 69888;  // Base t-states per frame
multiplier = 4;
scaledFrameLimit = 69888 * 4 = 279552;  // T-states per frame at 4x
```

## Comparison with Original

### Original Unreal Speccy

**Speed Control**: Adjusted `conf.intfq` (interrupt frequency)
- 50 Hz → 1x speed
- 100 Hz → 2x speed
- Resampler calculated frames per VSync

**Synchronization**: 
- Video: VSync lock
- Audio: Buffer throttling

**Audio**: Pitch-shifted by design

### New Implementation

**Speed Control**: Adjusted `speed_multiplier`
- Scales t-states per frame
- Interrupt frequency remains 50 Hz

**Synchronization**:
- Normal: Audio callback + condition variable
- Turbo: No synchronization

**Audio**: Pitch-shifted (same behavior)

### Advantages of New Implementation

1. **Cleaner API**: Dedicated methods vs direct config access
2. **Better separation**: Speed vs sync are independent
3. **More flexible**: Turbo mode for max speed
4. **Documented**: Comprehensive documentation
5. **Testable**: Clear entry points for testing

## Testing

### Unit Tests

```cpp
TEST(SpeedControl, SetSpeedMultiplier)
{
    Core core(context);
    
    core.SetSpeedMultiplier(4);
    EXPECT_EQ(4, core.GetSpeedMultiplier());
    EXPECT_EQ(14'000'000, core.GetCPUFrequency());
}

TEST(SpeedControl, TurboMode)
{
    Core core(context);
    
    core.EnableTurboMode();
    EXPECT_TRUE(core.IsTurboMode());
    
    core.DisableTurboMode();
    EXPECT_FALSE(core.IsTurboMode());
}
```

### Integration Tests

1. Load snapshot, run at 4x, verify frame count increases 4x faster
2. Enable turbo, verify FPS > 100
3. Toggle speeds during gameplay, verify smooth transitions
4. Verify audio pitch scales correctly with multiplier

## Future Enhancements

### Potential Improvements

1. **Variable Speed Multipliers**
   ```cpp
   void SetSpeedMultiplier(float multiplier);  // e.g., 1.5x, 2.3x
   ```

2. **Frame Skip in Turbo**
   ```cpp
   config.turbo_skip_video = true;  // Don't render video frames
   ```

3. **Gradual Speed Transitions**
   ```cpp
   core->SetSpeedMultiplierGradual(4, 500ms);  // Ease to 4x over 500ms
   ```

4. **Per-Device Speed Control**
   ```cpp
   core->SetCPUSpeed(4);    // CPU at 4x
   core->SetAYSpeed(1);     // AY chip at 1x (normal pitch)
   ```

5. **Rewind Feature**
   ```cpp
   // Use turbo + save states for rewind
   core->EnableRewindMode();
   ```

## References

- [Original Unreal Speccy](https://github.com/djdron/UnrealSpeccyP) - Speed control via `conf.intfq`
- [SPEED_MULTIPLIER_IMPLEMENTATION.md](../../SPEED_MULTIPLIER_IMPLEMENTATION.md) - Detailed implementation guide
- [SPEED_MULTIPLIER_USAGE_EXAMPLES.md](../../SPEED_MULTIPLIER_USAGE_EXAMPLES.md) - Usage examples
- [Audio System](sound-system.md) - Audio generation and synchronization
- [Main Loop](mainloop.md) - Frame execution and timing

