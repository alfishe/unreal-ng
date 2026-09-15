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
**Endpoint**: `GET /api/v1/emulator`  
**Description**: Get list of all emulator instances (each entry carries the machine identity block)  

**Response**:
```json
{
  "emulators": [
    {
      "id": "550e8400-e29b-41d4-a716-446655440000",
      "state": "running",
      "is_running": true,
      "is_paused": false,
      "is_debug": false,
      "model": "PENTAGON",
      "model_full_name": "Pentagon",
      "ram_kb": 128,
      "video_mode": "Standard",
      "speed_multiplier": 1,
      "config_folder": "pentagon128k"
    }
  ],
  "count": 1
}
```

**Machine identity fields** (present on every lifecycle response — list, details, create, model switch; also exposed via the MCP `machine` aspect):
- `model` / `model_full_name`: short name (for create requests) and human-readable name
- `ram_kb`: configured RAM
- `video_mode`: active video mode (`null` until the screen subsystem exists; `ATM16`/`Profi 512x240`/... on builds with extended video support)
- `speed_multiplier`: live Z80 frequency multiplier (reflects turbo)
- `config_folder`: machine config folder under `configs/`

### 2. Get Server Status
**Endpoint**: `GET /api/v1/emulator/status`  
**Description**: Instance counts by state, build fingerprint, and which machines this build can create  

**Response**:
```json
{
  "emulator_count": 1,
  "states": { "running": 1 },
  "server": {
    "version": "1.0.0",
    "git_branch": "master",
    "git_commit": "5719e27e",
    "build_type": "Release"
  },
  "models_creatable": ["PENTAGON", "48K", "128k", "PLUS3", "PROFI", "SCORPION", "PROFSCORP"]
}
```

> The `server` block is captured at CMake configure time — a branch switch or new commit requires reconfiguring to refresh. Use it to attribute triage sessions to a build. `models_creatable` lists the short names a create can succeed with.

### 3. Get Emulator Details
**Endpoint**: `GET /api/v1/emulator/{id}`  
**Description**: Get detailed information about a specific emulator (with the machine identity block)  

**Parameters**:
- `id` (path): Emulator UUID or index (0-based)

