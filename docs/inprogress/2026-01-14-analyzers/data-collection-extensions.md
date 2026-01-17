# Data Collection Extensions: New Instrumentation for Advanced Analysis

**Purpose:** Define new data structures and collection points needed for advanced analysis capabilities.

**Status:** Design Draft - Not Yet Implemented

**Parent:** [Analyzer Architecture](./analyzer-architecture.md)

---

## 1. Overview

The current data collection infrastructure (`MemoryAccessTracker` + `CallTraceBuffer`) provides ~40% of the data needed for advanced analysis. This document specifies extensions to enable the remaining 60%.

### 1.1 Gap Summary

| Analysis Goal | Current Coverage | Missing Data |
|--------------|------------------|--------------|
| Block Segmentation | 100% | - |
| Function Detection | 100% | - |
| Loop Detection | 100% | - |
| Interrupt Handler Detection | 20% | Interrupt context flag, handler entry/exit |
| Interrupt Activity Classification | 10% | T-state timing, scanline position |
| Screen Routine Detection | 40% | Data flow tracking, write patterns |
| Music Player Detection | 50% | Temporal periodicity, T-state timing |
| Timing Analysis | 0% | T-state counter exposure |
| **Main Loop Detection** | 30% | **Frame T-state in CallTrace events** |
| **Racing-Beam Detection** | 0% | **Beam position at VRAM write time** |

> **See also:** [Beam-to-Execution Correlation](./beam-to-execution-correlation.md) for detailed T-state/beam mapping design.

### 1.2 Design Principles

1. **Minimal overhead** - New tracking must not significantly impact emulation speed
2. **Optional** - Each extension gated by feature flags
3. **Layered** - Extensions build on existing infrastructure
4. **Non-invasive** - No changes to core emulation logic; hooks only

---

## 2. ExecutionContext Structure

### 2.1 Purpose

Capture execution state at each instruction for timing and context analysis.

### 2.2 Data Structure

```cpp
/// @brief Extended execution context captured at instruction execution
struct ExecutionContext {
    // === Timing ===
    uint64_t tstate;           // Global T-state counter (wraps at 64-bit max)
    uint32_t frameTstate;      // T-state within current frame (0..69888 for 48K)
    uint16_t scanline;         // Current scanline (0..311 for 48K)
    uint8_t scanlinePhase;     // Position within scanline (0..223)
    
    // === Interrupt State ===
    bool inInterrupt;          // True if executing interrupt handler
    uint8_t interruptNestLevel; // Nesting depth (0 = main, 1+ = interrupt)
    
    // === Call Stack ===
    uint8_t callDepth;         // Current call stack depth (0-255, saturates)
    
    // === Instruction Info ===
    uint8_t opcodeByte0;       // First opcode byte (for quick categorization)
    uint8_t instructionLength; // 1-4 bytes
    
    // === Memory Mapping ===
    uint8_t currentPage[4];    // Page numbers for each 16K bank
    bool isROM[4];             // ROM/RAM flags for each bank
};
```

### 2.3 Collection Point

```cpp
// In Z80 M1 cycle handler (z80.cpp)
void Z80::ExecuteInstruction() {
    if (_feature_execution_context_enabled) {
        ExecutionContext ctx = CaptureExecutionContext();
        _executionContextLog->Log(ctx, pc);
    }
    
    // ... existing instruction execution ...
}

ExecutionContext Z80::CaptureExecutionContext() {
    return {
        .tstate = _context->emulatorState.global_tstate,
        .frameTstate = _context->emulatorState.frame_tstate,
        .scanline = _context->pScreen->GetCurrentScanline(),
        .scanlinePhase = _context->pScreen->GetScanlinePhase(),
        .inInterrupt = _interruptContext,
        .interruptNestLevel = _interruptNestLevel,
        .callDepth = _callStackDepth,
        .opcodeByte0 = _memory->DirectReadFromZ80Memory(pc),
        .instructionLength = GetInstructionLength(pc),
        .currentPage = { _memory->GetPageForBank(0), ... },
        .isROM = { _memory->IsBankROM(0), ... }
    };
}
```

### 2.4 Storage Strategy

**Option A: Sampled Logging (Recommended)**

Log only at specific events (frame start, interrupt, segment boundary):

