# WebAPI - HTTP/REST Interface

## Overview

The WebAPI interface provides a RESTful HTTP API for controlling the emulator. Designed for web frontends, dashboards, and remote management applications.

**Status**: ✅ Partially Implemented
**Implementation**: `core/automation/webapi/`
**Framework**: Drogon (C++ HTTP framework)
**Port**: Configurable (default: TBD)
**Protocol**: HTTP/1.1, JSON request/response bodies

**Note**: See [command-interface.md](./command-interface.md) for the complete command reference that this WebAPI interface implements.  

## Architecture

```
┌─────────────────────┐
│  Web Frontend       │  (React, Vue, Angular, etc.)
│  SPA / Dashboard    │
└──────────┬──────────┘
           │ HTTP/REST
           ▼
┌─────────────────────┐
│ AutomationWebAPI    │  Drogon HTTP server
│                     │
│ • emulator_api.cpp  │  Emulator endpoints
│ • hello_world_api   │  Test endpoints
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│ Emulator Manager    │
│ & Emulator Core     │
└─────────────────────┘
```

## REST API Design

### Base URL
```
http://localhost:<port>/api/v1
```

### Stateless Design
Unlike CLI (stateful sessions), WebAPI is stateless:
- No persistent session context
- Emulator ID must be in URL path or query parameter
- Each request is independent
- Suitable for load balancing and horizontal scaling

### API Versioning
- Version in URL path: `/api/v1/...`
- Future versions: `/api/v2/...` without breaking existing clients
- Version negotiation via `Accept` header (future)

## Interface Parity

The WebAPI implements the same command semantics as other interfaces (CLI, Python, Lua). However, some endpoints are **WebAPI-specific** due to the stateless REST design:

| WebAPI Endpoint | CLI Equivalent | Notes |
| :--- | :--- | :--- |
| `GET /emulator/models` | — | Returns available Spectrum models for programmatic discovery; CLI users specify model in `create` command |
| `DELETE /emulator/{id}` | `stop` | Explicit remove endpoint; CLI's `stop` command stops and removes instance |
| `POST /emulator/{id}/start` | `resume` | Start an existing initialized emulator; CLI uses `resume` after `create` |

> [!NOTE]
> These WebAPI-specific endpoints support orchestration patterns (scripts, CI/CD, web dashboards) where fine-grained lifecycle control is needed. CLI provides equivalent functionality through its command set.

## Implemented Endpoints

### 1. List Emulators
**Endpoint**: `GET /api/v1/emulators`  
**Description**: Get list of all emulator instances  

**Request**:
```http
GET /api/v1/emulators HTTP/1.1
Host: localhost:8080
Accept: application/json
```

**Response**:
```json
{
  "emulators": [
    {
      "id": "550e8400-e29b-41d4-a716-446655440000",
      "symbolic_id": "main",
      "created_at": "2026-01-03T10:30:00Z",
      "last_activity": "2026-01-03T10:35:22Z",
      "state": "running",
      "uptime": "00:05:22"
    }
  ],
  "count": 1
}
```

### 2. Get Emulator Status
**Endpoint**: `GET /api/v1/emulators/status`  
**Description**: Get overall status of all emulators  

**Response**:
```json
{
  "status": "ok",
  "emulators": [
    {
      "id": "550e8400-e29b-41d4-a716-446655440000",
      "state": "running",
      "fps": 50.0,
      "cpu_usage": 45.2
    }
  ]
}
```

### 3. Get Emulator Details
**Endpoint**: `GET /api/v1/emulators/{id}`  
**Description**: Get detailed information about specific emulator  

**Parameters**:
- `id` (path): Emulator UUID or symbolic ID

**Response**:
```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "symbolic_id": "main",
  "state": "paused",
  "created_at": "2026-01-03T10:30:00Z",
  "uptime": "00:05:22",
  "registers": {
    "af": "0x44C4",
    "bc": "0x3F00",
    "de": "0x0000",
    "hl": "0x5C00",
    "pc": "0x0605",
    "sp": "0xFF24"
  }
}
```

