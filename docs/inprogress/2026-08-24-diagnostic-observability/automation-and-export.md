# Port Access Trace: Automation Access and Export Design

> [!NOTE]
> The whole subsystem is gated at runtime by the FeatureManager feature `porttrace` (`features.ini`, alias `pt`; no compile-time flags — see `implementation_plan.md` § "Feature Gate"). Transports are always compiled in and always register their commands; when the feature is off, all commands report "porttrace feature is disabled — enable with `feature porttrace on`" and no recorder is instantiated.

## 1. Core API (C++ Layer)

The `PortDiagnosticRecorder` lives in the core and provides a POD-returning API consumed by all automation transports. Following the "Common Controller" rule, all business logic is here — transports are thin proxies.

### 1.1 Session Lifecycle

```cpp
class PortDiagnosticRecorder
{
public:
    // ── Session Control ──
    void start();                   // Arm capture, clear buffer
    void stop();                    // Disarm capture, preserve data
    void pause();                   // Suspend capture, preserve data
    void resume();                  // Resume capture from paused
    void clear();                   // Purge buffer (any state)

    // ── Buffer Configuration (only while stopped) ──
    void setCapacity(size_t events);            // Default 1,048,576 (24 MB)
    enum class OverflowMode : uint8_t { Ring, StopWhenFull };
    void setOverflowMode(OverflowMode mode);    // Ring = evict oldest (default);
                                                // StopWhenFull = keep the start of the run
                                                // (boot-sequence debugging), auto-stop when full

    // ── Filter Configuration ──
    // Two-layer include/exclude with compound rules — see recording-control.md.
    // (An earlier single-mode watchPort()/watchDevice() API is superseded.)

    // ── Status ──
    ProfilerSessionState getSessionState() const;
    bool isCapturing() const;
    size_t eventCount() const;
    size_t capacity() const;
    uint64_t totalProduced() const;
    uint64_t totalEvicted() const;

    // ── Retrieval (POD returns, no formatting) ──
    std::vector<PortTraceEvent> getAll() const;
    std::vector<PortTraceEvent> getSince(uint64_t timestamp) const;
    std::vector<PortTraceEvent> getRange(size_t start, size_t count) const;
    std::vector<PortTraceEvent> getLast(size_t count) const;

    // ── Persistence ──
    // Session metadata is assembled by PortDecoder::getPortTraceSessionInfo()
    // (the recorder has no emulator-context knowledge) and passed in:
    bool saveToFile(const std::string& path, PortTraceExportFormat format,
                    const PortTraceSessionInfo& info) const;
    std::string describeFilter() const;   // Human-readable one-line filter description
};

enum class PortTraceExportFormat : uint8_t
{
    JSON,       // Structured, machine-readable
    CSV,        // Flat tabular, spreadsheet-friendly
    Binary      // Raw POD dump (sizeof(PortTraceEvent) × N), fastest save/load
};

// As implemented (portdiagrecorder.h). Session time bounds are derived from the
// first/last buffered events at read time rather than stored; counters
// (capacity, produced/evicted/filtered, overflow mode, filter description) come
// from the recorder itself at save time.
struct PortTraceDecodeRule { uint16_t mask; uint16_t match; uint16_t port; };

struct PortTraceSessionInfo
{
    std::string emulatorId;         // Emulator UUID
    std::string modelName;          // "Pentagon", "Spectrum128", ...
    uint32_t    tStatesPerFrame;    // Timing base used for absolute timestamps

    // Self-describing traces: the model's decode-rule table at capture time
    // (PortDecoder::getPortTraceDecodeRules(); Pentagon128 exports its
    // mask/match table, if-chain decoder models export an empty table),
    // so decodeRuleIndex values in events resolve without hardcoding masks
    // in offline tools.
    std::vector<PortTraceDecodeRule> decodeRules;
};

// Assembled by PortDecoder::getPortTraceSessionInfo(); passed to
// PortDiagnosticRecorder::saveToFile(path, format, info).
```

### 1.2 Resolution Chain

```
EmulatorContext
  └─> pPortDecoder
        └─> _portTrace (std::unique_ptr<PortDiagnosticRecorder>, nullptr while feature off)
```

Access from automation (nullptr while the porttrace feature is off):
```cpp
auto* recorder = context->pPortDecoder->getPortTraceRecorder();
auto  info     = context->pPortDecoder->getPortTraceSessionInfo();
```

---

## 2. CLI Interface

Following the Subcommand Dispatch Pattern (§4.3 of Automation Architecture), registered as `port-trace` command family.

