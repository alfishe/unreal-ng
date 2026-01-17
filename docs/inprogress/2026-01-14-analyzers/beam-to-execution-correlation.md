# Beam-to-Execution Correlation: Mapping Code to Screen Timing

**Purpose:** Design for correlating Z80 instruction execution with ULA beam position within each frame.

**Status:** Design Draft - Not Yet Implemented

**Parent:** [Analyzer Architecture](./analyzer-architecture.md)

---

## 1. Overview

The ZX Spectrum's ULA draws the screen continuously while the Z80 executes code. By correlating **when** code executes (T-state within frame) with **where** the beam is (scanline/pixel), we can:

| Analysis Goal | Insight Gained |
|--------------|----------------|
| **Sprite timing** | Code writing to VRAM while beam is on same line = racing the beam |
| **Main loop detection** | Code spanning entire frame (T-state 0 → ~70000) = main loop |
| **Interrupt vs main code** | Code in T-states 0-500 = interrupt handler; rest = main code |
| **Screen routine timing** | VRAM writes during blank = efficient; during beam = potential flicker |
| **Multicolor effects** | Attribute changes mid-scanline = intentional demo effect |
| **Long algorithm detection** | Code blocks spanning multiple frames = heavy processing |

### 1.1 Frame Timing Reference (ZX Spectrum 48K)

```
Frame Duration: 69,888 T-states (50 Hz PAL)
T-states per line: 224
Total lines: 312 (192 visible + 56 top border + 56 bottom border + 8 VBlank)

    T-state    Scanline    Region          Beam Activity
    ────────────────────────────────────────────────────────
    0          0           VBlank          INT fires here
    ...        ...         VBlank          
    14,336     64          Top Border      Border color
    ...        ...         Top Border      
    28,672     128         Screen          Pixel + attr fetch
    ...        ...         Screen (192 lines)
    71,680     320         Bottom Border   Border color
    ...        ...         VBlank/Sync     

    Per Line (224 T-states):
    T-state    Pixels      Region
    ────────────────────────────────
    0-15       0-31        HBlank/Sync
    16-47      32-95       Left Border (32 pixels)
    48-175     96-351      Screen Area (256 pixels)
    176-207    352-415     Right Border (32 pixels)
    208-223    416-447     HBlank
```

### 1.2 Existing Infrastructure

**Available in ScreenZX:**

```cpp
// Pre-computed LUT: T-state → screen coordinates (already implemented!)
struct TstateCoordLUT {
    uint16_t framebufferX;      // Framebuffer X (UINT16_MAX if invisible)
    uint16_t framebufferY;      // Framebuffer Y
    uint16_t zxX;               // ZX screen X (UINT16_MAX if border/invisible)
    uint8_t zxY;                // ZX screen Y (255 if border/invisible)
    uint8_t symbolX;            // Character column (x / 8)
    uint8_t pixelXBit;          // Pixel within character (x % 8)
    RenderTypeEnum renderType;  // RT_BLANK, RT_BORDER, RT_SCREEN
    uint16_t screenOffset;      // VRAM offset for this position
    uint16_t attrOffset;        // Attribute offset for this position
};

TstateCoordLUT _tstateLUT[MAX_FRAME_TSTATES];  // 71,680 entries

// Runtime methods
bool TransformTstateToFramebufferCoords(uint32_t tstate, uint16_t* x, uint16_t* y);
bool TransformTstateToZXCoords(uint32_t tstate, uint16_t* zxX, uint16_t* zxY);
bool IsOnScreenByTiming(uint32_t tstate);
RenderTypeEnum GetLineRenderTypeByTiming(uint32_t tstate);
```

**Available in Z80 CPU:**

```cpp
struct _z80_cpu_context {
    unsigned long tstate;       // T-state clock of current/last step
    unsigned char op_tstate;    // Clean T-state of instruction (no WAITs)
    // ...
};
```

**Available in EmulatorState:**

```cpp
struct EmulatorState {
    uint64_t t_states;          // Cumulative T-state counter (updated per frame)
    uint64_t frame_counter;     // Frame number
    // ...
};
```

---

## 2. Infrastructure Gaps

### 2.1 Current Data Collection Gaps

