# TR-DOS Analyzer Refactoring: Raw Events Architecture

## Goal

Refactor the TR-DOS analyzer from a "semantic interpretation" model to a **raw events capture** model:
- Core captures raw FDC and breakpoint events with full Z80 context
- **All automation modules (WebAPI, CLI, Lua, Python)** are pure passthrough (serdes only, no formatting)
- All interpretation/pattern-matching happens client-side or in a future analysis layer

## References

- [High-Level Disk Operations Design](docs/analysis/capture/high-level-disk-operations.md) - Original semantic analyzer design (being simplified)
- [Emulator Control Interface (ECI)](docs/emulator/design/control-interfaces/command-interface.md) - Unified command interface architecture

## ECI Conformance

Per [command-interface.md](docs/emulator/design/control-interfaces/command-interface.md), all automation modules must:

1. **Share same command semantics** across CLI, WebAPI, Python, Lua
2. **Transport-agnostic** - identical response structure regardless of interface  
3. **Passthrough design** - serialization/deserialization only, no business logic in transport layer

> [!IMPORTANT]
> The analyzer API must follow the same pattern as existing commands (e.g., `feature`, `memory`, `bp`).

---

## Proposed Changes

> [!NOTE]
> Raw and semantic events are **complementary**, not mutually exclusive.
> - **Layer 1 (Raw)**: Capture everything fast with full Z80 context
> - **Layer 2 (Semantic)**: Aggregate raw events into meaningful patterns

### 1. Core: Two-Layer Event Architecture

#### [MODIFY] [trdosevent.h](core/src/debugger/analyzers/trdos/trdosevent.h)

Replace semantic `TRDOSEventType` with raw event types:

```cpp
/// Raw FDC event - captures every WD1793 interaction
struct RawFDCEvent
{
    uint64_t tstate;           // T-state counter
    uint32_t frameNumber;      // Video frame
    
    // FDC state at time of event
    uint8_t port;              // Port accessed (0x1F, 0x3F, 0x5F, 0x7F, 0xFF)
    uint8_t direction;         // 0=read, 1=write
    uint8_t value;             // Value read/written
    uint8_t commandReg;        // Current command register
    uint8_t statusReg;         // Current status register
    uint8_t trackReg;          // Track register
    uint8_t sectorReg;         // Sector register
    uint8_t dataReg;           // Data register
    uint8_t systemReg;         // System register (0xFF)
    
    // Full Z80 context
    uint16_t pc;
    uint16_t sp;
    uint8_t a, f, b, c, d, e, h, l;
    uint8_t iff1, iff2, im;
    
    // Stack snapshot (first 8 bytes for call chain)
    uint8_t stack[8];
};

/// Raw breakpoint event
struct RawBreakpointEvent
{
    uint64_t tstate;
    uint32_t frameNumber;
    uint16_t address;          // Breakpoint address hit
    
    // Full Z80 context (same as above)
    uint16_t pc, sp;
    uint8_t a, f, b, c, d, e, h, l;
    uint8_t iff1, iff2, im;
    uint8_t stack[8];
};
```

---

### 2. Core: Simplified Analyzer

#### [MODIFY] [trdosanalyzer.cpp](core/src/debugger/analyzers/trdos/trdosanalyzer.cpp)

- Remove `TRDOSEvent::format()` entirely
- Remove semantic event types (`COMMAND_START`, `LOADER_DETECTED`, etc.)
- Simplify `onFDCCommand()` to just capture raw FDC state + Z80 context
- Simplify `onBreakpointHit()` to just capture raw breakpoint + Z80 context
- Store in two separate ring buffers: `_rawFdcEvents`, `_rawBreakpointEvents`
- Expose via public getters: `getRawFdcEvents()`, `getRawBreakpointEvents()`

---

### 3. Unified Automation Interface

> [!IMPORTANT]
> All automation modules must have **identical** analyzer endpoints with pure passthrough serialization.

#### Common Operations (must exist in ALL modules):

| Operation | WebAPI | CLI | Lua | Python |
|-----------|--------|-----|-----|--------|
| List analyzers | `GET /analyzer` | `analyzer list` | `analyzer.list()` | `analyzer.list()` |
| Get status | `GET /analyzer/{name}` | `analyzer status {name}` | `analyzer.status(name)` | `analyzer.status(name)` |
| Activate | `PUT /analyzer/{name}` | `analyzer activate {name}` | `analyzer.activate(name)` | `analyzer.activate(name)` |
| Deactivate | `DELETE /analyzer/{name}` | `analyzer deactivate {name}` | `analyzer.deactivate(name)` | `analyzer.deactivate(name)` |
| Get raw FDC events | `GET /analyzer/trdos/fdc` | `analyzer trdos fdc` | `analyzer.trdos.fdc()` | `analyzer.trdos.fdc()` |
| Get raw breakpoints | `GET /analyzer/trdos/breakpoints` | `analyzer trdos breakpoints` | `analyzer.trdos.breakpoints()` | `analyzer.trdos.breakpoints()` |
| Clear events | `DELETE /analyzer/trdos/events` | `analyzer trdos clear` | `analyzer.trdos.clear()` | `analyzer.trdos.clear()` |

---

### 4. WebAPI: Pure Passthrough

#### [MODIFY] [analyzers_api.cpp](core/automation/webapi/src/api/analyzers_api.cpp)

Remove all formatting logic. Just serialize raw structs to JSON:

```cpp
// Remove this:
ev["formatted"] = events[i].format();

// Keep only raw field serialization:
ev["tstate"] = event.tstate;
ev["frame"] = event.frameNumber;
ev["port"] = event.port;
ev["direction"] = event.direction;
ev["value"] = event.value;
ev["command_reg"] = event.commandReg;
ev["status_reg"] = event.statusReg;
ev["track_reg"] = event.trackReg;
ev["sector_reg"] = event.sectorReg;
ev["pc"] = event.pc;
ev["sp"] = event.sp;
// ... all Z80 registers and stack
```

---

### 5. CLI: Mirror WebAPI

#### [MODIFY] [cli-processor-analyzer-mgr.cpp](core/automation/cli/src/commands/cli-processor-analyzer-mgr.cpp)

Update `HandleAnalyzer` to support raw event commands:
- `analyzer trdos fdc [limit]` - dump raw FDC events
- `analyzer trdos breakpoints [limit]` - dump raw breakpoint events
- Output as JSON for consistency with WebAPI

---

### 6. Lua/Python: Future

Create bindings that expose same interface. (Not in initial scope but architecture should support it.)

---

## Verification Plan

1. Build and run emulator
2. Activate analyzer via WebAPI: `PUT /analyzer/trdos`
3. Execute `LIST` command
4. Retrieve raw FDC events: `GET /analyzer/trdos/fdc`
5. Retrieve same via CLI: `analyzer trdos fdc`
6. Compare outputs - must be identical JSON structure
7. Verify all fields populated (no formatting, just data)
8. Confirm stack snapshots are valid (can trace call chain)

---

## Benefits

- **Complete data**: No information lost during capture
- **Consistency**: All automation modules produce identical output
- **Flexibility**: All analysis/interpretation can evolve client-side
- **Debuggable**: Raw data is self-explanatory
- **Clean architecture**: Clear separation of concerns