```cpp
class ExecutionContextLog {
public:
    void LogAtEvent(const ExecutionContext& ctx, uint16_t pc, EventType event);
    
private:
    struct LogEntry {
        ExecutionContext ctx;
        uint16_t pc;
        EventType event;
        uint64_t frameNumber;
    };
    
    std::vector<LogEntry> _log;
    static constexpr size_t MAX_ENTRIES = 100000;
};
```

**Option B: Per-Instruction Logging (Expensive)**

Log every instruction for deep analysis:

```cpp
// Ring buffer with ~10M entries (1GB)
class DenseExecutionLog {
    std::vector<ExecutionContext> _buffer;
    size_t _head = 0;
    size_t _capacity = 10'000'000;
};
```

**Recommendation:** Use Option A by default; Option B only for intensive debugging sessions.

---

## 3. Interrupt Context Tracking

### 3.1 Purpose

Track interrupt entry/exit to classify activity during interrupt service routines.

### 3.2 Data Structure

```cpp
/// @brief Tracks a single interrupt invocation
struct InterruptEvent {
    uint64_t entryTstate;      // T-state at interrupt entry
    uint64_t exitTstate;       // T-state at RETI/EI
    uint64_t frameNumber;      // Frame when interrupt occurred
    uint16_t handlerAddress;   // Address jumped to (IM1: 0x0038, IM2: from table)
    uint16_t returnAddress;    // Where we came from (PC before interrupt)
    
    // Activity summary
    uint32_t instructionsExecuted;
    uint32_t memoryReads;
    uint32_t memoryWrites;
    uint32_t portReads;
    uint32_t portWrites;
    
    // Port activity detail (top 3 ports by access count)
    struct PortActivity {
        uint16_t port;
        uint8_t reads;
        uint8_t writes;
    };
    std::array<PortActivity, 3> topPorts;
};
```

### 3.3 Collection Points

```cpp
// In Z80 interrupt handler (z80.cpp)
void Z80::HandleInterrupt() {
    if (_feature_interrupt_tracking_enabled) {
        _currentInterrupt = {
            .entryTstate = _context->emulatorState.global_tstate,
            .frameNumber = _context->emulatorState.frame_counter,
            .handlerAddress = GetInterruptVector(),
            .returnAddress = pc
        };
        _inInterrupt = true;
        _interruptNestLevel++;
    }
    
    // ... existing interrupt handling ...
}

void Z80::ExecuteRETI() {
    if (_feature_interrupt_tracking_enabled && _inInterrupt) {
        _currentInterrupt.exitTstate = _context->emulatorState.global_tstate;
        _currentInterrupt.instructionsExecuted = _interruptInstructionCount;
        // ... capture other metrics ...
        _interruptLog->Log(_currentInterrupt);
        
        _interruptNestLevel--;
        if (_interruptNestLevel == 0) {
            _inInterrupt = false;
        }
    }
    
    // ... existing RETI handling ...
}
```

### 3.4 Interrupt Log

```cpp
class InterruptLog {
public:
    void Log(const InterruptEvent& event);
    
    // Query interface
    std::vector<InterruptEvent> GetEventsForFrame(uint64_t frame) const;
    uint32_t GetAverageInterruptDuration() const;  // In T-states
    uint16_t GetDetectedHandlerAddress() const;
    
private:
    std::vector<InterruptEvent> _events;
    static constexpr size_t MAX_EVENTS = 100000;  // ~33 min at 50Hz
};
```

---

## 4. Port Activity Patterns

### 4.1 Purpose

Detect temporal patterns in port activity for music player identification.

### 4.2 Data Structure

```cpp
/// @brief Tracks port access patterns over time
struct PortAccessEvent {
    uint16_t port;
    uint8_t value;
    bool isWrite;              // true = OUT, false = IN
    uint64_t tstate;           // When it happened
    uint64_t frame;
    uint16_t callerPC;
    bool duringInterrupt;
};

/// @brief Aggregated pattern for a specific port
struct PortPattern {
    uint16_t port;
    
    // Temporal analysis
    uint32_t totalAccesses;
    double avgTstatesBetweenAccesses;
    double stddevTstates;
    bool isPeriodic;           // True if regular interval detected
    uint32_t detectedPeriod;   // T-states between accesses (if periodic)
    
    // Context analysis
    float interruptRatio;      // % of accesses during interrupt
    std::vector<uint16_t> topCallers;  // PCs that access this port most
    
    // Value analysis
    std::unordered_map<uint8_t, uint32_t> valueHistogram;
};
```