### 4. Control Emulator State

#### Pause Emulator
**Endpoint**: `POST /api/v1/emulators/{id}/pause`  

**Request**:
```http
POST /api/v1/emulators/550e8400-e29b-41d4-a716-446655440000/pause HTTP/1.1
Content-Type: application/json

{}
```

**Response**:
```json
{
  "status": "success",
  "message": "Emulator paused",
  "state": "paused"
}
```

#### Resume Emulator
**Endpoint**: `POST /api/v1/emulators/{id}/resume`  

#### Start Emulator
**Endpoint**: `POST /api/v1/emulators/{id}/start`  

#### Stop Emulator
**Endpoint**: `POST /api/v1/emulators/{id}/stop`  

### 5. Create Emulator
**Endpoint**: `POST /api/v1/emulators`  
**Description**: Create new emulator instance  

**Request**:
```json
{
  "symbolic_id": "test_instance",
  "config": {
    "model": "spectrum128",
    "rom": "128k.rom"
  }
}
```

**Response**:
```json
{
  "status": "success",
  "id": "550e8400-e29b-41d4-a716-446655440001",
  "symbolic_id": "test_instance"
}
```

### 6. Remove Emulator
**Endpoint**: `DELETE /api/v1/emulators/{id}`  
**Description**: Destroy emulator instance  

**Response**:
```json
{
  "status": "success",
  "message": "Emulator removed"
}
```

## Implemented Debug Commands

> **Status**: ✅ Implemented in `debug_api.cpp` (2026-01)

### Execution Control
```
POST /api/v1/emulator/{id}/step      Execute single instruction
POST /api/v1/emulator/{id}/steps     Execute N instructions (body: {"count": N})
POST /api/v1/emulator/{id}/stepover  Step over CALL instructions
```

### Debug Mode
```
GET  /api/v1/emulator/{id}/debugmode     Get debug mode state
PUT  /api/v1/emulator/{id}/debugmode     Enable/disable (body: {"enabled": true})
```

> [!NOTE]
> Enabling any debug subfeature (breakpoints, memorytracking, calltrace) automatically enables the master `debugmode` switch. This ensures debug features work correctly.

### State Inspection
```
GET /api/v1/emulator/{id}/registers           Get CPU registers (AF, BC, DE, HL, PC, SP, etc.)
GET /api/v1/emulator/{id}/memory/{addr}       Read memory (?len=N, default 16, max 256)
PUT /api/v1/emulator/{id}/memory/{addr}       Write memory (body: {"data":[...]} or {"hex":"..."})
GET /api/v1/emulator/{id}/memory/{type}/{page}/{offset}   Read from physical page (?len=N)
PUT /api/v1/emulator/{id}/memory/{type}/{page}/{offset}   Write to physical page (body: {"data":[...],"force":true})
GET /api/v1/emulator/{id}/memory/info         Get memory configuration (page counts, bank mappings)
GET /api/v1/emulator/{id}/memcounters         Memory access statistics
GET /api/v1/emulator/{id}/calltrace           Call trace history (?limit=N)
GET /api/v1/emulator/{id}/disasm              Disassemble Z80 code (?address=&count=, default: PC)
GET /api/v1/emulator/{id}/disasm/page         Disassemble from physical page (?type=&page=&offset=&count=)
```

#### Physical Page Types
- `ram` - RAM pages (0-255)
- `rom` - ROM pages (0-63)
- `cache` - Cache/SIMM pages (0-3)
- `misc` - Miscellaneous pages (0-3)

#### Memory Write Request
```json
{
  "data": [0x00, 0x01, 0x02],
  "force": true
}
```
Or use hex string format:
```json
{
  "hex": "00 01 02 03",
  "force": true
}
```
> **Note**: `force` is required for ROM writes.