**Response**:
```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "state": "paused",
  "is_running": false,
  "is_paused": true,
  "is_debug": false,
  "model": "PENTAGON",
  "model_full_name": "Pentagon",
  "ram_kb": 128,
  "video_mode": "Standard",
  "speed_multiplier": 1,
  "config_folder": "pentagon128k"
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
**Endpoint**: `POST /api/v1/emulator/create` (also `POST /api/v1/emulator/start` = create + start)  
**Description**: Create new emulator instance. **Strict semantics**: a requested model this build cannot instantiate fails with `400` and a reason — there is NO silent fallback to a default 48K machine.  

**Request**:
```json
{
  "symbolic_id": "test_instance",
  "model": "128k",
  "ram_size": 128
}
```

**Response 201**:
```json
{
  "id": "550e8400-e29b-41d4-a716-446655440001",
  "state": "initialized",
  "symbolic_id": "test_instance",
  "model": "128k",
  "model_full_name": "ZX-Spectrum 128k",
  "ram_kb": 128,
  "video_mode": null,
  "speed_multiplier": 1,
  "config_folder": "spectrum128"
}
```

**Response 400** (model not creatable / unknown / bad RAM):
```json
{
  "error": "Bad Request",
  "message": "model 'ATM710' is not supported by this build (PortDecoder::GetPortDecoderForModel - unknown model 6)",
  "requested_model": "ATM710",
  "available_models_endpoint": "/api/v1/emulator/models"
}
```

### 5a. List Models (runtime-authoritative)
**Endpoint**: `GET /api/v1/emulator/models`  
**Description**: All known machine models with per-entry `creatable` flags. This endpoint — not docs — is the authoritative model list: a `creatable: false` entry (missing port decoder or config folder in this build) fails on create with the reason above.

```json
{
  "models": [
    { "id": 0, "name": "PENTAGON", "full_name": "Pentagon", "default_ram_kb": 128,
      "available_ram_sizes_kb": [128, 512, 1024], "creatable": true },
    { "id": 5, "name": "ATM3", "full_name": "ZX-Evo", "default_ram_kb": 1024,
      "available_ram_sizes_kb": [1024], "creatable": false }
  ],
  "count": 16
}
```

### 5b. Switch Model (validate-first)
**Endpoint**: `POST /api/v1/emulator/{id}/model` (body `{"model": "...", "ram_size": N}`)  
**Description**: The request is validated BEFORE the current instance is stopped/removed: an unknown model, unsupported RAM or non-creatable model returns `400` and the current emulator keeps running untouched. A successful switch stops the old instance, creates and starts a new one (different ID) and returns the machine identity block for the new instance.

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
POST /api/v1/emulator/{id}/step              Execute single instruction
POST /api/v1/emulator/{id}/steps             Execute N instructions (body: {"count": N})
POST /api/v1/emulator/{id}/stepover          Step over CALL instructions
POST /api/v1/emulator/{id}/stepout            Step out of current subroutine (breakpoints skipped)
POST /api/v1/emulator/{id}/skip_until         Fast-forward until PC reaches target (body: {"pc": "0x8000", "max_tstates": N})
POST /api/v1/emulator/{id}/run_tstates       Run N t-states (body: {"count": N})
POST /api/v1/emulator/{id}/run_to_scanline   Run until scanline N (body: {"scanline": N})
POST /api/v1/emulator/{id}/run_scanlines     Run N scanlines forward (body: {"count": N})
POST /api/v1/emulator/{id}/run_to_pixel      Run until next screen pixel boundary
POST /api/v1/emulator/{id}/run_to_interrupt  Run until Z80 accepts INT
POST /api/v1/emulator/{id}/run_frame         Run exactly one video frame
POST /api/v1/emulator/{id}/run_frames        Run N video frames (body: {"count": N})
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
POST /api/v1/emulator/{id}/memory/find        Search Z80 memory for a byte pattern (body: {"pattern_hex": "AF 3C"})
GET  /api/v1/emulator/{id}/state/screen/digest  Stable screen-content digest (range or banks, border folding; ?mode=active follows the displayed surface)
GET  /api/v1/emulator/{id}/ports             Static port map + live routing flags (which devices answer which ports, under which gates)
GET  /api/v1/emulator/{id}/video/beam         Current raster position and beam zone
GET  /api/v1/emulator/{id}/frame_cost         Per-frame halt/run cost accounting
GET  /api/v1/emulator/{id}/state/audio/ay      AY/SSG chips overview (core DeviceState report)
GET  /api/v1/emulator/{id}/state/audio/ay/{n}  One AY/SSG chip, registers and channels decoded
GET  /api/v1/emulator/{id}/state/audio/fm      TurboSound FM board latches + both YM2203 summaries (404 without TSFM)
GET  /api/v1/emulator/{id}/state/audio/fm/{n}  One YM2203 FM half: mode, timers, channels, operators, envelopes, key-on
GET  /api/v1/emulator/{id}/state/fdc           Beta Disk WD1793: registers, status bits, FSM, signals, drives (404 without Beta Disk)
```

