# Feature Reconciliation: Claude Brainstorm vs Codebase Reality

**Date:** 2026-01-14  
**Source:** [Claude-Unreal-ng emulator features for reverse engineering.md](./Claude-Unreal-ng%20emulator%20features%20for%20reverse%20engineering.md)

This document reconciles Claude's feature brainstorming with the **actual state** of the unreal-ng codebase. Claude lacked visibility into the codebase structure, so many of its "suggested" features already exist or are partially implemented.

---

## Executive Summary

| Category | Claude's Assessment | Reality |
|----------|---------------------|---------|
| Memory Tracking | "Critical - Not implemented" | ✅ **Fully Implemented** - `MemoryAccessTracker` |
| Call Trace | "Needed" | ✅ **Fully Implemented** - `CallTraceBuffer` with hot/cold compression |
| Disassembler | "Basic" | ✅ **174KB Implementation** - Full Z80 instruction set with control flow analysis |
| Breakpoints | "Basic" | ✅ **Comprehensive** - Memory, Port, Address with groups/annotations |
| Labels/Symbols | "Needed" | ✅ **Implemented** - `LabelManager` |
| Scripting API | "High Priority" | ✅ **Implemented** - WebAPI + Lua + CLI |
| Graphics Analysis | "High Priority" | ⚠️ **Partial** - Screen capture exists, extraction tools needed |
| Audio Analysis | "Medium Priority" | ⚠️ **Partial** - AY/Beeper emulation complete, analysis tools needed |
| Procedure Fingerprinting | "Proposed" | ❌ **Not Implemented** - Advanced concept for future |

---

## Detailed Reconciliation

### 1. Memory Analysis & Tracking

#### Claude's Proposal
```
- Real-time memory watch/breakpoint system with conditional triggers
- Memory access pattern analysis (read/write/execute frequency heatmaps)
- Automatic detection of self-modifying code
- Data structure inference (sprite tables, music patterns, level data)
```

#### **Codebase Reality: ✅ FULLY IMPLEMENTED**

**Location:** `core/src/emulator/memory/memoryaccesstracker.h` (381 lines, 51KB implementation)

**Existing Capabilities:**
- ✅ Per-address read/write/execute counters (`_z80ReadCounters`, `_z80WriteCounters`, `_z80ExecuteCounters`)
- ✅ Monitored memory regions with named tracking (`MonitoredRegion` struct)
- ✅ Monitored I/O ports (`MonitoredPort` struct)
- ✅ Caller address tracking (`callerAddresses` map per region)
- ✅ Data value frequency tracking (`dataValues` map)
- ✅ Segmented tracking by frame/interrupt (`TrackingSegment`, `TrackingEvent`)
- ✅ Multiple tracking modes (`TrackingMode::Continuous`, etc.)

**What's Missing:**
- ❌ Memory access **heatmap visualization** (data collected, no UI)
- ❌ Automatic self-modifying code **detection** (execute counters exist, logic not implemented)
- ❌ Data structure **inference** (this is the advanced procedure fingerprinting Claude proposed)

---

### 2. Execution Tracing & Call Stack

#### Claude's Proposal
```sql
CREATE TABLE execution_trace (
    pc INTEGER NOT NULL,
    opcode BLOB,
    mnemonic TEXT,
    reg_af INTEGER,
    ...
);
```

#### **Codebase Reality: ✅ FULLY IMPLEMENTED**

**Location:** `core/src/emulator/memory/calltrace.h` (195 lines, 24KB implementation)

**Existing Capabilities:**
- ✅ Control flow event logging (`Z80ControlFlowEvent` struct)
  - PC address, target address, opcode bytes, flags, type
  - Full memory bank mapping (ROM/RAM + page number for all 4 banks)
  - Stack pointer and top 3 stack values (for RET analysis)
- ✅ Hot/cold buffer architecture with loop compression
  - Cold buffer: 1M events, grows up to 1GB
  - Hot buffer: 1024 events for frequent patterns
  - Automatic loop detection and compression (`loop_count` field)
- ✅ Event types: `JP`, `JR`, `CALL`, `RST`, `RET`, `RETI`, `DJNZ`
- ✅ File export (`SaveToFile`)