| Gap | Description | Impact |
|-----|-------------|--------|
| **No T-state in memory access hooks** | `TrackMemoryRead/Write/Execute()` only receives address, value, caller | Cannot correlate memory access with beam position |
| **No T-state in CallTrace events** | `Z80ControlFlowEvent` lacks timing field | Cannot determine if jump happened during interrupt or main code |
| **No frame-relative T-state exposed** | Z80's `tstate` is cumulative, need `tstate % frame_duration` | Analyzers can't compute beam position |
| **TstateLUT not accessible to analyzers** | Private to ScreenZX class | Analyzers can't map T-state → screen region |

### 2.2 Gap Severity Assessment

```
┌────────────────────────────────────────────────────────────────────┐
│                    Beam Correlation Gaps                            │
├────────────────────────────────────────────────────────────────────┤
│                                                                    │
│   Z80 CPU                  Memory Tracker              Screen      │
│   ┌──────────────┐        ┌──────────────┐        ┌────────────┐  │
│   │ Has tstate!  │        │ NO tstate    │        │ Has LUT!   │  │
│   │              │───X───▶│ passed here  │───X───▶│ but private│  │
│   │ Knows timing │        │              │        │            │  │
│   │ per instr    │        │ Only:        │        │ tstate →   │  │
│   └──────────────┘        │ - address    │        │ screen pos │  │
│                           │ - value      │        └────────────┘  │
│                           │ - caller     │                        │
│   Gap: Not passed ────────┴──────────────┘                        │
│                                                                    │
└────────────────────────────────────────────────────────────────────┘
```

---

## 3. Proposed Solution

### 3.1 Phase 1: Context Awareness

Introduce a `TimingContext` that carries `tstate` and `frame` through the memory interface into the tracker.

```cpp
/// @brief Timing context for beam correlation
struct TimingContext {
    uint32_t frameTstate;     // T-states since frame start (0..69887)
    uint64_t frameNumber;     // Global frame counter
};
```

### 3.2 Phase 2: Beam Semantic Mapping

Add a method to `Screen` to provide semantic context for any given t-state.

```cpp
enum class BeamRegion : uint8_t {
    VBlank,         // Vertical blank (invisible)
    HBlank,         // Horizontal blank (invisible)
    TopBorder,      // Top border area
    BottomBorder,   // Bottom border area
    LeftBorder,     // Left border (within screen line)
    RightBorder,    // Right border (within screen line)
    Screen          // Active screen area (256×192)
};

struct BeamInfo {
    uint16_t scanline;
    uint16_t position;
    BeamRegion region;
    
    // For screen region only:
    uint8_t screenX;    // 0-255 (pixel) or 0-31 (character)
    uint8_t screenY;    // 0-191
};

// Add to Screen class
BeamInfo GetBeamInfo(uint32_t tstate) const;
```

### 3.3 Phase 3: Extended Data Structures

#### 3.3.1 Add T-state to Memory Access Events

```cpp
// Extended tracking method signatures
void TrackMemoryRead(uint16_t address, uint8_t value, uint16_t callerAddress, 
                     uint32_t frameTstate);  // NEW parameter
void TrackMemoryWrite(uint16_t address, uint8_t value, uint16_t callerAddress,
                      uint32_t frameTstate);  // NEW parameter
void TrackMemoryExecute(uint16_t address, uint16_t callerAddress,
                        uint32_t frameTstate);  // NEW parameter
```

#### 3.3.2 Add T-state to CallTrace Events

```cpp
struct Z80ControlFlowEvent {
    // ... existing fields ...
    
    // NEW: Timing information
    uint32_t frameTstate;       // T-state within current frame (0..69887)
    uint64_t frameNumber;       // Which frame this occurred in
};
```

### 3.4 Phase 4: Temporal Aggregation (Histogram)

Add a "Beam Histogram" to `AccessStats` to track access frequency across different screen regions without logging every individual timestamp.

```cpp
/// @brief Histogram of accesses by beam region
struct BeamHistogram {
    uint32_t vblankCount;
    uint32_t hblankCount;
    uint32_t borderCount;
    uint32_t screenCount;
    
    // Finer granularity: by scanline third
    uint32_t screenTopThird;     // Lines 0-63
    uint32_t screenMiddleThird;  // Lines 64-127
    uint32_t screenBottomThird;  // Lines 128-191
};

// Add to AccessStats
struct AccessStats {
    uint32_t readCount = 0;
    uint32_t writeCount = 0;
    uint32_t executeCount = 0;
    
    // ... existing maps ...
    
    // NEW: Beam correlation histogram
    BeamHistogram beamHistogram;
};
```