### Breakpoints
```
GET    /api/v1/emulator/{id}/breakpoints                   List all breakpoints
POST   /api/v1/emulator/{id}/breakpoints                   Add breakpoint
DELETE /api/v1/emulator/{id}/breakpoints                   Clear all
DELETE /api/v1/emulator/{id}/breakpoints/{bp_id}           Remove specific
PUT    /api/v1/emulator/{id}/breakpoints/{bp_id}/enable    Enable
PUT    /api/v1/emulator/{id}/breakpoints/{bp_id}/disable   Disable
GET    /api/v1/emulator/{id}/breakpoints/status            Last triggered breakpoint info
```

#### Add Breakpoint Request
```json
{
  "type": "execution|read|write|port_in|port_out",
  "address": 32768,
  "note": "optional annotation",
  "group": "optional group name"
}
```

### Analyzers
```
GET    /api/v1/emulator/{id}/analyzers                       # List all analyzers
GET    /api/v1/emulator/{id}/analyzer/{name}                 # Get analyzer status
PUT    /api/v1/emulator/{id}/analyzer/{name}                 # Enable/disable analyzer
GET    /api/v1/emulator/{id}/analyzer/{name}/events          # Get semantic events
DELETE /api/v1/emulator/{id}/analyzer/{name}/events          # Clear events
POST   /api/v1/emulator/{id}/analyzer/{name}/session         # Session control (activate/deactivate)
GET    /api/v1/emulator/{id}/analyzer/{name}/raw/fdc         # Get raw FDC events
GET    /api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints # Get raw breakpoint events
```

**Example - List Analyzers**:
```bash
curl http://localhost:8090/api/v1/emulator/{id}/analyzers
```
Response:
```json
{
  "emulator_id": "550e8400-...",
  "analyzers": [
    {"id": "trdos", "enabled": false}
  ]
}
```

**Example - Enable TRDOSAnalyzer**:
```bash
curl -X PUT http://localhost:8090/api/v1/emulator/{id}/analyzer/trdos \
     -H "Content-Type: application/json" \
     -d '{"enabled": true}'
```

**Example - Get Events**:
```bash
curl http://localhost:8090/api/v1/emulator/{id}/analyzer/trdos/events?limit=50
```
Response:
```json
{
  "emulator_id": "550e8400-...",
  "analyzer_id": "trdos",
  "events": [
    {"timestamp": 1234567, "type": 1, "formatted": "[0001234] TR-DOS Entry (PC=$3D00)"}
  ],
  "total_events": 47
}
```

**Example - Session Control (Activate)**:
```bash
curl -X POST http://localhost:8090/api/v1/emulator/{id}/analyzer/trdos/session \
     -H "Content-Type: application/json" \
     -d '{"action": "activate"}'
```
Response:
```json
{
  "emulator_id": "550e8400-...",
  "analyzer_id": "trdos",
  "action": "activate",
  "success": true,
  "message": "Session activated"
}
```

> [!NOTE]
> Session control actions:
> - `activate` - Activates analyzer and clears event buffers for fresh session
> - `deactivate` - Deactivates analyzer and closes session

**Example - Get Raw FDC Events**:
```bash
curl "http://localhost:8090/api/v1/emulator/{id}/analyzer/trdos/raw/fdc?limit=5"
```
Response:
```json
{
  "emulator_id": "550e8400-...",
  "analyzer_id": "trdos",
  "total_events": 96,
  "showing": 5,
  "events": [
    {
      "tstate": 15386112,
      "frame_number": 22725,
      "pc": 16075,
      "sp": 65320,
      "af": 6172,
      "bc": 2057,
      "de": 23802,
      "hl": 23802,
      "command_reg": 0,
      "status_reg": 32,
      "track_reg": 1,
      "sector_reg": 14,
      "data_reg": 9,
      "system_reg": 0,
      "iff1": 1,
      "iff2": 1,
      "im": 1,
      "stack": [91, 62, 155, 1, 147, 62, 9, 2, 134, 30, 97, 94, 33, 2, 66, 19]
    }
  ]
}
```