**What's Missing:**
- ❌ **Timeline visualization** (data exists, no UI)
- ❌ **Rewind/replay** capability (snapshots exist but not integrated with trace)
- ❌ **Code coverage overlay** in disassembly (execute counters exist, not visualized)

---

### 3. Disassembly & Code Analysis

#### Claude's Proposal
```
- Interactive disassembler with auto-labeling of subroutines
- Control flow graph generation
- Pattern recognition for common ZX Spectrum routines
- Cross-reference system
```

#### **Codebase Reality: ✅ COMPREHENSIVE**

**Location:** `core/src/debugger/disassembler/z80disasm.h` (376 lines, 174KB implementation)

**Existing Capabilities:**
- ✅ Full Z80 instruction decoding (`DecodedInstruction` struct)
  - Opcode flags (prefix, conditional, jump, call, return, etc.)
  - T-state tracking (normal, condition met, condition not met)
  - Operand types and addressing modes
- ✅ Control flow flags (`OF_JP`, `OF_JR`, `OF_CALL`, `OF_RET`, `OF_DJNZ`, etc.)
- ✅ Register exchange detection (`OF_REG_EXCHANGE`)
- ✅ Indirect addressing detection (`OF_INDIRECT`)
- ✅ Flag-affecting instruction detection (`OF_FLAGS_AFFECTED`, `OF_FLAGS_ALL`, `OF_FLAGS_SZ`)

**What's Missing:**
- ❌ **Control flow graph** generation/visualization
- ❌ **Pattern recognition** for common routines (ROM calls, screen plotting)
- ❌ **Cross-reference** system (which addresses reference this one)
- ❌ **Dead code detection**

---

### 4. Breakpoints

#### Claude's Proposal
```
- Screen position breakpoints (break when beam reaches X,Y)
- Register condition breakpoints
- Memory pattern breakpoints
- AY register write breakpoints
```

#### **Codebase Reality: ✅ COMPREHENSIVE**

**Location:** `core/src/debugger/breakpoints/breakpointmanager.h` (245 lines, 40KB implementation)

**Existing Capabilities:**
- ✅ Memory address breakpoints
- ✅ Port breakpoints
- ✅ Bank-aware address matching (`BRK_MATCH_ADDR`, `BRK_MATCH_BANK_ADDR`)
- ✅ Breakpoint groups for organization
- ✅ Annotations/notes per breakpoint
- ✅ Enable/disable individual breakpoints

**What's Missing:**
- ❌ **Conditional breakpoints** (register values, expressions)
- ❌ **Screen position breakpoints** (raster line/pixel)
- ❌ **Memory pattern breakpoints** (break when specific byte sequence appears)
- ❌ **Frame count breakpoints**

---

### 5. Labels & Symbols

#### Claude's Proposal
```
- Symbol database with community-contributed annotations
- Import/export label databases
```

#### **Codebase Reality: ✅ IMPLEMENTED**

**Location:** `core/src/debugger/labels/labelmanager.*`

**Existing Capabilities:**
- ✅ Label management for addresses
- ✅ Integration with disassembler

**What's Missing:**
- ❌ **Import/export** standard formats (SLD, Symbol files)
- ❌ **Community database** integration

---

### 6. Scripting & Automation

#### Claude's Proposal
```
- Python/Lua API for automation
- Batch processing of multiple files
- Automated testing framework
```

#### **Codebase Reality: ✅ FULLY IMPLEMENTED**

**Location:** `core/automation/`

**Existing Capabilities:**
- ✅ **WebAPI** - HTTP/REST interface (Drogon framework) - 24 files
- ✅ **Lua scripting** - Full access to emulator state - 7 files
- ✅ **CLI** - Full command interface - 17 files
  - Note: Batch command execution is in a feature branch, not yet merged to main
- ✅ **Python** - Python bindings automation - 5 source files (+ 4500 3rdparty deps)
- ✅ JSON-based communication

**What's Missing:**
- ❌ **Macro recording** for repetitive tasks
- ❌ **Pre-built analysis scripts** for common tasks

---

### 7. Graphics Analysis