### 4.3 Periodicity Detection Algorithm

```cpp
bool DetectPeriodicity(const std::vector<PortAccessEvent>& events, 
                       uint32_t& detectedPeriod) {
    if (events.size() < 10) return false;
    
    // Compute inter-access intervals
    std::vector<uint64_t> intervals;
    for (size_t i = 1; i < events.size(); i++) {
        intervals.push_back(events[i].tstate - events[i-1].tstate);
    }
    
    // Compute mean and stddev
    double mean = std::accumulate(intervals.begin(), intervals.end(), 0.0) / intervals.size();
    double variance = 0;
    for (auto interval : intervals) {
        variance += (interval - mean) * (interval - mean);
    }
    double stddev = std::sqrt(variance / intervals.size());
    
    // If coefficient of variation < 10%, consider periodic
    if (stddev / mean < 0.1) {
        detectedPeriod = static_cast<uint32_t>(mean);
        return true;
    }
    
    return false;
}
```

### 4.4 Collection Point

```cpp
// In Memory::HandlePortWrite / HandlePortRead
void Memory::HandlePortWrite(uint16_t port, uint8_t value) {
    if (_feature_port_pattern_tracking) {
        _portActivityLog->Log({
            .port = port,
            .value = value,
            .isWrite = true,
            .tstate = _context->emulatorState.global_tstate,
            .frame = _context->emulatorState.frame_counter,
            .callerPC = _context->pCore->GetZ80()->pc,
            .duringInterrupt = _context->pCore->GetZ80()->IsInInterrupt()
        });
    }
    
    // ... existing port handling ...
}
```

---

## 5. Data Flow Tracking (Optional)

### 5.1 Purpose

Correlate memory reads with subsequent writes to detect data movement (e.g., sprite blitting).

### 5.2 Data Structure

```cpp
/// @brief Tracks data movement between addresses
struct DataFlowEvent {
    uint16_t sourceAddr;
    uint16_t destAddr;
    uint8_t value;
    uint16_t pc;               // Instruction that performed the move
    uint64_t tstate;
};

/// @brief Aggregated flow pattern
struct DataFlowPattern {
    uint16_t sourceRegionStart;
    uint16_t sourceRegionEnd;
    uint16_t destRegionStart;
    uint16_t destRegionEnd;
    uint32_t moveCount;
    float confidence;          // How sure we are this is intentional
};
```

### 5.3 Tracking Approach

**Shadow Register Tracking:**

Track what value is in each register and where it came from:

```cpp
struct RegisterShadow {
    uint8_t value;
    uint16_t sourceAddress;    // Where this value was loaded from (or 0xFFFF if immediate)
    uint64_t loadTstate;
};

class DataFlowTracker {
public:
    void OnMemoryRead(uint16_t addr, uint8_t value, Register destReg) {
        _registerShadow[destReg] = {
            .value = value,
            .sourceAddress = addr,
            .loadTstate = _tstate
        };
    }
    
    void OnMemoryWrite(uint16_t addr, uint8_t value, Register srcReg) {
        auto& shadow = _registerShadow[srcReg];
        if (shadow.sourceAddress != 0xFFFF && shadow.value == value) {
            LogDataFlow(shadow.sourceAddress, addr, value);
        }
    }
    
private:
    std::array<RegisterShadow, 16> _registerShadow;  // A, B, C, D, E, H, L, ...
};
```

**Cost:** Moderate - requires per-register tracking.

**Recommendation:** Disabled by default; enable only for sprite/screen analysis.

---

## 6. Screen Activity Tracking

### 6.1 Purpose

Track writes to screen memory regions with caller information.

### 6.2 VRAM Layout (ZX Spectrum)

```
0x4000-0x57FF: Pixel data (6144 bytes)
0x5800-0x5AFF: Attributes (768 bytes)
```

### 6.3 Data Structure

```cpp
/// @brief Tracks screen memory writes
struct ScreenWriteEvent {
    uint16_t address;          // VRAM address (0x4000-0x5AFF)
    uint8_t value;
    uint16_t callerPC;
    uint64_t tstate;
    uint64_t frame;
    bool duringInterrupt;
};

/// @brief Aggregated screen activity analysis
struct ScreenActivitySummary {
    // Dirty region tracking
    uint16_t dirtyRectX1, dirtyRectY1;
    uint16_t dirtyRectX2, dirtyRectY2;
    
    // Write pattern
    uint32_t pixelWrites;
    uint32_t attrWrites;
    float writeRate;           // Writes per frame
    
    // Caller analysis
    struct CallerStats {
        uint16_t pc;
        uint32_t writeCount;
        std::string routineType;  // "clear", "scroll", "blit", "unknown"
    };
    std::vector<CallerStats> topCallers;
};
```