### 2.1 Commands

```
port-trace start                      Start capturing (clears buffer)
port-trace stop                       Stop capturing (data preserved)
port-trace pause                      Pause capture
port-trace resume                     Resume capture
port-trace clear                      Clear buffer

port-trace config capacity <n>        Set ring buffer capacity (default 1048576; only while stopped)
port-trace config overflow ring|stop  ring = evict oldest; stop = keep start of run, auto-stop when full

# Filtering uses the include/exclude command family from recording-control.md
# (the earlier "watch ..." single-mode commands are superseded):
port-trace include port FFFD direction out    Compound include rule (AND within one rule)
port-trace exclude device WD1793_Data         Exclude rule
port-trace preset <name>                      ay-only / fdc-only / no-fdc / unmapped / ...
port-trace filter show|clear [includes|excludes]

port-trace status                     Show session state, event count, filter

port-trace dump [N]                   Dump last N events to terminal (default 32)
port-trace save <path> [json|csv|bin] Save to file (default json)
```

Alias: `porttrace` dispatches to the same handler.

### 2.2 Terminal Output Format

```
Port Access Trace (4096 capacity, 847 events, capturing)
Filter: Port 0xFFFD, 0xBFFD
═══════════════════════════════════════════════════════════════════════════════════
 #    Frame  T-State        Dir  Raw    Decoded  Value  PC     Device          Flags
───  ──────  ─────────────  ───  ─────  ───────  ─────  ─────  ──────────────  ──────
  1   00312  0000000A'F830  OUT  FFFD   FFFD     FE     3E42   AY_FFFD         DH
  2   00312  0000000A'F83B  OUT  FFFD   FFFD     08     3E47   AY_FFFD         DH
  3   00312  0000000A'F846  OUT  BFFD   BFFD     20     3E4C   AY_BFFD         DH
  4   00312  0000000A'F860  OUT  FFFD   FFFD     FF     3E42   AY_FFFD         DH
  5   00312  0000000A'F86B  OUT  FFFD   FFFD     08     3E47   AY_FFFD         DH
  6   00312  0000000A'F876  OUT  BFFD   BFFD     10     3E4C   AY_BFFD         DH

Flags: D=decoded  H=hadHandler  G=beta128Gated  I=handledInline
```

### 2.3 Implementation Location

```
core/automation/cli/src/commands/cli-processor-porttrace.cpp
```

Follows pattern of `cli-processor-profiler.cpp`.

---

## 3. WebAPI Interface

RESTful endpoints under the profiler namespace, consistent with existing opcode profiler API.

### 3.1 Endpoints

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/api/v1/emulator/{id}/profiler/porttrace/start` | Start capture session |
| `POST` | `/api/v1/emulator/{id}/profiler/porttrace/stop` | Stop capture |
| `POST` | `/api/v1/emulator/{id}/profiler/porttrace/pause` | Pause capture |
| `POST` | `/api/v1/emulator/{id}/profiler/porttrace/resume` | Resume capture |
| `POST` | `/api/v1/emulator/{id}/profiler/porttrace/clear` | Clear buffer |
| `GET`  | `/api/v1/emulator/{id}/profiler/porttrace/status` | Session state + stats |
| `GET`  | `/api/v1/emulator/{id}/profiler/porttrace/events` | Retrieve events |
| `GET`/`POST` | `/api/v1/emulator/{id}/profiler/porttrace/filter` | Get description / set include-exclude filter wholesale (JSON rules or `{"preset": "name"}`) |
| `POST` | `/api/v1/emulator/{id}/profiler/porttrace/config` | Set capacity and/or overflow mode (only while stopped; current values in the response) |
| `POST` | `/api/v1/emulator/{id}/profiler/porttrace/save` | Save to file on server |

### 3.2 Event Retrieval Parameters

`GET .../events` query parameters:
- `limit` — max events to return (default: 0 = all). **Must default to unlimited per Retrieval Depth Mandate (Hazard #136).**
- `since` — T-state timestamp filter
- `offset` / `count` — range-based retrieval

### 3.3 JSON Response Schema

```json
{
  "session": {
    "state": "capturing",
    "emulator_id": "a1b2c3d4-...",
    "model": "Pentagon128",
    "filter": "Port 0xFFFD, 0xBFFD",
    "total_events": 847,
    "capacity": 4096,
    "evicted": 0,
    "start_frame": 312,
    "start_tstate": 180000000
  },
  "events": [
    {
      "index": 0,
      "timestamp": 180027440,
      "frame": 312,
      "raw_port": "0xFFFD",
      "decoded_port": "0xFFFD",
      "decode_rule": 0,
      "value": "0xFE",
      "pc": "0x3E42",
      "direction": "OUT",
      "device": "AY_FFFD",
      "decoded": true,
      "had_handler": true,
      "beta128_gated": false,
      "handled_inline": false
    }
  ]
}
```

> [!IMPORTANT]
> Port addresses and values use hex strings in JSON for human readability, but the native `PortTraceEvent` struct uses numeric types. The WebAPI serializer handles the conversion. Python/Lua bindings return native integers.

### 3.4 Implementation Location

```
core/automation/webapi/src/api/porttrace_api.cpp
core/automation/webapi/src/openapi/openapi_porttrace.inc
```

---

## 4. Python Bindings (pybind11)

Native bindings via `pybind11` in the embedded `unreal_emulator` module. Follows the Direct Binding Mandate — no WebAPI round-trip.

### 4.1 API Surface

```python
import unreal_emulator as emu

