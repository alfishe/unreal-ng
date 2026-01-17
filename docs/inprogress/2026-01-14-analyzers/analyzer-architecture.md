# Analyzer Architecture: High-Level Analysis Subsystem

**Purpose:** Architectural design for the analyzer layer that transforms raw execution data into meaningful program understanding.

**Status:** Design Draft - Not Yet Implemented

**Dependencies:**
- [Memory Counters Architecture](../../analysis/capture/memorycounters.md) - Low-level data collection (implemented)
- [Beam-to-Execution Correlation](./beam-to-execution-correlation.md) - T-state/beam position mapping (design)
- [Analysis Use Cases](./analysis-use-cases.md) - RE workflow requirements driving this design

---

## 1. Overview

The Analyzer subsystem sits atop the data collection layer (`MemoryAccessTracker` + `CallTraceBuffer`) and provides high-level program analysis capabilities:

```
+=====================================================================+
|                         ANALYZER LAYER                              |
|  (This Document)                                                    |
+---------------------------------------------------------------------+
|  +-----------------+  +-----------------+  +---------------------+  |
|  | Block           |  | Interrupt       |  | Routine             |  |
|  | Segmenter       |  | Analyzer        |  | Classifiers         |  |
|  | (CODE/DATA/VAR) |  | (Handler detect)|  | (Screen/Music/etc)  |  |
|  +-----------------+  +-----------------+  +---------------------+  |
|         |                    |                      |               |
|         +--------------------+----------------------+               |
|                              |                                      |
|                    +---------v----------+                           |
|                    | AnalysisCoordinator |                          |
|                    | (Query interface)   |                          |
|                    +--------------------+                           |
+=====================================================================+
                               |
                               | consumes
                               v
+=====================================================================+
|                     DATA COLLECTION LAYER                           |
|  (Already Implemented - see memorycounters.md)                      |
+---------------------------------------------------------------------+
|  +---------------------------+  +-------------------------------+   |
|  | MemoryAccessTracker       |  | CallTraceBuffer               |   |
|  | - R/W/X counters          |  | - Control flow events         |   |
|  | - Monitored regions/ports |  | - Hot/cold compression        |   |
|  | - Segmented tracking      |  | - Loop detection              |   |
|  +---------------------------+  +-------------------------------+   |
+=====================================================================+
                               |
                               | hooks into
                               v
+=====================================================================+
|                      EMULATION LAYER                                |
+---------------------------------------------------------------------+
|  +----------+  +----------+  +----------+  +---------------------+  |
|  | Z80 CPU  |  | Memory   |  | Ports    |  | Platform (128K/48K) |  |
|  +----------+  +----------+  +----------+  +---------------------+  |
+=====================================================================+
```

### 1.1 Design Goals

| Goal | Approach |
|------|----------|
| **Non-invasive** | All analysis is read-only; no emulation state modification |
| **Incremental** | Analysis can update as new data arrives |
| **Lazy** | Heavy computation only on demand |
| **Composable** | Analyzers can consume other analyzers' output |
| **Configurable** | Analysis depth/scope controlled per-session |

### 1.2 Gap Analysis: What We Have vs What We Need

| Analysis Goal | Current Data | Missing Data | Feasibility |
|--------------|--------------|--------------|-------------|
| Block Segmentation (CODE/DATA) | R/W/X counters | - | **90%** achievable |
| Self-Modifying Code Detection | W+X on same address | Instruction categorization | **80%** achievable |
| Function Boundary Detection | CALL/RET trace | - | **95%** achievable |
| Loop Detection | CallTrace loop_count | - | **90%** achievable |
| Interrupt Handler Detection | - | Interrupt context flag | **50%** - needs extension |
| Interrupt Activity Classification | Port writes during interrupt | T-state/scanline timing | **30%** - needs extension |
| Screen Routine Detection | VRAM region writes | Data flow tracking, beam correlation | **40%** - needs extension |
| Music Player Detection | Port 0xFFFD/0xBFFD writes | Temporal periodicity | **60%** - needs extension |
| **Main Loop Detection** | CallTrace loop patterns | **Frame T-state timing** | **70%** - needs beam correlation |
| **Racing-Beam Detection** | VRAM writes | **Beam position at write time** | **20%** - needs beam correlation |

