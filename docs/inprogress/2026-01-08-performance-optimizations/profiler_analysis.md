# CPU Profiler Analysis - unreal-qt

**Total samples: 36,232** | **Capture date: 2026-01-09**

## Functionality Breakdown (Sorted by CPU Impact)

| # | Functionality | Samples | % of Total | Optimization Tactic | Toggle | Est. Savings |
|---|---------------|---------|------------|---------------------|--------|--------------|
| 1 | **Screen Rendering** | 8,379 | 23.1% | Skip when in headless/videowall mode | `video` | ~20% |
| 2 | **Sound DSP** | 6,467 | 17.8% | Bypass FIR/oversampling | `soundhq` | ~15% |
| 3 | **GIF Recording** | 6,467 | 17.8% | Disable when not recording | `recording` (existing) | ~17% |
| 4 | **Debugger Hooks** | 3,685 | 10.2% | Disable when not debugging | `breakpoints` (existing) | ~10% |
| 5 | **FDD Controller** | 2,489 | 6.9% | Idle when no disk activity | Already optimized | N/A |
| 6 | **Memory Tracking** | 1,334 | 3.7% | Disable when not debugging | `memorytracking` (existing) | ~3.5% |
| 7 | **Sound Generation** | Included in #2 | - | Master sound off | `sound` | ~17% |

---

## Detailed Breakdown by Component

### 1. Screen Rendering (8,379 samples - 23.1%)
| Function | Samples | Notes |
|----------|---------|-------|
| `ScreenZX::Draw` | 3,652 | Per-pixel rendering |
| `TransformTstateToFramebufferCoords` | 1,889 | Coordinate transform |
| `TransformTstateToZXCoords` | 1,114 | ZX coord lookup |
| `Screen::DrawPeriod` | 1,060 | Draw timing |
| Other | 664 | - |

**Optimization**: Add `video` feature toggle to skip rendering when headless or in low-priority tile.

---

### 2. Sound DSP Pipeline (6,467 samples - 17.8%)
| Function | Samples | Toggle Impact |
|----------|---------|---------------|
| `FilterInterpolate::decimate` | 1,235 | `soundhq off` bypasses |
| `recalculateInterpolationCoefficients` | 1,159 | `soundhq off` bypasses |
| `SoundChip_AY8910::updateMixer` | 1,101 | `soundhq off` bypasses |
| `SoundChip_TurboSound::handleStep` | 632 | `sound off` skips entirely |
| `NoiseGenerator::updateState` | 326 | `soundhq off` reduces |
| `ToneGenerator::updateState` | 282 | Minimal (needed) |
| `FilterDC::filter` | 160 | `soundhq off` bypasses |
| Other | 1,572 | - |

**Optimization**:
- `sound off`: Skip all sound processing (saves ~17%)
- `soundhq off`: Skip FIR/oversampling, use direct chip output (saves ~15%)

---

### 3. GIF Recording (6,467 samples - 17.8%)
| Function | Samples | Notes |
|----------|---------|-------|
| `GifPartition` | 54 | Palette quantization |
| `GifSwapPixels` | 48 | Pixel manipulation |
| `GifSplitPalette` | 9 | - |
| Many small funcs | 6,356 | Spread across recording |

**Optimization**: Already conditional on `RecordingManager::IsRecording()`. Ensure no overhead when inactive.

---

### 4. Debugger/Breakpoints (3,685 samples - 10.2%)
| Function | Samples | Notes |
|----------|---------|-------|
| `MemoryAccessTracker::TrackMemoryExecute` | 21+ | Per-instruction tracking |
| `MemoryAccessTracker::TrackMemoryRead` | 6+ | Per-read tracking |
| `BreakpointDescriptor` lookups | 5+ | Hash table lookups |
| Spread overhead | ~3,650 | In Memory::*Debug methods |

**Optimization**: Existing `breakpoints` and `memorytracking` toggles. Ensure cache checks are in hot paths.

---

### 5. FDD Controller (2,489 samples - 6.9%)
| Function | Samples | Notes |
|----------|---------|-------|
| `WD1793::processClockTimings` | 520 | Main timing loop |
| `processFDDIndexStrobe` | 324 | Disk rotation |
| `WD1793::process` | 130 | Command dispatch |
| Other | 1,515 | Logging, FSM |

**Optimization**: Already minimal when no disk activity. Logging could be reduced.

---

## Summary: Maximum Potential Savings

| Scenario | Features Disabled | Est. CPU Reduction |
|----------|-------------------|-------------------|
| **Headless mode** | `video off`, `sound off` | ~40% |
| **Turbo/Fast mode** | `soundhq off` | ~15% |
| **Non-debug mode** | `breakpoints off`, `memorytracking off` | ~13.5% |
| **Quiet background** | `sound off` | ~17% |
| **Max performance** | All toggles off | ~50-60% |

---

## Recommended Feature Toggles (Priority Order)

1. ✅ `sound` - Master sound toggle (NEW - in plan)
2. ✅ `soundhq` - HQ audio DSP toggle (NEW - in plan)
3. ⏸️ `video` - Screen rendering toggle (FUTURE)
4. ✅ `breakpoints` - Already exists
5. ✅ `memorytracking` - Already exists