> [!IMPORTANT]
> **JSON Format**: All values are JSON numbers (not hex strings) for programmatic use.
> - PC, SP, registers: numeric (e.g., `"pc": 16075` = 0x3ECB)
> - Stack bytes: array of numbers (e.g., `[91, 62, ...]`)
>
> **Raw FDC Events** include:
> - Full Z80 main registers (AF, BC, DE, HL)
> - FDC register snapshot (command, status, track, sector, data, system)
> - 16-byte stack snapshot for call chain reconstruction
> - Timing (tstate, frame_number)

**Example - Get Raw Breakpoint Events**:
```bash
curl "http://localhost:8090/api/v1/emulator/{id}/analyzer/trdos/raw/breakpoints?limit=2"
```
Response:
```json
{
  "emulator_id": "550e8400-...",
  "analyzer_id": "trdos",
  "total_events": 7,
  "showing": 2,
  "events": [
    {
      "tstate": 6243328,
      "frame_number": 12486,
      "address": 119,
      "pc": 119,
      "sp": 65326,
      "af": 12570,
      "bc": 6,
      "de": 0,
      "hl": 0,
      "af_": 0,
      "bc_": 0,
      "de_": 0,
      "hl_": 0,
      "ix": 23610,
      "iy": 23738,
      "i": 63,
      "r": 18,
      "iff1": 1,
      "iff2": 1,
      "im": 1,
      "stack": [61, 47, 0, 0, 0, 0, 97, 94, 33, 2, 66, 19, 97, 94, 9, 19]
    }
  ]
}
```

> [!IMPORTANT]
> **Raw Breakpoint Events** include complete Z80 state:
> - Main registers: AF, BC, DE, HL
> - Alternate registers: AF', BC', DE', HL'
> - Index registers: IX, IY
> - Special registers: I, R
> - 16-byte stack snapshot
> - Timing information (tstate, frame_number)

### Memory Profiler

> **Status**: ✅ Implemented (2026-01)

Track memory access patterns (reads/writes/executes) across all physical memory pages (RAM/ROM/Cache).

```
POST /api/v1/emulator/{id}/profiler/memory/start      Start profiler, enable feature
POST /api/v1/emulator/{id}/profiler/memory/stop       Stop profiler, preserve data
POST /api/v1/emulator/{id}/profiler/memory/pause      Pause profiler, retain data
POST /api/v1/emulator/{id}/profiler/memory/resume     Resume paused session
POST /api/v1/emulator/{id}/profiler/memory/clear      Clear all profiler data
GET  /api/v1/emulator/{id}/profiler/memory/status     Get profiler status
GET  /api/v1/emulator/{id}/profiler/memory/pages      Get per-page access summaries (?limit=N)
GET  /api/v1/emulator/{id}/profiler/memory/counters   Get address-level counters (?page=N, ?mode=z80|physical)
GET  /api/v1/emulator/{id}/profiler/memory/regions    Get monitored region statistics
POST /api/v1/emulator/{id}/profiler/memory/save       Save access data to file (body: {"path": "...", "format": "yaml"})
```

**Status Response**:
```json
{
  "emulator_id": "550e8400-...",
  "session_state": "capturing",
  "feature_enabled": true,
  "tracking_mode": "physical"
}
```

**Pages Response** (`?limit=20`):
```json
{
  "emulator_id": "550e8400-...",
  "pages": [
    {"page": 0, "type": "RAM", "reads": 15234, "writes": 1200, "executes": 45000},
    {"page": 1, "type": "RAM", "reads": 8900, "writes": 500, "executes": 12000}
  ]
}
```

**Counters Response** (`?page=5&mode=physical`):
```json
{
  "emulator_id": "550e8400-...",
  "mode": "physical",
  "page": 5,
  "counters": {
    "reads": [0, 0, 15, 230, ...],
    "writes": [0, 0, 0, 5, ...],
    "executes": [100, 200, 0, 0, ...]
  }
}
```

### Call Trace Profiler

> **Status**: ✅ Implemented (2026-01)

Track CPU control flow events (CALL, RET, JP, JR, RST, etc.).