---

## 2. Core Components

### 2.1 AnalysisCoordinator

Central orchestrator that:
1. Receives triggers (frame end, user request, breakpoint hit)
2. Coordinates analyzer execution order
3. Caches analysis results
4. Provides unified query interface

```cpp
class AnalysisCoordinator {
public:
    AnalysisCoordinator(MemoryAccessTracker* tracker);
    
    // === Lifecycle ===
    void OnFrameEnd(uint64_t frame_number);
    void OnEmulationPause();
    void InvalidateCache();
    
    // === Query Interface ===
    BlockClassification GetBlockType(uint16_t address) const;
    std::vector<FunctionBoundary> GetFunctions() const;
    std::vector<LoopInfo> GetDetectedLoops() const;
    std::optional<InterruptInfo> GetInterruptHandler() const;
    std::vector<RoutineClassification> GetClassifiedRoutines() const;
    
    // === Configuration ===
    void SetAnalysisDepth(AnalysisDepth depth);  // Quick, Normal, Deep
    void EnableAnalyzer(AnalyzerType type, bool enable);
    
private:
    MemoryAccessTracker* _tracker;
    
    // Analyzers (lazy-initialized)
    std::unique_ptr<BlockSegmenter> _blockSegmenter;
    std::unique_ptr<FunctionDetector> _functionDetector;
    std::unique_ptr<LoopDetector> _loopDetector;
    std::unique_ptr<InterruptAnalyzer> _interruptAnalyzer;
    std::unique_ptr<RoutineClassifier> _routineClassifier;
    
    // Cache
    mutable std::optional<AnalysisSnapshot> _cachedAnalysis;
    uint64_t _cacheValidFrame = 0;
};
```

### 2.2 Analyzer Interface

All analyzers implement a common interface:

```cpp
class IAnalyzer {
public:
    virtual ~IAnalyzer() = default;
    
    // Returns true if analysis produced new results
    virtual bool Analyze(const AnalysisContext& ctx) = 0;
    
    // Reset internal state
    virtual void Reset() = 0;
    
    // Name for logging/debugging
    virtual std::string_view GetName() const = 0;
    
    // Dependencies (other analyzers that must run first)
    virtual std::vector<AnalyzerType> GetDependencies() const { return {}; }
};
```

### 2.3 AnalysisContext

Shared context passed to all analyzers:

```cpp
struct AnalysisContext {
    // === Data Sources ===
    const MemoryAccessTracker* tracker;
    const CallTraceBuffer* callTrace;
    const Memory* memory;
    
    // === Extended Data (when available) ===
    const ExecutionContextLog* execContextLog;  // T-state, interrupt state
    const PortActivityLog* portActivityLog;     // Temporal port patterns
    
    // === Analysis State ===
    uint64_t currentFrame;
    AnalysisDepth depth;
    
    // === Cross-Analyzer Communication ===
    const BlockSegmenter::Result* blockSegmentation;
    const FunctionDetector::Result* functionBoundaries;
};
```

---

## 3. Analyzer Specifications

### 3.1 BlockSegmenter

**Purpose:** Classify each memory address as CODE, DATA, VARIABLE, or SMC (self-modifying code).

**Input:** R/W/X counters from `MemoryAccessTracker`

**Algorithm:**
```
For each address in 0x0000..0xFFFF:
    R = readCount[addr]
    W = writeCount[addr]
    X = executeCount[addr]
    
    if X > 0 and W > 0:
        classification = SMC (self-modifying code)
    else if X > 0:
        classification = CODE
    else if W > 0:
        classification = VARIABLE (written at runtime)
    else if R > 0:
        classification = DATA (read-only data)
    else:
        classification = UNKNOWN
```

**Output:**
```cpp
struct BlockSegmentation {
    enum class Type : uint8_t { UNKNOWN, CODE, DATA, VARIABLE, SMC };
    
    std::array<Type, 65536> classification;
    
    // Aggregated regions for UI display
    struct Region {
        uint16_t start;
        uint16_t length;
        Type type;
    };
    std::vector<Region> regions;
};
```

