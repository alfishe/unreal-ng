# Diagnostic Observability for Port Decoding & Peripheral Emulation

## Problem Statement

Diagnosing port decoding and peripheral emulation bugs currently relies on two extremes:
1. **ModuleLogger** — modular, filterable, but text-based and slow on the hot path. Every `MLOGINFO` inside `DecodePortIn`/`DecodePortOut` calls `StringHelper::Format`, allocates `std::string`, and routes through the observer chain. On a 3.5 MHz emulated CPU executing ~70,000 instructions/frame × 50 fps ≈ 3.5M instructions/sec, even a guard-checked log macro on the IN/OUT path (≈5-15% of instructions) creates measurable overhead.
2. **MemoryAccessTracker** counters — efficient (atomic uint32_t increments), but store only aggregate hit-counts per port. No temporal ordering, no value history, no correlation between IN and OUT, no machine-state context.

Neither tool answers the core diagnostic questions for the two dominant bug classes:

| Bug Class | Example (Recent) | What You Need |
|---|---|---|
| **Port decode error** | SOUNDRIVE F1/F9 falsely decoded as 7FFD; Beta128 gate not blocking FDC when CF_TRDOS clear | Full 16-bit address → decoded port mapping, with the decoding rule that fired |
| **Peripheral behavior error** | TurboSound AY1 chip-select dropped; Ghost Byte on WD1793 #7F | Ordered sequence of (port, value, direction, T-state, PC) showing the peripheral saw two reads or wrong chip got data |

---

## Current Infrastructure Audit

### What exists and works well

| Component | Location | Strengths | Limitations |
|---|---|---|---|
| `ModuleLogger` | `common/modulelogger.h` | Per-module/submodule filtering (12 modules × 16 submodules), level gating (`LogTrace`→`LogError`), observer pattern, mute-per-port | String formatting on hot path; disabled under `_CODE_UNDER_TEST`; no structured output; no ring buffer — unbounded growth or loss |
| `MemoryAccessTracker` | `memory/memoryaccesstracker.h` | Aggregate counters (R/W/X per Z80 addr + physical page), `MonitoredPort` with caller tracking, session lifecycle (`Stopped`/`Capturing`/`Paused`) | Counters only — no event sequence; no T-state; no decoded-vs-raw port distinction; port tracking is a secondary feature bolted onto a memory-focused tool |
| `OnPortInComplete` / `OnPortOutComplete` | `portdecoder.cpp` L142-254 | Universal notification point called by all subclasses after hardware I/O; already has `port`, `result`/`value`, `pc`; hooks breakpoints + tracker + TTD probe | Currently only does breakpoints and counter increment — no structured event capture |
| `RingBuffer<T>` | `common/ringbuffer.h` | Thread-safe (shared_mutex), FIFO eviction, `getSince(timestamp)`, `getAll()`, `getRange()` | Ready to use but not wired to port events |
| TR-DOS Analyzer | (analyzer framework) | Full Layer-1/Layer-2 architecture with ring buffers, semantic aggregation, session model | Domain-specific to TR-DOS; not generalizable for ad-hoc port debugging |
| TTD IO Journal | `portdecoder.cpp` L237-254 | Records IO writes via `RecordIoWrite` + access probe matching | Write-only; separate system; not queryable for ad-hoc diagnostics |

### What's missing

1. **Structured port event stream** — an ordered ring buffer of `{timestamp, raw_port, decoded_port, value, direction, pc, decode_rule_id}` that can be queried without text parsing
2. **Decode-rule attribution** — when a port *does* decode, which rule in the `portMasksMatches[]` table matched? When it *doesn't*, was it the Beta128 gate, the `_loggingMutePorts` filter, or genuinely unmapped?
3. **Peripheral response tracking** — did `PeripheralPortIn` find a handler? Did the handler return a value or was it the default 0xFF? Was `_lastPortDecoded` set?
4. **Lightweight conditional capture** — ability to arm/disarm port tracing for specific ports or address ranges without recompilation, like the TTD probe but broader
5. **Cross-model comparison** — no easy way to diff port decode behavior between Pentagon128 and Spectrum128 for the same program

---

## Proposal: Port Diagnostic Recorder (PDR)

### Design Principles

