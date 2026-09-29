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


### 5b. ZX-Poly machine
**Endpoints**: `POST /api/v1/emulator/start` with `zxpoly`, `GET /api/v1/emulator/{id}/zxpoly`
**Description**: A ZX-Poly machine is four synchronized instances of one
model (default `PENTAGON`). Module 0, the master, is the machine every
endpoint addresses and the id returned. Modules 1-3 are hidden members: they
are not listed, but are reachable by id. Deleting the master removes all four.
Recipe: [.recipe/machines/zxpoly.md](../../../../.recipe/machines/zxpoly.md).

```bash
curl -X POST http://localhost:8090/api/v1/emulator/start \
  -H "Content-Type: application/json" \
  -d '{"model": "PENTAGON", "zxpoly": {"file": "/path/to/Alien8.zxp"}}'
```

The same machine also starts by name, like any model. There are three
configurations; `GET /api/v1/emulator/models` lists them with `"zxpoly":
true` and `base_model`:

| Configuration | Modules | Runs |
|:--|:--|:--|
| `ZXPOLY-48K` | 4 × 48K | replicated 48K software; ZX-Poly editions are refused (they need 128K paging) |
| `ZXPOLY-128K` | 4 × 128K | `.zxp`, the Test ROM |
| `ZXPOLY-PENTAGON` | 4 × Pentagon | everything, including multiloader disks (TR-DOS) |

```bash
curl -X POST http://localhost:8090/api/v1/emulator/start \
  -H "Content-Type: application/json" -d '{"model": "ZXPOLY-128K"}'
```

A configuration fixes the RAM size, so `ram_size` with a configuration name
returns 400.

The `zxpoly` field takes one of two forms:

- `true`: the bare machine;
- `{"file": path}`: a `.zxp` snapshot, a `.prom` ZX-Poly ROM image (the Test
  ROM), or a multiloader disk (`.trd`/`.scl`, which needs a model with
  TR-DOS).

The 201 response carries the group status under `zxpoly`. Every member's
`GET /api/v1/emulator/{id}` carries `"zxpoly": {"module", "master_id",
"locked", "video_mode"}`.

`GET /api/v1/emulator/{id}/zxpoly` (any member id):
```json
{
  "master_id": "e5dad078-...", "locked": true, "slaves_running": false, "parallel_slaves": true,
  "port_3d00": 157, "video_mode": 7,
  "modules": [ {"module": 0, "id": "e5dad078-...", "registers": [0, 0, 0, 0]},
               {"module": 1, "id": "f67691a2-...", "registers": [18, 0, 0, 0]}, "..." ],
  "divergence": {"diverged": false}
}
```
`divergence` compares each slave's control state (PC, SP, I, IM, IFF1, HALT,
T-state, `#7FFD`) with the master's. A 404 means no such instance, or it is
not a ZX-Poly machine.

### 5b. Switch Model (validate-first)
**Endpoint**: `POST /api/v1/emulator/{id}/model` (body `{"model": "...", "ram_size": N, "stranded": "refuse|save|discard|keep"}`)  
**Description**: The request is validated BEFORE the current instance is stopped/removed: an unknown model, unsupported RAM or non-creatable model returns `400` and the current emulator keeps running untouched. A successful switch stops the old instance, creates and starts a new one (different ID) and returns the machine identity block for the new instance.

