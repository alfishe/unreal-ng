# MCP Server Automation (`automation_mcp`)

Model Context Protocol server for Unreal-NG: lets AI agents drive the
emulator through tools (`tools/call`), discover the full WebAPI surface
(`search_api` / `invoke_api`) and read reference documents (`resources`).

Reference design: `docs/inprogress/2026-08-17-mcp/`.

## Endpoint & transport

- `POST http://localhost:8092/mcp` — Streamable HTTP, MCP protocol
  `2025-03-26`, stateless (no `Mcp-Session-Id`).
- Request → `200` + `application/json`; notification → `202` empty;
  JSON-RPC batch → `-32600`; malformed JSON → `400` + `-32700`.
- **SSE response mode**: when the request `Accept` header includes
  `text/event-stream` **and** `params._meta.progressToken` is present, the
  answer is `200` + `text/event-stream` (chunked): one `event: message`
  frame per `notifications/progress`, the final `event: message` frame is
  the JSON-RPC response, then the stream closes. All other combinations
  answer plain JSON — existing clients are unaffected. Progress-emitting
  tools: `inspect_state` (per aspect), `debug_code trace`,
  `analyze_performance profile_report/porttrace`, `capture_media`
  bounded `every_nth` recordings.
- `GET /mcp` → `200` + `text/event-stream` server-initiated stream with a
  `: keepalive` comment every 15 s (stream reserved for emulator lifecycle
  events; none are emitted yet).
- `DELETE /mcp`, `HEAD /mcp` → `405` (`Allow: GET, POST`) — stateless
  server, no sessions to manage.
- Requires `ENABLE_WEBAPI_AUTOMATION` (enforced by CMake + runtime start
  order). `/mcp` is also reachable on :8090 — both listeners share the same
  drogon app instance (documented, harmless side effect).

## Architecture

```
                 POST /mcp (:8092)
                       │
             automation-mcp.cpp        drogon adapter: body → jsoncpp → dispatcher
                       │
             mcp-dispatcher.cpp        JSON-RPC routing (drogon-free)
             ├── mcp-tools.cpp         5 core smart tools (drogon-free)
             ├── mcp-symbols.cpp       manage_symbols (Phase 2)
             ├── mcp-analysis.cpp      debug_code / analyze_performance (Phase 2)
             ├── mcp-media.cpp         capture_media (Phase 2)
             ├── mcp-router.cpp        search_api / invoke_api + OpenAPI cache
             ├── mcp-resources.cpp     6 resources (5 embedded + dynamic)
             └── target-resolver.cpp   "auto" → create/reuse/refuse
                       │
                webapi-client.cpp      IApiCaller → drogon HttpClient
                       ▼
        GET/POST http://127.0.0.1:8090/api/v1/...   (existing WebAPI)
```

Everything below the adapter is drogon-free and depends only on jsoncpp, so
`core-tests` exercises the whole protocol stack against a `FakeApiCaller`.
The single source of truth for emulator state stays in WebAPI — the MCP layer
is protocol + orchestration only.

### Start/stop ordering

`drogon::app()` is process-global: `Automation::start()` calls
`startMCP()` **before** `startWebAPI()` (the :8092 listener must exist before
WebAPI's thread calls `run()`), and `stopMCP()` runs **after** `stopWebAPI()`.

## Tools

### Phase 1 (core 5 + router 2)

| Tool | Purpose |
|:--|:--|
| `emulator_manage` | create/list/status/start/stop/pause/resume/reset/destroy, list_models |
| `load_software` | load `.sna/.z80` snapshots, `.tap/.tzx` tapes (auto-play flag), `.trd/.scl/.fdi` disks |
| `control_execution` | run/pause/resume/step/step_n/step_over/step_out, run_frames/run_tstates/run_to_interrupt, breakpoints (add/remove/enable/disable/clear/list); the raw `skip_until` endpoint is reachable via `invoke_api` |
| `inspect_state` | aspects fan-out: machine, registers, memory, disasm, stack, breakpoints, memory_banks, screen_ocr, screen_image, screen_digest, timing |
| `type_input` | type (tokenized BASIC entry), tap/press/release, combo, macro, release_all, status, list_keys |
| `search_api` | keyword search over the OpenAPI spec (scored), optional `auto_invoke` |
| `invoke_api` | direct WebAPI call with `{id}` target substitution |

### Phase 2 (4 more)

| Tool | Purpose |
|:--|:--|
| `manage_symbols` | load_labels / list / resolve / load_listing / source_at / step_line / run_to_line (sjasmplus `.lst` support) |
| `debug_code` | disassemble / assemble (`Z80TextAssembler`) / find_bytes / trace (calltrace sessions) / porttrace |
| `analyze_performance` | coverage_* (executed-address map + gaps), frame_cost, profiler suites (calltrace/porttrace/memory) |
| `capture_media` | screenshot, screen_digest, record_video (GIF; `every_nth:"auto"` samples the digest quantum), audio_capture (RMS/peak/dominant-Hz, WAV) |

Every tool accepts `target` (emulator id or `auto`; `auto` creates a 128K
machine when none exists, refuses when several exist) and answers with dual
content: `content[]` text summary + `structuredContent` machine data.
Tool-level failures are `isError: true` results with remediation hints —
never JSON-RPC errors.

## Resources

`unreal://keyboard-layout`, `unreal://basic-reference`, `unreal://z80-isa`,
`unreal://trdos-commands`, `unreal://memory-map` (embedded markdown) and
`unreal://emulator-state` (dynamic instance overview).

## Quick test

```bash
curl -s -X POST http://localhost:8092/mcp \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call",
       "params":{"name":"emulator_manage","arguments":{"action":"list"}}}' | jq .

# Streamed progress (SSE mode): Accept + progressToken opt in
curl -N -s -X POST http://localhost:8092/mcp \
  -H 'Content-Type: application/json' -H 'Accept: text/event-stream' \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call",
       "params":{"name":"inspect_state","arguments":{"aspects":["registers","disasm"]},
                  "_meta":{"progressToken":1}}}'
```

Or through the stdio bridge (`bridge/`): `unreal-mcp-bridge` — see
`bridge/README.md` and `scripts/mcp-smoke-test.sh`.

## Testing

The drogon-free sources compile into `core-tests` (jsoncpp-only; wired in
`core/tests/CMakeLists.txt` under a `jsoncpp_static` target guard) and are
driven through a synchronous `FakeApiCaller`:

```bash
ninja -C cmake-build-release && cmake-build-release/bin/core-tests \
  --gtest_filter='McpSse_Test.*:McpDispatcher_*:McpRouter_Test.*:McpTools_Test.*'
```

Core-level companions: `Z80TextAssembler_Test`, `ListingParser_Test`,
`CoverageAnalyzer_Test`, `ScreenDigest_Test`, `CallTraceBuffer_Test`
(hot/cold buffer pipeline incl. `FlushAllHotToCold` and Reset semantics).

## Comparison with xspeccy-mcp

| Aspect | xspeccy-mcp | unreal-ng (this module) |
|:--|:--|:--|
| Transport | stdio | Streamable HTTP :8092 + stdio bridge |
| Backend | direct core calls | loopback WebAPI (single source of truth) |
| Tool count | 48 flat tools | 7 Phase 1 (11 after Phase 2) + schema-driven router |
| Context cost | ~4k tokens | ~1.6k tokens |
| Resources | — | 6 (keyboard/BASIC/Z80/TR-DOS/memory map/state) |
