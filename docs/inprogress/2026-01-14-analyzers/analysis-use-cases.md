# Analysis Use Cases: Retro-Computing Reverse Engineering Workflows

**Purpose:** Comprehensive catalog of use cases driving the analyzer subsystem design.

**Status:** Requirements Document

**Parent:** [Analyzer Architecture](./analyzer-architecture.md)

---

## 1. Overview

This document catalogs the real-world reverse engineering workflows that the analyzer subsystem must support. Each use case defines:

- **What** the user wants to accomplish
- **Why** it matters for understanding retro software
- **Data requirements** — what must be captured/stored
- **Query patterns** — how the data is accessed
- **Cross-analyzer dependencies** — which components must cooperate

---

## 2. Use Case Categories

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Analysis Use Cases                              │
├─────────────────────────────────────────────────────────────────────────┤
│  Navigation       │ Cross-References, Symbol Lookup, Call Graph         │
├───────────────────┼─────────────────────────────────────────────────────┤
│  Temporal         │ Sequence Reconstruction, Timeline, Lifecycle        │
├───────────────────┼─────────────────────────────────────────────────────┤
│  Behavioral       │ Variable Profiling, Routine Classification          │
├───────────────────┼─────────────────────────────────────────────────────┤
│  Structural       │ Block Segmentation, Module Detection, Boundaries    │
├───────────────────┼─────────────────────────────────────────────────────┤
│  SMC Analysis     │ Lifecycle, Pattern Detection, Patcher ID            │
├───────────────────┼─────────────────────────────────────────────────────┤
│  Data Flow        │ Provenance, Block Moves, Value Tracking             │
├───────────────────┼─────────────────────────────────────────────────────┤
│  Comparative      │ Frame Diff, State Diff, Before/After                │
├───────────────────┼─────────────────────────────────────────────────────┤
│  Timing           │ Beam Correlation, Interrupt Timing, Raster Effects  │
└───────────────────┴─────────────────────────────────────────────────────┘
```

---

## 2.1 Quick Reference: All Use Cases

### Navigation (5 use cases)

| ID | Name | User Question | Primary Analyzer |
|----|------|---------------|------------------|
| UC-NAV-01 | Code→Data Xrefs | "What data does this routine access?" | MemoryAccessTracker |
| UC-NAV-02 | Data→Code Xrefs | "What routines read/write this variable?" | MemoryAccessTracker |
| UC-NAV-03 | Call Graph | "Who calls this? What does it call?" | CallTraceBuffer |
| UC-NAV-04 | Interrupt Entry Points | "What code runs during interrupts?" | InterruptAnalyzer |
| UC-NAV-05 | Address Context | "What is this address? Part of which routine?" | BlockSegmenter |

### Temporal (5 use cases)

| ID | Name | User Question | Primary Analyzer |
|----|------|---------------|------------------|
| UC-TMP-01 | Call Sequence | "What's the call sequence leading to this state?" | CallTraceBuffer |
| UC-TMP-02 | First Execution Order | "In what order were routines first called?" | CallTraceBuffer |
| UC-TMP-03 | Boot Sequence | "What happens during startup before game runs?" | CallTraceBuffer |
| UC-TMP-04 | Routine Activity Span | "When is this routine active? Only during load?" | CallTraceBuffer |
| UC-TMP-05 | Memory Region Lifecycle | "When does this memory region become active?" | MemoryAccessTracker |

### Behavioral (8 use cases)

| ID | Name | User Question | Primary Analyzer |
|----|------|---------------|------------------|
| UC-BEH-01 | Access Frequency | "How often is this variable written? Per-frame?" | MemoryAccessTracker |
| UC-BEH-02 | Scope Inference | "Is this a global or local variable?" | MemoryAccessTracker |
| UC-BEH-03 | Reader/Writer Sets | "Which functions read this? Which write?" | MemoryAccessTracker |
| UC-BEH-04 | Variable Type Inference | "Is this a counter, flag, pointer, or buffer?" | VariableProfiler |
| UC-BEH-05 | Screen Routine Detection | "Which routines write to screen memory?" | RoutineClassifier |
| UC-BEH-06 | Sound Routine Detection | "Which routines produce sound?" | RoutineClassifier |
| UC-BEH-07 | Input Handler Detection | "Which routines read keyboard/joystick?" | RoutineClassifier |
| UC-BEH-08 | ISR Classification | "What does the interrupt handler do?" | InterruptAnalyzer |

### Structural (6 use cases)

| ID | Name | User Question | Primary Analyzer |
|----|------|---------------|------------------|
| UC-STR-01 | Memory Map | "Show me the memory layout — code, data, variables?" | BlockSegmenter |
| UC-STR-02 | Disassembly Guidance | "Should I disassemble this or show as data?" | BlockSegmenter |
| UC-STR-03 | SMC Region ID | "Where is self-modifying code?" | BlockSegmenter |
| UC-STR-04 | Function Boundaries | "Where does this function start and end?" | CallTraceBuffer |
| UC-STR-05 | Code+Data Grouping | "What data belongs to this routine?" | Xref Analysis |
| UC-STR-06 | Module Clustering | "Group related routines and data together" | CallGraph + Xrefs |

### SMC Analysis (5 use cases)

| ID | Name | User Question | Primary Analyzer |
|----|------|---------------|------------------|
| UC-SMC-01 | Modification Timeline | "When did this code get modified? How many times?" | SMCAnalyzer |
| UC-SMC-02 | Modification Cycles | "How many modification cycles did this SMC go through?" | SMCAnalyzer |
| UC-SMC-03 | Patcher Identification | "What code performs the modification?" | MemoryAccessTracker |
| UC-SMC-04 | SMC Pattern Detection | "What kind of SMC is this? (decompressor, patch, etc.)" | SMCAnalyzer |
| UC-SMC-05 | Correlated Activity | "What else happens when SMC is active?" | Temporal Correlation |

### Data Flow (3 use cases)

| ID | Name | User Question | Primary Analyzer |
|----|------|---------------|------------------|
| UC-DFL-01 | Value Origin | "Where did this value come from?" | DataFlowAnalyzer |
| UC-DFL-02 | Block Move Tracking | "Where was this data copied from? (LDIR)" | Block Move Detector |
| UC-DFL-03 | Decompression Mapping | "What got decompressed to where?" | SMCAnalyzer |

### Comparative (3 use cases)

| ID | Name | User Question | Primary Analyzer |
|----|------|---------------|------------------|
| UC-CMP-01 | Frame-to-Frame Diff | "What changed between frame N and M?" | SnapshotDiff |
| UC-CMP-02 | State Comparison | "What's different between game state A and B?" | SnapshotDiff |
| UC-CMP-03 | Before/After SMC | "What did the code look like before modification?" | Value History |

### Timing (8 use cases)

| ID | Name | User Question | Primary Analyzer |
|----|------|---------------|------------------|
| UC-TIM-01 | Execution→Beam Mapping | "Which scan line was being drawn when this executed?" | BeamCorrelator |
| UC-TIM-02 | Raster Effect Analysis | "Is this routine timing-critical for display?" | PortTracker |
| UC-TIM-03 | Race Condition Detection | "Is this screen write racing the beam?" | BeamCorrelator |
| UC-TIM-04 | Interrupt Jitter | "How consistent is the interrupt timing?" | InterruptAnalyzer |
| UC-TIM-05 | ISR Duration | "How long does the interrupt handler take?" | InterruptAnalyzer |
| UC-TIM-06 | Function Frame Cost | "What % of frame does this function consume?" | FrameProfiler |
| UC-TIM-07 | CPU Budget Analysis | "How much free CPU time is left in frame?" | FrameProfiler |
| UC-TIM-08 | Multicolor Effect Detection | "Is this code synchronizing attribute writes with the beam?" | BeamCorrelator |

---

### Summary Statistics

| Category | Count | Priority | Implementation Phase |
|----------|-------|----------|---------------------|
| Navigation | 5 | HIGH | Phase 1-2 |
| Temporal | 5 | MEDIUM | Phase 2 |
| Behavioral | 8 | HIGH | Phase 2 |
| Structural | 6 | HIGH | Phase 1 |
| SMC Analysis | 5 | MEDIUM | Phase 3 |
| Data Flow | 3 | LOW | Phase 5 |
| Comparative | 3 | LOW | Phase 5 |
| Timing | 7 | MEDIUM | Phase 4 |
| **Total** | **42** | | |

### Aggregated Use Cases Table

| ID | Category | Name | User Question | Primary Analyzer |
|----|----------|------|---------------|------------------|
| UC-NAV-01 | Navigation | Code→Data Xrefs | "What data does this routine access?" | MemoryAccessTracker |
| UC-NAV-02 | Navigation | Data→Code Xrefs | "What routines read/write this variable?" | MemoryAccessTracker |
| UC-NAV-03 | Navigation | Call Graph | "Who calls this? What does it call?" | CallTraceBuffer |
| UC-NAV-04 | Navigation | Interrupt Entry Points | "What code runs during interrupts?" | InterruptAnalyzer |
| UC-NAV-05 | Navigation | Address Context | "What is this address? Part of which routine?" | BlockSegmenter |
| UC-TMP-01 | Temporal | Call Sequence | "What's the call sequence leading to this state?" | CallTraceBuffer |
| UC-TMP-02 | Temporal | First Execution Order | "In what order were routines first called?" | CallTraceBuffer |
| UC-TMP-03 | Temporal | Boot Sequence | "What happens during startup before game runs?" | CallTraceBuffer |
| UC-TMP-04 | Temporal | Routine Activity Span | "When is this routine active? Only during load?" | CallTraceBuffer |
| UC-TMP-05 | Temporal | Memory Region Lifecycle | "When does this memory region become active?" | MemoryAccessTracker |
| UC-BEH-01 | Behavioral | Access Frequency | "How often is this variable written? Per-frame?" | MemoryAccessTracker |
| UC-BEH-02 | Behavioral | Scope Inference | "Is this a global or local variable?" | MemoryAccessTracker |
| UC-BEH-03 | Behavioral | Reader/Writer Sets | "Which functions read this? Which write?" | MemoryAccessTracker |
| UC-BEH-04 | Behavioral | Variable Type Inference | "Is this a counter, flag, pointer, or buffer?" | VariableProfiler |
| UC-BEH-05 | Behavioral | Screen Routine Detection | "Which routines write to screen memory?" | RoutineClassifier |
| UC-BEH-06 | Behavioral | Sound Routine Detection | "Which routines produce sound?" | RoutineClassifier |
| UC-BEH-07 | Behavioral | Input Handler Detection | "Which routines read keyboard/joystick?" | RoutineClassifier |
| UC-BEH-08 | Behavioral | ISR Classification | "What does the interrupt handler do?" | InterruptAnalyzer |
| UC-STR-01 | Structural | Memory Map | "Show me the memory layout — code, data, variables?" | BlockSegmenter |
| UC-STR-02 | Structural | Disassembly Guidance | "Should I disassemble this or show as data?" | BlockSegmenter |
| UC-STR-03 | Structural | SMC Region ID | "Where is self-modifying code?" | BlockSegmenter |
| UC-STR-04 | Structural | Function Boundaries | "Where does this function start and end?" | CallTraceBuffer |
| UC-STR-05 | Structural | Code+Data Grouping | "What data belongs to this routine?" | Xref Analysis |
| UC-STR-06 | Structural | Module Clustering | "Group related routines and data together" | CallGraph + Xrefs |
| UC-SMC-01 | SMC Analysis | Modification Timeline | "When did this code get modified? How many times?" | SMCAnalyzer |
| UC-SMC-02 | SMC Analysis | Modification Cycles | "How many modification cycles did this SMC go through?" | SMCAnalyzer |
| UC-SMC-03 | SMC Analysis | Patcher Identification | "What code performs the modification?" | MemoryAccessTracker |
| UC-SMC-04 | SMC Analysis | SMC Pattern Detection | "What kind of SMC is this? (decompressor, patch, etc.)" | SMCAnalyzer |
| UC-SMC-05 | SMC Analysis | Correlated Activity | "What else happens when SMC is active?" | Temporal Correlation |
| UC-DFL-01 | Data Flow | Value Origin | "Where did this value come from?" | DataFlowAnalyzer |
| UC-DFL-02 | Data Flow | Block Move Tracking | "Where was this data copied from? (LDIR)" | Block Move Detector |
| UC-DFL-03 | Data Flow | Decompression Mapping | "What got decompressed to where?" | SMCAnalyzer |
| UC-CMP-01 | Comparative | Frame-to-Frame Diff | "What changed between frame N and M?" | SnapshotDiff |
| UC-CMP-02 | Comparative | State Comparison | "What's different between game state A and B?" | SnapshotDiff |
| UC-CMP-03 | Comparative | Before/After SMC | "What did the code look like before modification?" | Value History |
| UC-TIM-01 | Timing | Execution→Beam Mapping | "Which scan line was being drawn when this executed?" | BeamCorrelator |
| UC-TIM-02 | Timing | Raster Effect Analysis | "Is this routine timing-critical for display?" | PortTracker |
| UC-TIM-03 | Timing | Race Condition Detection | "Is this screen write racing the beam?" | BeamCorrelator |
| UC-TIM-04 | Timing | Interrupt Jitter | "How consistent is the interrupt timing?" | InterruptAnalyzer |
| UC-TIM-05 | Timing | ISR Duration | "How long does the interrupt handler take?" | InterruptAnalyzer |
| UC-TIM-06 | Timing | Function Frame Cost | "What % of frame does this function consume?" | FrameProfiler |
| UC-TIM-07 | Timing | CPU Budget Analysis | "How much free CPU time is left in frame?" | FrameProfiler |
| UC-TIM-08 | Timing | Multicolor Effect Detection | "Is this code synchronizing attribute writes with the beam?" | BeamCorrelator |

---

## 3. Navigation Use Cases

### 3.1 Cross-References (Xrefs)

**Goal:** Bidirectional navigation between code and data.

#### UC-NAV-01: Code → Data References {#uc-nav-01}

| Aspect | Detail |
|--------|--------|
| **User Question** | "What data does this routine access?" |
| **Example** | Routine at 0x8000 reads from 0x9000-0x90FF (sprite table) |
| **Data Required** | Per-address: set of {reader_PC, access_type} |
| **Query** | `GetDataAccessedBy(routine_start, routine_end)` → list of data addresses |
| **Source** | MemoryAccessTracker.callerAddresses |

#### UC-NAV-02: Data → Code References {#uc-nav-02}

| Aspect | Detail |
|--------|--------|
| **User Question** | "What routines read/write this variable?" |
| **Example** | Variable at 0x5C00 is read by 3 routines, written by 1 |
| **Data Required** | Per-address: set of {accessor_PC, access_type} |
| **Query** | `GetAccessorsOf(data_address)` → list of {PC, read/write} |
| **Source** | MemoryAccessTracker.callerAddresses |

#### UC-NAV-03: Code → Code References (Call Graph) {#uc-nav-03}

| Aspect | Detail |
|--------|--------|
| **User Question** | "Who calls this routine? What does it call?" |
| **Example** | Main loop calls DrawSprite, HandleInput, UpdateSound |
| **Data Required** | CallTraceBuffer entries with caller/callee |
| **Query** | `GetCallers(routine_addr)`, `GetCallees(routine_addr)` |
| **Source** | CallTraceBuffer |

#### UC-NAV-04: Interrupt Entry Points {#uc-nav-04}

| Aspect | Detail |
|--------|--------|
| **User Question** | "What code runs during interrupts?" |
| **Example** | IM2 vector table at 0xFE00 points to handler at 0x8100 |
| **Data Required** | Interrupt events with handler addresses |
| **Query** | `GetInterruptHandlers()` → list of ISR addresses |
| **Source** | InterruptAnalyzer |

---

### 3.2 Symbol and Label Resolution

#### UC-NAV-05: Address → Context {#uc-nav-05}

| Aspect | Detail |
|--------|--------|
| **User Question** | "What is this address? Part of which routine/data block?" |
| **Example** | 0x8042 is offset +0x42 into routine "DrawSprite" |
| **Data Required** | Block regions with boundaries, user-defined labels |
| **Query** | `GetContext(address)` → {block_type, block_start, label, offset} |
| **Source** | BlockSegmenter + user annotations |

---

## 4. Temporal Use Cases

### 4.1 Sequence Reconstruction

#### UC-TMP-01: Call Sequence to State {#uc-tmp-01}

| Aspect | Detail |
|--------|--------|
| **User Question** | "What's the call sequence leading to this state?" |
| **Example** | Crash at 0x8500: called from MainLoop → GameTick → CollisionCheck |
| **Data Required** | CallTraceBuffer with temporal ordering |
| **Query** | `GetCallStackAt(tstate)` → ordered list of active calls |
| **Source** | CallTraceBuffer |

#### UC-TMP-02: First Execution Order {#uc-tmp-02}

| Aspect | Detail |
|--------|--------|
| **User Question** | "In what order were routines first called?" |
| **Example** | Boot sequence: ROM init → BASIC → loader → game init → main loop |
| **Data Required** | First-execution timestamp per routine |
| **Query** | `GetExecutionOrder()` → routines sorted by first_seen_tstate |
| **Source** | CallTraceBuffer (first occurrence of each target) |

#### UC-TMP-03: Boot Sequence Mapping {#uc-tmp-03}

| Aspect | Detail |
|--------|--------|
| **User Question** | "What happens during startup before the game runs?" |
| **Example** | Tape loader → decompressor → relocator → jump to main |
| **Data Required** | Temporal flow of execution + memory writes |
| **Query** | `GetBootSequence(end_tstate)` → ordered events during boot |
| **Source** | CallTraceBuffer + MemoryAccessTracker |

---

### 4.2 Lifecycle Analysis

#### UC-TMP-04: Routine Activity Span {#uc-tmp-04}

| Aspect | Detail |
|--------|--------|
| **User Question** | "When is this routine active? Only during load? Every frame?" |
| **Example** | Decompressor runs T=0 to T=500000, then never again |
| **Data Required** | First/last execution timestamp per routine |
| **Query** | `GetActivityWindow(routine_addr)` → {first_tstate, last_tstate, call_count} |
| **Source** | CallTraceBuffer aggregation |

#### UC-TMP-05: Memory Region Lifecycle {#uc-tmp-05}

| Aspect | Detail |
|--------|--------|
| **User Question** | "When does this memory region become active?" |
| **Example** | Screen buffer starts being written at T=100000 |
| **Data Required** | First R/W/X timestamp per address |
| **Query** | `GetFirstAccess(addr_range)` → first_tstate |
| **Source** | MemoryAccessTracker (requires timestamp extension) |

---

## 5. Behavioral Use Cases

### 5.1 Variable Profiling

#### UC-BEH-01: Access Frequency Analysis {#uc-beh-01}

| Aspect | Detail |
|--------|--------|
| **User Question** | "How often is this variable written? Per-frame? Rarely?" |
| **Example** | 0x5C78 written 50 times/sec → frame counter |
| **Data Required** | Write count + time range → frequency |
| **Query** | `GetWriteFrequency(address)` → writes_per_frame |
| **Source** | MemoryAccessTracker + frame timing |

#### UC-BEH-02: Scope Inference {#uc-beh-02}

| Aspect | Detail |
|--------|--------|
| **User Question** | "Is this a global or local variable?" |
| **Example** | 0x5C00 accessed by 5 routines → global; 0x8100 by 1 → local |
| **Data Required** | Unique accessor count per address |
| **Query** | `GetAccessorCount(address)` → num_unique_routines |
| **Source** | MemoryAccessTracker.callerAddresses |

#### UC-BEH-03: Reader/Writer Sets {#uc-beh-03}

| Aspect | Detail |
|--------|--------|
| **User Question** | "Which functions read this? Which write?" |
| **Example** | Score variable: written by AddPoints, read by DrawScore, SaveHighScore |
| **Data Required** | Per-address: separate reader and writer sets |
| **Query** | `GetReaders(address)`, `GetWriters(address)` |
| **Source** | MemoryAccessTracker.callerAddresses (with R/W distinction) |

#### UC-BEH-04: Variable Type Inference {#uc-beh-04}

| Aspect | Detail |
|--------|--------|
| **User Question** | "Is this a counter, flag, pointer, or buffer?" |
| **Example** | Incremented by 1 each frame → counter; values 0/1 → flag |
| **Data Required** | Value history, write patterns |
| **Query** | `InferVariableType(address)` → {counter, flag, pointer, buffer, unknown} |
| **Source** | Value change tracking (requires extension) |

---

### 5.2 Routine Classification

#### UC-BEH-05: Screen Routine Detection {#uc-beh-05}

| Aspect | Detail |
|--------|--------|
| **User Question** | "Which routines write to screen memory?" |
| **Example** | DrawSprite, ClearScreen, PrintText all write 0x4000-0x5AFF |
| **Data Required** | Routine → screen memory access correlation |
| **Query** | `GetScreenRoutines()` → list with write patterns |
| **Source** | RoutineClassifier |

#### UC-BEH-06: Sound Routine Detection {#uc-beh-06}

| Aspect | Detail |
|--------|--------|
| **User Question** | "Which routines produce sound?" |
| **Example** | MusicPlayer outputs to port 0xFFFD (AY), BeepFX to port 0xFE |
| **Data Required** | Routine → sound port access correlation |
| **Query** | `GetSoundRoutines()` → list with port patterns |
| **Source** | RoutineClassifier + PortTracker |

#### UC-BEH-07: Input Handler Detection {#uc-beh-07}

| Aspect | Detail |
|--------|--------|
| **User Question** | "Which routines read keyboard/joystick?" |
| **Example** | ReadKeys reads port 0xFE with various address lines |
| **Data Required** | Routine → input port access correlation |
| **Query** | `GetInputRoutines()` → list of handlers |
| **Source** | RoutineClassifier + PortTracker |

#### UC-BEH-08: Interrupt Handler Classification {#uc-beh-08}

| Aspect | Detail |
|--------|--------|
| **User Question** | "What does the interrupt handler do?" |
| **Example** | ISR: reads keyboard, updates music, increments frame counter |
| **Data Required** | ISR execution trace with side effects |
| **Query** | `ClassifyISR(handler_addr)` → {keyboard, sound, timer, mixed} |
| **Source** | InterruptAnalyzer + RoutineClassifier |

---

## 6. Structural Use Cases

### 6.1 Block Segmentation

#### UC-STR-01: Memory Map Generation {#uc-str-01}

| Aspect | Detail |
|--------|--------|
| **User Question** | "Show me the memory layout — what's code, data, variables?" |
| **Example** | Visual map: ROM (0-16K), screen (16K-22K), code (32K-48K) |
| **Data Required** | Per-address classification |
| **Query** | `GetMemoryMap()` → list of regions with types |
| **Source** | BlockSegmenter |

#### UC-STR-02: Disassembly Guidance {#uc-str-02}

| Aspect | Detail |
|--------|--------|
| **User Question** | "Should I disassemble this address or show as data?" |
| **Example** | 0x8000 = CODE (disassemble), 0x9000 = DATA (hex dump) |
| **Data Required** | Block type per address |
| **Query** | `ShouldDisassemble(address)` → bool |
| **Source** | BlockSegmenter |

#### UC-STR-03: SMC Region Identification {#uc-str-03}

| Aspect | Detail |
|--------|--------|
| **User Question** | "Where is self-modifying code?" |
| **Example** | Decompressor at 0x8200-0x8300 modifies 0x9000-0xBFFF |
| **Data Required** | Regions where X > 0 AND W > 0 |
| **Query** | `GetSMCRegions()` → list of SMC blocks |
| **Source** | BlockSegmenter |

---

### 6.2 Module/Cluster Detection

#### UC-STR-04: Function Boundary Detection {#uc-str-04}

| Aspect | Detail |
|--------|--------|
| **User Question** | "Where does this function start and end?" |
| **Example** | DrawSprite: 0x8000-0x8050 (ends with RET) |
| **Data Required** | Entry points + RET/JP/JR destinations |
| **Query** | `GetFunctionBoundaries(entry_point)` → {start, end, size} |
| **Source** | CallTraceBuffer + disassembly analysis |

#### UC-STR-05: Code + Data Grouping {#uc-str-05}

| Aspect | Detail |
|--------|--------|
| **User Question** | "What data belongs to this routine?" |
| **Example** | PrintText routine uses CharTable at 0x9100-0x91FF |
| **Data Required** | Routine → exclusive data access patterns |
| **Query** | `GetAssociatedData(routine_addr)` → list of data regions |
| **Source** | Cross-reference analysis |

#### UC-STR-06: Module Clustering {#uc-str-06}

| Aspect | Detail |
|--------|--------|
| **User Question** | "Group related routines and data together" |
| **Example** | Sound module: MusicPlayer + SFXPlayer + MusicData + SFXData |
| **Data Required** | Call graph + data access patterns |
| **Query** | `DetectModules()` → list of {routines[], data[]} clusters |
| **Source** | CallGraph + Xrefs clustering |

---

## 7. SMC (Self-Modifying Code) Use Cases

### 7.1 SMC Lifecycle

#### UC-SMC-01: Modification Timeline {#uc-smc-01}

| Aspect | Detail |
|--------|--------|
| **User Question** | "When did this code get modified? How many times?" |
| **Example** | Address 0x8234 written 5 times between T=1000 and T=2000 |
| **Data Required** | Write timestamps to SMC regions |
| **Query** | `GetModificationTimeline(smc_addr)` → list of {tstate, value, writer_pc} |
| **Source** | SMCAnalyzer (requires write timestamping) |

#### UC-SMC-02: Modification Cycle Count {#uc-smc-02}

| Aspect | Detail |
|--------|--------|
| **User Question** | "How many modification cycles did this SMC region go through?" |
| **Example** | Inline patch modified 100 times (once per game tick) |
| **Data Required** | Write/execute alternation count |
| **Query** | `GetModificationCycles(smc_region)` → cycle_count |
| **Source** | SMCAnalyzer |

#### UC-SMC-03: Patcher Identification {#uc-smc-03}

| Aspect | Detail |
|--------|--------|
| **User Question** | "What code performs the modification?" |
| **Example** | Address 0x8200 writes to SMC region at 0x9000 |
| **Data Required** | Writer PC for each SMC write |
| **Query** | `GetPatchers(smc_addr)` → list of patcher PCs |
| **Source** | MemoryAccessTracker.callerAddresses |

### 7.2 SMC Pattern Classification

#### UC-SMC-04: SMC Type Detection {#uc-smc-04}

| Aspect | Detail |
|--------|--------|
| **User Question** | "What kind of SMC is this?" |
| **Example** | Burst writes then execute = decompressor |
| **Data Required** | Temporal pattern of W vs X |
| **Query** | `ClassifySMCPattern(smc_region)` → pattern_type |
| **Source** | SMCAnalyzer |

**SMC Pattern Types:**

| Pattern | Signature | Example |
|---------|-----------|---------|
| Inline Patch | Same routine writes + executes | `LD (self+1), A` |
| Decompressor | Burst writes → single execute | LZ unpacker |
| Dynamic Dispatch | Write JP target → execute | Jump table modification |
| Copy Protection | Obfuscated write patterns | Anti-debug tricks |
| Screen Blit | Writes to executed screen area | ULA tricks |

#### UC-SMC-05: Correlated Activity {#uc-smc-05}

| Aspect | Detail |
|--------|--------|
| **User Question** | "What else happens when SMC is active?" |
| **Example** | During decompression: tape loading, progress bar updates |
| **Data Required** | Activity during SMC windows |
| **Query** | `GetCorrelatedActivity(smc_window)` → other active routines |
| **Source** | Temporal correlation analysis |

---

## 8. Data Flow Use Cases

### 8.1 Value Provenance

#### UC-DFL-01: Value Origin Tracking {#uc-dfl-01}

| Aspect | Detail |
|--------|--------|
| **User Question** | "Where did this value come from?" |
| **Example** | Value at 0x5C00 was loaded from tape, decompressed, then stored |
| **Data Required** | Write history with source information |
| **Query** | `GetValueOrigin(address)` → chain of {writer_pc, source} |
| **Source** | Data flow tracking (requires extension) |

#### UC-DFL-02: Block Move Tracking {#uc-dfl-02}

| Aspect | Detail |
|--------|--------|
| **User Question** | "Where was this data copied from?" |
| **Example** | LDIR moved 0x1000 bytes from 0x6000 to 0x9000 |
| **Data Required** | LDIR/LDDR detection with source/dest |
| **Query** | `GetBlockMoves()` → list of {src, dest, size, pc} |
| **Source** | Block move detector (instruction pattern matching) |

#### UC-DFL-03: Decompression Source/Dest {#uc-dfl-03}

| Aspect | Detail |
|--------|--------|
| **User Question** | "What got decompressed to where?" |
| **Example** | Compressed data at 0x6000 unpacked to 0x8000-0xBFFF |
| **Data Required** | Decompressor input/output regions |
| **Query** | `GetDecompressionEvents()` → list of {src_region, dest_region, algorithm} |
| **Source** | SMCAnalyzer + block move correlation |

---

## 9. Comparative Use Cases

### 9.1 Differential Analysis

#### UC-CMP-01: Frame-to-Frame Diff {#uc-cmp-01}

| Aspect | Detail |
|--------|--------|
| **User Question** | "What changed between frame N and M?" |
| **Example** | Sprite positions updated, score incremented |
| **Data Required** | Memory snapshots at frame boundaries |
| **Query** | `DiffFrames(frame_n, frame_m)` → list of changed addresses with old/new |
| **Source** | Snapshot capture + diff engine |

#### UC-CMP-02: State Comparison {#uc-cmp-02}

| Aspect | Detail |
|--------|--------|
| **User Question** | "What's different between game state A and B?" |
| **Example** | Level 1 vs Level 2: different level data, same player vars |
| **Data Required** | Named snapshots |
| **Query** | `DiffSnapshots(snapshot_a, snapshot_b)` → categorized changes |
| **Source** | Snapshot system |

#### UC-CMP-03: Before/After SMC {#uc-cmp-03}

| Aspect | Detail |
|--------|--------|
| **User Question** | "What did the code look like before modification?" |
| **Example** | Original instruction was `NOP`, patched to `LD A, 5` |
| **Data Required** | Value history for SMC addresses |
| **Query** | `GetValueHistory(smc_addr)` → list of {tstate, old_value, new_value} |
| **Source** | Value change log |

---

## 10. Timing Use Cases

### 10.1 Beam Correlation

#### UC-TIM-01: Execution-to-Beam Mapping {#uc-tim-01}

| Aspect | Detail |
|--------|--------|
| **User Question** | "Which scan line was being drawn when this executed?" |
| **Example** | Screen write at T=32000 → line 128, column 64 |
| **Data Required** | T-state + beam position LUT |
| **Query** | `TstateToBeamPosition(tstate)` → {line, column} |
| **Source** | ScreenZX::TstateCoordLUT |

#### UC-TIM-02: Raster Effect Analysis {#uc-tim-02}

| Aspect | Detail |
|--------|--------|
| **User Question** | "Is this routine timing-critical for display?" |
| **Example** | Border color changes sync'd to scan lines for rainbow effect |
| **Data Required** | Port writes with T-state, beam position |
| **Query** | `AnalyzeRasterEffects()` → list of timing-dependent port sequences |
| **Source** | PortTracker + beam correlation |

#### UC-TIM-03: Race Condition Detection {#uc-tim-03}

| Aspect | Detail |
|--------|--------|
| **User Question** | "Is this screen write racing the beam?" |
| **Example** | Writing to line 100 when beam is at line 98 → visible tearing |
| **Data Required** | Screen writes with beam position at write time |
| **Query** | `DetectBeamRaces()` → list of problematic writes |
| **Source** | Screen write analysis + beam tracking |

### 10.2 Interrupt Timing

#### UC-TIM-04: Interrupt Jitter Analysis {#uc-tim-04}

| Aspect | Detail |
|--------|--------|
| **User Question** | "How consistent is the interrupt timing?" |
| **Example** | IM2 fires at T=0 ±3 cycles due to instruction completion |
| **Data Required** | Interrupt timestamps |
| **Query** | `GetInterruptJitter()` → {mean_tstate, std_dev, min, max} |
| **Source** | InterruptAnalyzer |

#### UC-TIM-05: ISR Duration Measurement {#uc-tim-05}

| Aspect | Detail |
|--------|--------|
| **User Question** | "How long does the interrupt handler take?" |
| **Example** | ISR runs for 2000 T-states (blocking other interrupts) |
| **Data Required** | ISR entry/exit timestamps |
| **Query** | `GetISRDuration(handler_addr)` → {min, max, avg} T-states |
| **Source** | InterruptAnalyzer |

### 10.3 Developer Profiling

> **Note:** These use cases target developers writing games/demos who need to optimize their code for the fixed frame budget, not just reverse engineering existing software.

#### UC-TIM-06: Function Frame Cost {#uc-tim-06}

| Aspect | Detail |
|--------|--------|
| **User Question** | "What percentage of my frame does this function consume?" |
| **Example** | DrawSprites takes 15,000 T-states = 21.5% of frame budget |
| **Scenario** | Developer optimizing a game wants to know which routines are hotspots |
| **Data Required** | Per-function: total T-states consumed, call count per frame |
| **Computation** | `(function_tstates / 69888) × 100` |
| **Query** | `GetFunctionFrameCost(routine_addr)` → {total_tstates, percentage, calls_per_frame} |
| **Output Example** | "DrawSprites: 15,000 T-states (21.5%), called 8× per frame" |
| **Source** | FrameProfiler (CallTraceBuffer + T-state timestamping) |

#### UC-TIM-07: CPU Budget Analysis {#uc-tim-07}

| Aspect | Detail |
|--------|--------|
| **User Question** | "How much free CPU budget do I have within the frame?" |
| **Example** | Total used: 55,000 T-states → Free: 14,888 (21.3%) + need 2,000 margin for ISR |
| **Scenario** | Developer adding new features needs to know if there's headroom |
| **Data Required** | Per-frame: total T-states consumed by all code, ISR duration |
| **Computation** | `free = 69888 - total_consumed - isr_overhead_margin` |
| **Query** | `GetFrameBudget()` → {used_tstates, free_tstates, isr_reserved, percentage_free} |
| **Output Example** | "Frame budget: 55,000 used (78.7%) + 2,000 ISR reserved → 12,888 free (18.4%)" |
| **Related** | UC-TIM-05 (ISR Duration) — need to know ISR cost to compute margin |
| **Source** | FrameProfiler |

#### UC-TIM-08: Multicolor Effect Detection {#uc-tim-08}

| Aspect | Detail |
|--------|--------|
| **User Question** | "Is this code synchronizing attribute writes with the beam?" |
| **Example** | Routine writes to attribute memory (0x5800-0x5AFF) exactly when ULA is rendering the corresponding scanline |
| **Scenario** | ZX Spectrum's 2-color-per-8x8-cell limit can be bypassed by changing attributes mid-scanline, exactly as the ULA reads them |
| **Technique** | CPU tracks beam position (via T-state counting from interrupt) and times attribute writes to occur just before ULA fetches them |
| **Data Required** | Per-attribute write: exact T-state, VRAM address, which scanline ULA is drawing |
| **Detection Signature** | (1) Writes to 0x5800-0x5AFF, (2) occur during BeamRegion::Screen, (3) write T-state matches ULA fetch T-state for that row ±8 T-states |
| **Query** | `DetectMulticolorRoutines()` → List of {routine_addr, affected_scanlines, sync_precision} |
| **Output Example** | "Multicolor effect at 0x8000: rows 64-127, sync precision ±4 T-states" |
| **False Positives** | General screen blits also write to attribute area — distinguish by timing correlation |
| **Visualization** | Highlight scanlines with multicolor, show timing diagram of write vs ULA fetch |
| **Related** | UC-TIM-01 (Beam mapping), UC-TIM-03 (Race detection), UC-BEH-05 (Screen routine) |
| **Source** | BeamCorrelator + TimedAccessLog |

**Developer Profiling Visualization Ideas:**

| Visualization | Description |
|---------------|-------------|
| **Frame Budget Bar** | Horizontal bar showing used% / ISR reserved% / free% |
| **Function Flamegraph** | Hierarchical view of T-state consumption by call tree |
| **Per-Frame Timeline** | T-state timeline showing which function is executing when |
| **Hotspot Heatmap** | Routines colored by CPU cost (red = expensive) |

---

## 11. Data Requirements Summary

### 11.1 Currently Available Data

| Data Source | Available Fields | Missing for Use Cases |
|-------------|------------------|----------------------|
| MemoryAccessTracker | R/W/X counts, callerAddresses | **T-state timestamps**, value history |
| CallTraceBuffer | caller, target, event_type, tstate | Complete |
| ScreenZX | TstateCoordLUT, beam position methods | Complete |
| PortTracker (proposed) | port, value, direction, address | **Needs implementation** |

### 11.2 Required Extensions

| Extension | Use Cases Enabled | Priority |
|-----------|------------------|----------|
| **T-state in memory hooks** | UC-TMP-05, UC-SMC-01, UC-TIM-* | HIGH |
| **Value change logging** | UC-BEH-04, UC-CMP-03, UC-DFL-01 | MEDIUM |
| **Port tracking** | UC-BEH-06, UC-BEH-07, UC-TIM-02 | HIGH |
| **Block move detection** | UC-DFL-02, UC-DFL-03 | MEDIUM |
| **Snapshot system** | UC-CMP-01, UC-CMP-02 | LOW |

---

## 12. Cross-Reference: Use Cases → Analyzers

| Use Case Category | Primary Analyzer | Supporting Analyzers |
|-------------------|------------------|---------------------|
| Navigation (UC-NAV-*) | MemoryAccessTracker | CallTraceBuffer |
| Temporal (UC-TMP-*) | CallTraceBuffer | MemoryAccessTracker |
| Behavioral (UC-BEH-*) | RoutineClassifier | MemoryAccessTracker, PortTracker |
| Structural (UC-STR-*) | BlockSegmenter | CallTraceBuffer, Xrefs |
| SMC (UC-SMC-*) | SMCAnalyzer | MemoryAccessTracker, CallTraceBuffer |
| Data Flow (UC-DFL-*) | DataFlowAnalyzer | MemoryAccessTracker |
| Comparative (UC-CMP-*) | SnapshotDiff | All (for capture) |
| Timing (UC-TIM-*) | BeamCorrelator, FrameProfiler | InterruptAnalyzer, PortTracker |

---

## 13. Priority Matrix

### 13.1 By Implementation Effort vs User Value

```
                          USER VALUE
                    Low            High
               ┌──────────────┬──────────────┐
          Low  │ UC-CMP-*     │ UC-NAV-*     │
   EFFORT      │              │ UC-STR-01/02 │
               ├──────────────┼──────────────┤
          High │ UC-DFL-*     │ UC-SMC-*     │
               │              │ UC-TIM-*     │
               └──────────────┴──────────────┘