**Detailed Design:** [block-segmentation.md](./block-segmentation.md)

---

### 3.2 FunctionDetector

**Purpose:** Identify function boundaries from call/return patterns.

**Input:** `CallTraceBuffer` events (CALL, RST, RET, RETI)

**Algorithm:**
1. Build call graph from CALL events
2. Identify function entry points (CALL targets)
3. Track matching RET addresses
4. Correlate with CODE blocks from BlockSegmenter

**Output:**
```cpp
struct FunctionBoundary {
    uint16_t entryPoint;
    std::optional<uint16_t> exitPoint;  // If single exit detected
    uint32_t callCount;                  // Times this function was called
    std::vector<uint16_t> callers;       // PCs of CALL instructions
    std::string autoName;                // "sub_8234" or detected name
};
```

---

### 3.3 LoopDetector

**Purpose:** Identify loops from compressed CallTrace events.

**Input:** `CallTraceBuffer` events with `loop_count > 1`

**Algorithm:**
1. Scan for events with high `loop_count` (tight loops)
2. Identify JR/DJNZ self-loops (loop_count at same PC)
3. Identify multi-instruction loops (repeated PC patterns)
4. Correlate with function boundaries

**Output:**
```cpp
struct LoopInfo {
    uint16_t loopHead;           // Address of loop entry/back-edge target
    uint16_t loopTail;           // Address of backward jump
    uint32_t iterationCount;     // Total observed iterations
    LoopType type;               // Tight, Multi-instruction, Nested
    std::optional<uint16_t> containingFunction;
};

enum class LoopType { Tight, MultiInstruction, Nested };
```

---

### 3.4 InterruptAnalyzer

**Purpose:** Detect interrupt handler and classify activity during interrupts.

**Input:** Extended execution context (requires new data collection)

**Current Limitation:** Cannot reliably detect interrupt boundaries without:
- Interrupt context flag (`in_interrupt`)
- T-state timing for periodicity detection

**Proposed Extension:** See [data-collection-extensions.md](./data-collection-extensions.md)

**Output (when implemented):**
```cpp
struct InterruptInfo {
    uint16_t handlerAddress;         // Detected IM1/IM2 handler entry
    uint32_t avgTstatesInHandler;    // Average time spent in handler
    std::vector<InterruptActivity> activities;
};

struct InterruptActivity {
    ActivityType type;  // KeyboardPoll, MusicPlay, TimerUpdate, etc.
    uint16_t codeStart;
    uint16_t codeEnd;
    float confidence;  // 0.0-1.0
};
```

**Detailed Design:** [interrupt-analyzer.md](./interrupt-analyzer.md)

---

### 3.5 RoutineClassifier

**Purpose:** Identify specific routine types (screen, music, sprite, etc.).

**Input:** Block segmentation, function boundaries, port activity, memory regions

**Classification Targets:**

| Routine Type | Detection Heuristics |
|-------------|---------------------|
| Screen Clear | VRAM (0x4000-0x5AFF) writes in tight loop, often LD (HL),A pattern |
| Screen Scroll | VRAM reads + writes in adjacent addresses |
| Sprite Blit | Non-contiguous VRAM writes, often with address calculation |
| AY Music | Periodic writes to ports 0xFFFD/0xBFFD |
| Beeper Music | Tight loop with port 0xFE writes, OUT timing patterns |
| Keyboard Poll | Port 0xFE reads with varying high byte (row selection) |
| Tape Load | Port 0xFE reads in tight timing loop |

**Output:**
```cpp
struct RoutineClassification {
    uint16_t startAddress;
    uint16_t endAddress;
    RoutineType type;
    float confidence;
    std::string evidence;  // Human-readable explanation
};

enum class RoutineType {
    ScreenClear, ScreenScroll, SpriteBlit,
    AYMusic, BeeperMusic, COVOX, GeneralSound,
    KeyboardPoll, TapeLoad, Delay,
    Unknown
};
```

