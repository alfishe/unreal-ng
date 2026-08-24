# Port Access Trace: Automation Access and Export Design

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

    // ── Filter Configuration ──
    void watchAllPorts();
    void watchPort(uint16_t decodedPort);
    void watchDevice(PortDeviceId deviceId);
    void watchPCRange(uint16_t pcLo, uint16_t pcHi);
    void watchUnmapped();           // Only events where decodedPort==0

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
    bool saveToFile(const std::string& path, ExportFormat format) const;

    // ── Metadata ──
    PortTraceSessionInfo getSessionInfo() const;
};

enum class ExportFormat : uint8_t
{
    JSON,       // Structured, machine-readable
    CSV,        // Flat tabular, spreadsheet-friendly
    Binary      // Raw POD dump (sizeof(PortTraceEvent) × N), fastest save/load
};

struct PortTraceSessionInfo
{
    std::string emulatorId;         // UUID
    std::string modelName;          // "Pentagon128", "Spectrum128", etc.
    uint64_t    startTimestamp;      // T-state when session started
    uint64_t    endTimestamp;        // T-state when session stopped (0 if still active)
    uint32_t    startFrame;
    uint32_t    endFrame;
    size_t      totalEvents;
    size_t      evictedEvents;
    std::string filterDescription;  // Human-readable: "All ports" / "Port 0xFFFD" / etc.
};
```

### 1.2 Resolution Chain

```
EmulatorContext
  └─> pPortDecoder
        └─> _portDiagRecorder (PortDiagnosticRecorder*)
```

Access from automation:
```cpp
auto* recorder = context->pPortDecoder->getDiagRecorder();
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

port-trace watch all                  Capture all port I/O (default)
port-trace watch port <hex>           Filter by decoded port (e.g., "port-trace watch port FFFD")
port-trace watch device <name>        Filter by device (e.g., "port-trace watch device WD1793_Data")
port-trace watch pc <lo> <hi>         Filter by PC range
port-trace watch unmapped             Only unmapped ports

port-trace status                     Show session state, event count, filter

port-trace dump [--last=N]            Dump events to terminal (formatted table)
port-trace dump --format=csv          Dump as CSV to terminal
port-trace save <path> [--format=json|csv|bin]   Save to file
```

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
| `POST` | `/api/v1/emulator/{id}/profiler/porttrace/watch` | Set filter |
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

# Filtering
emu.porttrace_watch_all()
emu.porttrace_watch_port(0xFFFD)
emu.porttrace_watch_device("WD1793_Data")
emu.porttrace_watch_pc_range(0x3D00, 0x3FFF)
emu.porttrace_watch_unmapped()

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

-- Filtering
emu.porttrace_watch_port(0xFFFD)

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
    "start_tstate": 180000000,
    "end_tstate": 182500000,
    "start_frame": 312,
    "end_frame": 362,
    "filter": "Ports: 0xFFFD, 0xBFFD",
    "total_captured": 847,
    "total_evicted": 0
  },
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
[Header: 32 bytes]
  magic:     "PTRC" (4 bytes)
  version:   uint16_t = 1
  count:     uint32_t
  capacity:  uint32_t
  reserved:  18 bytes

[Events: count × sizeof(PortTraceEvent)]
  Raw POD structs, no padding normalization needed (struct is already aligned)
```

Binary is intended for large captures (millions of events) where JSON/CSV overhead is prohibitive. The Python converter tool reads this format.

---

## 7. Python Converter Tool (`porttrace_convert.py`)

A standalone Python script (no emulator required) that reads saved trace files and converts between formats, with analysis features.

### 7.1 Location

```
tools/porttrace_convert.py
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

### 7.4 Implementation Sketch

```python
#!/usr/bin/env python3
"""porttrace_convert.py — Unreal-NG Port Access Trace converter and analyzer."""

import argparse
import csv
import json
import struct
import sys
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from typing import List, Optional


class PortDeviceId(IntEnum):
    NONE = 0x00
    ULA_FE = 0x01
    MEMORY_7FFD = 0x02
    MEMORY_1FFD = 0x03
    AY_FFFD = 0x04
    AY_BFFD = 0x05
    WD1793_STATUS = 0x06
    WD1793_TRACK = 0x07
    WD1793_SECTOR = 0x08
    WD1793_DATA = 0x09
    BETA128_SYSTEM = 0x0A
    COVOX = 0x0B
    KEMPSTON = 0x0C
    MOUSE = 0x0D
    CUSTOM = 0x0E
    INLINE_DECODER = 0x0F
    GATED = 0x10


