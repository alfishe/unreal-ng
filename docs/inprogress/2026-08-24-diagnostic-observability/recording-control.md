# Port Trace Recording Control: Filter Design

## The Problem

Simple "watch port X" filters are insufficient for real diagnostic sessions:

| Scenario | What user means | What current design supports |
|---|---|---|
| "Only AY-related OUTs" | Include `{FFFD, BFFD}` AND direction=OUT | ✗ Can watch ports but not filter direction |
| "Everything except FDC noise" | Exclude `{1F, 3F, 5F, 7F, FF}` | ✗ Only positive inclusion, no exclusion |
| "All OUTs, skip all INs" | Direction filter only | ✗ No direction filter at capture time |
| "AY chip-selects only" | Port=FFFD AND direction=OUT AND value ∈ {0xFE, 0xFF} | ✗ No value filter |
| "Only unmapped ports from ROM code" | decoded=false AND PC ∈ [0x0000, 0x3FFF] | ✗ Can't combine unmapped + PC range |
| "Start with everything, then narrow down" | Reconfigure filter while capturing | ✗ Current design requires stop/reconfigure/start |

---

## Design: Two-Layer Filter with Include/Exclude

### Mental Model

Think of it as two passes over each incoming event:

```
Event arrives
  │
  ▼
┌────────────────────┐     No include rules defined
│  INCLUDE filter    │──── means "include everything"
│  (whitelist)       │     
└────────┬───────────┘     
         │ matched
         ▼
┌────────────────────┐     No exclude rules defined
│  EXCLUDE filter    │──── means "exclude nothing"
│  (blacklist)       │     
└────────┬───────────┘
         │ not excluded
         ▼
    Record event
```

An event is recorded if:
1. It matches **at least one include rule** (or include list is empty = match all)
2. It matches **no exclude rules**

Exclude always wins over include — this prevents accidental capture of noise.

### Filter Predicates

Each filter rule tests one dimension of the event:

```cpp
enum class FilterDimension : uint8_t
{
    Port,       // Match on decoded port value
    Device,     // Match on device ID enum
    Direction,  // Match on IN or OUT
    PCRange,    // Match on PC within [lo, hi]
    RawPort,    // Match on raw (undecoded) port value
    Unmapped,   // Match events where decoded==false
    Value,      // Match on data value (exact or mask)
    ValueRange  // Match on data value within [lo, hi]
};

struct FilterRule
{
    FilterDimension dimension;
    
    union {
        uint16_t port;                      // For Port, RawPort
        PortDeviceId deviceId;              // For Device
        uint8_t  direction;                 // For Direction: 0=IN, 1=OUT
        struct { uint16_t lo, hi; } range;  // For PCRange, ValueRange
        struct { uint8_t val, mask; } vm;   // For Value: (val & mask) == (event.value & mask)
    };
};
```

### C++ API

```cpp
class PortDiagnosticRecorder
{
public:
    // ── Include Rules (whitelist — empty = match all) ──
    void includePort(uint16_t decodedPort);
    void includeDevice(PortDeviceId deviceId);
    void includeDirection(uint8_t dir);             // 0=IN, 1=OUT
    void includePCRange(uint16_t lo, uint16_t hi);
    void includeUnmapped();
    
    // ── Exclude Rules (blacklist — empty = exclude nothing) ──
    void excludePort(uint16_t decodedPort);
    void excludeDevice(PortDeviceId deviceId);
    void excludeDirection(uint8_t dir);
    void excludePCRange(uint16_t lo, uint16_t hi);
    
    // ── Filter Management ──
    void clearIncludeRules();
    void clearExcludeRules();
    void clearAllRules();                           // Reset to "capture everything"
    
    // ── Live Reconfiguration ──
    // All filter methods are safe to call while capturing.
    // New rules take effect on the next event (no buffer flush).
    // This uses a simple spinlock-free swap: the recorder reads
    // a pointer to an immutable FilterSet; reconfiguration builds
    // a new FilterSet and atomically swaps the pointer.
    
    // ── Convenience Presets ──
    void presetAll();                               // Clear all rules (capture everything)
    void presetAYOnly();                            // Include FFFD+BFFD, exclude nothing
    void presetFDCOnly();                           // Include 1F+3F+5F+7F+FF
    void presetNoFDC();                             // Include all, exclude 1F+3F+5F+7F+FF
    void presetOutsOnly();                          // Include direction=OUT
    void presetInsOnly();                           // Include direction=IN
};
```

### Filter Evaluation (Hot Path)

```cpp
bool FilterSet::matches(const PortTraceEvent& event) const
{
    // Step 1: Include check (empty include list = match all)
    if (!_includeRules.empty())
    {
        bool anyIncludeMatch = false;
        for (const auto& rule : _includeRules)
        {
            if (ruleMatches(rule, event))
            {
                anyIncludeMatch = true;
                break;  // Short-circuit: one match is enough
            }
        }
        if (!anyIncludeMatch) return false;
    }
    
    // Step 2: Exclude check (any exclude match = reject)
    for (const auto& rule : _excludeRules)
    {
        if (ruleMatches(rule, event))
            return false;
    }
    
    return true;
}
```