# Session lifecycle
emu.porttrace_start()
emu.porttrace_stop()
emu.porttrace_pause()
emu.porttrace_resume()
emu.porttrace_clear()

# Buffer configuration (only while stopped)
emu.porttrace_set_capacity(65536)
emu.porttrace_set_overflow("ring")       # or "stop" (keep start of run)

# Filtering — include/exclude API with compound kwargs, per recording-control.md
# (the earlier single-mode porttrace_watch_* API is superseded)
emu.porttrace_include(port=0xFFFD, direction="out")   # kwargs = AND
emu.porttrace_include(port=0xBFFD, direction="out")   # separate call = OR
emu.porttrace_exclude(device="WD1793_Data")
emu.porttrace_preset("no-fdc")
emu.porttrace_filter_clear()

# Status
status = emu.porttrace_status()
# Returns: { "state": "capturing", "events": 847, "capacity": 4096, ... }

# Retrieval — returns list of PortTraceEvent objects
events = emu.porttrace_events()           # all
events = emu.porttrace_events_last(100)   # last N
events = emu.porttrace_events_since(ts)   # since T-state

# Each event has typed attributes:
for e in events:
    print(f"T={e.timestamp} {e.direction} port={e.raw_port:#06x} -> {e.decoded_port:#06x} "
          f"val={e.value:#04x} PC={e.pc:#06x} dev={e.device}")

# Save to file (server-side)
emu.porttrace_save("/tmp/trace.json", "json")
emu.porttrace_save("/tmp/trace.csv", "csv")
emu.porttrace_save("/tmp/trace.bin", "bin")
```

### 4.2 Event Object Binding

```cpp
// In python_emulator.h
py::class_<PortTraceEvent>(m, "PortTraceEvent")
    .def_readonly("timestamp",      &PortTraceEvent::timestamp)
    .def_readonly("frame",          &PortTraceEvent::frameNumber)
    .def_readonly("raw_port",       &PortTraceEvent::rawPort)
    .def_readonly("decoded_port",   &PortTraceEvent::decodedPort)
    .def_readonly("decode_rule",    &PortTraceEvent::decodeRuleIndex)
    .def_readonly("value",          &PortTraceEvent::value)
    .def_readonly("pc",             &PortTraceEvent::pc)
    .def_readonly("device",         &PortTraceEvent::deviceId)  // enum -> string via property
    .def_readonly("direction",      &PortTraceEvent::direction) // extracted from flags
    .def_readonly("decoded",        &PortTraceEvent::wasDecoded)
    .def_readonly("had_handler",    &PortTraceEvent::hadHandler)
    .def_readonly("beta128_gated",  &PortTraceEvent::wasBeta128Gated)
    .def_readonly("handled_inline", &PortTraceEvent::wasHandledInline);
```

### 4.3 Implementation Location

```
core/automation/python/src/bindings/python_porttrace.h
```

---

## 5. Lua Bindings (sol2)

Same API surface as Python, adapted for Lua conventions.

```lua
-- Session lifecycle
emu.porttrace_start()
emu.porttrace_stop()

-- Filtering (include/exclude, compound via table = AND; separate calls = OR)
emu.porttrace_include({ port = 0xFFFD, direction = "out" })
emu.porttrace_exclude({ device = "WD1793_Data" })