@dataclass
class PortTraceEvent:
    timestamp: int
    frame: int
    raw_port: int
    decoded_port: int
    decode_rule: int
    value: int
    pc: int
    device_id: int
    flags: int

    @property
    def direction(self) -> str:
        return "OUT" if (self.flags & 0x01) else "IN"

    @property
    def decoded(self) -> bool:
        return bool(self.flags & 0x02)

    @property
    def had_handler(self) -> bool:
        return bool(self.flags & 0x04)

    @property
    def beta128_gated(self) -> bool:
        return bool(self.flags & 0x08)

    @property
    def handled_inline(self) -> bool:
        return bool(self.flags & 0x10)

    @property
    def device_name(self) -> str:
        try:
            return PortDeviceId(self.device_id).name
        except ValueError:
            return f"UNKNOWN_{self.device_id:#04x}"


@dataclass
class SessionInfo:
    emulator_id: str = ""
    model: str = ""
    start_tstate: int = 0
    end_tstate: int = 0
    start_frame: int = 0
    end_frame: int = 0
    total_captured: int = 0
    total_evicted: int = 0
    filter_desc: str = "All ports"


# ── Readers ────────────────────────────────────────────────────────

def read_json(path: Path) -> tuple[SessionInfo, List[PortTraceEvent]]:
    with open(path) as f:
        data = json.load(f)
    session = SessionInfo(**{k: data["session"].get(k, "") for k in SessionInfo.__dataclass_fields__})
    events = [PortTraceEvent(
        timestamp=e["ts"], frame=e["frame"], raw_port=e["raw"], decoded_port=e["dec"],
        decode_rule=e["rule"], value=e["val"], pc=e["pc"], device_id=e["dev"], flags=e["flags"]
    ) for e in data["events"]]
    return session, events


def read_csv(path: Path) -> tuple[SessionInfo, List[PortTraceEvent]]:
    session = SessionInfo()
    events = []
    with open(path) as f:
        for line in f:
            if line.startswith("# Model:"):
                session.model = line.split("Model:")[1].split(",")[0].strip()
            if not line.startswith("#"):
                break
        reader = csv.DictReader(f)
        for row in reader:
            events.append(PortTraceEvent(
                timestamp=int(row["timestamp"]),
                frame=int(row["frame"]),
                raw_port=int(row["raw_port"], 16),
                decoded_port=int(row["decoded_port"], 16),
                decode_rule=int(row["decode_rule"]),
                value=int(row["value"], 16),
                pc=int(row["pc"], 16),
                device_id=PortDeviceId[row["device"]].value,
                flags=(int(row.get("decoded", 0)) << 1) | ...
            ))
    return session, events


BINARY_STRUCT = struct.Struct("<QIHHBBHBBxx")  # 24 bytes per event

def read_binary(path: Path) -> tuple[SessionInfo, List[PortTraceEvent]]:
    session = SessionInfo()
    events = []
    with open(path, "rb") as f:
        header = f.read(32)
        magic = header[:4]
        assert magic == b"PTRC", f"Invalid magic: {magic}"
        version = struct.unpack_from("<H", header, 4)[0]
        count = struct.unpack_from("<I", header, 6)[0]
        for _ in range(count):
            data = f.read(BINARY_STRUCT.size)
            ts, frame, raw, dec, rule, val, pc, dev, flags = BINARY_STRUCT.unpack(data)
            events.append(PortTraceEvent(ts, frame, raw, dec, rule, val, pc, dev, flags))
    return session, events


# ── Writers ────────────────────────────────────────────────────────

def write_json(session: SessionInfo, events: List[PortTraceEvent], out) -> None:
    data = {
        "format": "unreal-ng-porttrace-v1",
        "session": vars(session),
        "events": [{"ts": e.timestamp, "frame": e.frame, "raw": e.raw_port,
                     "dec": e.decoded_port, "rule": e.decode_rule, "val": e.value,
                     "pc": e.pc, "dev": e.device_id, "dir": 1 if e.direction == "OUT" else 0,
                     "flags": e.flags} for e in events]
    }
    json.dump(data, out, indent=2)