### 3.5 Beam Position Service

Expose TstateLUT functionality to analyzers:

```cpp
/// @brief Service to convert T-states to beam positions
/// Wraps ScreenZX's TstateLUT for analysis layer access
class BeamPositionService {
public:
    BeamPositionService(Screen* screen);
    
    /// Get beam position for a given frame T-state
    BeamInfo GetBeamInfo(uint32_t frameTstate) const;
    
    /// Check if T-state is in visible screen area
    bool IsOnScreen(uint32_t frameTstate) const;
    
    /// Check if T-state is in border area
    bool IsBorder(uint32_t frameTstate) const;
    
    /// Check if T-state is in blanking area
    bool IsBlank(uint32_t frameTstate) const;
    
    /// Get T-state range for a specific scanline
    TstateRange GetTstateRangeForScanline(uint16_t scanline) const;
    
    /// Get T-state range for visible screen area
    TstateRange GetScreenAreaTstates() const;
    
    /// Get frame duration in T-states
    uint32_t GetFrameDuration() const;
    
private:
    Screen* _screen;
};

struct TstateRange {
    uint32_t start;
    uint32_t end;
};
```

---

## 4. Timed Access Log

For detailed analysis, log accesses with timing:

```cpp
/// @brief Memory access event with timing
struct TimedMemoryAccess {
    uint16_t address;
    uint8_t value;
    uint16_t callerPC;
    uint32_t frameTstate;
    uint64_t frameNumber;
    AccessType type;            // Read, Write, Execute
    BeamRegion beamRegion;      // Where beam was during access
};

/// @brief Ring buffer of timed memory accesses
class TimedAccessLog {
public:
    void Log(const TimedMemoryAccess& access);
    
    // Query by frame
    std::vector<TimedMemoryAccess> GetAccessesForFrame(uint64_t frame) const;
    
    // Query by T-state range
    std::vector<TimedMemoryAccess> GetAccessesInTstateRange(
        uint64_t frame, uint32_t startTstate, uint32_t endTstate) const;
    
    // Query by beam region
    std::vector<TimedMemoryAccess> GetAccessesDuringScreen(uint64_t frame) const;
    std::vector<TimedMemoryAccess> GetAccessesDuringBlank(uint64_t frame) const;
    
    // Query VRAM accesses specifically
    std::vector<TimedMemoryAccess> GetVRAMAccessesDuringBeam(uint64_t frame) const;
    
private:
    std::vector<TimedMemoryAccess> _buffer;
    static constexpr size_t MAX_ENTRIES = 1'000'000;
};
```

---

## 5. Analysis Algorithms

### 5.1 Frame Span Analysis

Classify code blocks by how much of the frame they occupy:

```cpp
enum class FrameSpanType {
    InterruptOnly,      // T-states 0-1000 only
    MainLoopOnly,       // T-states 1000+ only
    FullFrame,          // Spans entire frame (likely main loop)
    MultiFrame,         // Execution crosses frame boundary
    Sporadic            // Scattered across frame
};

struct CodeBlockTiming {
    uint16_t startAddress;
    uint16_t endAddress;
    uint32_t minTstate;         // Earliest T-state seen
    uint32_t maxTstate;         // Latest T-state seen
    uint32_t avgTstate;         // Average T-state
    FrameSpanType spanType;
    float frameOccupancy;       // % of frame this code runs (0.0-1.0)
};

FrameSpanType ClassifyFrameSpan(uint32_t minT, uint32_t maxT, uint32_t frameDuration) {
    const uint32_t interruptWindow = 1000;  // ~1000 T-states for ISR
    
    if (maxT < interruptWindow) {
        return FrameSpanType::InterruptOnly;
    }
    if (minT > interruptWindow) {
        return FrameSpanType::MainLoopOnly;
    }
    if (maxT - minT > frameDuration * 0.8) {
        return FrameSpanType::FullFrame;
    }
    return FrameSpanType::Sporadic;
}
```