```
POST /api/v1/emulator/{id}/profiler/calltrace/start    Start profiler, enable feature
POST /api/v1/emulator/{id}/profiler/calltrace/stop     Stop profiler, preserve data
POST /api/v1/emulator/{id}/profiler/calltrace/pause    Pause profiler, retain data
POST /api/v1/emulator/{id}/profiler/calltrace/resume   Resume paused session
POST /api/v1/emulator/{id}/profiler/calltrace/clear    Clear all profiler data
GET  /api/v1/emulator/{id}/profiler/calltrace/status   Get profiler status
GET  /api/v1/emulator/{id}/profiler/calltrace/entries  Get trace entries (?count=N, default 100)
GET  /api/v1/emulator/{id}/profiler/calltrace/stats    Get call/return statistics
```

**Status Response**:
```json
{
  "emulator_id": "550e8400-...",
  "session_state": "capturing",
  "feature_enabled": true,
  "entry_count": 4500,
  "buffer_capacity": 10000
}
```

**Entries Response** (`?count=50`):
```json
{
  "emulator_id": "550e8400-...",
  "requested_count": 50,
  "returned_count": 50,
  "entries": [
    {"type": "CALL", "from_pc": 0x1234, "to_pc": 0x5678, "sp": 0xFFFE, "frame": 1200, "tstate": 45000},
    {"type": "RET", "from_pc": 0x567A, "to_pc": 0x1237, "sp": 0x0000, "frame": 1200, "tstate": 45100}
  ]
}
```

**Stats Response**:
```json
{
  "emulator_id": "550e8400-...",
  "total_calls": 12500,
  "total_returns": 12480,
  "total_jumps": 45000,
  "call_depth_max": 24,
  "top_targets": [
    {"address": 0x0038, "count": 5000, "type": "RST"},
    {"address": 0x1234, "count": 2500, "type": "CALL"}
  ]
}
```


### Opcode Profiler

> **Status**: ✅ Implemented (2026-01)

Track Z80 opcode execution statistics and capture execution traces.

```
POST /api/v1/emulator/{id}/profiler/opcode/start      Start profiler, enable feature
POST /api/v1/emulator/{id}/profiler/opcode/stop       Stop profiler, preserve data
POST /api/v1/emulator/{id}/profiler/opcode/pause      Pause profiler, retain data
POST /api/v1/emulator/{id}/profiler/opcode/resume     Resume paused session
POST /api/v1/emulator/{id}/profiler/opcode/clear      Clear all profiler data
GET  /api/v1/emulator/{id}/profiler/opcode/status     Get profiler status
GET  /api/v1/emulator/{id}/profiler/opcode/counters   Get opcode counters (?limit=N, default 100)
GET  /api/v1/emulator/{id}/profiler/opcode/trace      Get execution trace (?count=N, default 100)
```

**Start Profiler**:
```bash
curl -X POST http://localhost:8090/api/v1/emulator/{id}/profiler/opcode/start
```
Other actions: `stop`, `pause`, `resume`, `clear`

**Status Response**:
```json
{
  "emulator_id": "550e8400-...",
  "feature_enabled": true,
  "capturing": true,
  "total_executions": 15234567,
  "trace_size": 10000,
  "trace_capacity": 10000
}
```

**Counters Response** (`?limit=50`):
```json
{
  "emulator_id": "550e8400-...",
  "total_executions": 15234567,
  "count": 50,
  "counters": [
    {"prefix": 0, "prefix_name": "none", "opcode": 126, "count": 2156789},
    {"prefix": 203, "prefix_name": "CB", "opcode": 110, "count": 1500000}
  ]
}
```

**Trace Response** (`?count=100`):
```json
{
  "emulator_id": "550e8400-...",
  "requested_count": 100,
  "returned_count": 100,
  "trace": [
    {"pc": 0x1234, "prefix": 0, "opcode": 126, "flags": 68, "a": 66, "frame": 1200, "tstate": 45000}
  ]
}
```

### Unified Profiler Control

> **Status**: ✅ Implemented (2026-01)

Control all profilers (opcode, memory, calltrace) simultaneously.

