# Feature Management System

## Overview

The Feature Management system provides runtime-toggleable features for debugging, analysis, and performance optimization. Features can be enabled/disabled at runtime without restarting the emulator, with per-instance control for multi-emulator scenarios.

## Architecture

### Core Components

| Component | File | Purpose |
|-----------|------|---------|
| **FeatureManager** | `core/src/base/featuremanager.h/.cpp` | Manages feature registration, state, and persistence |
| **Feature Cache** | Various components | Cached boolean flags for zero-overhead checks in hot paths |
| **features.ini** | Config file | UTF-8 persistence of feature states across restarts |

### Feature Cache Pattern

Hot paths (called thousands of times per frame) use **cached boolean flags** to avoid repeated hash map lookups:

```cpp
// FeatureManager::onFeatureChanged() triggers cache updates
void Memory::UpdateFeatureCache() {
    _feature_breakpoints_enabled = _context->pFeatureManager->isEnabled(Features::kBreakpoints);
}

// Hot path: direct boolean check (no overhead)
if (_feature_breakpoints_enabled) {
    BreakpointManager::HandlePCChange(pc);
}
```

**Propagation Flow:**
```
User CLI/API → FeatureManager::setFeature()
    ↓
FeatureManager::onFeatureChanged()
    ↓
Memory::UpdateFeatureCache()        // Updates _feature_breakpoints_enabled
SoundManager::UpdateFeatureCache()  // Updates _feature_sound_enabled
    ↓
Hot paths use cached booleans
```

---

## Supported Features

### Debug Features

| Feature | ID | Alias | Default | Use Case |
|---------|-----|-------|---------|----------|
| **Debug Mode** | `debugmode` | `dbg` | OFF | Master debug switch (gates all debug features) |
| **Breakpoints** | `breakpoints` | `bp` | OFF | Enable breakpoint serving (~10% CPU when ON) |
| **Memory Tracking** | `memorytracking` | `mem` | OFF | Track memory access stats (~4% CPU when ON) |
| **Call Trace** | `calltrace` | `ct` | OFF | Collect CALL/RET trace for debugging |

### Performance Features

| Feature | ID | Alias | Default | Use Case |
|---------|-----|-------|---------|----------|
| **Sound Generation** | `sound` | `snd` | ON | Master audio toggle (saves ~18% CPU when OFF) |
| **High-Quality DSP** | `soundhq` | `hq` | ON | FIR filters + oversampling (saves ~15% CPU when OFF) |
| **Screen HQ** | `screenhq` | `vhq` | ON | Per-t-state video rendering for multicolor demos |
| **Recording** | `recording` | `rec` | OFF | Master recording toggle (guards RecordingManager) |
| **Shared Memory** | `sharedmemory` | `shm` | OFF | Export emulator memory for external tool access |

---

## Sound Feature Details

### Sound Master Toggle (`sound`)

**Purpose:** Completely disable all sound processing.

**Implementation:**
- Early-exit in `SoundManager::handleStep()` (hot path)
- Called ~70,000 times per frame, skips expensive AY chip updates
- **CPU Savings:** ~18%

**Use Cases:**
- Videowall background instances
- Turbo/fast-forward mode
- Headless/automation mode

---

### High-Quality DSP Toggle (`soundhq`)

**Purpose:** Control audio quality vs. performance trade-off.

**ON (High Quality - Default):**
- 192-tap FIR polyphase filter
- 8x oversampling
- DC offset filtering
- **Quality:** Audiophile-grade, pristine sound
- **CPU:** Baseline

**OFF (Low Quality - Fast):**
- Direct AY chip output
- No FIR filtering
- No oversampling
- **Quality:** Acceptable, minor aliasing
- **CPU:** ~15% savings vs. HQ mode

**Implementation:** Conditional path in `SoundChip_TurboSound::handleStep()`

**Behavior Matrix:**

| `sound` | `soundhq` | Result | CPU |
|---------|-----------|--------|-----|
| OFF | - | Silent, no processing | +18% savings |
| ON | OFF | Economy mode (direct output) | +15% savings |
| ON | ON | HQ mode (FIR + oversampling) | Baseline |

---

### Shared Memory Toggle (`sharedmemory`)

**Purpose:** Export emulator memory via POSIX shared memory (`shm_open`) or Windows shared memory for external tool access.