### 5.2 VRAM Write Timing Analysis

Detect racing-the-beam patterns:

```cpp
struct VRAMWriteAnalysis {
    uint32_t writesBeforeBeam;    // VRAM writes when beam hasn't reached address yet
    uint32_t writesAfterBeam;     // VRAM writes when beam already passed
    uint32_t writesRacingBeam;    // VRAM writes on same scanline as beam
    
    bool hasFlickerRisk;          // True if racing beam detected
    std::vector<uint16_t> flickerAddresses;
};

VRAMWriteAnalysis AnalyzeVRAMWriteTiming(
    const std::vector<TimedMemoryAccess>& vramWrites,
    const BeamPositionService& beam) 
{
    VRAMWriteAnalysis result = {};
    
    for (const auto& write : vramWrites) {
        // Get beam position when write occurred
        auto beamPos = beam.GetBeamInfo(write.frameTstate);
        
        // Get screen position being written
        auto [writeY, writeX] = VRAMAddressToScreenCoord(write.address);
        
        // Compare beam Y to write Y
        if (beamPos.screenY < writeY) {
            result.writesBeforeBeam++;  // Safe: beam hasn't reached this line
        } else if (beamPos.screenY > writeY) {
            result.writesAfterBeam++;   // Safe: beam already passed
        } else {
            // Same scanline - racing the beam!
            result.writesRacingBeam++;
            result.flickerAddresses.push_back(write.address);
        }
    }
    
    result.hasFlickerRisk = (result.writesRacingBeam > 0);
    return result;
}
```

### 5.3 Main Loop Detection via Timing

```cpp
struct MainLoopCandidate {
    uint16_t loopHeadAddress;
    uint32_t avgIterationTstates;   // How long each iteration takes
    float frameSpan;                 // What fraction of frame it occupies
    bool spansEntireFrame;
    bool crossesFrameBoundary;
};

std::vector<MainLoopCandidate> DetectMainLoop(
    const CallTraceBuffer& trace,
    const BeamPositionService& beam) 
{
    std::vector<MainLoopCandidate> candidates;
    
    // Look for loops with high iteration count spanning most of frame
    for (const auto& event : trace.GetAll()) {
        if (event.type == Z80CFType::JR || event.type == Z80CFType::JP) {
            if (event.target_addr <= event.m1_pc) {  // Backward jump = loop
                // Check timing span of this loop
                // ... analyze T-state range covered by iterations
            }
        }
    }
    
    return candidates;
}
```

### 5.4 Interrupt Handler Timing

```cpp
struct InterruptTimingAnalysis {
    uint32_t avgDurationTstates;
    uint32_t maxDurationTstates;
    float framePercentage;          // What % of frame spent in interrupt
    bool overlapsWithScreen;        // Does ISR run while beam is on screen?
    uint16_t beamLineAtExit;        // Where beam is when ISR completes
};

InterruptTimingAnalysis AnalyzeInterruptTiming(
    const InterruptLog& log,
    const BeamPositionService& beam)
{
    InterruptTimingAnalysis result = {};
    
    uint64_t totalDuration = 0;
    uint32_t maxDuration = 0;
    
    for (const auto& event : log.GetAllEvents()) {
        uint32_t duration = event.exitTstate - event.entryTstate;
        totalDuration += duration;
        maxDuration = std::max(maxDuration, duration);
        
        // Check beam position at ISR exit
        auto exitBeam = beam.GetBeamInfo(event.exitTstate);
        if (exitBeam.region == BeamRegion::Screen) {
            result.overlapsWithScreen = true;
        }
    }
    
    result.avgDurationTstates = totalDuration / log.GetEventCount();
    result.maxDurationTstates = maxDuration;
    result.framePercentage = static_cast<float>(result.avgDurationTstates) / 
                             beam.GetFrameDuration();
    
    return result;
}
```

---

## 6. Integration Points

### 6.1 Memory.cpp Hooks (Modified)