```
POST /api/v1/emulator/{id}/profiler/start      Start all profilers
POST /api/v1/emulator/{id}/profiler/stop       Stop all profilers
POST /api/v1/emulator/{id}/profiler/pause      Pause all profilers
POST /api/v1/emulator/{id}/profiler/resume     Resume all profilers
POST /api/v1/emulator/{id}/profiler/clear      Clear all profiler data
GET  /api/v1/emulator/{id}/profiler/status     Get status of all profilers
```

**Start All Profilers**:
```bash
curl -X POST http://localhost:8090/api/v1/emulator/{id}/profiler/start
```

**Status Response** (all profilers):
```json
{
  "opcode": {"session_state": "capturing", "total_executions": 1523456},
  "memory": {"session_state": "capturing", "feature_enabled": true},
  "calltrace": {"session_state": "capturing", "entry_count": 450}
}
```


### Snapshots

#### List Breakpoints Response
Response contains type-specific fields based on breakpoint type:

**Memory Breakpoint** (execute/read/write):
```json
{
  "count": 2,
  "breakpoints": [
    {
      "id": 1,
      "type": "memory",
      "address": 32768,
      "execute": true,
      "read": false,
      "write": false,
      "active": true,
      "note": "entry point",
      "group": "default"
    }
  ]
}
```

**Port Breakpoint** (in/out only):
```json
{
  "id": 2,
  "type": "port",
  "address": 254,
  "in": true,
  "out": true,
  "active": true,
  "note": "ULA port",
  "group": "io"
}
```

#### Breakpoint Status Response
```json
{
  "is_paused": true,
  "breakpoints_count": 3,
  "last_triggered_id": 1,
  "last_triggered_type": "memory",
  "last_triggered_address": 32768,
  "paused_by_breakpoint": true
}
```

### Keyboard Injection

> **Status**: ✅ Implemented (2026-01)

Programmatic keyboard input injection for automation and testing.

```
POST /api/v1/emulator/{id}/keyboard/tap         Tap key (press + auto-release)
POST /api/v1/emulator/{id}/keyboard/press       Press and hold key
POST /api/v1/emulator/{id}/keyboard/release     Release held key
POST /api/v1/emulator/{id}/keyboard/combo       Tap modifier+key combo (e.g., SS+P for quote)
POST /api/v1/emulator/{id}/keyboard/macro       Execute predefined macro
POST /api/v1/emulator/{id}/keyboard/type        Type text sequence
POST /api/v1/emulator/{id}/keyboard/release_all Release all pressed keys
POST /api/v1/emulator/{id}/keyboard/abort       Abort current sequence
GET  /api/v1/emulator/{id}/keyboard/status      Get keyboard state
GET  /api/v1/emulator/{id}/keyboard/keys        List recognized key names
```

**Single Key Tap**:
```bash
curl -X POST http://localhost:8090/api/v1/emulator/{id}/keyboard/tap \
     -H "Content-Type: application/json" \
     -d '{"key": "a", "frames": 3}'
```

**Key Combo** (e.g., SYMBOL+P for double-quote):
```bash
curl -X POST http://localhost:8090/api/v1/emulator/{id}/keyboard/combo \
     -H "Content-Type: application/json" \
     -d '{"keys": ["ss", "p"], "frames": 3}'
```

**Type Text** (plain literal characters):
```bash
curl -X POST http://localhost:8090/api/v1/emulator/{id}/keyboard/type \
     -H "Content-Type: application/json" \
     -d '{"text": "hello world", "delay_frames": 3}'
```

**Type BASIC Command** (tokenized mode - first char = K-mode keyword):
```bash
curl -X POST http://localhost:8090/api/v1/emulator/{id}/keyboard/type \
     -H "Content-Type: application/json" \
     -d '{"text": "P\"hello\"", "tokenized": true, "delay_frames": 3}'
```
Result: Types `PRINT "hello"` (P → PRINT token, quotes handled automatically)

> [!NOTE]
> **`tokenized` parameter**: When `true`, the first character produces a K-mode keyword token (e.g., 'P' → PRINT), and quotes are handled as SS+P combos. When `false` (default), all characters are typed literally.