```

### 13.2 Recommended Implementation Order

| Phase | Use Cases | Rationale |
|-------|-----------|-----------|
| **Phase 1** | UC-STR-01, UC-STR-02, UC-NAV-01/02 | Core block classification + xrefs |
| **Phase 2** | UC-NAV-03, UC-TMP-01, UC-BEH-05/06 | Call graph + routine classification |
| **Phase 3** | UC-SMC-01/02/03/04, UC-STR-03 | SMC analysis |
| **Phase 4** | UC-TIM-01/02, UC-BEH-08 | Timing + interrupt analysis |
| **Phase 5** | UC-DFL-*, UC-CMP-* | Advanced data flow + comparison |

---

## 14. References

### Parent Documents
- [Analyzer Architecture](./analyzer-architecture.md)

### Related Design Documents
- [Block Segmentation](./block-segmentation.md)
- [Interrupt Analyzer](./interrupt-analyzer.md)
- [Routine Classifiers](./routine-classifiers.md)
- [Beam-to-Execution Correlation](./beam-to-execution-correlation.md)
- [Data Collection Extensions](./data-collection-extensions.md)

### Source Infrastructure
- [Memory Access Tracker](../../../core/src/emulator/memory/memoryaccesstracker.h)
- [Call Trace Buffer](../../../core/src/emulator/memory/calltrace.h)
- [Screen ZX](../../../core/src/emulator/video/zx/screenzx.h)