```cpp
// BEFORE: 
uint8_t Memory::ReadFromZ80Memory(uint16_t address) {
    uint8_t value = DirectReadFromZ80Memory(address);
    _memoryAccessTracker->TrackMemoryRead(address, value, 
        _context->pCore->GetZ80()->pc);
    return value;
}

// AFTER:
uint8_t Memory::ReadFromZ80Memory(uint16_t address) {
    uint8_t value = DirectReadFromZ80Memory(address);
    
    uint32_t frameTstate = GetCurrentFrameTstate();  // NEW
    
    _memoryAccessTracker->TrackMemoryRead(address, value, 
        _context->pCore->GetZ80()->pc, frameTstate);  // MODIFIED
    
    return value;
}

// Helper to get frame-relative T-state
uint32_t Memory::GetCurrentFrameTstate() {
    Z80* z80 = _context->pCore->GetZ80();
    uint32_t frameDuration = _context->pScreen->GetFrameDuration();
    return z80->GetTstate() % frameDuration;
}
```

### 6.2 CallTrace Logging (Modified)

```cpp
// BEFORE:
void CallTraceBuffer::LogEvent(const Z80ControlFlowEvent& event, uint64_t current_frame) {
    // ... existing code
}

// AFTER:
void CallTraceBuffer::LogEvent(const Z80ControlFlowEvent& event, 
                                uint64_t current_frame,
                                uint32_t frameTstate) {  // NEW parameter
    Z80ControlFlowEvent timedEvent = event;
    timedEvent.frameNumber = current_frame;
    timedEvent.frameTstate = frameTstate;
    // ... rest of logging
}
```

### 6.3 Feature Flags

```cpp
// New feature flags
Features::kTimedMemoryTracking    // Log T-state with memory accesses
Features::kTimedCallTrace         // Log T-state with control flow events
Features::kBeamCorrelation        // Enable beam position analysis
```

### 6.4 Memory Budget

| Component | Storage | Notes |
|-----------|---------|-------|
| TimedAccessLog (1M entries) | ~28 MB | `TimedMemoryAccess` = 28 bytes |
| Extended CallTrace events | +8 bytes/event | `frameTstate` + `frameNumber` |
| BeamHistogram per region | ~28 bytes | 7 × 4-byte counters |
| BeamPositionService | ~0 | References existing ScreenZX LUT |

---

## 7. Analysis Output

### 7.1 Execution Timeline

```yaml
frame_execution_timeline:
  frame: 12345
  duration_tstates: 69888
  
  regions:
    - name: "interrupt_handler"
      tstate_start: 0
      tstate_end: 285
      pc_range: [0x0038, 0x0055]
      beam_during: "vblank"
      
    - name: "main_loop_iteration_1"
      tstate_start: 300
      tstate_end: 35000
      pc_range: [0x8000, 0x8500]
      beam_during: "vblank → screen_line_80"
      
    - name: "screen_update_routine"
      tstate_start: 35000
      tstate_end: 45000
      pc_range: [0x9000, 0x9200]
      beam_during: "screen_line_80 → screen_line_124"
      vram_writes: 512
      racing_beam: false
      
    - name: "main_loop_iteration_2"
      tstate_start: 45000
      tstate_end: 69888
      pc_range: [0x8000, 0x8500]
      beam_during: "screen_line_124 → bottom_border"
```

### 7.2 Routine Timing Summary

```yaml
routine_timing:
  - routine: "screen_clear"
    address: 0x9000
    avg_start_tstate: 35000
    avg_duration_tstates: 8500
    typical_beam_region: "screen_area"
    racing_beam: false
    frames_analyzed: 1000
    
  - routine: "sprite_draw"
    address: 0x9500
    avg_start_tstate: 44000
    avg_duration_tstates: 2000
    typical_beam_region: "screen_area"
    racing_beam: true  # WARNING
    flicker_risk_addresses: [0x4800, 0x4820, 0x4840]
```

---

## 8. Verification Plan

### 8.1 Automated Tests