### 6.4 Dirty Rectangle Tracking

```cpp
class ScreenActivityTracker {
public:
    void OnVRAMWrite(uint16_t addr, uint8_t value, uint16_t callerPC) {
        // Convert address to X,Y
        auto [x, y] = AddressToScreenCoord(addr);
        
        // Update dirty rectangle
        _dirtyRect.Expand(x, y);
        
        // Track caller
        _callerStats[callerPC]++;
        
        // Log event (if detailed tracking enabled)
        if (_detailedLogging) {
            _writeLog.push_back({addr, value, callerPC, _tstate, _frame});
        }
    }
    
    ScreenActivitySummary GetSummary() const;
    void ResetFrame();
    
private:
    Rect _dirtyRect;
    std::unordered_map<uint16_t, uint32_t> _callerStats;
    std::vector<ScreenWriteEvent> _writeLog;
};
```

---

## 7. Integration Points

### 7.1 Feature Flags

| Flag | Effect | Overhead |
|------|--------|----------|
| `Features::kExecutionContext` | Enable ExecutionContext logging | ~5% |
| `Features::kInterruptTracking` | Track interrupt entry/exit | ~2% |
| `Features::kPortPatterns` | Track port access patterns | ~3% |
| `Features::kDataFlow` | Track register-based data movement | ~10% |
| `Features::kScreenActivity` | Track VRAM writes | ~2% |

### 7.2 Emulator Hooks

```cpp
// === Z80 CPU ===
// M1 cycle
OnInstructionFetch(uint16_t pc) {
    if (kExecutionContext) LogExecutionContext(pc);
}

// Interrupt entry
OnInterruptEntry() {
    if (kInterruptTracking) StartInterruptEvent();
}

// RETI execution
OnRETI() {
    if (kInterruptTracking) EndInterruptEvent();
}

// === Memory ===
// Port access
OnPortAccess(uint16_t port, uint8_t value, bool isWrite) {
    if (kPortPatterns) LogPortAccess(port, value, isWrite);
}

// Memory read (for data flow)
OnMemoryRead(uint16_t addr, uint8_t value, Register destReg) {
    if (kDataFlow) TrackDataSource(destReg, addr, value);
}

// Memory write
OnMemoryWrite(uint16_t addr, uint8_t value, Register srcReg) {
    if (kDataFlow) TrackDataFlow(srcReg, addr);
    if (kScreenActivity && IsVRAM(addr)) TrackScreenWrite(addr, value);
}
```

### 7.3 Memory Budget

| Component | Storage |
|-----------|---------|
| ExecutionContextLog (sampled) | ~10 MB |
| InterruptLog | ~5 MB |
| PortActivityLog | ~20 MB |
| DataFlowTracker | ~1 MB (registers only) |
| ScreenActivityTracker | ~5 MB |
| **Total (all enabled)** | **~41 MB** |

---

## 8. API Design

### 8.1 Query Interface

```cpp
class DataCollectionExtensions {
public:
    // === Execution Context ===
    std::optional<ExecutionContext> GetContextAtTstate(uint64_t tstate) const;
    std::vector<ExecutionContext> GetContextsForFrame(uint64_t frame) const;
    
    // === Interrupt Tracking ===
    std::vector<InterruptEvent> GetInterruptsForFrame(uint64_t frame) const;
    InterruptStatistics GetInterruptStatistics() const;
    bool WasInInterruptAt(uint64_t tstate) const;
    
    // === Port Patterns ===
    PortPattern GetPortPattern(uint16_t port) const;
    std::vector<uint16_t> GetPeriodicPorts() const;
    std::vector<PortAccessEvent> GetPortAccessesInRange(uint64_t tstateStart, 
                                                         uint64_t tstateEnd) const;
    
    // === Data Flow ===
    std::vector<DataFlowPattern> GetDataFlowPatterns() const;
    std::optional<uint16_t> GetDataSource(uint16_t destAddr) const;
    
    // === Screen Activity ===
    ScreenActivitySummary GetScreenActivity() const;
    Rect GetDirtyRect() const;
    std::vector<uint16_t> GetScreenWriteCallers() const;
};
```