def write_csv(session: SessionInfo, events: List[PortTraceEvent], out) -> None:
    out.write(f"# Unreal-NG Port Access Trace v1\n")
    out.write(f"# Model: {session.model}, Emulator: {session.emulator_id}\n")
    out.write(f"# Events: {len(events)}\n")
    writer = csv.writer(out)
    writer.writerow(["index", "timestamp", "frame", "direction", "raw_port",
                      "decoded_port", "decode_rule", "value", "pc", "device",
                      "decoded", "had_handler", "beta128_gated", "handled_inline"])
    for i, e in enumerate(events):
        writer.writerow([i, e.timestamp, e.frame, e.direction,
                          f"0x{e.raw_port:04X}", f"0x{e.decoded_port:04X}",
                          e.decode_rule, f"0x{e.value:02X}", f"0x{e.pc:04X}",
                          e.device_name, int(e.decoded), int(e.had_handler),
                          int(e.beta128_gated), int(e.handled_inline)])


def write_markdown(session: SessionInfo, events: List[PortTraceEvent], out) -> None:
    out.write(f"## Port Access Trace\n\n")
    out.write(f"**Model**: {session.model} | **Events**: {len(events)}\n\n")
    out.write("| # | Frame | T-State | Dir | Raw | Decoded | Value | PC | Device | Flags |\n")
    out.write("|---|-------|---------|-----|-----|---------|-------|----|--------|-------|\n")
    for i, e in enumerate(events):
        flags = ""
        if e.decoded: flags += "D"
        if e.had_handler: flags += "H"
        if e.beta128_gated: flags += "G"
        if e.handled_inline: flags += "I"
        out.write(f"| {i} | {e.frame} | {e.timestamp:X} | {e.direction} "
                  f"| {e.raw_port:04X} | {e.decoded_port:04X} | {e.value:02X} "
                  f"| {e.pc:04X} | {e.device_name} | {flags} |\n")


def write_text(session: SessionInfo, events: List[PortTraceEvent], out) -> None:
    out.write(f"Port Access Trace: {session.model} ({session.emulator_id})\n")
    out.write(f"{'═' * 79}\n")
    out.write(f" {'#':>4}  {'Frame':>6}  {'T-State':>13}  {'Dir':3}  {'Raw':>5}  "
              f"{'Decoded':>7}  {'Value':>5}  {'PC':>5}  {'Device':<14}\n")
    out.write(f" {'─'*4}  {'─'*6}  {'─'*13}  {'─'*3}  {'─'*5}  "
              f"{'─'*7}  {'─'*5}  {'─'*5}  {'─'*14}\n")
    for i, e in enumerate(events):
        hi = (e.timestamp >> 16) & 0xFFFFFFFF
        lo = e.timestamp & 0xFFFF
        out.write(f" {i:>4}  {e.frame:>06}  {hi:08X}'{lo:04X}  {e.direction:3}  "
                  f"{e.raw_port:>05X}  {e.decoded_port:>07X}  {e.value:>05X}  "
                  f"{e.pc:>05X}  {e.device_name:<14}\n")


def write_summary(session: SessionInfo, events: List[PortTraceEvent], out) -> None:
    from collections import Counter
    out.write(f"Port Access Trace Summary\n{'═' * 30}\n")
    out.write(f"Model:    {session.model}\n")
    out.write(f"Events:   {len(events)} captured, {session.total_evicted} evicted\n\n")

    dirs = Counter(e.direction for e in events)
    out.write("By Direction:\n")
    for d, c in dirs.most_common():
        out.write(f"  {d:3}:  {c:>5} ({100*c/len(events):5.1f}%)\n")

    devs = Counter(e.device_name for e in events)
    out.write("\nBy Device:\n")
    bar_max = 20
    for d, c in devs.most_common():
        bar = "█" * int(bar_max * c / len(events)) + "░" * (bar_max - int(bar_max * c / len(events)))
        out.write(f"  {d:<18} {c:>5}  ({100*c/len(events):5.1f}%)   {bar}\n")

    ports = Counter(f"0x{e.decoded_port:04X}" if e.decoded_port else "unmapped" for e in events)
    out.write("\nBy Decoded Port:\n")
    for p, c in ports.most_common():
        out.write(f"  {p:<8} {c:>5}  ({100*c/len(events):5.1f}%)\n")

    unmapped = [e for e in events if not e.decoded]
    if unmapped:
        raw_unmapped = Counter(f"0x{e.raw_port:04X}" for e in unmapped)
        out.write("\nUnmapped Port Addresses (raw):\n  ")
        out.write("  ".join(f"{p} ×{c}" for p, c in raw_unmapped.most_common(10)))
        out.write("\n")