The three device reports (AY, FM, FDC) are built once in the core
(`core/src/emulator/state/devicestate.h`) and are byte-for-byte the same
data the CLI, Lua, Python and MCP return — see
[command-interface.md §3.3](./command-interface.md#33-device-state-reports-ay--ssg-turbosound-fm-beta-disk-fdc)
for the field list. Every endpoint also has an active-emulator form without
`{id}` (`/api/v1/emulator/state/audio/fm`, `/api/v1/emulator/state/fdc`).

### Labels & Symbols
```
GET    /api/v1/emulator/{id}/labels            List labels (filters: ?module=&type=&bank=&from=&to=&active=)
POST   /api/v1/emulator/{id}/labels            Add label (body: {"name", "address", "type", ...})
DELETE /api/v1/emulator/{id}/labels            Clear all labels
GET    /api/v1/emulator/{id}/labels/resolve    Resolve by name or address (?name= or ?address=): exact label, aliases at the address, nearest below/above
GET    /api/v1/emulator/{id}/labels/{name}     Get label by name
DELETE /api/v1/emulator/{id}/labels/{name}     Remove label
PUT    /api/v1/emulator/{id}/labels/{name}     Update label (body: {"address", "type", ...})
```

### Assembler & Source Listings
```
POST /api/v1/emulator/{id}/assemble            Assemble Z80 source (body: {"code", "address", "write": false})
POST /api/v1/emulator/{id}/listing/load        Load source listing (body: {"path"})
GET  /api/v1/emulator/{id}/listing/source_at   Source line for an address (?address=, default: PC)
POST /api/v1/emulator/{id}/listing/step_line   Run until the source line changes (body: {"max_tstates": N})
POST /api/v1/emulator/{id}/listing/run_to_line Run to first code byte of a line (body: {"line": N})
```

### Analysis & Capture
```
POST /api/v1/emulator/{id}/coverage/start      Activate coverage analyzer (body: {"keep": false})
POST /api/v1/emulator/{id}/coverage/stop       Deactivate (data retained)
POST /api/v1/emulator/{id}/coverage/clear      Clear collected data
GET  /api/v1/emulator/{id}/coverage            Coverage summary (executed count, ranges)
GET  /api/v1/emulator/{id}/coverage/gaps       Executed ranges and gaps (?start=&end=&max=)
POST /api/v1/emulator/{id}/ay/log              AY log control (body: {"action": "start|stop|clear", "capacity"})
GET  /api/v1/emulator/{id}/ay/log              Get AY log entries (?count=&offset=)
POST /api/v1/emulator/{id}/audio/capture       Audio capture control (body: {"action": "start|stop|clear", "seconds"})
GET  /api/v1/emulator/{id}/audio/capture/status   Capture state and level statistics
GET  /api/v1/emulator/{id}/audio/capture/result   Captured samples (?format=wav&path=... to export)
POST /api/v1/emulator/{id}/video/record        Video recording control (body: {"action": "start|stop|pause|resume", ...})
GET  /api/v1/emulator/{id}/video/record/status    Recording state
```

#### Disassembly Response

Each instruction entry contains the following fields (optional fields are omitted when not applicable):

| Field | Description |
| :--- | :--- |
| `address` | Z80 address of the instruction (first prefix/opcode byte) |
| `bytes` | Instruction bytes as uppercase hex string |
| `mnemonic` | Formatted disassembly. When a label exists for a jump/call/memory target, both the label and the address are printed: `call TEST_ROUTINE (#8010)` |
| `size` | Instruction length in bytes (1-4, includes prefixes) |
| `label` | Label at the instruction address itself, if any |
| `target` | Resolved jump/call target address (JR/DJNZ targets are computed as `address + size + signed offset`). Omitted for indirect jumps (`jp (hl)`/`jp (ix)`/`jp (iy)`) unless runtime registers were available to resolve them |
| `targetLabel` | Label at `target`, if any |
| `displacement` | Signed IX/IY displacement (`-128..127`) for indexed instructions; requires runtime registers |
| `effectiveAddress` | Effective address (`IX+d`/`IY+d`) for indexed instructions; requires runtime registers |
| `effectiveAddressLabel` | Label at `effectiveAddress`, if any |

`/disasm` decodes with live runtime context (registers and memory), so runtime-dependent fields are populated. `/disasm/page` performs static decoding from a physical page, so `displacement`/`effectiveAddress` are omitted there.

**Example:**
```json
{
  "instructions": [
    {"address": 32768, "bytes": "CD1080", "mnemonic": "call TEST_ROUTINE (#8010)", "size": 3, "target": 32784, "targetLabel": "TEST_ROUTINE"},
    {"address": 32771, "bytes": "DD46FB", "mnemonic": "ld b,(ix-#05)", "size": 3, "displacement": -5, "effectiveAddress": 65531, "effectiveAddressLabel": "BUFFER_END"},
    {"address": 32774, "bytes": "18F8", "mnemonic": "jr TEST_START (#8000)", "size": 2, "target": 32768, "targetLabel": "TEST_START"}
  ]
}
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

## Videowall API

### 8. Set Videowall Single Sync Mode
**Endpoint**: `POST /api/v1/videowall/singlesync`  
**Description**: Toggles the Single Emulator Sync mode in the Videowall app, providing 100% synchronized rendering and video recording.  

**Request**:
```json
{
  "enable": true,
  "emulator_id": "optional-id"
}
```

**Response**:
```json
{
  "success": true
}
```

## Input API

### 9. Keyboard Inputjection

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

### 10. Mouse Input Injection

> **Status**: ✅ Implemented (2026-09). Source: `core/automation/webapi/src/api/mouse_api.cpp`;
> every range check lives in the core `DebugMouseManager`, so all interfaces answer the same.

Drives the emulated Kempston Mouse. The mouse is **relative**: requests change its X/Y
counters, and the running program moves its own cursor by how much the counters changed.
Full command semantics, units and a worked example: [command-interface.md §11](./command-interface.md#11-mouse-input-injection).

```
POST /api/v1/emulator/{id}/mouse/move         Move by dx/dy emulated pixels      {"dx":10,"dy":-5}
POST /api/v1/emulator/{id}/mouse/press        Press and hold a button            {"button":"left"}
POST /api/v1/emulator/{id}/mouse/release      Release a button                   {"button":"left"}
POST /api/v1/emulator/{id}/mouse/click        Press, hold N frames, release      {"button":"left","frames":2}
POST /api/v1/emulator/{id}/mouse/buttons      Set the exact pressed set          {"pressed":["left","middle"]}
POST /api/v1/emulator/{id}/mouse/wheel        Scroll by notches                  {"steps":-1}
POST /api/v1/emulator/{id}/mouse/release_all  Release all, cancel pending click  (no body)
POST /api/v1/emulator/{id}/mouse/counters     Debug: write raw X/Y counters      {"x":31,"y":85}
GET  /api/v1/emulator/{id}/mouse/status       Current mouse state
GET  /api/v1/emulator/{id}/mouse/buttons      Valid button names and aliases
```

| Field | Type | Range | Notes |
|-------|------|-------|-------|
| `dx` | integer | −127 … 127 | + = right. Either `dx` or `dy` may be omitted (= 0), not both; both 0 is rejected. |
| `dy` | integer | −127 … 127 | + = **up** |
| `button` | string | `left`, `right`, `middle`, `l`, `r`, `m` | case-insensitive |
| `frames` | integer | 1 … 65535 | optional, default 2 |
| `pressed` | array of button names | — | `[]` = nothing pressed; duplicates ignored |
| `steps` | integer | −7 … 7, not 0 | + = away from the user |
| `x`, `y` | integer | 0 … 255 | both required |

Numbers must be JSON integers: `"10"` and `1.5` are rejected with 400.

**Every successful POST returns the resulting state**, so no follow-up status call is needed:

```jsonc
// POST /mouse/move {"dx":10,"dy":-5}, from reset (X=31, Y=85), shipped config Wheel=NONE
{
  "success": true,
  "dx": 10, "dy": -5,
  "message": "Mouse moved: dx=+10 dy=-5",
  "state": {
    "available": true, "present": true, "wheel_enabled": false,
    "x": 41, "y": 80,
    "buttons": {"left": false, "right": false, "middle": false},
    "button_mask": 255, "wheel": 0,
    "ports": {"FADF": 255, "FBDF": 41, "FFDF": 80},
    "pending_click": null,
    "ttd_journal": "supported"
  }
}
```

`GET /mouse/status` returns the same object as `state` plus `emulator_id`. Field meanings:

| Field | Meaning |
|-------|---------|
| `present` | A mouse is fitted: `[INPUT] Mouse=KEMPSTON` **and** feature `kempstonmouse` on. `false` = nothing answers on the ports. |
| `wheel_enabled` | `[INPUT] Wheel=KEMPSTON`: the wheel counter appears in the top 4 bits of `#FADF`. |
| `button_mask` | Internal button byte, active-low (a pressed button is bit 0). 254 = left down. |
| `ports` | What the three ports return right now, as integers. `FADF` = buttons (+ wheel), `FBDF` = X, `FFDF` = Y. |
| `pending_click` | `null`, or `{"button":"left","frames_left":1}` while a click is being held. |
| `routing` | `{"ports_decoded": bool, "note": "..."}` — would a mouse port read be decoded right now? Hidden while TR-DOS ports are accessible, when a registered peripheral claims the port family, or behind model-specific gating (Scorpion DOS trigger / Shadow Monitor beta mirrors). Same live source as `GET /ports`. |
| `ttd_journal` | `"supported"`: TTD recordings include mouse input. |

A successful response may carry a `"warning"` string: the mouse is not fitted
(`mouse not present: guest reads floating bus on the mouse ports`), or a wheel step was sent
with no wheel fitted (`no wheel fitted ([INPUT] Wheel=NONE): the guest does not see the wheel counter`).
The change is still applied.

**Errors** use the usual `{"error": "...", "message": "..."}` body, with CORS headers:

| Condition | Code | `message` example |
|-----------|------|-------------------|
| Unknown emulator id | 404 | `Emulator with specified ID not found` |
| Mouse manager missing | 500 | `Mouse manager not available` |
| Missing body field | 400 | `Missing 'button' field in request body` |
| Wrong JSON type | 400 | `'dx' must be an integer` |
| Out of range | 400 | `dx=200 out of range -127..127; split into several moves with run_frames between them` |
| Zero move or zero wheel | 400 | `move requires a non-zero dx or dy` |
| Unknown button | 400 | `Unknown button 'foo'. Valid: left, right, middle (l, r, m)` |
| TTD replay in progress | 409 | `TTD replay in progress; live mouse input refused` |

409 is returned **only** during TTD replay. Writing counters while TTD records is allowed
(the write is journalled).

**Example: click an icon 32 px right and 16 px up of the cursor, reproducibly**

```bash
ID=...   # emulator id
curl -X POST localhost:8090/api/v1/emulator/$ID/pause
curl -X POST localhost:8090/api/v1/emulator/$ID/mouse/move  -H 'Content-Type: application/json' -d '{"dx":32,"dy":16}'
curl -X POST localhost:8090/api/v1/emulator/$ID/run_frames  -H 'Content-Type: application/json' -d '{"count":2}'
curl -X POST localhost:8090/api/v1/emulator/$ID/mouse/click -H 'Content-Type: application/json' -d '{"button":"left","frames":2}'
curl -X POST localhost:8090/api/v1/emulator/$ID/run_frames  -H 'Content-Type: application/json' -d '{"count":3}'

curl -X POST localhost:8090/api/v1/emulator/$ID/mouse/wheel -H 'Content-Type: application/json' -d '{"steps":-9}'
# 400 {"error":"Bad Request","message":"steps=-9 out of range -7..7"}
```

MCP clients use the `mouse_input` tool (actions `move`, `press`, `release`, `click` with an
optional `dx`/`dy` pre-move, `buttons`, `wheel`, `release_all`, `status`), which forwards to
these routes. `counters` is not a `mouse_input` action; reach it through `invoke_api`.

## Tape Control

Full tape transport, inspection and the offline audio bridge — one-to-one with the CLI `tape` commands ([command-interface.md §10](./command-interface.md#10-tape-control-commands)), the Lua `tape_*` functions and the Python `tape_*` methods. All endpoints are scoped under `/api/v1/emulator/{id}/tape`.

| Method | Endpoint | Body | Description |
|:-------|:---------|:-----|:------------|
| `POST` | `/tape/load` | `{"path": "..."}` | Load tape image (.tap/.tzx/.csw/…) |
| `POST` | `/tape/eject` | — | Stop playback, drop image and catalog |
| `POST` | `/tape/play` | — | Start at consumption cursor; resumes in place when paused |
| `POST` | `/tape/pause` | — | Freeze mid-block; next play resumes exactly there (idempotent when already paused; 400 when not playing) |
| `POST` | `/tape/stop` | — | Terminal stop: invalidates the loaded image |
| `POST` | `/tape/rewind` | — | Rewind to block 0, image kept |
| `POST` | `/tape/seek` | `{"block": N}` | Position the head at catalog block N |
| `GET` | `/tape` | — | Full snapshot: status, file, format, state, position, cursor, fast/turbo flags, fast-load plan, `blocks[]` catalog |
| `GET` | `/tape/info` | — | Alias of `GET /tape` |
| `GET` | `/tape/blocks/{index}` | — | One catalog descriptor + `payload_bytes` and a 64-byte `preview_hex` |
| `POST` | `/tape/render` | see below | Render a tape image to WAV/FLAC audio |
| `POST` | `/tape/import` | see below | Import WAV/FLAC/MP3 as a .tzx/.tap image |

Playback `state` is one of `"idle"`, `"playing"`, `"paused"`, `"ended"` — identical strings across CLI, WebAPI, Lua and Python.

**`GET /tape` response shape** (trimmed):

```json
{
  "status": "loaded",
  "file": "/path/to/game.tap",
  "format": "tap",
  "state": "playing",
  "position": {"block": 4, "pulse": 1234, "seconds_into_block": 1.2, "block_total_seconds": 4.5},
  "cursor": 5,
  "block_count": 12,
  "total_seconds": 355.8,
  "fast_tape": true,
  "turbo_tape": true,
  "fast_load": {"verdict": "...", "eligible_blocks": 12, "accelerated_seconds": 340.0,
                "total_seconds": 355.8, "advisory": true, "summary": "..."},
  "blocks": [ {"index": 0, "kind": "header", "name": "...", "type": "Program",
                "seconds": 2.1, "playable": true, "fast_load": "yes", ...}, ... ]
}
```

**`POST /tape/render` request body:**

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `sourcePath` | string | No | inserted tape | Tape image to render; omitted renders the instance's tape (same catalog indices as `GET /tape`) |
| `blocks` | `"all"` or `[first]` or `[first, last]` | No | `"all"` | Catalog-index block selection |
| `format` | `"wav"` \| `"flac"` | No | from extension | Must agree with the `outputPath` extension |
| `sampleRate` | number | No | 44100 | 8000-192000 Hz |
| `amplitude` | number | No | 0.8 | 0.01-1.0 full-scale fraction |
| `invertLevel` | boolean | No | false | Invert signal polarity |
| `outputPath` | string | Yes | - | Must end in `.wav` or `.flac` (FLAC requires ffmpeg on the host) |

Pure file conversion — never touches emulator state. Success returns `blocks_rendered`, `duration_sec`, `samples_written`, `sample_rate`, `encoder`, `output_path`, `warnings[]`.

**`POST /tape/import` request body:**

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `sourcePath` | string | Yes | - | WAV/FLAC/MP3 recording |
| `outputPath` | string | Yes | - | Must end in `.tzx` or `.tap` |
| `target` | `"auto"` \| `"tzx"` \| `"tap"` | No | `"auto"` | Must match the `outputPath` extension |
| `hysteresis` | number | No | 0.2 | Schmitt band 0.05-0.45 of signal range (lower = tighter noise rejection) |
| `insert` | boolean | No | false | Load the produced image into the instance (same path as `/tape/load`) |

Decode + pulse extraction + recognition run headlessly. Success returns `decoder`, `sample_rate`, `samples_decoded`, `signal_edges`, `blocks_recognized`, `kind_counts`, `blocks[]`, `blocks_written`, `output_path`, `inserted`, `warnings[]`. A `.tap` save refusal keeps all recognition stats and surfaces `tap_refusal_reason` (re-target `.tzx` without re-importing).

**cURL examples:**

```bash
# Load and play
curl -X POST http://localhost:8090/api/v1/emulator/$ID/tape/load \
     -H "Content-Type: application/json" -d '{"path": "/path/to/game.tap"}'
curl -X POST http://localhost:8090/api/v1/emulator/$ID/tape/play

# Inspect
curl -s http://localhost:8090/api/v1/emulator/$ID/tape | jq .
curl -s http://localhost:8090/api/v1/emulator/$ID/tape/blocks/0 | jq .

# Seek + pause/resume
curl -X POST http://localhost:8090/api/v1/emulator/$ID/tape/seek \
     -H "Content-Type: application/json" -d '{"block": 4}'
curl -X POST http://localhost:8090/api/v1/emulator/$ID/tape/pause
curl -X POST http://localhost:8090/api/v1/emulator/$ID/tape/play

# Audio bridge
curl -X POST http://localhost:8090/api/v1/emulator/$ID/tape/render \
     -H "Content-Type: application/json" \
     -d '{"sourcePath": "game.tzx", "blocks": [2, 5], "sampleRate": 48000, "outputPath": "game.wav"}'
curl -X POST http://localhost:8090/api/v1/emulator/$ID/tape/import \
     -H "Content-Type: application/json" \
     -d '{"sourcePath": "recording.wav", "target": "tzx", "outputPath": "imported.tzx"}'
```

## Planned Endpoints (Not Yet Implemented)

### Media Operations (Implemented Separately)
```
POST /api/v1/emulator/{id}/tape/*         ✅ Implemented — see [Tape Control](#tape-control)
POST /api/v1/emulator/{id}/disk/{drive}/insert  ✅ Implemented
POST /api/v1/emulator/{id}/disk/{drive}/eject   ✅ Implemented
```

### Snapshots (Implemented Separately)
```
POST /api/v1/emulator/{id}/snapshot/save  ✅ Implemented
POST /api/v1/emulator/{id}/snapshot/load  ✅ Implemented
```

### Time-Travel Debugging

All TTD endpoints are scoped under `/api/v1/emulator/{id}/ttd/...`. Full command semantics (request bodies, response envelopes, halt reasons, session invalidation rules) live in [command-interface.md §8](./command-interface.md#8-time-travel-debugging-ttd).

| Method | Path | Body / Query | Description | Status |
| :--- | :--- | :--- | :--- | :--- |
| `GET`  | `/ttd/status` | — | Session origin (`loaded_from_file`, `source_path`, `captured_at_unix_ms`), machine (`model_id`, `model_ram_pages`), frame range, checkpoint count, write-journal size (`write_journal_records`/`_bytes`), coverage-index size (`coverage_index_frames`/`_bytes`) and memory. Schema: `TTDStatusResponse`. Always available regardless of the `timetravel` feature flag. | ✅ Implemented |
| `POST` | `/ttd/start` | — | Begin recording at the next frame boundary. Returns `202 Accepted` with `{armed: true, anchor_frame: null}` if invoked mid-frame. | 🔮 Phase 1 |
| `POST` | `/ttd/stop` | — | Stop capturing; retain history. | 🔮 Phase 1 |
| `POST` | `/ttd/clear` | — | Drop all captured data; live emulator state untouched. | 🔮 Phase 1 |
| `GET`  | `/ttd/timeline` | `?from=N&to=N&limit=N` | Paginated per-frame summary entries (dirty-page counts, event ticks, bookmark presence) for UI rendering. | 🔮 Phase 3 |
| `POST` | `/ttd/seek` | `{"frame": N}` *or* `{"tstate": T}` *or* `{"frame": N, "tstate": T}` | Seek to an absolute target point. Emulator must be paused (run-control claim enforced). Returns `{ok, reached_frame, reached_tstate, halt_reason}`. | 🔮 Phase 2 |
| `POST` | `/ttd/step` | `{"dir": "back"\|"fwd", "unit": "instruction"\|"frame", "count"?: N}` | Relative navigation. Default `count` is 1. | 🔮 Phase 2 |
| `POST` | `/ttd/find_last` | `{"addr": A, "access": "write"\|"read"\|"execute"\|"out", "value"?: V, "pc_from"?, "pc_to"?, "before"?}` | Reverse search (`FindLastAccess`). Returns `{frame, tstate, pc, value, physpage}` or `null`. | 🔮 Phase 4 |
| `POST` | `/ttd/resume_from_here` | `{"confirm": true}` | Truncate future at current (detached) position; resume live recording. Confirmation required if truncation would drop > N frames. | 🔮 Phase 2 |
| `GET`  | `/ttd/bookmarks` | — | List bookmarks. | 🔮 Phase 3 |
| `POST` | `/ttd/bookmarks` | `{"at": T, "label": "..."}` | Add a bookmark at a recorded time point. | 🔮 Phase 3 |
| `DELETE` | `/ttd/bookmarks/{id}` | — | Remove a bookmark. | 🔮 Phase 3 |

**`GET /ttd/status` response shape:**

```json
{
  "recording": true,
  "feature_enabled": true,
  "position": {"frame": 12345, "tstate": 0},
  "bounds": {"first_frame": 0, "last_frame": 12345},
  "memory": {"used_bytes": 222298112, "budget_bytes": 67108864},
  "budget_exceeded": false,
  "detached": false,
  "invalidation_reason": null
}
```

When `feature_enabled` is `false`, all fields except `recording` (which is `false`) and `feature_enabled` are omitted.

**`POST /ttd/seek` response shape:**

```json
{
  "ok": true,
  "reached_frame": 4823,
  "reached_tstate": 14982,
  "halt_reason": "target"
}
```

`halt_reason` is one of `target`, `external_event`, `out_of_range`. See [command-interface.md §8](./command-interface.md#8-time-travel-debugging-ttd) for the full semantics.

**Errors specific to TTD:**

| HTTP | Code | Meaning |
| :--- | :--- | :--- |
| 409 | `E_RUN_CONTROL_BUSY` | Another surface holds the run-control claim on this instance. Response includes `holder` (surface label). |
| 409 | `E_TTD_NOT_RECORDING` | Operation requires an active recording session; none exists. |
| 400 | `E_TTD_OUT_OF_RANGE` | Target frame/tstate is outside recorded bounds. |
| 403 | `E_TTD_FEATURE_DISABLED` | `timetravel` feature flag is off; recording/seek/replay refused. `GET /ttd/status` still works. |
| 410 | `E_TTD_SESSION_INVALIDATED` | Session was invalidated (e.g. by a load/reset); client must `POST /ttd/start` again. Includes `invalidation_reason`. |

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
const response = await fetch('http://localhost:8090/api/v1/emulators');
const data = await response.json();
console.log(data.emulators);

// Pause emulator
await fetch('http://localhost:8090/api/v1/emulators/550e8400-.../pause', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: '{}'
});

// Get registers
const regs = await fetch('http://localhost:8090/api/v1/emulators/550e8400-.../registers');
console.log(await regs.json());
```

### Python (requests library)
```python
import requests

base_url = 'http://localhost:8090/api/v1'

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
curl http://localhost:8090/api/v1/emulators

# Pause emulator
curl -X POST http://localhost:8090/api/v1/emulators/550e8400-.../pause

# Create emulator
curl -X POST http://localhost:8090/api/v1/emulators \
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