```cpp
TEST(BeamPositionService, CorrectlyMapsInterruptTime) {
    BeamPositionService beam(screen);
    
    // T-state 0 is start of frame (VBlank)
    auto pos = beam.GetBeamInfo(0);
    EXPECT_EQ(pos.region, BeamRegion::VBlank);
}

TEST(BeamPositionService, CorrectlyMapsScreenArea) {
    BeamPositionService beam(screen);
    
    // Calculate T-state for screen line 96, pixel 128
    uint32_t tstate = 64 * 224 + 96 * 224 + 48 + 64;  // Simplified
    auto pos = beam.GetBeamInfo(tstate);
    
    EXPECT_EQ(pos.region, BeamRegion::Screen);
    EXPECT_EQ(pos.screenY, 96);
    EXPECT_NEAR(pos.screenX, 128, 2);
}

TEST(VRAMWriteAnalysis, DetectsRacingBeam) {
    // Given: VRAM write to line 100 when beam is on line 100
    TimedMemoryAccess write = {
        .address = 0x4000 + ScreenLineToVRAMOffset(100),
        .frameTstate = TstateForScanline(100) + 50
    };
    
    auto analysis = AnalyzeVRAMWriteTiming({write}, beam);
    
    EXPECT_TRUE(analysis.hasFlickerRisk);
    EXPECT_EQ(analysis.writesRacingBeam, 1);
}

TEST(FrameSpanAnalysis, DetectsInterruptOnlyCode) {
    CodeBlockTiming block = {
        .minTstate = 0,
        .maxTstate = 500
    };
    
    EXPECT_EQ(ClassifyFrameSpan(block.minTstate, block.maxTstate, 69888),
              FrameSpanType::InterruptOnly);
}
```

### 8.2 Manual Verification

- [ ] Unit tests for `GetBeamInfo` mapping accuracy for 48K/128K timings
- [ ] Benchmark overhead of passing `TimingContext` through for all memory accesses
- [ ] Verify that "Music Player" (AY writes) consistently clusters within interrupt handler/border area
- [ ] Verify that "Sprite Draw" routines show high correlation with `Screen` region

---

## 9. Implementation Roadmap

### Phase 1: Context Awareness (Week 1)
- [ ] Add `frameTstate` parameter to `TrackMemory*()` methods
- [ ] Add `frameTstate` / `frameNumber` to `Z80ControlFlowEvent`
- [ ] Implement `GetCurrentFrameTstate()` helper
- [ ] Update all call sites

### Phase 2: Beam Semantic Mapping (Week 1)
- [ ] Add `GetBeamInfo()` method to Screen
- [ ] Create `BeamPositionService` class
- [ ] Expose `TstateCoordLUT` access

### Phase 3: Temporal Aggregation (Week 2)
- [ ] Add `BeamHistogram` to `AccessStats`
- [ ] Increment histogram counters in `TrackMemory*` methods
- [ ] Add histogram to export format

### Phase 4: Timed Logging (Week 2)
- [ ] Implement `TimedAccessLog` ring buffer
- [ ] Add feature flag `kTimedMemoryTracking`
- [ ] Query interface for frame/tstate range

### Phase 5: Analysis Algorithms (Week 2-3)
- [ ] Frame span analysis
- [ ] VRAM write timing analysis
- [ ] Racing-beam detection
- [ ] Interrupt timing analysis

### Phase 6: Integration (Week 3)
- [ ] Connect to RoutineClassifier
- [ ] Export timeline to YAML
- [ ] UI integration (future)

---

## 10. References

### Source Files
- [screenzx.h](../../../core/src/emulator/video/zx/screenzx.h) - TstateCoordLUT, beam methods
- [screen.h](../../../core/src/emulator/video/screen.h) - RasterState, RasterDescriptor
- [platform.h](../../../core/src/emulator/platform.h) - EmulatorState.t_states
- [z80ex/typedefs.h](../../../core/src/3rdparty/z80ex/typedefs.h) - Z80 tstate field
- [memoryaccesstracker.h](../../../core/src/emulator/memory/memoryaccesstracker.h) - Tracking hooks
- [calltrace.h](../../../core/src/emulator/memory/calltrace.h) - Z80ControlFlowEvent

### Related Documents
- [Analyzer Architecture](./analyzer-architecture.md)
- [Data Collection Extensions](./data-collection-extensions.md)
- [Interrupt Analyzer](./interrupt-analyzer.md)
- [Routine Classifiers](./routine-classifiers.md)

### ZX Spectrum Technical References
- "The Complete Spectrum ROM Disassembly" - Frame timing details
- "Spectrum Machine Language for the Absolute Beginner" - ULA timing
- "ZX Spectrum Technical FAQ" - Contended memory and timing