**Execute Macro**:
```bash
curl -X POST http://localhost:8090/api/v1/emulator/{id}/keyboard/macro \
     -H "Content-Type: application/json" \
     -d '{"name": "format"}'
```

**Type Request Body**:
```json
{
  "text": "P\"hello\"",
  "delay_frames": 3,
  "tokenized": true
}
```

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `text` | string | Yes | - | Text to type |
| `delay_frames` | number | No | 2 | Frames between characters |
| `tokenized` | boolean | No | false | Enable K-mode token for first char |

## Planned Endpoints (Not Yet Implemented)

### Disassembly
```
GET /api/v1/emulator/{id}/disassemble?address=0x8000&count=10
```

### Media Operations (Implemented Separately)
```
POST /api/v1/emulator/{id}/tape/load    ✅ Implemented
POST /api/v1/emulator/{id}/tape/eject   ✅ Implemented
POST /api/v1/emulator/{id}/disk/{drive}/insert  ✅ Implemented
POST /api/v1/emulator/{id}/disk/{drive}/eject   ✅ Implemented
```

### Snapshots (Implemented Separately)
```
POST /api/v1/emulator/{id}/snapshot/save  ✅ Implemented
POST /api/v1/emulator/{id}/snapshot/load  ✅ Implemented
```

## WebSocket Support (Future)

Real-time streaming for live updates without polling.

**Endpoint**: `WS /api/v1/emulators/{id}/stream`  

**Use Cases**:
- Live screen updates (video streaming)
- Real-time register monitoring
- Event notifications (breakpoint hit, state change)
- Audio streaming

**Message Format**:
```json
{
  "type": "register_update",
  "timestamp": "2026-01-03T10:35:22.123Z",
  "data": {
    "pc": "0x0605",
    "af": "0x44C4"
  }
}
```

## HTTP Headers & CORS

### CORS Configuration
```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS
Access-Control-Allow-Headers: Content-Type, Authorization
Access-Control-Max-Age: 86400
```

**Note**: Wildcard origin (`*`) for development. Restrict in production:
```
Access-Control-Allow-Origin: https://yourdomain.com
```

### Content Negotiation
```
Accept: application/json
Content-Type: application/json
```

### Authentication (Future)
```
Authorization: Bearer <token>
X-API-Key: <api-key>
```

## Error Handling

### HTTP Status Codes

| Code | Meaning | Example |
| :--- | :--- | :--- |
| 200 | OK | Successful GET request |
| 201 | Created | Emulator created |
| 204 | No Content | Successful DELETE |
| 400 | Bad Request | Invalid JSON or parameters |
| 404 | Not Found | Emulator ID doesn't exist |
| 409 | Conflict | Emulator already exists |
| 500 | Internal Server Error | Emulator crash or bug |
| 503 | Service Unavailable | Server overloaded |

### Error Response Format
```json
{
  "status": "error",
  "error": {
    "code": "EMULATOR_NOT_FOUND",
    "message": "Emulator with ID '12345' does not exist",
    "details": {
      "requested_id": "12345",
      "available_ids": ["550e8400-..."]
    }
  }
}
```

### Common Error Codes
- `EMULATOR_NOT_FOUND` - Invalid emulator ID
- `INVALID_STATE` - Operation not valid in current state
- `INVALID_PARAMETER` - Bad request parameter
- `INTERNAL_ERROR` - Server-side error

## Client Examples

### JavaScript (Fetch API)
```javascript
// List emulators
const response = await fetch('http://localhost:8080/api/v1/emulators');
const data = await response.json();
console.log(data.emulators);

// Pause emulator
await fetch('http://localhost:8080/api/v1/emulators/550e8400-.../pause', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: '{}'
});

// Get registers
const regs = await fetch('http://localhost:8080/api/v1/emulators/550e8400-.../registers');
console.log(await regs.json());
```