# ── Filters ────────────────────────────────────────────────────────

def apply_filters(events, args) -> List[PortTraceEvent]:
    if args.filter_port:
        port_val = int(args.filter_port, 16)
        events = [e for e in events if e.decoded_port == port_val or e.raw_port == port_val]
    if args.filter_device:
        dev = args.filter_device.upper()
        events = [e for e in events if e.device_name == dev]
    if args.filter_direction:
        d = args.filter_direction.upper()
        events = [e for e in events if e.direction == d]
    if args.filter_pc:
        lo, hi = (int(x, 16) for x in args.filter_pc.split("-"))
        events = [e for e in events if lo <= e.pc <= hi]
    if args.filter_unmapped:
        events = [e for e in events if not e.decoded]
    return events


# ── Main ───────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Unreal-NG Port Access Trace converter")
    parser.add_argument("input", help="Input trace file (.json, .csv, or .bin)")
    parser.add_argument("--to", choices=["json", "csv", "markdown", "text"], default="text")
    parser.add_argument("-o", "--output", help="Output file (default: stdout)")
    parser.add_argument("--summary", action="store_true", help="Print summary statistics")
    parser.add_argument("--filter-port", help="Filter by port (hex, e.g. 0xFFFD)")
    parser.add_argument("--filter-device", help="Filter by device name")
    parser.add_argument("--filter-direction", help="Filter by direction (IN/OUT)")
    parser.add_argument("--filter-pc", help="Filter by PC range (hex, e.g. 0x3D00-0x3FFF)")
    parser.add_argument("--filter-unmapped", action="store_true", help="Only unmapped ports")
    args = parser.parse_args()

    path = Path(args.input)
    if path.suffix == ".json":
        session, events = read_json(path)
    elif path.suffix == ".csv":
        session, events = read_csv(path)
    elif path.suffix == ".bin":
        session, events = read_binary(path)
    else:
        print(f"Unknown file format: {path.suffix}", file=sys.stderr)
        sys.exit(1)

    events = apply_filters(events, args)
    out = open(args.output, "w") if args.output else sys.stdout

    if args.summary:
        write_summary(session, events, out)
    elif args.to == "json":
        write_json(session, events, out)
    elif args.to == "csv":
        write_csv(session, events, out)
    elif args.to == "markdown":
        write_markdown(session, events, out)
    elif args.to == "text":
        write_text(session, events, out)

    if args.output:
        out.close()


if __name__ == "__main__":
    main()
```

---

## 8. End-to-End Workflow Examples

### 8.1 Interactive CLI debugging session

```
$ telnet localhost 8091
> port-trace watch port FFFD
Filter set: Port 0xFFFD
> port-trace watch port BFFD
Filter set: Ports 0xFFFD, 0xBFFD
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

emu.porttrace_watch_port(0xFFFD)
emu.porttrace_watch_port(0xBFFD)
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
python tools/porttrace_convert.py results/turbosound-trace.bin --to markdown -o report.md

# Show only unmapped ports (diagnostic for missing handlers)
python tools/porttrace_convert.py results/trace.json --filter-unmapped --to text

# Get summary stats
python tools/porttrace_convert.py results/trace.json --summary

# Compare FDC activity across two captures (diff the CSV)
python tools/porttrace_convert.py trace-pentagon.json --filter-device WD1793_Data --to csv -o a.csv
python tools/porttrace_convert.py trace-spectrum.json --filter-device WD1793_Data --to csv -o b.csv
diff a.csv b.csv
```

### 8.4 WebAPI remote capture

```bash
# Start capture
curl -X POST http://localhost:8090/api/v1/emulator/$ID/profiler/porttrace/start

# Set filter
curl -X POST http://localhost:8090/api/v1/emulator/$ID/profiler/porttrace/watch \
     -H "Content-Type: application/json" \
     -d '{"ports": ["0xFFFD", "0xBFFD"]}'

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