**Detailed Design:** [routine-classifiers.md](./routine-classifiers.md)

---

## 4. Data Flow Architecture

```
+-------------------------------------------------------------------+
|                         Frame N End                                |
+-------------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------------+
| AnalysisCoordinator::OnFrameEnd(N)                                |
+-------------------------------------------------------------------+
                              |
             +----------------+----------------+
             |                                 |
             v                                 v
+------------------------+        +------------------------+
| If cache valid: return |        | If cache stale:        |
+------------------------+        | Run analysis pipeline  |
                                  +------------------------+
                                             |
                    +------------------------+------------------------+
                    |                        |                        |
                    v                        v                        v
          +------------------+    +------------------+    +------------------+
          | BlockSegmenter   |    | FunctionDetector |    | LoopDetector     |
          | (no deps)        |    | (needs blocks)   |    | (no deps)        |
          +------------------+    +------------------+    +------------------+
                    |                        |                        |
                    v                        v                        v
          +------------------------------------------------------------------------+
          |                     InterruptAnalyzer                                   |
          |                     (needs blocks, functions)                           |
          +------------------------------------------------------------------------+
                                             |
                                             v
          +------------------------------------------------------------------------+
          |                     RoutineClassifier                                   |
          |                     (needs all above)                                   |
          +------------------------------------------------------------------------+
                                             |
                                             v
          +------------------------------------------------------------------------+
          |                     Cache Results                                       |
          |                     _cacheValidFrame = N                                |
          +------------------------------------------------------------------------+
```

### 4.1 Dependency Graph

```
BlockSegmenter ─────────┬──────────────> InterruptAnalyzer ──> RoutineClassifier
                        |                        ^                     ^
FunctionDetector ───────┴────────────────────────┘                     |
                                                                       |
LoopDetector ──────────────────────────────────────────────────────────┘
```

### 4.2 Incremental Analysis

For real-time debugging, some analyzers support incremental updates:

| Analyzer | Incremental Support |
|----------|---------------------|
| BlockSegmenter | Yes - counter deltas |
| FunctionDetector | Partial - new CALL events |
| LoopDetector | Yes - new loop_count changes |
| InterruptAnalyzer | No - requires full rescan |
| RoutineClassifier | No - requires full rescan |

---

## 5. API Design

### 5.1 Query Interface

```cpp
// === Block Classification ===
BlockClassification coord.GetBlockType(0x8000);
// Returns: { type: CODE, confidence: 1.0 }

std::vector<BlockRegion> coord.GetBlockRegions(BlockType::CODE);
// Returns: [{ start: 0x8000, length: 0x100 }, ...]

// === Functions ===
std::optional<FunctionBoundary> coord.GetFunctionAt(0x8234);
std::vector<FunctionBoundary> coord.GetAllFunctions();
std::vector<uint16_t> coord.GetCallersOf(0x9A00);

// === Loops ===
std::optional<LoopInfo> coord.GetLoopContaining(0x8050);
std::vector<LoopInfo> coord.GetHotLoops(min_iterations: 1000);

// === Interrupt ===
std::optional<InterruptInfo> coord.GetInterruptHandler();
bool coord.IsInInterruptContext(uint16_t address);

// === Routines ===
std::vector<RoutineClassification> coord.GetRoutinesOfType(RoutineType::AYMusic);
RoutineClassification coord.ClassifyRoutine(0x8500, 0x8600);
```

### 5.2 Event Callbacks

```cpp
// Register for analysis updates
coord.OnAnalysisComplete([](const AnalysisSnapshot& snapshot) {
    // Update UI with new analysis results
});

coord.OnBlockTypeChanged([](uint16_t addr, BlockType oldType, BlockType newType) {
    // Address classification changed (e.g., now SMC)
});
```

---

## 6. Configuration

### 6.1 Analysis Depth

```cpp
enum class AnalysisDepth {
    Quick,   // Block segmentation only, ~1ms
    Normal,  // + Functions + Loops, ~10ms
    Deep     // + Interrupt + Routines, ~100ms
};
```

