# Recipe: Environment Setup and Instance Lifecycle

Every other recipe assumes this one: a freshly built emulator, a free port
8090, and one emulator instance whose id you keep in `EMU_ID`.

> **How to use the sections:** [MCP](#mcp-preferred) is the preferred path —
> it covers the whole lifecycle. Use [WebAPI](#webapi) only when embedding
> these steps in host-side Python/bash pipelines or when MCP is unavailable
> (server/bridge fault — policy and triage in
> [transports.md](transports.md)).

## MCP (preferred)

The entire lifecycle without HTTP wrangling (`tool {"args"}` notation —
envelope in [transports.md](transports.md); every tool takes `target`,
`"auto"` reuses the single instance):

```text
emulator_manage {"action":"list_models"}                 # creatable flags before choosing
emulator_manage {"action":"server"}                      # build fingerprint (branch/commit)
emulator_manage {"action":"create","model":"PENTAGON"}   # → structuredContent.emulators[0].id
control_execution {"action":"run_frames","frames":100}   # pause + synchronous advance
control_execution {"action":"step"}                      # single instruction
control_execution {"action":"resume"}                    # free-run
inspect_state    {"aspects":["screen_ocr"]}              # "did the menu appear?"
inspect_state    {"aspects":["screen_digest"]}           # deterministic frame digest
capture_media    {"action":"screenshot"}                 # PNG/GIF + metadata
emulator_manage {"action":"destroy"}                     # frees TTD history, traces, counters
```

Build and launch (sections 1-2 below) are host steps and apply to both
transports — MCP rides on the same emulator process and dies with it.

## WebAPI

The curl walkthrough below is the scripted form — right choice when a Python
or bash pipeline drives these steps directly over HTTP.

### 1. Build

```bash
cmake -S . -B cmake-build-release -G Ninja
ninja -C cmake-build-release
```

Automation (WebAPI + MCP) rides along automatically; tests/benchmarks are
opt-in (`-DTESTS=ON`, `-DBENCHMARKS=ON`) and not needed for driving the
emulator.

### 2. Launch with a clean slate

Only **one process can bind port 8090** — a stale instance silently wins or
loses the race and everything downstream gets confusing. Always start with:

```bash
# 1. Kill stale instances
pkill -9 unreal-qt 2>/dev/null || true
sleep 1

# 2. Verify port 8090 is free
lsof -i :8090 2>/dev/null && echo "WARNING: port 8090 still in use!" || echo "port free"

# 3. Start the freshly built emulator (macOS path; Linux: bin/unreal-qt)
./cmake-build-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt &
sleep 4   # WebAPI + MCP listeners come up with the app

# 4. Smoke-test both automation surfaces
curl -s http://localhost:8090/api/v1/emulator | jq '.emulators | length'
curl -s -X POST http://localhost:8092/mcp -H 'Content-Type: application/json' \
     -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}' | jq '.result.tools | length'
```

Port map (all bind without authentication — trusted network only):

| Port | Surface |
|:--|:--|
| 8090 | WebAPI `/api/v1/...` (`/mcp` also reachable here) |
| 8092 | MCP Streamable HTTP (`POST /mcp`, `GET /mcp` keepalive) |
| 8765 | CLI automation (telnet-style, `ttd`/`port-trace` commands) |

iOS/embedded hosts may use different ports (e.g. the cube app uses 8091) —
a port collision means a second host silently serves nothing.

### 3. Pick a machine model

Runtime-authoritative list with `creatable` flags:

```bash
curl -s http://localhost:8090/api/v1/emulator/models | jq '.[] | {id, creatable}'
```

Creatable on stock builds: `PENTAGON`, `48K`, `128k`, `PLUS3`, `ATM710`,
`ATM3`, `SCORPION`, `PROFSCORP`. A create request for a non-creatable model
fails with **HTTP 400 + reason** — never a silent 48K fallback.

Model guidance:

- **TR-DOS disk work** → `PENTAGON` or `128k` (Beta 128 / TR-DOS 5.03)
- **Plain tape / 48K demos** → `48K`
- **ATM Turbo 2+/3 specifics** → `ATM710` / `ATM3`

Build fingerprint (matters when comparing two captures):

```bash
curl -s http://localhost:8090/api/v1/emulator/status \
  | jq '{branch: .server.git_branch, commit: .server.git_commit}'
```

### 4. Create an instance

WebAPI:

```bash
EMU_ID=$(curl -s -X POST "http://localhost:8090/api/v1/emulator/start" \
  -H 'Content-Type: application/json' \
  -d '{"model": "PENTAGON"}' | jq -r '.id')
echo "EMU_ID=$EMU_ID"
```

MCP equivalent (returns `structuredContent.emulators[0].id` on create, and
`target: "auto"` reuses the single existing instance):

```bash
curl -s -X POST http://localhost:8092/mcp -H 'Content-Type: application/json' -d '{
  "jsonrpc":"2.0","id":1,"method":"tools/call",
  "params":{"name":"emulator_manage","arguments":{"action":"create","model":"PENTAGON"}}}' | jq .
```

### 5. Instance lifecycle

| Operation | WebAPI | MCP action |
|:--|:--|:--|
| List instances | `GET /api/v1/emulator` | `emulator_manage` `list` |
| One instance | `GET /api/v1/emulator/{id}` | `emulator_manage` `status` |
| Pause / resume | `POST .../{id}/pause` / `resume` | `emulator_manage` `pause` / `resume` |
| Reset | `POST .../{id}/reset` | `emulator_manage` `reset` |
| Hard stop | `POST .../{id}/stop` | `emulator_manage` `stop` |
| Destroy (free memory) | `DELETE /api/v1/emulator/{id}` | `emulator_manage` `destroy` |
| Switch model | `POST .../{id}/model {"model":"48K"}` | recreate instead |

Notes agents trip over:

- `Pause` is **asynchronous** — the Z80 thread parks at the next frame
  boundary. State-mutating endpoints (TTD seek, tape snapshot) do their own
  pause+confirm internally; only roll-your-own loops need to care.
- `stop` freezes the instance; `destroy` deletes it. TTD history, traces and
  counters die with the instance — dump them to `scratch/` first.
- Emulator ids are UUIDs; MCP `target` also accepts a **unique id prefix**.

### 6. Run control (needed by later recipes)

```bash
# Run exactly N frames (pause + synchronous advance; breakpoints skipped by default)
curl -s -X POST "$BASE/emulator/$EMU_ID/run_frames" \
     -H 'Content-Type: application/json' -d '{"frames": 100}' | jq .

# Single-step one instruction
curl -s -X POST "$BASE/emulator/$EMU_ID/step" | jq '.registers.pc'

# Resume free-running
curl -s -X POST "$BASE/emulator/$EMU_ID/resume" | jq .
```

MCP: `control_execution` with actions `run`, `pause`, `resume`, `step`,
`step_n`, `run_frames`, `run_tstates`, `run_to_interrupt`.

### 7. Verify what is on screen (assertion primitives)

```bash
# Screen text via OCR — great for "did the menu appear?"
curl -s "$BASE/emulator/$EMU_ID/capture/ocr" | jq '.text'

# Deterministic content digest — great for "same frame as last run?"
curl -s "$BASE/emulator/$EMU_ID/state/screen/digest" | jq '.digest'

# Screen image capture (GIF by default; add ?format=png&mode=full for PNG with border)
curl -s "$BASE/emulator/$EMU_ID/capture/screen?format=png" | jq '{format, width, height, size}'
# the base64 image bytes are in the "data" field of the same response
```

MCP: `inspect_state` with `aspects: ["screen_ocr"]` /
`["screen_digest"]` / `["screen_image"]`.

### 8. Teardown

```bash
curl -s -X DELETE "http://localhost:8090/api/v1/emulator/$EMU_ID" | jq .
pkill -9 unreal-qt 2>/dev/null || true
```

Kill the app when a recipe sequence is finished; long-lived instances with
TTD recording or traces accumulate hundreds of MB before you notice.