The `FilterSet` is a small struct (typically <10 rules, each 8 bytes). The evaluation is a tight loop with no allocation. Inline rules are fixed-size comparisons — branch predictor handles this well.

### Performance Note

Filter evaluation adds ~5-20 ns per event on the hot path (when armed). For the common case of 1-3 rules this is dominated by the first comparison. The `std::atomic<FilterSet*>` swap for live reconfiguration has no cost during evaluation — it's a single pointer load per event.

---

## CLI Commands

```
port-trace include port FFFD             Add decoded port to include list
port-trace include port BFFD             
port-trace include device AY_FFFD        Add device to include list
port-trace include direction out         Only OUTs
port-trace include unmapped              Only unmapped ports
port-trace include pc 3D00-3FFF          Only I/O from this code region

port-trace exclude port 001F             Exclude FDC status port
port-trace exclude device WD1793_Data    Exclude all WD1793 data register events  
port-trace exclude direction in          Skip all INs

port-trace filter clear                  Reset to "capture everything"
port-trace filter clear includes         Reset only include rules
port-trace filter clear excludes         Reset only exclude rules
port-trace filter show                   Show current filter configuration

port-trace preset all                    Capture everything
port-trace preset ay-only                Include FFFD+BFFD only
port-trace preset fdc-only               Include FDC ports only
port-trace preset no-fdc                 Exclude FDC ports
port-trace preset outs-only              Include direction=OUT only
```

### CLI Filter Display (`port-trace filter show`)

```
Port Trace Filter Configuration
════════════════════════════════
Include rules (event must match at least one):
  1. Port = 0xFFFD
  2. Port = 0xBFFD

Exclude rules (event must match none):
  (none)

Effective: Capture IN and OUT on ports 0xFFFD, 0xBFFD
```

Or a more complex setup:

```
Port Trace Filter Configuration
════════════════════════════════
Include rules (event must match at least one):
  1. Direction = OUT

Exclude rules (event must match none):
  1. Device = WD1793_Status
  2. Device = WD1793_Track
  3. Device = WD1793_Sector
  4. Device = WD1793_Data
  5. Device = Beta128_System

Effective: Capture all OUTs except FDC/Beta128 ports
```

---

## Python API

```python
import unreal_emulator as emu

# Scenario 1: Only AY-related OUTs
emu.porttrace_include_port(0xFFFD)
emu.porttrace_include_port(0xBFFD)
emu.porttrace_include_direction("out")
emu.porttrace_start()

# Scenario 2: Everything except FDC noise
emu.porttrace_exclude_device("WD1793_Status")
emu.porttrace_exclude_device("WD1793_Track")
emu.porttrace_exclude_device("WD1793_Sector")
emu.porttrace_exclude_device("WD1793_Data")
emu.porttrace_exclude_device("Beta128_System")
emu.porttrace_start()

# Scenario 3: Use a preset, then customize
emu.porttrace_preset("no-fdc")           # Exclude all FDC
emu.porttrace_exclude_port(0x00FE)       # Also skip ULA
emu.porttrace_start()

# Scenario 4: Start broad, then narrow without losing data
emu.porttrace_start()                    # Capture everything
# ... observe high volume ...
# Narrow down WHILE still capturing:
emu.porttrace_include_port(0xFFFD)       # Now only FFFD events are recorded
# ... captured events from before the filter change are PRESERVED ...

# Scenario 5: Value filter — only AY chip-select commands
emu.porttrace_include_port(0xFFFD)
emu.porttrace_include_direction("out")
# (value filtering in post-processing via porttrace_convert.py is preferred
#  over capture-time value filters for most cases — see rationale below)

# Clear everything
emu.porttrace_filter_clear()

# Inspect current filter
print(emu.porttrace_filter_show())
```

### Lua API (identical semantics)

```lua
emu.porttrace_include_port(0xFFFD)
emu.porttrace_include_port(0xBFFD)
emu.porttrace_exclude_direction("in")
emu.porttrace_start()
```

---

## WebAPI

```bash
# Set filter via structured JSON
curl -X POST http://localhost:8090/api/v1/emulator/$ID/profiler/porttrace/filter \
  -H "Content-Type: application/json" \
  -d '{
    "include": [
      {"type": "port", "value": "0xFFFD"},
      {"type": "port", "value": "0xBFFD"},
      {"type": "direction", "value": "out"}
    ],
    "exclude": [
      {"type": "device", "value": "WD1793_Data"}
    ]
  }'

# Use a preset
curl -X POST .../profiler/porttrace/filter \
  -d '{"preset": "no-fdc"}'

# Get current filter
curl http://localhost:8090/api/v1/emulator/$ID/profiler/porttrace/filter
# Returns the same JSON structure
```

---

## Scenario Walkthrough Table