0. **Runtime feature gate, no compile-time flags** — gated exclusively by the FeatureManager feature `porttrace` (alias `pt`, `features.ini`, default off), like `kMemoryTracking`/`kCallTrace`/`kTimeTravel`. Feature off → recorder not instantiated, no buffer allocated. See `implementation_plan.md` § "Feature Gate".
1. **Zero-cost when off** — with the feature off, the hot path is a single cached-bool test + never-taken branch (same `UpdateFeatureCache` pattern as `Memory` and `TimeTravelManager`)
2. **Structured, not textual** — fixed-size POD event struct pushed to `RingBuffer<T>`; no `std::string`, no `StringHelper::Format`
3. **Hook into the existing universal handler** — `OnPortInComplete` / `OnPortOutComplete` already receive all needed data; add a 3-line capture block
4. **Queryable via existing transports** — CLI, WebAPI, Python, Lua — using the established analyzer/profiler retrieval patterns
5. **Complementary, not replacement** — ModuleLogger stays for human-readable narrative; PDR is for machine-parseable forensics

### Event Structure

> [!WARNING]
> **SUPERSEDED.** This early 20-byte sketch packs `decodeRuleIndex` into 3 flag bits (max 8 rules — decode tables will outgrow that) and lacks `deviceId`. The authoritative event struct is the 24-byte `PortTraceEvent` in `use-cases.md`, which uses a full `uint8_t decodeRuleIndex` and adds device attribution. Kept for historical context only.

```cpp
struct PortDiagnosticEvent
{
    uint64_t timestamp;      // Absolute T-state (frame_counter * tpf + cpu.t)
    uint32_t frameNumber;    // Emulator frame counter
    uint16_t rawPort;        // Full 16-bit address bus value seen by Z80
    uint16_t decodedPort;    // Port after model-specific decoding (0 = unmapped)
    uint16_t pc;             // Program counter of the IN/OUT instruction (m1_pc)
    uint8_t  value;          // Data byte read or written
    uint8_t  flags;          // Packed bitfield (see below)
    // flags layout:
    //   bit 0:    direction (0=IN, 1=OUT)
    //   bit 1:    wasDecoded (_lastPortDecoded)
    //   bit 2:    hadHandler (PortDevice* was found)
    //   bit 3:    wasBeta128Gated (Beta128 port blocked by !CF_TRDOS)
    //   bit 4:    wasLoggingMuted (port was in _loggingMutePorts)
    //   bits 5-7: decodeRuleIndex (0-7, index into portMasksMatches[]; 7 = BDI fallback)
};
// sizeof = 20 bytes → 4096 events = 80 KB
```

### Capture Points

> [!IMPORTANT]
> **Reality check (code audit)**: `OnPortInComplete`/`OnPortOutComplete` currently receive only the **raw** port (`portdecoder_pentagon128.cpp:194` passes `port`, not `decodedPort`). The decoded port, rule index, gate outcome, and handler attribution are locals inside each subclass's `DecodePortIn/Out`. The hooks therefore must be **extended** to receive a `PortDecodeDisposition` struct filled by the subclass — a mechanical edit across all 7 models. This keeps a single capture point; inline handlers contribute only a disposition flag and never push events themselves (one event per I/O operation is an invariant). See `implementation_plan.md`.

```cpp
// Extended hook signature; capture is one guarded call behind the cached
// runtime-feature flag (kPortTrace via UpdateFeatureCache):
void PortDecoder::OnPortOutComplete(uint16_t rawPort, uint8_t value, uint16_t pc,
                                    const PortDecodeDisposition& disp)
{
    // ... existing breakpoint / tracker / TTD logic ...
    if (_portTraceCache) [[unlikely]]
        _portTrace->record(/*isOut=*/true, rawPort, value, pc, disp);
}
```

> [!NOTE]
> The legacy base-class `DecodePortIn/Out` path calls `PeripheralPortIn/Out` directly without invoking the hooks (`portdecoder.cpp:128`). A Ghost-Byte double read through that path would be invisible to the PDR. It must be either retired or instrumented with a `viaLegacyBasePath` flag — see `implementation_plan.md`.

### Decode Rule Attribution

The `decodePort()` method in each subclass iterates `portMasksMatches[]` linearly. To capture *which rule matched*, the method can return a struct instead of a bare `uint16_t`:

```cpp
struct DecodeResult
{
    uint16_t port;      // Resolved port (0 = no match)
    uint8_t  ruleIndex; // Index in portMasksMatches[] that matched (0xFF = none/fallback)
};
```

This is a low-risk refactor: `decodePort()` is called exactly twice per I/O operation (once in `DecodePortIn`, once in `DecodePortOut`), both in the same subclass. The struct is 3 bytes and returned by value.

### Selective Arming

> [!WARNING]
> **SUPERSEDED.** This single-mode `FilterMode` API cannot express real diagnostic sessions ("only AY OUTs", "everything except FDC noise"). The authoritative filter design is the two-layer include/exclude system with compound rules in `recording-control.md`. Kept for historical context only.