---

## 9. Implementation Roadmap

### Phase 1: Core Infrastructure
- [ ] ExecutionContext struct definition
- [ ] Feature flag integration
- [ ] Basic logging infrastructure

### Phase 2: Interrupt Tracking
- [ ] InterruptEvent struct
- [ ] Entry/exit hooks in Z80
- [ ] InterruptLog with query API

### Phase 3: Port Pattern Tracking
- [ ] PortAccessEvent logging
- [ ] Periodicity detection algorithm
- [ ] Integration with RoutineClassifier

### Phase 4: Screen Activity
- [ ] VRAM write hooks
- [ ] Dirty rectangle tracking
- [ ] Caller analysis

### Phase 5: Data Flow (Optional)
- [ ] Register shadow tracking
- [ ] Data movement correlation
- [ ] Integration with sprite detection

---

## 10. Test Specifications

### 10.1 Interrupt Tracking Tests

```cpp
TEST(InterruptTracking, CapturesInterruptDuration) {
    // Given: Interrupt handler that takes 100 T-states
    SetupInterruptHandler(100);
    
    // When: Interrupt fires
    TriggerInterrupt();
    ExecuteUntilRETI();
    
    // Then: Duration recorded
    auto events = interruptLog.GetEventsForFrame(currentFrame);
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].exitTstate - events[0].entryTstate, 100);
}

TEST(InterruptTracking, TracksNestedInterrupts) {
    // Given: NMI during maskable interrupt
    EnableNestedInterrupts();
    
    // When: Both fire
    TriggerMaskableInterrupt();
    TriggerNMI();
    
    // Then: Both recorded with correct nesting
    auto events = interruptLog.GetAll();
    EXPECT_EQ(events[0].interruptNestLevel, 1);
    EXPECT_EQ(events[1].interruptNestLevel, 2);
}
```

### 10.2 Port Pattern Tests

```cpp
TEST(PortPatterns, DetectsPeriodicAccess) {
    // Given: AY music player writing every 882 T-states
    for (int i = 0; i < 100; i++) {
        portLog.Log({.port = 0xFFFD, .tstate = i * 882});
    }
    
    // When: Pattern analysis runs
    auto pattern = portLog.GetPortPattern(0xFFFD);
    
    // Then: Periodic with ~882 T-state interval
    EXPECT_TRUE(pattern.isPeriodic);
    EXPECT_NEAR(pattern.detectedPeriod, 882, 10);
}

TEST(PortPatterns, DetectsNonPeriodicAccess) {
    // Given: Random keyboard polling
    std::mt19937 rng;
    for (int i = 0; i < 100; i++) {
        portLog.Log({.port = 0xFE, .tstate = rng() % 100000});
    }
    
    // When: Pattern analysis runs
    auto pattern = portLog.GetPortPattern(0xFE);
    
    // Then: Not periodic
    EXPECT_FALSE(pattern.isPeriodic);
}
```

### 10.3 Screen Activity Tests

```cpp
TEST(ScreenActivity, TracksDirtyRectangle) {
    // Given: Writes to screen corners
    screenTracker.OnVRAMWrite(0x4000, 0xFF, 0x8000);  // Top-left
    screenTracker.OnVRAMWrite(0x57FF, 0xFF, 0x8000);  // Bottom-right
    
    // Then: Dirty rect spans full screen
    auto rect = screenTracker.GetDirtyRect();
    EXPECT_EQ(rect.x1, 0);
    EXPECT_EQ(rect.y1, 0);
    EXPECT_EQ(rect.x2, 255);
    EXPECT_EQ(rect.y2, 191);
}
```

---

## 11. References

### Parent Documents
- [Analyzer Architecture](./analyzer-architecture.md)

### Related Documents
- [Interrupt Analyzer](./interrupt-analyzer.md)
- [Routine Classifiers](./routine-classifiers.md)

### Source Files (to be modified)
- `core/src/emulator/cpu/z80.h` - Add interrupt context flags
- `core/src/emulator/cpu/z80.cpp` - Add hooks
- `core/src/emulator/memory/memory.h` - Add port logging hooks
- `core/src/emulator/memory/memory.cpp` - Implement logging