| Scenario | Include Rules | Exclude Rules | Result |
|---|---|---|---|
| **Capture everything** | (empty) | (empty) | All IN+OUT recorded |
| **Only AY OUTs** | port=FFFD, port=BFFD, dir=OUT | (empty) | ⚠️ This is wrong — see below |
| **Only AY OUTs (correct)** | (empty) | dir=IN, all non-AY devices | All OUTs to AY ports |
| **Everything except FDC** | (empty) | device=WD1793_* | All I/O except FDC |
| **All OUTs** | dir=OUT | (empty) | All OUT operations |
| **Unmapped from ROM** | unmapped=true, pc=0000-3FFF | (empty) | Events decoded=false from ROM |
| **AY chip-selects only** | port=FFFD, dir=OUT | (empty) | OUT to FFFD (includes reg selects AND chip-selects) |

### Important: Include Rule Interaction

Include rules use **OR** logic (match *any* one). This means:

```
include port=FFFD
include direction=OUT
```

Captures: **any event on port FFFD (IN or OUT)** + **any OUT on any port**. This is probably not what the user intended.

To express "OUTs to FFFD only", you need **compound include rules** or a two-step approach:

**Option A: Compound rule (AND within a single include entry)**
```
include { port=FFFD AND direction=OUT }
```

**Option B: Include broad, exclude narrow (simpler)**
```
include port=FFFD
exclude direction=IN
```

Option B is simpler to implement and covers the most common cases. Option A requires adding a `CompoundFilterRule` type. 

### Recommendation: Support both simple and compound

```cpp
struct FilterRule
{
    // Simple: single dimension
    FilterDimension dimension;
    // ... value union ...
};

struct CompoundFilterRule
{
    // Compound: ALL dimensions must match
    std::vector<FilterRule> conditions;  // AND logic within
};

// Include list uses CompoundFilterRule:
//   Event matches include list if ANY CompoundFilterRule matches
//   A CompoundFilterRule matches if ALL its conditions match
std::vector<CompoundFilterRule> _includeRules;  // OR of ANDs
std::vector<FilterRule> _excludeRules;          // OR (any exclude = reject)
```

CLI compound syntax:
```
port-trace include port FFFD direction out       # Single compound rule: FFFD AND OUT
port-trace include port BFFD direction out       # Another compound rule: BFFD AND OUT
```

Python compound syntax:
```python
emu.porttrace_include(port=0xFFFD, direction="out")   # kwargs = AND
emu.porttrace_include(port=0xBFFD, direction="out")   # separate call = OR
```

WebAPI compound syntax:
```json
{
  "include": [
    {"port": "0xFFFD", "direction": "out"},
    {"port": "0xBFFD", "direction": "out"}
  ]
}
```

---

## Live Reconfiguration

All filter changes apply **immediately** to the next event without stopping the capture. Previously recorded events are never removed — only future events are affected.

```
> port-trace start                          # Start with no filters (everything)
  ... 200 events captured ...
> port-trace include port FFFD              # NOW only FFFD events are recorded
  ... 50 more events (only FFFD) ...
> port-trace filter clear includes          # Back to everything
  ... 100 more events (all ports) ...
> port-trace stop
  Total: 350 events (mixed filtering)
```

This is safe because filter reconfiguration uses atomic pointer swap:
```cpp
void reconfigureFilter(FilterSet* newFilter)
{
    FilterSet* old = _activeFilter.exchange(newFilter, std::memory_order_acq_rel);
    // old is deleted after a grace period (or immediately if no capture in progress)
}
```

The capture hot path reads the pointer once per event:
```cpp
void onEvent(const PortTraceEvent& event)
{
    FilterSet* filter = _activeFilter.load(std::memory_order_acquire);
    if (filter->matches(event))
        _ringBuffer.push(event);
}
```

---

## Presets

Presets are convenience macros that configure include/exclude lists for common scenarios. They can be the starting point, then customized with additional include/exclude calls.

| Preset | Include Rules | Exclude Rules | Use Case |
|---|---|---|---|
| `all` | (empty) | (empty) | Capture everything |
| `ay-only` | port=FFFD, port=BFFD | (empty) | Debug AY/TurboSound |
| `fdc-only` | device=WD1793_*, device=Beta128_System | (empty) | Debug disk operations |
| `no-fdc` | (empty) | device=WD1793_*, device=Beta128_System | Everything except disk noise |
| `no-fe` | (empty) | port=00FE | Skip high-frequency ULA reads |
| `outs-only` | dir=OUT | (empty) | Only writes to peripherals |
| `ins-only` | dir=IN | (empty) | Only reads from peripherals |
| `unmapped` | unmapped=true | (empty) | Find missing port handlers |
| `sound` | device=AY_FFFD, device=AY_BFFD, device=Covox | (empty) | All sound-related I/O |
| `paging` | device=Memory_7FFD, device=Memory_1FFD | (empty) | Memory paging activity |

Presets clear all existing rules before applying. Additional include/exclude calls after a preset **add to** the preset's rules.

```python
emu.porttrace_preset("no-fdc")       # Exclude FDC
emu.porttrace_exclude_port(0x00FE)   # Also exclude ULA
emu.porttrace_start()                # Capture sound + paging + misc
```