The media follow ([media.md → Model switch](../../../features/media.md#model-switch)): each medium goes into the slot with the same id on the new machine, unsaved writes included. `media` in the reply lists the slot ids `attached` (followed), `detached` (no slot, unsaved writes kept) and `closed` (no slot, nothing unsaved). A medium with unsaved writes the new model has no slot for answers `409` (`code: "dirty"`, the media in `stranded`) and changes nothing, unless `stranded` says `save`, `discard` or `keep`.

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
GET  /api/v1/emulator/{id}/state/screen        Screen state: video mode, resolution, border, shadow screen, active screen + RAM pages, contention, flash (?verbose=true adds per-screen RAM page + Z80 mapping and decoded #7FFD)
GET  /api/v1/emulator/{id}/state/screen/mode   Video mode: picture format, memory layout, displayed RAM pages, #EFF7/#DFFD/#FF77
GET  /api/v1/emulator/{id}/state/screen/flash  FLASH phase and timing
GET  /api/v1/emulator/{id}/state/screen/digest  Stable screen-content digest (range or banks, border folding; ?mode=active follows the displayed surface)
GET  /api/v1/emulator/{id}/ports             Static port map + live routing flags (which devices answer which ports, under which gates); rows carry semantic `tags` (memory/rom/screen/sound_ay/…) and the `latch` live-value binding (p7FFD, p1FFD, … or null)
GET  /api/v1/emulator/{id}/state/paging      Unified paging state (P1-2): tagged latch rows with live values + §5.1 decoded bits, 4-bank table with ROM `name`/`role`/`signature` (§5.2 — role≠name is the wrong-ROM signal), `paging_locked`, `trdos_active`; on `PROFI` the `pDFFD` latch decodes to `extended_ram_bank`, `sco`, `worom`, `cpm`, `scr`, `video_512x240` (see [profi-1024.md](../../../hardware/profi-1024.md))
GET  /api/v1/emulator/{id}/video/beam         Current raster position and beam zone
GET  /api/v1/emulator/{id}/frame_cost         Per-frame halt/run cost accounting
GET  /api/v1/emulator/{id}/state/audio/ay      AY/SSG chips overview (core DeviceState report)
GET  /api/v1/emulator/{id}/state/audio/ay/{n}  One AY/SSG chip, registers and channels decoded
GET  /api/v1/emulator/{id}/state/audio/fm      TurboSound FM board latches + both YM2203 summaries (404 without TSFM)
GET  /api/v1/emulator/{id}/state/audio/fm/{n}  One YM2203 FM half: mode, timers, channels, operators, envelopes, key-on
GET  /api/v1/emulator/{id}/state/audio/gs      General Sound / NeoGS: mailbox, page, DAC channels, card CPU, "neogs" block (404 without a card; ?ram=1 adds the #4000-#7FFF window)
GET  /api/v1/emulator/{id}/state/audio/covox   Covox / SoundDrive: fitment, ports this model decodes, Beta-128 shared ports, DAC latches (404 without Covox)
GET  /api/v1/emulator/{id}/state/audio/channels  Audio mixer overview: per-device levels + master (muted, sample_rate_hz = live core rate, channels, bit depth)
GET  /api/v1/emulator/{id}/state/fdc           Beta Disk WD1793: registers, status bits, FSM, signals, drives (404 without Beta Disk)
GET  /api/v1/emulator/{id}/state/contention    Memory contention: rule, switch, effective, interface, I/O rule, contended slots, per-kind waits (debug mode)
```

The device reports (AY, FM, GS, Covox, FDC) are built once in the core
(`core/src/emulator/state/devicestate.h`) and are byte-for-byte the same
data the CLI, Lua, Python and MCP return — see
[command-interface.md §3.3](./command-interface.md#33-device-state-reports-ay--ssg-turbosound-fm-beta-disk-fdc)
for the field list. The three screen reports come from the same core
(`DeviceState::Screen` / `ScreenMode` / `ScreenFlash`, fields in
[command-interface.md §6.6](./command-interface.md#66-screen-configuration)); `/state/screen` keeps `is_128k` and `display_mode`, and
`/state/screen/mode` its per-mode flags (`eff7_16col`, `eff7_hwmc`, `eff7_512`, `overscan`,
`profi_hires`), as aliases for existing clients. Every endpoint also has an active-emulator form without
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

While a time-travel (TTD) recording runs, a memory write (`PUT /memory/{addr}`) and a physical page write (`PUT /memory/{type}/{page}/{offset}`) record a `debugger_edit` marker, a replay barrier, and briefly pause a running emulator for the edit so the recording sees it. No marker is written when no recording runs.

### Settings Management
```
GET  /api/v1/emulator/{id}/settings            All settings, grouped (io_acceleration, disk_interface, audio) plus turbo_mode, speed, turbo_active, turbo_audio
GET  /api/v1/emulator/{id}/settings/{name}     One setting value (fast_tape, turbo_tape, fast_disk, turbo_mode, speed, turbo_audio, turbo_active, trdos_traps, audio_rate, ...)
PUT  /api/v1/emulator/{id}/settings/{name}     Update a setting (body: {"value": ...})
```

Settings are per-instance and runtime-only — nothing is written to the ini.

**Speed and turbo.** The same switches as CLI `setting speed` / `setting turbo_audio` and Lua/Python `set_speed` / `get_speed`:

| Setting | Value | PUT answers |
| :--- | :--- | :--- |
| `speed` | Host speed multiplier `1`, `2`, `4`, `8` or `16` (a JSON number, or a decimal or `"0x.."` string). The emulated machine runs N times faster; applied at the next frame. `GET /settings/speed` also lists `allowed`. | 400 for any other value. 409 for anything but `1` while a TTD recording runs. A change on a stopped or loaded TTD session drops that session's history (frame timing is part of the recording); re-selecting the current speed changes nothing. |
| `turbo_mode` | Bool: run as fast as the host allows (the `turbomode` feature). | 409 for `true` while a TTD recording runs. |
| `turbo_audio` | Bool: keep generating audio (at a raised pitch) in turbo mode. Off by default. Applied at once if turbo is running. | — |
| `turbo_active` | Read-only bool: the engine runs unthrottled right now, because of `turbo_mode` or because turbo tape is warping a load. | 400 (read-only; switch turbo with `turbo_mode`). |

```json
PUT /api/v1/emulator/{id}/settings/speed
{"value": 4}
// -> {"name": "speed", "value": 4, "message": "Speed multiplier set to 4x (applied at the next frame)", "emulator_id": "..."}
```

`fast_tape`, `turbo_tape` and `fast_disk` in `GET /settings` show the state in effect: they read as `false` while TTD holds them off (see Feature Management below).

**audio_rate** controls the core audio sample rate — the rate the DSP stack
and every capture/recording run at. The value is one of 44100, 48000, 88200,
96000, 176400, 192000 or `"auto"`:

```json
PUT /api/v1/emulator/{id}/settings/audio_rate
{"value": 96000}          // pin: overrides even a connected audio device
{"value": "auto"}         // release: follow device rate > [SOUND] CoreRate > 44100
```

- Applied at the next frame boundary; deferred while a recording is in progress.
- Unsupported values fail with HTTP 400.
- Same switch everywhere: CLI `setting audio_rate`, Lua/Python `set_audio_rate`.
- `POST /video/record` has no audio-rate option — pin via this endpoint first
  when a recording must be stamped with a specific rate:
  `PUT /settings/audio_rate {"value":48000}` → `POST /video/record {"action":"start"}`.

`GET /settings` returns the audio group alongside the others:

```json
"audio": { "audio_rate": "auto", "core_rate_hz": 44100 }
```

The live core rate is also visible in `GET /state/audio/channels` →
`master.sample_rate_hz` (it follows the pin).

### Feature Management
```
GET      /api/v1/emulator/{id}/features          Every feature with its state in effect
GET      /api/v1/emulator/{id}/feature/{name}    One feature (id or alias)
PUT|POST /api/v1/emulator/{id}/feature/{name}    Enable/disable (body: {"enabled": true})
```

The features, their aliases and defaults are listed in [command-interface.md §5](./command-interface.md#5-feature-management--configuration). Time-travel debugging holds some features off: `turbomode` while a recording runs, and `fasttape`, `turbotape`, `fastdisk` while a recording runs, history is replayed, or the machine sits in history. A held feature reads as `enabled: false` in both GET routes. Enabling it answers **409 Conflict** (`{"error": "Conflict", "message": "Cannot enable 'turbomode' while TTD recording is active or history is being replayed"}`); an unknown feature answers 404 with `available_features`; a body without `enabled` answers 400. Disabling always succeeds.

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
| `POST` | `/tape/load` | `{"path": "..."}` | Load a tape (.tap/.tzx/.spc/.sta/.ltp/.zxt) or a folder into the tape slot; 400 names the reason |
| `POST` | `/tape/eject` | — | The tape leaves the tape slot; 409 while a TTD recording runs |
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

All TTD endpoints are scoped under `/api/v1/emulator/{id}/ttd/...` (`{id}` is the emulator UUID or index). Handlers: `core/automation/webapi/src/api/ttd_api.cpp`; OpenAPI schemas: `core/automation/webapi/src/openapi/openapi_ttd.inc` (`TTDStatusResponse`, `TTDSeekRequest`, …). The CLI equivalents and the background are in [command-interface.md §8](./command-interface.md#8-time-travel-debugging-ttd).

**Read the session rules first:** [command-interface.md → TTD Session Rules](./command-interface.md#ttd-session-rules) (states, 409 while recording, what wipes a session, reset keeps history, markers vs. bookmarks, the acceleration lock). The short version:

- States are `idle`, `recording`, `detached` (positioned in history, emulator paused).
- `seek`, `step-back`, `step-forward`, `step-instruction`, `find-last`, `reverse-step` and `reverse-continue` return **409 Conflict** while recording — call `POST /ttd/stop` first.
- `start` switches the `timetravel` feature on by itself.
- While recording, snapshot/tape/disk load (and disk autostart), disk create, `invalidate`, switching `timetravel`/`debugmode` off and a GS `switch_personality` return **409 Conflict** whose `message` says why and to stop the recording first. On a stopped session those loads, ROM reload, a host speed change and `invalidate` drop the history; a reset stops the recording and keeps it. `GET /ttd/status` → `last_drop_reason` names what dropped the last history.
- While recording (and through `detached`) the host speed is locked to 1x, turbo mode is off, and fast tape / turbo tape / fast disk read as off.

Positions are always a pair `frame` (absolute frame number) + `tinframe` (offset inside the frame in T-states at the machine's top CPU clock: plain T-states on machines without a hardware turbo; ×2 on Scorpion/ATM Turbo 2+, ×4 on ZX-Evo - see [command-interface.md → Time](./command-interface.md#ttd-session-rules)).

| Method | Path | Body / Query | Response fields | Status |
| :--- | :--- | :--- | :--- | :--- |
| `GET`  | `/ttd/status` | — | See "status response" below. | ✅ Implemented |
| `POST` | `/ttd/start` | Optional `{"mode": "gaming"\|"development", "enable_write_journal": bool}`. `gaming` = no write journal; `development` (default) = journal on. `enable_write_journal` wins over `mode`. Ignored when already recording. | `started`, `already_active`, `state`, `write_journal_enabled` | ✅ Implemented |
| `POST` | `/ttd/stop` | — | `stopped` (false if it was not recording), `state` | ✅ Implemented |
| `POST` | `/ttd/invalidate` | Optional `{"reason": "..."}` (default `"WebAPI invalidate"`) | `invalidated`, `reason`, `state`. 409 while recording (stop first). | ✅ Implemented |
| `POST` | `/ttd/seek` | `{"frame": N, "tinframe"?: T}` *or* `{"bookmark": "label"}` | `reached`, `arrived_at {frame, tinframe}`, `halt_reason`, `blocking_marker {frame, tinframe, kind, reason}` (only when `halt_reason` is `external_event`), `state`, `bookmark` (bookmark seeks). 400 without `frame`/`bookmark` or with an empty label; 404 for an unknown bookmark. | ✅ Implemented |
| `POST` | `/ttd/step-back` | — | `stepped`, `frame`, `tinframe` (one frame back, same position inside the frame) | ✅ Implemented |
| `POST` | `/ttd/step-forward` | — | `stepped`, `frame`, `tinframe` (one frame forward, inside recorded history) | ✅ Implemented |
| `POST` | `/ttd/step-instruction` | Optional `{"dir": "back"\|"forward"\|"fwd"}` (default `back`) | `stepped`, `dir` (`back`/`forward`), `frame`, `tinframe` | ✅ Implemented |
| `POST` | `/ttd/reverse-step` | Exactly one of `{"count": N}` (instructions) or `{"tstates": T}` (lands on the nearest instruction start at or before the target). 400 for both or neither. | `reached`, `mode` (`count`/`tstates`), `frame`, `tinframe` | ✅ Implemented |
| `POST` | `/ttd/reverse-continue` | `{"pcs": [A, B, ...]}` — non-empty array of addresses 0..65535: numbers, or strings (decimal, `"0x.."`, `"#.."`, `"$.."`) | `matched`, `pc`, `frame`, `tinframe`, `blocked_by_marker {kind, reason, frame, tinframe}` (only when a marker stopped it), `covered_from`, `covered_from_tinframe`, `covered_to`, `covered_to_tinframe` (the searched span; see [Search window](./command-interface.md#ttd-session-rules)) | ✅ Implemented |
| `POST` | `/ttd/find-last` | See "find-last request" below | `found`; on a hit `frame`, `tinframe`, `pc`, `value`, `phys_page` (`null` for ROM / no RAM page), `access`; when a marker blocked the search `blocked: true`, `marker_frame`, `marker_tinframe`, `marker_kind`, `marker_reason`; always (unless refused) `covered_from`, `covered_from_tinframe`, `covered_to`, `covered_to_tinframe` - the searched span | ✅ Implemented |
| `POST` | `/ttd/resume` | Optional `{"frame": N, "tinframe"?: T}`; default is the current position | `resumed`, `frame`, `tinframe`, `state`. Truncates everything after the point and records again; resumes the emulator on success. Fails (`resumed: false`) from `idle` — seek first. | ✅ Implemented |
| `GET`  | `/ttd/position` | — | `current {frame, tinframe}`, `session_end {frame, tinframe}`, `state` | ✅ Implemented |
| `GET`  | `/ttd/markers` | — | `count`, `markers[] {frame, tinframe, kind, reason}` — kinds `tape_control`, `disk_write`, `debugger_edit` (a tool edit made while recording), `other`; `hardware_reset` is reserved and never written (a reset stops the recording instead) | ✅ Implemented |
| `GET`  | `/ttd/bookmarks` | — | `count`, `bookmarks[] {frame, tinframe, label}` (time-sorted) | ✅ Implemented |
| `POST` | `/ttd/bookmarks` | `{"label": "...", "frame"?: N, "tinframe"?: T}` — no `frame` = current position. Label: non-empty, at most 63 characters, unique per session. | **201** with `added`, `label`, `frame`, `tinframe`. 400 for a missing/empty/overlong label; 409 for a duplicate label or a position outside the timeline. | ✅ Implemented |
| `DELETE` | `/ttd/bookmarks/{label}` | — | `removed`, `label`; 404 for an unknown label | ✅ Implemented |
| `POST` | `/ttd/dump` | `{"path": "..."}` | `ok`; on success `path`, `bytes`; on failure `error`. 400 without `path`, 500 if the file cannot be opened. | ✅ Implemented |
| `POST` | `/ttd/load` | `{"path": "..."}` | `ok`, `path`, `checkpoint_count`, `session_start_frame`, `current_end_frame`, `state` (`idle`). 400 without `path` or when the file is refused (`ok: false`, `error` — e.g. a model mismatch naming both model ids); 404 if the file cannot be opened. | ✅ Implemented |
| `GET`  | `/ttd/coverage/probe` | `?frame=N&kind=executed\|written\|read&addr_from=A1&addr_to=A2&phys_page=P` | `frame`, `kind`, `addr_from`, `addr_to` (as `"0x%04X"` strings), `phys_page` (if given), `touched`, `index_available`. Frames outside the covered window return `index_available: false, touched: false`. 400 for missing `frame`, invalid `kind`, `addr_from > addr_to`, `phys_page > 255` or non-numeric values. | ✅ Implemented |
| `GET`  | `/ttd/coverage/scan` | `?from_frame=F1&to_frame=F2&kind=…&addr_from=A1&addr_to=A2&phys_page=P&limit=L` (default limit 200) | `kind`, `addr_from`, `addr_to`, `phys_page`, `frames[]`, `first_match`, `last_match`, `matching_frames`, `scanned_frames`, `truncated`, `index_available`, and `covered_from`/`covered_to` when the index is available. Same 400 validation as probe (plus `limit >= 1`). | ✅ Implemented |
| `GET`  | `/ttd/coverage/summary` | `?from_frame=F1&to_frame=F2&kind=K&bucket_size=B&limit=L` (default limit 100; `bucket_size=0` = automatic) | `from_frame`, `to_frame`, `bucket_size`, `bucket_count`, `buckets[] {frame_start, frame_end, executed_distinct, written_distinct, read_distinct, has_keyframe}`, `index_available`, and `covered_from`/`covered_to` when available | ✅ Implemented |

Query parameters for the coverage routes accept decimal or `0x` hex. `to_frame` defaults to the session end, the address range to the whole 64K, and `kind` to `executed` (probe/scan) or all kinds (summary).

There are no `/ttd/clear`, `/ttd/timeline`, `/ttd/step` or `/ttd/resume_from_here` routes: use `/ttd/invalidate`, `/ttd/step-back` / `/ttd/step-forward` / `/ttd/step-instruction` and `/ttd/resume`.

**`GET /ttd/status` response shape:**

```json
{
  "state": "idle",
  "ttd_available": true,
  "session_start_frame": 98,
  "current_end_frame": 397,
  "checkpoint_count": 301,
  "page_store_bytes": 40960,
  "page_store_used_bytes": 665600,
  "baseline_frames_captured": 2159,
  "session_heap_bytes": 1043968,
  "loaded_from_file": false,
  "source_path": "",
  "captured_at_unix_ms": 0,
  "model_id": 0,
  "model_ram_pages": 8,
  "write_journal_enabled": true,
  "write_journal_complete": false,
  "write_journal_wrapped": false,
  "write_journal_gap": {"reason": "debug mode switched off during the recording", "frame": 240, "tinframe": 18211},
  "write_journal_records": 729025,
  "write_journal_bytes": 8748300,
  "coverage_index_frames": 300,
  "coverage_index_bytes": 13926,
  "bookmark_count": 0,
  "last_drop_reason": null
}
```

`state` is `idle`, `recording` or `detached`. `last_drop_reason` is `null` until something drops a history, then names it (e.g. `"snapshot-load"`). When the build has no TTD engine the response still comes back with `ttd_available: false`, `state: "idle"` and zero counters. Field meanings: [command-interface.md → Status fields](./command-interface.md#status-fields).

**`POST /ttd/seek` response shape:**

```json
{
  "reached": false,
  "arrived_at": {"frame": 4700, "tinframe": 0},
  "halt_reason": "external_event",
  "blocking_marker": {"frame": 4700, "tinframe": 1234, "kind": "tape_control", "reason": "tape play"},
  "state": "detached"
}
```

`halt_reason` is one of `target`, `external_event`, `out_of_range`. The emulator is left paused after a seek; `POST /ttd/resume` resumes it.

**find-last request:**

```json
{
  "addr": "0x5B00",
  "access": "write",
  "value": 7,
  "pc_from": "#8000", "pc_to": "$8FFF",
  "phys_page": 5,
  "before_frame": 4823, "before_tin": 0
}
```

| Field | Meaning |
| :--- | :--- |
| `addr` | Single address. Or use `addr_from` / `addr_to` for a range (either end may be omitted: 0 / 0xFFFF). |
| `access` | `write` (default), `read`, `execute`, `io`. An unknown value falls back to `write`. |
| `value` | Exact byte value, 0..255. |
| `pc_from`, `pc_to` | Only accesses made by an instruction whose PC is in this range. |
| `phys_page` | 0..255 (JSON number only). Pins the search to one physical RAM page, so a banked address does not answer with another page's write. |
| `before_frame`, `before_tin` | Search at or before this point instead of the current position. `before_tin` is only read together with `before_frame`. |

At least one of `addr`, `addr_from`, `addr_to`, `pc_from`, `pc_to`, `value` is required (400 otherwise). `addr`, `addr_from`, `addr_to`, `value`, `pc_from` and `pc_to` may be JSON numbers or strings: decimal, `"0x.."`, `"#.."` or `"$.."`; an out-of-range value is a 400.

**Errors specific to TTD** (all bodies are `{"error": ..., "message": ...}` or `{"error": ...}`):

| HTTP | When |
| :--- | :--- |
| 400 | Bad request body or query (missing required field, bad number, bad label, both `count` and `tstates`, empty `pcs`, refused `.ttd` load). |
| 404 | Unknown emulator id, unknown bookmark label, or `.ttd` file not found on load. |
| 409 | Seek/step/find-last/reverse-* while recording (`{"error": "Conflict", "message": ..., "state": "recording"}`); duplicate bookmark label or bookmark outside the timeline. |
| 500 | Emulator context unavailable, or `dump` could not open the output file. |
| 501 | The build has no TTD engine. |
| 503 | The emulator is shutting down. |

Everything else is reported in a 200 body: `reached: false`, `stepped: false`, `resumed: false`, `found: false`, `matched: false`.

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