-- Retrieval — returns Lua table of tables
local events = emu.porttrace_events()
for i, e in ipairs(events) do
    print(string.format("T=%d %s port=0x%04X -> 0x%04X val=0x%02X PC=0x%04X dev=%s",
        e.timestamp, e.direction, e.raw_port, e.decoded_port, e.value, e.pc, e.device))
end

-- Save
emu.porttrace_save("/tmp/trace.json", "json")
```

### Implementation Location

```
core/automation/lua/src/bindings/lua_porttrace.h
```

---

## 6. Save to Disk (C++ Core Implementation)

The `saveToFile()` method is implemented in the core, not in transports. This ensures consistent output regardless of which interface triggers it.

### 6.1 JSON Format

```json
{
  "format": "unreal-ng-porttrace-v1",
  "session": {
    "emulator_id": "a1b2c3d4-...",
    "model": "Pentagon128",
    "tstates_per_frame": 71680,
    "start_tstate": 180000000,
    "end_tstate": 182500000,
    "start_frame": 312,
    "end_frame": 362,
    "filter": "Ports: 0xFFFD, 0xBFFD",
    "overflow_mode": "ring",
    "total_captured": 847,
    "total_evicted": 0
  },
  "decode_rules": [
    {"index": 0, "mask": "0xC002", "match": "0xC000", "port": "0xFFFD"},
    {"index": 1, "mask": "0xC002", "match": "0x8000", "port": "0xBFFD"},
    {"index": 2, "mask": "0x8006", "match": "0x0004", "port": "0x7FFD"}
  ],
  "device_map": {
    "0x04": "AY_FFFD",
    "0x05": "AY_BFFD",
    "0x06": "WD1793_Status"
  },
  "events": [
    {
      "ts": 180027440,
      "frame": 312,
      "raw": 65533,
      "dec": 65533,
      "rule": 0,
      "val": 254,
      "pc": 15938,
      "dev": 4,
      "dir": 1,
      "flags": 3
    }
  ]
}
```

Events use compact numeric fields in the array for space efficiency. The `device_map` header allows decoders to resolve `dev` integers to names.

### 6.2 CSV Format

```csv
# Unreal-NG Port Access Trace v1
# Model: Pentagon128, Emulator: a1b2c3d4-...
# Session: frames 312-362, T-states 180000000-182500000
# Filter: Ports: 0xFFFD, 0xBFFD
# Events: 847 (0 evicted)
index,timestamp,frame,direction,raw_port,decoded_port,decode_rule,value,pc,device,decoded,had_handler,beta128_gated,handled_inline
0,180027440,312,OUT,0xFFFD,0xFFFD,0,0xFE,0x3E42,AY_FFFD,1,1,0,0
1,180027451,312,OUT,0xFFFD,0xFFFD,0,0x08,0x3E47,AY_FFFD,1,1,0,0
2,180027462,312,OUT,0xBFFD,0xBFFD,1,0x20,0x3E4C,AY_BFFD,1,1,0,0
```

CSV uses hex-formatted port/value/PC columns for readability. Comment lines (prefixed `#`) carry session metadata.

### 6.3 Binary Format

Raw dump for maximum speed and minimum size:

```
[Header: 32 bytes, little-endian]
  offset 0   magic:       "PTRC" (4 bytes)
  offset 4   version:     uint16_t = 1
  offset 6   count:       uint32_t
  offset 10  capacity:    uint32_t
  offset 14  tpf:         uint32_t  (tStatesPerFrame)
  offset 18  ruleCount:   uint16_t
  offset 20  reserved:    12 bytes

[Decode rules: ruleCount × 6 bytes]
  {mask: uint16_t, match: uint16_t, port: uint16_t} — the model's decode table
  at capture time, so decodeRuleIndex is self-describing offline

[Events: count × 24 bytes]
  Raw PortTraceEvent structs, layout static_assert-pinned in portdiagrecorder.cpp:
    u64 timestamp, u32 frame, u16 rawPort, u16 decodedPort, u16 pc,
    u8 value, u8 decodeRuleIndex, u8 deviceId, u8 flags, 2 bytes padding
  Python struct format: "<QIHHHBBBBxx" (see tools/porttrace/porttrace_convert.py)
```

Binary is intended for large captures (millions of events) where JSON/CSV overhead is prohibitive. The Python converter tool reads this format.

---

## 7. Python Converter Tool (`porttrace_convert.py`)

A standalone Python script (no emulator required) that reads saved trace files and converts between formats, with analysis features.

### 7.1 Location

```
tools/porttrace/porttrace_convert.py
```

### 7.2 Usage

