# Recipe: Choosing and Using a Transport (WebAPI vs MCP)

The emulator exposes the same capabilities through two front doors. **Parity
rule:** every automation module serves identical information from the same
source handlers — pick whichever is closest to your caller, the data is the
same.

## Transport policy — MCP first

**MCP is the preferred transport for every recipe in this library.** Each
recipe carries a `MCP (preferred)` section and a `WebAPI` section plus a
short note at the top explaining when to use which. Reach for WebAPI only
when:

1. **A host-side Python tool drives the step** — `tools/diskinfo`,
   `tools/diskconverter`, `tools/porttrace/porttrace_capture.py` speak plain
   HTTP; pipelines that embed them stay on WebAPI.
2. **MCP is unavailable or faulty** — bridge not running (`unreal-mcp-bridge`
   absent), `:8092` busy (a busy port disables MCP for that instance with a
   console warning), or a tool answers `isError` with "WebAPI unreachable".
   Triage: `curl -s -X POST http://localhost:8092/mcp -d
   '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'` — no tool list ⇒
   fall back to WebAPI and keep going.

Even on the MCP path, endpoints without a smart tool go through the
`search_api` / `invoke_api` router — never hand-rolled HTTP from the agent.

| | WebAPI | MCP |
|:--|:--|:--|
| Address | `http://localhost:8090/api/v1/...` | `POST http://localhost:8092/mcp` (also on :8090) |
| Style | REST verbs, one endpoint per operation | 13 smart tools + schema-driven router |
| Best for | scripts, CI, curl/jq one-liners, full 212-path surface | LLM agents, IDEs, intent-level calls, local-file upload |
| Auth | none | none |
| Sessions | none (state lives in emulator instances) | none (no `Mcp-Session-Id`, fully stateless) |

## WebAPI idioms

```bash
BASE="http://localhost:8090/api/v1"

# GET with query params
curl -s "$BASE/emulator/$EMU_ID/registers" | jq '.registers'

# POST with JSON body
curl -s -X POST "$BASE/emulator/$EMU_ID/tape/play" \
     -H 'Content-Type: application/json' -d '{}' | jq .

# PUT for settings/features
curl -s -X PUT "$BASE/emulator/$EMU_ID/feature/porttrace" \
     -H 'Content-Type: application/json' -d '{"enabled": true}' | jq .
```

Error envelope (same everywhere):

```json
{ "error": "Conflict", "message": "Cannot scrub while recording is active …" }
```

HTTP codes carry meaning: `400` bad body/state, `404` unknown instance,
`409` lifecycle conflict (TTD scrub while recording, buffer reconfig while
capturing), `501` build lacks the subsystem.

Interactive endpoint browser: `http://localhost:8090/api/v1/openapi.json`
(212 paths, generated from the live build — the source of truth when a
recipe and the binary disagree).

## MCP idioms

Raw JSON-RPC 2.0 against `POST /mcp`:

```bash
MCP=http://localhost:8092/mcp

# Tool call — the pattern every MCP recipe uses
curl -s -X POST $MCP -H 'Content-Type: application/json' -d '{
  "jsonrpc":"2.0","id":1,"method":"tools/call",
  "params":{"name":"load_software","arguments":{"path":"/abs/game.trd","autostart":true}}}' | jq .
```

Every tool answers **dual content**: `content[]` (human/LLM summary) plus
`structuredContent` (machine data — the same JSON the WebAPI handler
returns). Tool-level failures are `isError: true` results with remediation
hints, not JSON-RPC errors.

Tool catalog (13):

| Tool | Highlights |
|:--|:--|
| `emulator_manage` | create/list/status/start/stop/pause/resume/reset/destroy, `list_models`, `server` |
| `load_software` | snapshots/tapes/disks, `play`, `autostart`, local-file upload |
| `control_execution` | run/pause/step/step_n/step_over/step_out, run_frames/tstates/to_interrupt, breakpoints |
| `inspect_state` | aspects: machine, registers, memory, disasm, stack, memory_banks, paging, ports, screen_ocr/image/digest, timing, rom, audio_ay/fm, fdc, mouse |
| `type_input` | type (tokenized BASIC), tap/press/release, combo, macro, release_all, list_keys |
| `mouse_input` | Kempston move/press/click/wheel |
| `time_travel` | status, bookmark_add/list/delete/seek, coverage_probe/scan/summary |
| `manage_symbols` | labels + sjasmplus listings, step_line, run_to_line |
| `debug_code` | disassemble, assemble, find_bytes, trace (calltrace) |
| `analyze_performance` | coverage_*, frame_cost, profiler suites, porttrace |
| `capture_media` | screenshot, screen_digest, GIF recording, audio capture |
| `search_api` / `invoke_api` | the escape hatch (below) |

MCP also serves static references worth reading before guessing:
`unreal://keyboard-layout`, `unreal://basic-reference`, `unreal://z80-isa`,
`unreal://trdos-commands`, `unreal://memory-map`, `unreal://emulator-state`.

### The escape hatch: `search_api` → `invoke_api`

No smart tool? Don't fall back to raw HTTP — the router exposes the entire
WebAPI over MCP:

```bash
# 1. Find the endpoint by keywords (scored OpenAPI search)
curl -s -X POST $MCP -H 'Content-Type: application/json' -d '{
  "jsonrpc":"2.0","id":2,"method":"tools/call",
  "params":{"name":"search_api","arguments":{"query":"disk catalog"}}}' | jq '.result.structuredContent'

# 2. Call it — {id} is substituted with the resolved target
curl -s -X POST $MCP -H 'Content-Type: application/json' -d '{
  "jsonrpc":"2.0","id":3,"method":"tools/call",
  "params":{"name":"invoke_api","arguments":{
    "method":"GET","path":"/api/v1/emulator/{id}/disk/A/catalog"}}}' | jq .
```

### Local-file upload (agent-side files)

`load_software` reads the file **on the MCP host** when it can; if present it
uploads the bytes embedded in the request, otherwise the path is passed
through for the emulator to load directly. WebAPI media endpoints accept
either a JSON `path` or a multipart file upload of the same shape.

## The stdio bridge (IDE agents)

`.mcp.json` at the repo root already points clients at:

```json
{ "mcpServers": { "unreal-ng": { "command": "cmake-build-release/bin/unreal-mcp-bridge" } } }
```

The bridge is a byte pipe: one stdin line = one JSON-RPC POST, one stdout
line per SSE event. No dependencies, and progress notifications arrive as
separate stdout lines for free. Point it elsewhere with `--url` or
`UNREAL_MCP_URL`.

Smoke test: `scripts/mcp-smoke-test.sh` (initialize → tools/list → live
`emulator_manage` round-trip against a running emulator).

## Choosing, quickly

- Agent workflow (the default) → **MCP tools** — they encode target
  resolution, pauses and remediation hints
- Step embedded in a host-side Python/bash pipeline → **WebAPI + jq**
- Endpoint not covered by a smart tool → **`search_api`/`invoke_api`**,
  not hand-rolled HTTP from the agent
- MCP down or misbehaving → WebAPI (see the policy above), fix MCP later
- Long captures (bounded GIF, multi-aspect inspect) → add
  `"Accept: text/event-stream"` + `_meta.progressToken` for progress frames