#### Claude's Proposal
```
- Screen memory visualizer showing attribute and pixel bytes separately
- Sprite/character extraction tools
- Animation frame extraction
- Layer separation (background, sprites, UI elements)
```

#### **Codebase Reality: ⚠️ PARTIAL**

**Existing Capabilities:**
- ✅ Screen rendering (multiple modes)
- ✅ Framebuffer access
- ✅ Screen capture for recording (`RecordingManager`)
- ✅ GIF/APNG/Video encoding

**What's Missing:**
- ❌ **Screen memory visualizer** (separate pixel/attribute view)
- ❌ **Sprite extraction** tools
- ❌ **Animation detection** and extraction
- ❌ **Dirty rectangle tracking** (what changed between frames)

---

### 8. Audio Analysis

#### Claude's Proposal
```
- AY register logging and pattern analysis
- Beeper routine identification
- Music format detection (Soundtracker, Vortex)
- Export to VGM/MOD formats
```

#### **Codebase Reality: ⚠️ PARTIAL**

**Existing Capabilities:**
- ✅ AY8910 emulation (complete)
- ✅ Beeper emulation (complete)
- ✅ TurboSound (dual AY) support
- ✅ Multi-track audio recording to WAV (`RecordingManager`)

**Planned Audio Hardware:**
- 🔲 COVOX
- 🔲 General Sound
- 🔲 Moonsound
- 🔲 ZX-Next 3xAY

**What's Missing:**
- ❌ **AY register logging** with pattern visualization
- ❌ **Music tracker format detection**
- ❌ **Beeper routine analysis**
- ❌ **VGM/MOD export**

---

### 9. Procedure Fingerprinting (Claude's Novel Proposal)

#### Claude's Proposal
This was a substantial part of the document (~1000 lines) proposing:
- Memory access pattern classification
- Temporal pattern detection
- Routine classification (music player, decompressor, effect, sprite drawer)
- Feature-based adaptive classification

#### **Codebase Reality: ❌ NOT IMPLEMENTED**

This is genuinely **novel and valuable**. The foundation exists:
- `MemoryAccessTracker` collects the data needed
- `CallTraceBuffer` tracks control flow

But the **analysis layer** (feature extraction, classification) doesn't exist.

**Implementation effort:** Medium-High. Would require:
1. Feature extractor consuming `MemoryAccessTracker` data
2. Classification rules/heuristics
3. Confidence scoring
4. UI for visualization

---

## Extended Feature Wishlist

### Resource Discovery & Extraction

**Graphics Resources:**
- 🔲 Sprite discovery (8x8, 16x16, arbitrary sizes)
- 🔲 Bitmap font detection and extraction
- 🔲 Tileset/charset recognition
- 🔲 Masked sprite detection (sprite + mask pairs)
- 🔲 Animation sequence detection
- 🔲 Screen layout/tilemap reconstruction
- 🔲 Color palette extraction

**Audio Resources:**
- 🔲 AY music ripping (native formats: ProTracker, ASC, SoundTracker, Vortex)
- 🔲 Beeper routine detection and waveform capture
- 🔲 Sample/digitized audio extraction
- 🔲 Music pattern/instrument identification

---

### Peripheral Visualization & Activity Monitoring

**Memory System:**
- 🔲 Memory page switching visualization (which pages active when)
- 🔲 Bank switching timeline
- 🔲 ROM/RAM mapping changes over time

**Storage Devices:**
- 🔲 FDC/Floppy disk activity (read/write/seek operations)
- 🔲 TR-DOS command logging
- 🔲 Disk sector access heatmap
- 🔲 Tape activity visualization (loading phases, pulses)

**Other Peripherals:**
- 🔲 AY register change timeline
- 🔲 Port I/O activity summary
- 🔲 Keyboard polling detection
- 🔲 Joystick/mouse activity
- 🔲 IDE/HDD operations (if emulated)

**Activity Reports:**
- 🔲 High-level peripheral usage summary
- 🔲 "What was activated" brief report for quick analysis
- 🔲 Operation sequence timeline

---

### Advanced Memory Analysis

**Heatmap Integration:**
- 🔲 Memory access heatmap overlay in disassembly view
- 🔲 Read/Write/Execute separate color channels
- 🔲 Temporal heatmap (access over time, not just totals)
- 🔲 Clickable regions to jump to disassembly