### Python (requests library)
```python
import requests

base_url = 'http://localhost:8080/api/v1'

# List emulators
r = requests.get(f'{base_url}/emulators')
emulators = r.json()['emulators']

# Pause emulator
emu_id = emulators[0]['id']
requests.post(f'{base_url}/emulators/{emu_id}/pause')

# Get status
status = requests.get(f'{base_url}/emulators/{emu_id}').json()
print(f"State: {status['state']}")
```

### cURL
```bash
# List emulators
curl http://localhost:8080/api/v1/emulators

# Pause emulator
curl -X POST http://localhost:8080/api/v1/emulators/550e8400-.../pause

# Create emulator
curl -X POST http://localhost:8080/api/v1/emulators \
  -H "Content-Type: application/json" \
  -d '{"symbolic_id":"test","config":{"model":"spectrum128"}}'
```

## Rate Limiting (Future)

```
X-RateLimit-Limit: 100
X-RateLimit-Remaining: 99
X-RateLimit-Reset: 1609459200
```

**Default Limits**:
- 100 requests per minute per IP
- 1000 requests per hour per API key

## Performance Characteristics

- **Latency**: ~5-20ms per request (local)
- **Throughput**: ~1000-5000 requests/second (depends on endpoint)
- **Concurrent Connections**: ~10,000 (Drogon limit)
- **Memory**: ~1-2MB per connection

## Implementation Details

### Framework: Drogon
- High-performance C++ HTTP framework
- Asynchronous I/O (epoll/kqueue)
- Built-in JSON support
- WebSocket support
- Middleware pipeline

### Source Files
- `core/automation/webapi/src/automation-webapi.cpp` - Server initialization
- `core/automation/webapi/src/emulator_api.cpp` - Emulator endpoints
- `core/automation/webapi/src/emulator_websocket.cpp` - WebSocket handler

### Configuration
```json
{
  "listeners": [
    {
      "address": "0.0.0.0",
      "port": 8080,
      "https": false
    }
  ],
  "threads": 4,
  "enable_compression": true,
  "max_connections": 10000
}
```

## Security Considerations

### Current Limitations
- No authentication
- No HTTPS (plain HTTP only)
- No rate limiting
- CORS allows all origins

### Production Recommendations
1. **Enable HTTPS**: Use TLS certificates
2. **Add Authentication**: JWT tokens or API keys
3. **Restrict CORS**: Whitelist specific origins
4. **Rate Limiting**: Prevent abuse
5. **Input Validation**: Sanitize all inputs
6. **Firewall**: Restrict access by IP

### HTTPS Setup (Future)
```json
{
  "listeners": [
    {
      "address": "0.0.0.0",
      "port": 8443,
      "https": true,
      "cert": "/path/to/cert.pem",
      "key": "/path/to/key.pem"
    }
  ]
}
```

## Troubleshooting

### Server won't start
- Check port not in use: `netstat -an | grep 8080`
- Verify configuration file syntax
- Check logs for error messages

### 404 Not Found
- Verify endpoint URL (case-sensitive)
- Check API version in path (`/api/v1/...`)
- Ensure emulator ID is correct

### CORS errors in browser
- Check CORS headers in response
- Verify origin is allowed
- Use browser DevTools Network tab

### Slow responses
- Check emulator is not hung
- Monitor server CPU/memory
- Consider caching for frequently accessed data

## See Also

### Interface Documentation
- **[Command Interface Overview](./command-interface.md)** - Core command reference and architecture
- **[CLI Interface](./cli-interface.md)** - TCP-based text protocol for interactive debugging
- **[Python Bindings](./python-interface.md)** - Direct C++ bindings for automation and AI/ML
- **[Lua Bindings](./lua-interface.md)** - Lightweight scripting and embedded logic

### Advanced Interfaces (Future)
- **[GDB Protocol](./gdb-protocol.md)** - Professional debugging with standard GDB/LLDB clients
- **[Universal Debug Bridge](./udb-protocol.md)** - High-performance analysis and profiling

### Navigation
- **[Interface Documentation Index](./README.md)** - Overview of all control interfaces

### External Resources
- **[Drogon Framework Documentation](https://drogon.org/)** - HTTP framework used for WebAPI implementation