```cpp
class PortDiagnosticRecorder
{
public:
    // Arm/disarm the recorder globally
    void arm();
    void disarm();
    bool isArmed() const { return _armed.load(std::memory_order_relaxed); }

    // Filter modes (applied when armed):
    void watchAllPorts();                         // Capture everything
    void watchPort(uint16_t decodedPort);         // Only this decoded port
    void watchPortRange(uint16_t lo, uint16_t hi);
    void watchPCRange(uint16_t pcLo, uint16_t pcHi); // Only I/O from this code region
    void watchUnmapped();                         // Only events where decodedPort==0

    // Retrieval
    std::vector<PortDiagnosticEvent> getAll() const;
    std::vector<PortDiagnosticEvent> getSince(uint64_t timestamp) const;
    size_t count() const;

private:
    std::atomic<bool> _armed{false};
    RingBuffer<PortDiagnosticEvent> _events{4096};

    // Filter state
    enum class FilterMode { All, Port, PortRange, PCRange, Unmapped };
    FilterMode _filterMode = FilterMode::All;
    uint16_t _filterPortLo = 0, _filterPortHi = 0xFFFF;
    uint16_t _filterPCLo = 0, _filterPCHi = 0xFFFF;
};
```

### Sizing and Performance

| Metric | Value |
|---|---|
| Ring buffer default | 1,048,576 events (24 MB), **configurable**. Rationale: ~3,500 port ops/frame unfiltered means the earlier 4096 default held barely one frame — useless for "capture everything during boot" workflows |
| Overflow modes | `ring` (evict oldest, default) or `stop-when-full` (keep the start of the run) |
| Per-event push cost | ~50 ns (struct copy + `shared_mutex` acquire; no allocation). **Not lock-free** — `RingBuffer<T>` takes a `unique_lock` per push; uncontended single-producer this is acceptable and the hot-path bench verifies it |
| Hot-path cost, feature off or not capturing | Single cached-bool test + never-taken branch (feature off additionally means no recorder instance, no buffer memory) |
| Worst-case overhead when armed | ~175 µs/frame at 3,500 port ops/frame (≈0.9% of 20ms frame) |

### Information Collected Per Bug Class

#### Port Decode Errors

| Diagnostic Question | PDR Field |
|---|---|
| What 16-bit address was on the bus? | `rawPort` |
| What did the model decode it to? | `decodedPort` |
| Which mask/match rule fired? | `flags[5:7]` (`decodeRuleIndex`) |
| Was the Beta128 gate involved? | `flags[3]` (`wasBeta128Gated`) |
| Where in the code did this happen? | `pc` |
| When in the frame? | `timestamp`, `frameNumber` |

**Workflow**: arm PDR with `watchPort(0x7FFD)`, run suspect program, dump events, check if any `rawPort` values that *should* decode to 0x7FFD are instead getting `decodedPort=0xFE` or `decodedPort=0x00FB` (COVOX).

#### Peripheral Behavior Errors

| Diagnostic Question | PDR Field |
|---|---|
| What sequence of reads/writes did the peripheral see? | Ordered event stream with `direction`, `value`, `timestamp` |
| Did the handler exist? | `flags[2]` (`hadHandler`) |
| Was the port actually decoded? | `flags[1]` (`wasDecoded`) |
| Is there a double-read (Ghost Byte)? | Two consecutive IN events to same `decodedPort` with ~11 T-state gap |
| Did chip-select arrive before data write? | Sequence of OUT events to 0xFFFD then 0xBFFD |

**Workflow**: arm PDR with `watchPort(0xFFFD)` + `watchPort(0xBFFD)`, run TurboSound test, verify OUT sequence shows `(FFFD, 0xFE)` → `(FFFD, 0x08)` → `(BFFD, 0x20)` with no interleaving.

---

## Proposal: Model-Specific Decode Verification Harness

Beyond runtime tracing, the existing port decode tests (e.g., `PortDecoder_Pentagon128_Test::IsPort_7FFD` which exhaustively checks all 65536 addresses) provide a pattern that should be systematized.

### Cross-Model Decode Matrix Test

A parameterized test fixture that:
1. Instantiates every `PortDecoder` subclass
2. Feeds all 65536 port addresses through `decodePort()`
3. Builds a 65536-entry decode map per model
4. Compares models pairwise to surface **decode divergences** — addresses that resolve differently across models

This directly catches the class of bug where Pentagon128 adds a new mask rule (e.g., SOUNDRIVE bit2 exclusion) that Spectrum128 or Scorpion256 doesn't have.

> [!NOTE]
> `decodePort()` is state-dependent in some models (TR-DOS flag, paging lock), so the matrix must run under **pinned machine states** — at minimum TR-DOS on and off, compared separately. Also, models *genuinely* differ by design, so expect the first run to be an audit exercise that produces the divergence allow-list, not a green test.