```bash
# Format conversion
python porttrace_convert.py trace.json --to csv -o trace.csv
python porttrace_convert.py trace.json --to markdown -o trace.md
python porttrace_convert.py trace.bin  --to json -o trace.json
python porttrace_convert.py trace.csv  --to json -o trace.json

# Analysis / filtering
python porttrace_convert.py trace.json --filter-port 0xFFFD --to csv
python porttrace_convert.py trace.json --filter-device AY_FFFD --to markdown
python porttrace_convert.py trace.json --filter-direction OUT --to csv
python porttrace_convert.py trace.json --filter-pc 0x3D00-0x3FFF --to csv
python porttrace_convert.py trace.json --filter-unmapped --to markdown

# Summary statistics
python porttrace_convert.py trace.json --summary

# Combine filters
python porttrace_convert.py trace.json --filter-port 0x007F --filter-direction IN --to markdown
```

### 7.3 Output Formats

#### Text (human-readable, similar to CLI dump)
```
Port Access Trace: Pentagon128 (a1b2c3d4-...)
Frames 312-362 | T-states 180000000-182500000 | 847 events
Filter: Ports: 0xFFFD, 0xBFFD
═══════════════════════════════════════════════════════════════════════════════
 #    Frame  T-State        Dir  Raw    Decoded  Value  PC     Device
───  ──────  ─────────────  ───  ─────  ───────  ─────  ─────  ──────────────
  0   00312  0000000A'F830  OUT  FFFD   FFFD     FE     3E42   AY_FFFD
  1   00312  0000000A'F83B  OUT  FFFD   FFFD     08     3E47   AY_FFFD
  2   00312  0000000A'F846  OUT  BFFD   BFFD     20     3E4C   AY_BFFD
```

#### Markdown (embeddable in docs/issues)
```markdown
## Port Access Trace

**Model**: Pentagon128 | **Frames**: 312–362 | **Events**: 847

| # | Frame | T-State | Dir | Raw | Decoded | Value | PC | Device |
|---|-------|---------|-----|-----|---------|-------|----|--------|
| 0 | 312 | 0A'F830 | OUT | FFFD | FFFD | FE | 3E42 | AY_FFFD |
| 1 | 312 | 0A'F83B | OUT | FFFD | FFFD | 08 | 3E47 | AY_FFFD |
| 2 | 312 | 0A'F846 | OUT | BFFD | BFFD | 20 | 3E4C | AY_BFFD |
```

#### Summary (--summary)
```
Port Access Trace Summary
═════════════════════════
Model:    Pentagon128
Session:  frames 312-362 (50 frames, 1.0 sec)
Events:   847 captured, 0 evicted
Buffer:   847 / 4096 (20.7% full)

By Direction:
  IN:  312 (36.8%)
  OUT: 535 (63.2%)

By Device:
  AY_FFFD          312  (36.8%)   ████████████░░░░░░░░
  AY_BFFD          298  (35.2%)   ███████████░░░░░░░░░
  ULA_FE           120  (14.2%)   ████░░░░░░░░░░░░░░░░
  WD1793_Status     53  ( 6.3%)   ██░░░░░░░░░░░░░░░░░░
  WD1793_Data       48  ( 5.7%)   ██░░░░░░░░░░░░░░░░░░
  None              16  ( 1.9%)   █░░░░░░░░░░░░░░░░░░░

By Decoded Port:
  0xFFFD           312  (36.8%)
  0xBFFD           298  (35.2%)
  0x00FE           120  (14.2%)
  0x001F            53  ( 6.3%)
  0x007F            48  ( 5.7%)
  unmapped          16  ( 1.9%)

Unmapped Port Addresses (raw):
  0x00F1  ×8     0x00F9  ×8

Decode Rule Distribution:
  Rule 0 (FFFD):     312
  Rule 1 (BFFD):     298
  Rule 3 (FE):       120
  BDI fallback:       53
  No match:           16
```

### 7.4 Implementation

> [!NOTE]
> **Implemented** as `tools/porttrace/porttrace_convert.py` (standalone, stdlib-only, Python 3.8+).
> The doc previously carried a design sketch here; the real tool supersedes it.
>
> A companion driver, **`tools/porttrace/porttrace_capture.py`**, performs the whole session in one
> command over the WebAPI: enable the `porttrace` feature → configure buffer + filter
> (presets or repeatable `--include port=FFFD,direction=out` compound rules) → capture
> for `--duration N` seconds (or `--wait-key`) → save the canonical JSON server-side →
> convert to any of `--to json,csv,text,markdown,bin` (plus `--summary`). It assumes the
> WebAPI runs on the same machine so the saved trace is locally readable.