**Signature Analysis:**
- 🔲 Known routine signatures (ROM calls, common libraries)
- 🔲 Compression algorithm detection (RLE, LZ, Hrum, etc.)
- 🔲 Protection scheme signatures
- 🔲 Music player routine signatures
- 🔲 Custom signature database (user-defined patterns)

---

### Smart Code Analyzers

**Block Classification:**
- 🔲 Code blocks vs data blocks vs variables detection
- 🔲 Dead code identification
- 🔲 Reachability analysis from entry points

**Self-Modifying Code:**
- 🔲 Decruncher detection (code that modifies then jumps)
- 🔲 Runtime-optimized render procedures
- 🔲 Protection/anti-debug code identification
- 🔲 SMC region highlighting in disassembly

**Interrupt Analysis:**
- 🔲 ISR routine detection (called from IM1/IM2)
- 🔲 Music player interrupt identification
- 🔲 Keyboard polling routines
- 🔲 Timer-based routines
- 🔲 Interrupt frequency measurement

**Structure Detection:**
- 🔲 Main loop detection (games/demos)
- 🔲 State machine identification
- 🔲 Input handling routines
- 🔲 Game state transitions

**Function Classification:**
- 🔲 Screen rendering routines
- 🔲 Sound/music access routines
- 🔲 Pure calculation routines
- 🔲 Data block processing (blitting, copying)
- 🔲 Sprite masking routines
- 🔲 Scroll routines (horizontal, vertical, pixel, attribute)
- 🔲 Collision detection routines
- 🔲 Random number generators
- 🔲 Math routines (multiply, divide, sine tables)

---

### Additional Analysis Features

**Data Flow:**
- 🔲 Where does data come from / go to
- 🔲 Input → processing → output chains
- 🔲 Variable lifetime tracking

**Cross-References:**
- 🔲 What calls this routine
- 🔲 What this routine calls
- 🔲 What data this routine accesses
- 🔲 Reverse call graph

**Comparative Analysis:**
- 🔲 Compare two snapshots/states
- 🔲 Diff memory between frames
- 🔲 Track variable changes over time

**Pattern Matching:**
- 🔲 Find similar code blocks
- 🔲 Detect copy-pasted routines
- 🔲 Library routine identification

**Demo/Game Specific:**
- 🔲 Demo part boundary detection
- 🔲 Effect catalog (plasma, rasters, scrollers)
- 🔲 Music/graphics sync point detection
- 🔲 Loading screen extraction
- 🔲 Packer/loader identification

---

## Implementation Priority Tiers

### Already Done (Just Needs UI/Visualization)
1. **Memory Heatmap** - Data collected by `MemoryAccessTracker`, needs visualization
2. **Code Coverage Overlay** - Execute counters exist, need disassembly integration
3. **Call Graph** - `CallTraceBuffer` has the data, needs graph generation

### High Value Gap Fills
1. **Conditional Breakpoints** - Register value conditions, expressions
2. **Cross-References** - "What references this address?"
3. **Screen Memory Visualizer** - Separate pixel/attribute view
4. **AY Register Logger** - Capture I/O writes with timestamps

### Advanced/Future
1. **Procedure Fingerprinting** - Claude's classification system
2. **Demo Part Detection** - Automatic segmentation
3. **Effect Recognition** - Pattern matching for common effects

---

## Conclusion

Claude's brainstorming was well-intentioned but significantly **underestimated** the existing codebase. The core infrastructure for reverse engineering is **already in place**:

- Memory tracking ✅
- Call tracing ✅
- Disassembly ✅
- Breakpoints ✅
- Automation APIs ✅

The real opportunities are in:
1. **Visualization layers** on top of existing data collection
2. **Advanced analysis** (procedure fingerprinting, pattern recognition)
3. **Quality-of-life features** (conditional breakpoints, cross-references)

Claude's **procedure fingerprinting proposal** (lines 2044-2817) is the most novel contribution and worth considering for future implementation, but the SQLite-based data model is **overkill** for an emulator - the existing in-memory structures are more appropriate for real-time analysis.