```cpp
TEST_P(CrossModelDecodeTest, CompareDecodeMap)
{
    auto [modelA, modelB] = GetParam();
    auto decoderA = PortDecoder::GetPortDecoderForModel(modelA, _context);
    auto decoderB = PortDecoder::GetPortDecoderForModel(modelB, _context);

    for (uint32_t port = 0; port <= 0xFFFF; port++)
    {
        uint16_t resultA = decoderA->decodePort(port);
        uint16_t resultB = decoderB->decodePort(port);

        if (resultA != resultB)
        {
            // Record divergence for post-test analysis
            _divergences.push_back({port, resultA, resultB});
        }
    }

    // Report divergences (expected divergences can be allow-listed)
    // ...
}
```

### Peripheral Registration Audit Test

A test that verifies every model's `reset()` + standard peripheral registration results in the expected handler map:

```cpp
TEST_P(PeripheralRegistrationTest, AllExpectedPortsHandled)
{
    auto decoder = PortDecoder::GetPortDecoderForModel(GetParam(), _context);
    decoder->reset();

    // For each known peripheral port, verify a handler exists
    for (uint16_t port : expectedPeripheralPorts)
    {
        uint8_t result = decoder->PeripheralPortIn(port);
        // If no handler, PeripheralPortIn logs a warning and returns 0xFF
        // Capture whether _lastPortDecoded is set
    }
}
```

---

## Proposal: Lightweight Port Activity Summary (Frame-Scoped)

For quick visual diagnostics in the debugger UI without the overhead of a full event ring buffer:

```cpp
struct PortActivitySummary
{
    // Per-frame rolling counters (reset each frame)
    uint16_t inCount;           // Total IN operations this frame
    uint16_t outCount;          // Total OUT operations this frame
    uint16_t unmappedInCount;   // IN to ports with no handler
    uint16_t unmappedOutCount;  // OUT to ports with no handler
    uint16_t beta128GatedCount; // Port accesses blocked by TR-DOS gate

    // Most-recent-N unique decoded ports (small fixed array)
    uint16_t recentDecodedPorts[8];
    uint8_t  recentPortCount;
};
```

This could live as a member of `PortDecoder` and be incremented in `OnPortInComplete`/`OnPortOutComplete` with no allocations. The debugger's port status panel reads it once per frame. Cost: ~6 counter increments per I/O operation.

---

## Implementation Phases

> [!NOTE]
> The authoritative phase breakdown lives in `implementation_plan.md`. Summary: **Phase 0/1** — runtime feature gate (FeatureManager `porttrace`), recorder core, extended `OnPort*Complete` hooks across all 7 models, `decodePort()` → `DecodeResult`, legacy-path resolution, TTD `RecordIoRead` symmetry fix, and the frame-scoped `PortActivitySummary` (promoted from Phase 4 — it's a 30-minute change with immediate payoff). **Phase 2** — CLI/WebAPI/Python/Lua transports + export. **Phase 3** — cross-model verification tests. **Phase 4** — offline converter tool, debugger UI, optional capture triggers.

---

## Relationship to Existing Tools

```
┌─────────────────────────────────────────────────────────────────┐
│                    Diagnostic Tool Stack                        │
├─────────────────────┬───────────────────────┬───────────────────┤
│   ModuleLogger      │  Port Diag Recorder   │  TR-DOS Analyzer  │
│   (Human narrative) │  (Structured events)  │  (Domain expert)  │
│                     │                       │                   │
│  MLOGINFO("[In]...  │  recordIn(port,val,pc │  Layer-1 FDC raw  │
│                     │    ,decoded,flags)    │  Layer-2 semantic  │
│  Pro: readable      │  Pro: fast, queryable │  Pro: deep domain │
│  Con: slow, noisy   │  Con: no semantics    │  Con: TR-DOS only │
├─────────────────────┴───────────────────────┴───────────────────┤
│                    OnPortInComplete / OnPortOutComplete          │
│                    (Universal notification point)               │
├─────────────────────────────────────────────────────────────────┤
│            MemoryAccessTracker (aggregate counters)             │
├─────────────────────────────────────────────────────────────────┤
│                 TTD IO Journal (reverse-debug)                  │
└─────────────────────────────────────────────────────────────────┘
```

The PDR fills the gap between "readable but slow" (ModuleLogger) and "fast but aggregate" (MemoryAccessTracker). It's not a replacement for either; it's the missing middle layer that enables answering "what happened in the last 4096 port operations?" without recompilation or performance cliffs.