Key facts:
- Input format is auto-detected: `PTRC` magic → binary, `.csv` extension → CSV, otherwise JSON.
- The binary event layout mirrors the C++ `PortTraceEvent` (`"<QIHHHBBBBxx"`); the C++ side
  static_asserts the offsets so the two cannot drift silently.
- `--analyze-strictness` performs single-bit near-miss analysis of unmapped events against the
  decode-rule table embedded in the trace header — for each unmapped raw port it reports every
  rule that would have matched if exactly one masked address line were ignored, naming the line
  (use-case Category 2: over-strict decode).
- `--selftest` round-trips a synthetic trace through the binary, JSON, and CSV readers/writers
  and asserts the strictness analysis output; run it in CI alongside the C++ export test
  (`PortTrace_Test.ExportAllFormats`), which produces real artifacts the converter parses.

## 8. End-to-End Workflow Examples

### 8.1 Interactive CLI debugging session

```
$ telnet localhost 8091
> port-trace include port FFFD
Include rule added: Port 0xFFFD
> port-trace include port BFFD
Include rule added: Port 0xBFFD
> port-trace start
Port trace started (capacity: 4096)
    ... user plays music for 2 seconds ...
> port-trace stop
Port trace stopped: 847 events captured
> port-trace dump --last=10
    ... table output ...
> port-trace save /tmp/ay-debug.json --format=json
Saved 847 events to /tmp/ay-debug.json
```

### 8.2 Python automation script

```python
#!/usr/bin/env python3
"""Capture AY chip-select sequence and verify TurboSound routing."""
import unreal_emulator as emu
import time

emu.porttrace_include(port=0xFFFD)
emu.porttrace_include(port=0xBFFD)
emu.porttrace_start()

time.sleep(2.0)  # Let music play

emu.porttrace_stop()
events = emu.porttrace_events()

# Verify chip-select pattern
chip_selects = [e for e in events if e.decoded_port == 0xFFFD and e.value in (0xFE, 0xFF)]
print(f"Chip-select events: {len(chip_selects)}")
for cs in chip_selects[:10]:
    chip = "AY0 (primary)" if cs.value == 0xFF else "AY1 (secondary)"
    print(f"  T={cs.timestamp} PC={cs.pc:#06x} -> {chip}")

# Save for offline analysis
emu.porttrace_save("results/turbosound-trace.json", "json")
```

### 8.3 Post-capture offline analysis

```bash
# Convert binary dump to markdown for pasting in issue tracker
python tools/porttrace/porttrace_convert.py results/turbosound-trace.bin --to markdown -o report.md

# Show only unmapped ports (diagnostic for missing handlers)
python tools/porttrace/porttrace_convert.py results/trace.json --filter-unmapped --to text

# Get summary stats
python tools/porttrace/porttrace_convert.py results/trace.json --summary

# Compare FDC activity across two captures (diff the CSV)
python tools/porttrace/porttrace_convert.py trace-pentagon.json --filter-device WD1793_Data --to csv -o a.csv
python tools/porttrace/porttrace_convert.py trace-spectrum.json --filter-device WD1793_Data --to csv -o b.csv
diff a.csv b.csv
```

### 8.4 WebAPI remote capture

```bash
# Start capture
curl -X POST http://localhost:8090/api/v1/emulator/$ID/profiler/porttrace/start

# Set filter
curl -X POST http://localhost:8090/api/v1/emulator/$ID/profiler/porttrace/filter \
     -H "Content-Type: application/json" \
     -d '{"include": [{"port": "0xFFFD"}, {"port": "0xBFFD"}]}'

# Wait, then retrieve
curl http://localhost:8090/api/v1/emulator/$ID/profiler/porttrace/events?limit=0 \
     | python -m json.tool > trace.json

# Or save server-side
curl -X POST http://localhost:8090/api/v1/emulator/$ID/profiler/porttrace/save \
     -H "Content-Type: application/json" \
     -d '{"path": "/tmp/trace.json", "format": "json"}'
```

---

## 9. File Naming Convention

Following the Standardized Result Curation pattern (§17.1):

```
results/
  2026-08-24-19-10-porttrace-turbosound/
    trace.json                  # Full capture
    trace-summary.txt           # Summary statistics
    trace-fffd-only.csv         # Filtered subset
    session_metadata.json       # Emulator state at capture time
```

The `save` command auto-generates the timestamped directory when a bare filename is given.
