# WD1793/TR-DOS Analyzer Architecture - Design Summary

## Overview

The [high-level-disk-operations.md](docs/analysis/capture/high-level-disk-operations.md) document defines a comprehensive **two-stage event capture and aggregation pipeline** for semantic TR-DOS disk operation analysis.

---

## Key Architecture Components

### 1. Event Context with Full Bank Mapping

Updated `BankMapping` structure to support **256 RAM pages** (0-255):

```cpp
struct BankMapping {
    uint8_t pageNumber;      // Physical page number (0-255)
    uint8_t isROM;           // 1=ROM, 0=RAM
};  // 2 bytes per bank, 8 bytes total

struct EventContext {
    uint16_t m1_pc;                      // Current M1 Program Counter
    uint16_t callerAddress;              // Immediate return address from stack
    std::vector<uint16_t> stackTrace;    // Full trace up to RAM caller
    uint16_t originalRAMCaller;          // First non-ROM address in stack
    std::array<BankMapping, 4> banks;    // 4× 16KB bank mappings (8 bytes)
};
```

**Key insight**: Changed from 7-bit (0-127) to 8-bit (0-255) page addressing to match `MAX_RAM_PAGES = 256` in [platform.h](core/src/emulator/platform.h#L227).

---

## 2. Two-Stage Event Pipeline

### Stage 1: Low-Level Capture → RawEvent Buffer

**Capture Sources**:
- Execution breakpoints at TR-DOS ROM entrypoints ($3D00, $3D21, etc.)
- Port I/O hooks via `WD1793Collector` (ports $1F, $3F, $5F, $7F, $FF)
- FDC state change callbacks
- Direct memory reads for TR-DOS system variables ($5CD1, $5CF6, etc.)

**RawEvent Buffer**:
```cpp
struct RawEvent {
    uint64_t tstate;        // 8 bytes
    uint16_t address;       // 2 bytes
    uint8_t  port;          // 1 byte
    uint8_t  value;         // 1 byte
    uint8_t  type;          // 1 byte (enum)
    uint8_t  flags;         // 1 byte
    // Total: 16 bytes per event
};

constexpr size_t RAW_BUFFER_SIZE = 1024;  // 16 KB total
```

**Lifetime**: < 1 frame (20ms), disposed immediately after aggregation.

---

### Stage 2: Aggregation → SemanticEvent Buffer

**State Machine** (7 states):
1. `IDLE` - Monitoring for TR-DOS entry
2. `IN_TRDOS` - Inside ROM, command not yet identified
3. `IN_COMMAND` - Command identified, tracking operation
4. `IN_SECTOR_OP` - Mid-sector read/write
5. `IN_MULTI_OP` - Multi-sector operation
6. `COMPLETING` - Operation finishing
7. `IN_CUSTOM` - Custom loader detected

**Example Transition**:
```
IDLE → [Exec BP $3D00] → IN_TRDOS (emit: TRDOS_ENTRY)
IN_TRDOS → [Exec BP $3D21] → IN_COMMAND (emit: COMMAND_START)
IN_COMMAND → [Port OUT $1F] → IN_SECTOR_OP (emit: FDC_COMMAND)
IN_SECTOR_OP → [256× Port $7F] → IN_SECTOR_OP (accumulate)
IN_SECTOR_OP → [FDC INTRQ] → IN_COMMAND (emit: SECTOR_TRANSFER)
```

**SemanticEvent Buffer**:
```cpp
struct TRDOSEvent {
    uint64_t timestamp;     // 8 bytes
    uint32_t frameNumber;   // 4 bytes
    uint16_t eventType;     // 2 bytes
    uint16_t flags;         // 2 bytes
    // Event-specific data: ~32 bytes
    // Total: ~48 bytes per event
};

constexpr size_t SEMANTIC_BUFFER_SIZE = 10000;  // 480 KB total
```

**Lifetime**: Configurable (default: until buffer full, FIFO eviction).

---

## 3. Memory Budget

| Component | Size | Allocation | Lifetime |
|:----------|:-----|:-----------|:---------|
| RawEvent buffer | 16 KB | Static | Permanent (reused) |
| SemanticEvent buffer | 480 KB | Static | Permanent (reused) |
| AnalyzerContext | ~128 bytes | Per-analyzer | Analyzer lifetime |
| String pool | ~8 KB | Dynamic | Event lifetime |
| **Total per analyzer** | **~512 KB** | - | - |

---

## 4. Buffer Disposal Triggers

| Trigger | RawEvent Buffer | SemanticEvent Buffer |
|:--------|:----------------|:---------------------|
| Frame boundary | Flush → aggregator | No action |
| Buffer full | Oldest overwritten | Oldest overwritten |
| Analyzer disabled | Clear | Optionally preserve |
| Export requested | N/A | Write to file, clear |
| User clear | Clear | Clear |

**Critical design decision**: RawEvent buffer has **< 1 frame lifetime** to minimize memory footprint during long operations (e.g., FORMAT with 665,600 raw port accesses).

---

## 5. API and Automation Surface

### C++ Core API

```cpp
class ITRDOSAnalyzer {
    virtual void enable() = 0;
    virtual void disable() = 0;
    virtual size_t getEventCount() const = 0;
    virtual std::vector<TRDOSEvent> getEventsSince(uint64_t tstate) const = 0;
    virtual void subscribe(EventCallback callback) = 0;
    virtual void exportToFile(const std::string& path, ExportFormat format) = 0;
    virtual void clear() = 0;
};
```

### WebAPI Endpoints

```
GET  /api/v1/emulator/{id}/analyzers/trdos/status
GET  /api/v1/emulator/{id}/analyzers/trdos/events?since=0&limit=100&type=...
POST /api/v1/emulator/{id}/analyzers/trdos/enable
POST /api/v1/emulator/{id}/analyzers/trdos/disable
POST /api/v1/emulator/{id}/analyzers/trdos/clear
WS   /api/v1/emulator/{id}/analyzers/trdos/stream  (real-time events)
```

### Python Bindings

```python
emu.analyzers.trdos.enable()
events = emu.analyzers.trdos.events(since=0, limit=100)
emu.analyzers.trdos.subscribe(lambda event: print(event.format()))
emu.analyzers.trdos.export("trace.json", format="json")
```

### Lua Bindings

```lua
emu:analyzers():trdos():enable()
local events = emu:analyzers():trdos():events()
emu:analyzers():trdos():on_event(function(event) ... end)
```

---

## 6. Generic Framework for Reusability

### EventAnalyzer Template

```cpp
template<typename TEvent, typename TContext>
class EventAnalyzer : public IAnalyzer {
protected:
    TContext _context;
    RingBuffer<TEvent> _events;
    std::vector<std::function<void(const TEvent&)>> _subscribers;
    
    void emit(TEvent&& event) {
        _events.push(std::move(event));
        for (auto& sub : _subscribers) {
            sub(_events.back());
        }
    }
    
public:
    virtual void processRawEvent(const RawEvent& raw) = 0;
};
```

### Reusable Components

| Component | Purpose | Reusable For |
|:----------|:--------|:-------------|
| `RingBuffer<T>` | Fixed-size FIFO | Any analyzer needing bounded storage |
| `StateContext` | State machine base | Any multi-state tracking |
| `EventEmitter` | Pub/sub pattern | Any real-time notification |
| `BreakpointSet` | Grouped BP management | Any ROM/RAM monitoring |
| `PortMonitor` | Port access aggregation | Any peripheral analysis |

### Multi-Subsystem Composition

```cpp
class CombinedAnalyzer : public IAnalyzer {
    TRDOSAnalyzer _disk;
    AYAnalyzer _sound;
    ULAAnalyzer _video;
    
    void onBreakpointHit(uint16_t address, Z80* cpu) override {
        if (address >= 0x3D00 && address < 0x4000) {
            _disk.onBreakpointHit(address, cpu);
        }
        // Route to appropriate analyzer
    }
    
    // Unified event stream
    std::vector<std::variant<TRDOSEvent, AYEvent, ULAEvent>> getAllEvents();
};
```

---

## Key Design Patterns

> [!IMPORTANT]
> **Immediate Aggregation**: RawEvents are processed on **frame boundaries** to prevent unbounded memory growth during long operations (e.g., FORMAT generating 665,600 port accesses).

> [!NOTE]
> **Stack Trace Capture**: Full call stack from user code → TR-DOS ROM → FDC access is captured to understand which routine initiated disk operations.

> [!TIP]
> **Generic Framework**: The `EventAnalyzer<TEvent, TContext>` template can be reused for AY-3-8910 sound analysis, ULA video analysis, or any other subsystem requiring semantic event capture.

---

## Example Output

```
[0001234] TR-DOS Entry (RST #8 from $8000)
[0001456] Command: LOAD "game"
[0002100] Reading catalog (Track 0, Sectors 1-8)
[0003500] File found: "game    " Type:B Length:12345 Start:T01/S10
[0003890] Loading BASIC module (12345 bytes, 49 sectors from T01/S10)
[0015234] BASIC loaded successfully
[0015300] Module loader detected: Starting CODE section
[0015890] Loading CODE module (24567 bytes, 96 sectors from T04/S01)
[0045670] CODE loaded to $8000
[0045780] *** Non-standard loader detected (entry: $8000) ***
[0050000] [CUSTOM] FDC: RESTORE
[0050500] [CUSTOM] FDC: SEEK Track 10
[0051000] [CUSTOM] Read sector T10/S01 (256 bytes)
```

---

## 7. WD1793 Observer Design Decision

### The Question

How should TRDOSAnalyzer receive FDC events? Two options:

### Option A: Mix with WD1793Collector ❌ Not Recommended

**Approach**: Extend existing `WD1793Collector` to support callbacks.

**Pros**:
- No new files
- Collector already has FDC access

**Cons**:
| Issue | Impact |
|:------|:-------|
| Single responsibility violation | Collector becomes storage + dispatch |
| Coupling | All analyzers depend on Collector, not just interface |
| Unbounded storage | `std::vector` grows forever, no FIFO |
| Single consumer | Only one analyzer at a time |
| Not thread-safe | UI/API queries would race with emulation |

### Option B: Separate `IWD1793Observer` Interface ✅ Recommended

**Approach**: Create observer interface, WD1793 maintains observer list.

**Pros**:
| Benefit | Impact |
|:--------|:-------|
| Clean separation | Observer pattern decoupled from storage |
| Multiple observers | Any number of analyzers can subscribe |
| Reusable | Future analyzers (format detector, protection scanner) use same interface |
| Own storage | TRDOSAnalyzer uses `RingBuffer<T>`, Collector unchanged |
| Testable | Mock observer for unit tests |

**Cons**:
- One new small file (~30 LOC)

### Implementation

#### IWD1793Observer Interface

```cpp
// core/src/emulator/io/fdc/iwd1793observer.h
#pragma once
#include <cstdint>

class WD1793;

class IWD1793Observer {
public:
    virtual ~IWD1793Observer() = default;
    
    /// Called when FDC command is issued (port $1F write)
    virtual void onFDCCommand(uint8_t command, const WD1793& fdc) {}
    
    /// Called on any FDC port access
    virtual void onFDCPortAccess(uint8_t port, uint8_t value, bool isWrite, const WD1793& fdc) {}
    
    /// Called when command completes (INTRQ raised)
    virtual void onFDCCommandComplete(uint8_t status, const WD1793& fdc) {}
};
```

#### WD1793 Changes (~15 LOC)

```cpp
// In wd1793.h, add:
#include "iwd1793observer.h"

// Add to class WD1793:
private:
    std::vector<IWD1793Observer*> _observers;

public:
    void addObserver(IWD1793Observer* obs) { 
        _observers.push_back(obs); 
    }
    
    void removeObserver(IWD1793Observer* obs) { 
        _observers.erase(
            std::remove(_observers.begin(), _observers.end(), obs), 
            _observers.end()
        ); 
    }

// In processWD93Command(), after existing collector call:
for (auto* obs : _observers) {
    obs->onFDCCommand(value, *this);
}

// In command completion path (raiseIntrq or S_END_COMMAND):
for (auto* obs : _observers) {
    obs->onFDCCommandComplete(_statusRegister, *this);
}

// In port access handlers:
for (auto* obs : _observers) {
    obs->onFDCPortAccess(port, value, isWrite, *this);
}
```

#### TRDOSAnalyzer Usage

```cpp
class TRDOSAnalyzer : public IAnalyzer, public IWD1793Observer {
public:
    void onActivate(AnalyzerManager* mgr) override {
        // Subscribe to FDC events
        _fdc->addObserver(this);
        
        // Also set breakpoints for TR-DOS ROM
        mgr->requestExecutionBreakpoint(0x3D00, getUUID());  // TR-DOS entry
        mgr->requestExecutionBreakpoint(0x3D21, getUUID());  // Command dispatch
        mgr->requestExecutionBreakpoint(0x0077, getUUID());  // Exit
    }
    
    void onDeactivate() override {
        _fdc->removeObserver(this);
        // Breakpoints auto-cleaned by AnalyzerManager
    }
    
    // IWD1793Observer implementation
    void onFDCCommand(uint8_t cmd, const WD1793& fdc) override {
        // Process FDC command, update state machine
        _rawBuffer.push(RawEvent{...});
    }
    
    void onFDCCommandComplete(uint8_t status, const WD1793& fdc) override {
        // Aggregate raw events → emit semantic event
        aggregateAndEmit();
    }
};
```

### File Structure

```
core/src/emulator/io/fdc/
├── wd1793.h                    # Add observer list + methods
├── wd1793.cpp                  # Call observers on events
├── wd1793_collector.h          # UNCHANGED (legacy dump)
├── wd1793_collector.cpp        # UNCHANGED
└── iwd1793observer.h           # NEW (~30 LOC)

core/src/debugger/analyzers/trdos/
├── trdosanalyzer.h             # Implements IAnalyzer + IWD1793Observer
├── trdosanalyzer.cpp           # State machine, event aggregation
├── trdosevent.h                # TRDOSEvent types
└── trdoscontext.h              # AnalyzerContext state

core/src/common/
└── ringbuffer.h                # Thread-safe ring buffer
```

### Relationship Diagram

```mermaid
graph TD
    subgraph "Emulator Core"
        WD1793[WD1793 FDC]
        Collector[WD1793Collector<br/>Legacy dump]
    end
    
    subgraph "Observer Pattern"
        IWD1793Obs[IWD1793Observer<br/>interface]
    end
    
    subgraph "Analyzers"
        TRDOSAna[TRDOSAnalyzer]
        FutureAna[Future Analyzers<br/>FormatDetector, etc.]
    end
    
    WD1793 -->|owns| Collector
    WD1793 -->|notifies| IWD1793Obs
    IWD1793Obs <|.. TRDOSAna
    IWD1793Obs <|.. FutureAna
```

---

## 8. Infrastructure Status

| Component | Status | Location |
|:----------|:-------|:---------|
| `IAnalyzer` | ✅ Exists | [ianalyzer.h](core/src/debugger/analyzers/ianalyzer.h) |
| `AnalyzerManager` | ✅ Exists | [analyzermanager.h](core/src/debugger/analyzers/analyzermanager.h) |
| Reference impl | ✅ Exists | [ROMPrintDetector](core/src/debugger/analyzers/rom-print/romprintdetector.h) |
| `IWD1793Observer` | ⚠️ Create | `core/src/emulator/io/fdc/iwd1793observer.h` |
| `RingBuffer<T>` | ⚠️ Create | `core/src/common/ringbuffer.h` |
| `TRDOSAnalyzer` | ⚠️ Create | `core/src/debugger/analyzers/trdos/` |

---

## Next Steps

1. **Phase 0: Infrastructure** (~200 LOC)
   - [ ] Create `IWD1793Observer` interface
   - [ ] Add observer support to `WD1793`
   - [ ] Create `RingBuffer<T>` helper

2. **Phase 1: Core Analyzer** (~500 LOC)
   - [ ] Create `TRDOSAnalyzer` implementing `IAnalyzer` + `IWD1793Observer`
   - [ ] Implement state machine (7 states)
   - [ ] Implement event aggregation

3. **Phase 2: Integration**
   - [ ] Register with `AnalyzerManager`
   - [ ] Add CLI commands
   - [ ] Add WebAPI endpoints

4. **Phase 3: Verification**
   - [ ] Test with `trdos-snapshot.z80` + `LOAD "test"`
   - [ ] Test with games using custom loaders (Elite, Turbo Out Run)
   - [ ] Test FORMAT operation
   - [ ] Test disk protection detection