### 6.2 Feature Flags

| Flag | Effect |
|------|--------|
| `Features::kAnalysis` | Master gate for analysis subsystem |
| `Features::kAnalysisAutoRun` | Run analysis at each frame end |
| `Features::kAnalysisRoutines` | Enable routine classification (expensive) |

### 6.3 Thresholds

```cpp
struct AnalysisConfig {
    // Block Segmentation
    uint32_t minExecuteCountForCode = 1;
    uint32_t minWriteCountForVariable = 1;
    
    // Function Detection
    uint32_t minCallsForFunction = 1;
    
    // Loop Detection
    uint32_t minIterationsForLoop = 10;
    uint32_t hotLoopThreshold = 1000;
    
    // Routine Classification
    float minConfidenceThreshold = 0.7f;
};
```

---

## 7. Integration Points

### 7.1 Debugger UI Integration

```cpp
// Disassembler annotation
for (uint16_t addr = start; addr < end; addr++) {
    auto blockType = coord.GetBlockType(addr);
    auto funcInfo = coord.GetFunctionAt(addr);
    auto loopInfo = coord.GetLoopContaining(addr);
    
    // Render with block type coloring
    // Show function entry/exit markers
    // Show loop back-edge indicators
}
```

### 7.2 Export Format

```yaml
analysis:
  timestamp: "2026-01-14T17:00:00Z"
  frame: 12345
  
  blocks:
    code_regions:
      - { start: 0x8000, length: 0x1000 }
      - { start: 0x9000, length: 0x0500 }
    data_regions:
      - { start: 0x5B00, length: 0x0100 }
    smc_addresses: [0x8234, 0x8235]
  
  functions:
    - entry: 0x8000
      name: "main_loop"
      call_count: 1
      callers: []
    - entry: 0x9A00
      name: "sub_9A00"
      call_count: 5000
      callers: [0x8050, 0x8100]
  
  loops:
    - head: 0x8050
      tail: 0x8060
      iterations: 50000
      type: "tight"
  
  routines:
    - start: 0xA000
      end: 0xA100
      type: "ay_music"
      confidence: 0.92
```

---

## 8. Implementation Roadmap

### Phase 1: Core Infrastructure (Week 1)
- [ ] AnalysisCoordinator skeleton
- [ ] IAnalyzer interface
- [ ] AnalysisContext definition
- [ ] Basic caching mechanism

### Phase 2: Block Segmentation (Week 1-2)
- [ ] BlockSegmenter implementation
- [ ] Region aggregation
- [ ] UI integration for disassembler coloring

### Phase 3: Function & Loop Detection (Week 2-3)
- [ ] FunctionDetector from CallTrace
- [ ] LoopDetector from loop_count
- [ ] Disassembler annotation

### Phase 4: Data Collection Extensions (Week 3-4)
- [ ] ExecutionContext struct
- [ ] Interrupt context tracking
- [ ] T-state/scanline exposure

### Phase 5: Advanced Analyzers (Week 4-5)
- [ ] InterruptAnalyzer
- [ ] RoutineClassifier (screen, music)
- [ ] Confidence scoring

### Phase 6: Polish (Week 5-6)
- [ ] Export/import
- [ ] Incremental analysis optimization
- [ ] Performance profiling

---

## 9. References

### Source Files
- [memoryaccesstracker.h](../../../core/src/emulator/memory/memoryaccesstracker.h)
- [memoryaccesstracker.cpp](../../../core/src/emulator/memory/memoryaccesstracker.cpp)
- [calltrace.h](../../../core/src/emulator/memory/calltrace.h)
- [calltrace.cpp](../../../core/src/emulator/memory/calltrace.cpp)

### Design Documents
- [Memory Counters Architecture](../../analysis/capture/memorycounters.md)
- [Block Segmentation](./block-segmentation.md) (this folder)
- [Interrupt Analyzer](./interrupt-analyzer.md) (this folder)
- [Routine Classifiers](./routine-classifiers.md) (this folder)
- [Data Collection Extensions](./data-collection-extensions.md) (this folder)