**ON (Shared Memory):**
- Memory allocated via shared memory object
- Accessible by external tools (debuggers, memory viewers)
- Unique per-instance: `/zxspectrum_memory-<instance-id>` (POSIX) or `Local\zxspectrum_memory-<instance-id>` (Windows)
- Instance ID derived from emulator UUID (last 12 characters)
- Slightly higher allocation overhead

**OFF (Heap Memory - Default):**
- Standard heap allocation (`new[]`)
- Faster startup, lower overhead
- Memory private to emulator process

**Runtime Transition Behavior:**

| Transition | Action | Memory Content |
|------------|--------|----------------|
| OFF → ON | Allocate shared memory, copy heap content, free heap | **Preserved** |
| ON → OFF | Allocate heap, copy shared memory content, unmap shared | **Preserved** |
| ON → ON | No-op | Unchanged |
| OFF → OFF | No-op | Unchanged |

**Implementation Notes:**
- Memory content is always preserved during transitions
- Failed shared memory allocation falls back to heap gracefully
- Derived pointers (`_ramBase`, `_romBase`, etc.) are updated automatically
- Supports per-emulator instance control

**Use Cases:**
- External memory viewers / analyzers
- Real-time memory debugging with external tools
- Benchmarking (disable for accurate measurements)
- Headless automation (disable for simplicity)

---

## Multi-Instance Control

Features are **per-instance**, allowing different configurations per emulator:

```cpp
// Instance 1: Active gameplay
emulator1->getContext()->pFeatureManager->setFeature("sound", true);
emulator1->getContext()->pFeatureManager->setFeature("soundhq", true);

// Instance 2: Videowall background
emulator2->getContext()->pFeatureManager->setFeature("sound", false);
emulator2->getContext()->pFeatureManager->setFeature("debugmode", false);
// Result: 28% CPU savings on instance 2
```

---

## CLI Usage

```bash
# List all features
feature list

# Enable/disable features
feature sound off          # Disable sound completely
feature soundhq off        # Switch to low-quality audio
feature bp on              # Enable breakpoints
feature debugmode on       # Enable debug mode

# Query state
feature sound              # Show current state
```

---

## Implementation Guide

### Adding a New Feature Toggle

**1. Define constants** (`featuremanager.h`):
```cpp
constexpr const char* const kMyFeature = "myfeature";
constexpr const char* const kMyFeatureAlias = "mf";
constexpr const char* const kMyFeatureDesc = "Description of my feature";
```

**2. Register feature** (`featuremanager.cpp`):
```cpp
registerFeature({Features::kMyFeature,
                 Features::kMyFeatureAlias,
                 Features::kMyFeatureDesc,
                 true,  // default enabled
                 "",
                 {Features::kStateOff, Features::kStateOn},
                 Features::kCategoryPerformance});
```

**3. Add cache field** (component header):
```cpp
bool _feature_myfeature_enabled = true;
```

**4. Implement cache update** (component .cpp):
```cpp
void MyComponent::UpdateFeatureCache() {
    if (_context && _context->pFeatureManager) {
        _feature_myfeature_enabled = _context->pFeatureManager->isEnabled(Features::kMyFeature);
    }
}
```

**5. Hook into FeatureManager** (`featuremanager.cpp::onFeatureChanged()`):
```cpp
if (_context && _context->pMyComponent) {
    _context->pMyComponent->UpdateFeatureCache();
}
```

**6. Use in hot path**:
```cpp
void MyComponent::hotPath() {
    if (!_feature_myfeature_enabled)
        return;  // Fast exit
    
    // Expensive operation
}
```

---

## Best Practices

1. **Use cache for hot paths** - Never call `isEnabled()` directly in loops
2. **Per-instance isolation** - Each emulator has independent feature states
3. **Default to ON** - Performance features should default enabled for normal use
4. **Early exit** - Place feature checks at function entry for maximum savings
5. **Propagate immediately** - Use `onFeatureChanged()` callback for instant updates

---

## Performance Impact Summary

| Scenario | Features | CPU Savings |
|----------|----------|-------------|
| Headless/videowall | `sound off`, `debugmode off` | ~28% |
| Turbo mode | `sound off` | ~18% |
| Low-spec hardware | `soundhq off` | ~15% |
| Debug session | `debugmode on`, `bp on`, `sound off` | -10% (debug overhead) |
