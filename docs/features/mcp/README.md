# MCP Server (Model Context Protocol)

> AI-agent control surface for Unreal-NG: 11 smart tools + 2 router tools,
> 7 resources, Streamable HTTP with optional SSE progress streaming, and a
> zero-dependency stdio bridge for IDE integration.

## Overview

The MCP server lets LLM-driven agents (Claude Desktop, Claude Code, Cursor,
Cline, Continue, VS Code, custom SDK clients) operate the emulator the way a
human does — through high-level intent ("load this tape and run it", "why is
this frame slow?", "record a GIF of the effect") instead of raw register
poking. It sits on top of the existing WebAPI: every tool fans out to loopback
HTTP calls and forwards the payloads verbatim, so MCP clients get the same
information WebAPI clients get — identical data from the same handlers
(parity rule: all automation modules are equally important).

Design facts at a glance:

| Property | Value |
|:--|:--|
| Protocol | MCP `2025-03-26` (JSON-RPC 2.0) |
| Transport | Streamable HTTP — JSON or SSE answers, stateless |
| Endpoint | `http://localhost:8092/mcp` (also reachable on :8090) |
| stdio | via `unreal-mcp-bridge` (one process, no deps) |
| Tools | 11 smart + `search_api`/`invoke_api` schema-driven router |
| Resources | 6 (`unreal://…`) |
| Sessions | none — no `Mcp-Session-Id`, every request is self-contained |
| Prompts | none advertised (empty `prompts/list`) |
| Token cost | ~1.6k for the tool catalog |

Implementation: [`core/automation/mcp/`](../../../core/automation/mcp/README.md)
(module internals, tool→endpoint mapping, test layout). Client hookup:
[integration-guide.md](integration-guide.md). Bridge details:
[bridge README](../../../core/automation/mcp/bridge/README.md).

## Architecture

```mermaid
graph TB
    subgraph "Agents & Clients"
        IDE["IDE / Claude Desktop / Cursor / SDK clients"]
        HTTPC["HTTP client (curl, scripts)"]
    end

    subgraph "unreal-mcp-bridge (optional, zero deps)"
        STDIO["stdio: newline JSON-RPC"]
        TRANSLATE["SSE → line translation"]
    end

    subgraph "Emulator process (shared drogon app)"
        MCPPOST["POST /mcp :8092<br/>JSON or SSE answer"]
        MCPGET["GET /mcp :8092<br/>keepalive stream"]
        ADAPTER["automation-mcp.cpp<br/>(drogon adapter)"]
        DISPATCH["mcp-dispatcher.cpp<br/>(drogon-free)"]
        TOOLS["tools + router + resources<br/>(drogon-free, jsoncpp only)"]
        CLIENT["webapi-client.cpp<br/>IApiCaller"]
        WEBAPI["WebAPI :8090<br/>/api/v1/..."]
        CORE["Emulator core<br/>(EmulatorManager)"]
    end

    IDE -- "stdio lines" --> STDIO --> TRANSLATE -- "one POST per line" --> MCPPOST
    HTTPC -- "POST / GET" --> MCPPOST
    HTTPC -- "curl -N (keepalive)" --> MCPGET

    MCPPOST --> ADAPTER --> DISPATCH --> TOOLS --> CLIENT --> WEBAPI --> CORE
    MCPGET --> ADAPTER

    TOOLS -. "progress via progressToken<br/>(SSE frames)" .-> ADAPTER
```

Key properties:

- **Drogon-free core**: the dispatcher, all tools, the router and resources
  depend only on jsoncpp — `core-tests` drives the entire protocol stack
  against a synchronous `FakeApiCaller`, no HTTP in sight.
- **Loopback orchestration**: tools never touch the emulator directly; they
  call `http://127.0.0.1:8090/api/v1/...` through `IApiCaller`. One transport,
  one truth.
- **Bridge stays a byte pipe**: it never parses JSON; SSE answers are
  translated at the line level (each `data:` payload → one stdout line).

## Transport matrix

| Path | Client speaks | Request framing | Answer | Best for |
|:--|:--|:--|:--|:--|
| stdio bridge | newline JSON-RPC on stdin | one POST per line | JSON body → one stdout line; SSE body → one line per event | Claude Desktop, IDEs, sandboxed tool environments |
| HTTP JSON (`curl`, SDKs) | HTTP POST | JSON-RPC body | `application/json` (202 + empty for notifications) | scripts, tests, simple clients |
| HTTP SSE | HTTP POST + `Accept: text/event-stream` + `_meta.progressToken` | JSON-RPC body | `text/event-stream`: `notifications/progress` frames, final frame = response | long tools (bounded recordings, multi-aspect inspection) |
| GET stream | HTTP GET | — | `text/event-stream`, `: keepalive` comment every 15 s | future lifecycle events; liveness probing |

The SSE answer mode is strictly opt-in on both ends: without the `Accept`
header **or** without a `progressToken` the server answers plain JSON — every
existing client keeps working unmodified. `DELETE /mcp` and `HEAD /mcp`
return `405` (`Allow: GET, POST`): the server is stateless, there are no
sessions to manage.

## Tool catalog

All tools accept `target` (emulator id or `"auto"`) and answer with dual
content: `content[]` (human/LLM text summary) + `structuredContent`
(machine-readable data). Tools that perform real multi-step work report
progress when the request carries a `_meta.progressToken` (see
[Protocol details](#protocol-details)).

| Tool | Purpose | Progress |
|:--|:--|:--|
| `emulator_manage` | create/list/status/start/stop/pause/resume/reset/destroy, `list_models` (per-model `creatable` flags), `server` (build fingerprint + `models_creatable`); responses carry machine identity | — |
| `load_software` | `.sna/.z80` snapshots, `.tap/.tzx` tapes (auto-play), `.trd/.scl/.fdi` disks | — |
| `control_execution` | run/pause/resume/step/step_n/step_over/step_out, `run_frames`/`run_tstates`/`run_to_interrupt`, breakpoints | — |
| `inspect_state` | aspect fan-out: machine, registers, memory, disasm, stack, breakpoints, memory_banks, paging (tagged latches + bank table), ports (static port map with semantic tags + live routing), video (video mode report), screen (screen state, verbose), screen_flash (FLASH timing) — screen reports per command-interface.md §6.6, screen_ocr, screen_image, screen_digest, timing, rom, audio_ay, audio_fm, audio_gs, fdc, mouse (device reports, see command-interface.md §3.3), ttd (time-travel session: state, recorded range, checkpoints, current position) | one notification per aspect |
| `type_input` | type (tokenized BASIC entry), tap/press/release, combo, macro, `release_all`, status, `list_keys` | — |
| `time_travel` | time-travel debugging: record, then seek / step / search backward through the recording; `.ttd` files, bookmarks, coverage index — see [Time-travel debugging](#time-travel-debugging) | — |
| `manage_symbols` | `load_labels`, list, resolve, `load_listing`, `source_at`, `step_line`, `run_to_line` (sjasmplus `.lst`) | — |
| `debug_code` | disassemble, assemble (two-pass, labels), `find_bytes`, `trace` (calltrace sessions), `porttrace` | `trace`: per phase (start/run/stop/read) |
| `analyze_performance` | coverage_* (+gaps), `frame_cost`, profiler suites, `profile_report`, `porttrace` | `profile_report` + `porttrace`: per phase |
| `capture_media` | screenshot (PNG/GIF + metadata), `screen_digest`, video recording (GIF native, `every_nth:"auto"` quantum sampling), `audio_capture` (RMS/peak/dominant-Hz, WAV) | bounded `every_nth` recordings: captured-frame counter (throttled to ~20 updates) |
| `search_api` | keyword search over the OpenAPI spec (scored), optional `auto_invoke` | — |
| `invoke_api` | direct WebAPI call with `{id}` target substitution | — |

The router pair (`search_api` / `invoke_api`) exposes the entire WebAPI —
28 endpoint groups, 212 paths — without minting a tool per endpoint. Agents
discover the surface via `search_api` and fall back to `invoke_api` when a
smart tool doesn't cover the use case.

## Time-travel debugging

Time-travel debugging (TTD) records the machine while it runs, so that
afterwards you can move backward through what happened: jump to any recorded
moment, step back one instruction, or ask "who last wrote to this address?".
The `time_travel` tool exposes the whole feature; each action is a thin
wrapper over one WebAPI route under `/api/v1/emulator/{id}/ttd/…`
([webapi-interface.md](../../emulator/design/control-interfaces/webapi-interface.md)
has the route reference, the
[TTD design](../../emulator/design/debugger/time-travel-debug/time-travel-debugging-tdd.md)
the internals).

### Terms

| Term | Meaning |
|:--|:--|
| frame | One video frame of emulated time (about 1/50 s). Frame numbers are the emulator's frame counter. |
| `tinframe` | T-states (CPU clock ticks) inside a frame. `frame 12 t=500` is a point in time. |
| checkpoint | A saved machine state taken at a frame boundary while recording. Moving through history restores the nearest checkpoint and replays forward from it. |
| session state | `idle` (not recording; history may or may not exist), `recording`, `detached` (the machine sits at a point in the recorded past and is paused). |
| marker | A replay barrier: something the recording cannot reproduce happened here (tape play/stop, a disk sector write, a memory edit made by a tool while recording). A reset writes no marker: it stops the recording instead (the `hardware_reset` kind is reserved and never written). Seek and backward searches stop at a marker and say so. |
| bookmark | Your own label on a point in time. Advisory only, never a barrier. |
| write journal | A log of every memory write made while recording (on by default). It makes `find_last` for writes instant; without it the search replays history. It is used only when it holds every write of the session (never paused mid-recording); a gap sends the search to replay, which is slower but always right. |

### Actions

| Action | Arguments | What it does |
|:--|:--|:--|
| `status` | — | Session state, recorded frame range, checkpoint count, memory use |
| `start` | `mode`: `development` (default, write journal on) or `gaming` (journal off, less memory); `enable_write_journal` overrides `mode` | Begin recording |
| `stop` | — | Stop recording; history is kept and can be browsed |
| `invalidate` | `reason` (optional) | Drop all history |
| `position` | — | Current point and session end |
| `markers` | — | List the replay barriers |
| `seek` | `frame`, `tinframe` (default 0) | Move the machine to that point in the recording |
| `step_back_frame` / `step_forward_frame` | — | One frame back / forward |
| `step_back_instruction` / `step_forward_instruction` | — | One instruction back / forward |
| `reverse_step` | `count` (instructions) **or** `tstates` | Step back several instructions or T-states |
| `reverse_continue` | `pcs`: list of addresses | Run backward until the CPU was about to execute one of them |
| `find_last` | `addr` or `addr_from`/`addr_to`; `access` (`write` default, `read`, `execute`, `io`); optional `value`, `pc_from`/`pc_to`, `phys_page`, `before_frame`/`before_tin` | Latest matching access before the current point (or before `before_frame`) |
| `resume` | `frame`/`tinframe` (optional, default: current point) | Continue recording live from that point; **everything recorded after it is discarded**. Needs the machine positioned in history (`seek` or a step first); right after `stop` it fails |
| `dump` / `load` | `path` | Save / load a `.ttd` session file |
| `bookmark_add` / `bookmark_list` / `bookmark_delete` / `seek_bookmark` | `label`, optional `frame`/`tinframe` | Named points in time |
| `coverage_probe` / `coverage_scan` / `coverage_summary` | `frame` or `from_frame`/`to_frame`, `kind`, `addr_from`/`addr_to`, … | Which frames touched which addresses, without replaying |

Addresses may be integers or strings such as `"0x5800"`. Everything that moves
through history (`seek`, the step actions, `reverse_*`, `find_last`) needs a
stopped session: while recording the call fails with HTTP 409 and the tool
tells you to call `stop` first. `find_last` reports where the access happened;
`seek` to the reported `frame`/`tinframe` to inspect the machine there.

### Session rules

- **Recording runs at real speed.** While a session records (and while the
  machine sits in the past), the host speed control is held at 1x and 2x-16x
  is refused, turbo mode is off and cannot be switched on. Fast tape, turbo
  tape and fast disk loading are also off, both while recording and while a
  stopped or loaded session is replayed. The previous settings come back when
  the session returns to `idle`. The emulated machine's own hardware turbo
  (ATM, Scorpion) is not affected. Feature lists show a held feature as off,
  and switching it on answers HTTP 409.
- **These wipe the history:** loading a snapshot, tape or disk (or creating a
  disk), reloading the ROM, changing the speed on a stopped session that still
  holds history, and `invalidate`. Load your software *before* `start`.
- **Reset keeps the history.** A reset (including a disk autostart) stops the
  recording; the recorded history stays browsable.
- **Devices TTD cannot follow end the recording.** Using the ZX-Evo SD card
  while recording ends (and drops) the session at the next frame boundary.
- **`load`** only restores into a machine of the model the session was
  recorded on (the error names both models). After a load the session is
  `idle`: `seek` to position the machine inside it. The path is resolved by
  the emulator process.

### Worked example: who overwrote the screen attribute?

A game's top-left attribute cell (`0x5800`) turns red and you want the code
that did it.

```jsonc
// 1. Load first (a load after start would wipe the recording), then record
{"name": "load_software",     "arguments": {"path": "/games/game.sna"}}
{"name": "time_travel",       "arguments": {"action": "start"}}
{"name": "control_execution", "arguments": {"action": "run_frames", "frames": 500}}
{"name": "time_travel",       "arguments": {"action": "stop"}}
// -> "TTD recording stopped on emu-1; history kept (state idle) - seek/step/find_last are available now"

// 2. Ask for the last write to 0x5800
{"name": "time_travel", "arguments": {"action": "find_last", "addr": "0x5800", "access": "write"}}
// -> "Last write at frame 431 t=20112 by PC 0x8F3A, value 16, RAM page 5. Seek to that frame/tinframe ..."

// 3. Go there and look at the code
{"name": "time_travel",   "arguments": {"action": "seek", "frame": 431, "tinframe": 20112}}
{"name": "inspect_state", "arguments": {"aspects": ["registers", "disasm", "ttd"]}}
```

Narrow a noisy search with `value` (only writes of that byte), `pc_from` /
`pc_to` (only code in that range) or `phys_page` (only that RAM bank on a
128K+ machine).

### Worked example: when was this routine last entered?

```jsonc
{"name": "time_travel", "arguments": {"action": "reverse_continue", "pcs": ["0xBF00", "0xBF40"]}}
// -> "Hit PC 0xBF00 at frame 212 t=31000"
{"name": "time_travel", "arguments": {"action": "step_back_instruction"}}   // the caller's last instruction
{"name": "time_travel", "arguments": {"action": "resume"}}                  // continue live from here (later history dropped)
```

If the answer is `"... blocked by marker tape_control 'play' at frame 180"`,
the search reached a point the recording cannot replay across; everything
before the marker is out of reach for that search.

### Worked example: keep a session for later

```jsonc
{"name": "time_travel", "arguments": {"action": "bookmark_add", "label": "depacker done"}}
{"name": "time_travel", "arguments": {"action": "dump", "path": "/tmp/game.ttd"}}
// later, on a machine of the same model:
{"name": "time_travel", "arguments": {"action": "load", "path": "/tmp/game.ttd"}}
{"name": "time_travel", "arguments": {"action": "seek_bookmark", "label": "depacker done"}}
```

## Resources

| URI | Content |
|:--|:--|
| `unreal://keyboard-layout` | ZX Spectrum key names for `type_input` |
| `unreal://basic-reference` | BASIC tokens/commands cheat sheet |
| `unreal://z80-isa` | Z80 instruction set reference (markdown) |
| `unreal://trdos-commands` | TR-DOS command reference |
| `unreal://memory-map` | 48K/128K memory map |
| `unreal://machine/profi` | Profi 1024: ROM pages, ports, hi-res mode, limitations |
| `unreal://emulator-state` | live instance overview (dynamic — fetched per read) |

## Protocol details

### initialize negotiation

```json
→ {"jsonrpc":"2.0","id":1,"method":"initialize",
   "params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"curl","version":"0"}}}
← {"jsonrpc":"2.0","id":1,"result":{
     "protocolVersion":"2025-03-26",
     "capabilities":{"tools":{"listChanged":false},"resources":{"subscribe":false,"listChanged":false}},
     "serverInfo":{"name":"unreal-ng","version":"1.0.0"},
     "instructions":"…agent-facing briefing…"}}
```

The server echoes the client's `protocolVersion` when supported, otherwise
answers with `2025-03-26`. `notifications/initialized` completes the
handshake (acknowledged with `202` + empty body, like every notification).

### Progress streaming

A client opts in per call by sending a `progressToken` (any string or number)
inside `_meta`:

```json
→ {"jsonrpc":"2.0","id":2,"method":"tools/call",
   "params":{"name":"inspect_state","arguments":{"aspects":["registers","disasm","screen_ocr"]},
             "_meta":{"progressToken":42}}}
```

- Over **HTTP with `Accept: text/event-stream`**, the answer becomes an SSE
  stream: one `event: message` frame per `notifications/progress` (params echo
  the token, `progress` increases monotonically, `total`/`message` describe
  the step), the final frame carries the JSON-RPC response, then the stream
  closes.
- Over the **stdio bridge**, each progress notification and the final response
  arrive as separate stdout lines — stdio clients get progress for free.
- Without a token (or over plain JSON HTTP) progress is silently dropped —
  never a protocol violation.

### Statelessness

No `Mcp-Session-Id` is ever issued or required. Consequences: any number of
clients may share the server; requests may be retried; load-balancing is
trivial; `DELETE /mcp` (session termination in the spec) is `405`. State that
*matters* (emulator instances, breakpoints, recordings) lives in the
emulator, addressed by `target`.

### Error model

| Situation | Result |
|:--|:--|
| Tool ran, work failed | tool result with `isError: true` + remediation hint (e.g. "Already recording — stop it first (record_stop)") — never a JSON-RPC error |
| Unknown tool | `-32602` Invalid params |
| Unknown method | `-32601` Method not found (`resources/subscribe` etc.) |
| Batch array | `-32600` (batching removed in `2025-03-26`) |
| Malformed JSON body | HTTP `400` + `-32700` |
| WebAPI unreachable | tool `isError` result: "WebAPI unreachable — is the emulator running with WebAPI enabled (port 8090)?" |
| Bridge cannot connect | `{"error":{"code":-32603,"message":"bridge: cannot reach <url> (…)"}}` on stdout, bridge keeps running |

## Comparison with xspeccy-mcp

| Aspect | xspeccy-mcp | unreal-ng (this server) |
|:--|:--|:--|
| Transport | stdio only | Streamable HTTP :8092 (JSON/SSE) + stdio bridge |
| Backend | direct core calls | loopback WebAPI — shared handlers, identical information |
| Tool count | 48 flat tools | 11 smart tools + schema-driven router |
| Context cost | ~4k tokens | ~1.6k tokens |
| Progress | — | `notifications/progress` per real work unit (SSE or stdio lines) |
| Resources | — | 6 (keyboard/BASIC/Z80/TR-DOS/memory map/state) |
| Sessions | per-process | stateless — share one server between clients |

## Security

- The MCP listener binds all interfaces (`0.0.0.0:8092`) with **no
  authentication** — the trust boundary is the network itself. This matches
  the WebAPI (:8090) and CLI (:8765) listeners. Run on a trusted machine or
  behind a firewall; anyone who can reach the port can control the emulator
  and read/write emulator memory.
- There is no path traversal or file access through MCP beyond what WebAPI
  itself exposes (load files the emulator process can already read, write
  recordings to the scratch dir).
- The bridge adds no surface of its own: it only connects to
  `http://127.0.0.1:8092/mcp` (or `--url`/`UNREAL_MCP_URL`).

## Quick test

```bash
# JSON round-trip
curl -s -X POST http://localhost:8092/mcp \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}' | jq '.result.tools | length'
# → 11

# SSE progress stream
curl -N -s -X POST http://localhost:8092/mcp \
  -H 'Content-Type: application/json' -H 'Accept: text/event-stream' \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call",
       "params":{"name":"inspect_state","arguments":{"aspects":["registers","screen_ocr"]},
                  "_meta":{"progressToken":1}}}'

# stdio bridge
echo '{"jsonrpc":"2.0","id":1,"method":"ping"}' | ./cmake-build-release/bin/unreal-mcp-bridge
```

The emulator application must be running (it serves `/mcp`; a busy :8092
disables MCP for that instance with a console warning — the app keeps
running).

## Testing

Unit tests (drogon-free, `FakeApiCaller`-driven) run in `core-tests`:

```bash
ninja -C cmake-build-release && cmake-build-release/bin/core-tests \
  --gtest_filter='McpSse_Test.*:McpDispatcher_*:McpRouter_Test.*:McpTools_Test.*'
```

- `McpSse_Test` — SSE frame encoding, progress notification shape
- `McpDispatcher_Test` / `McpDispatcher_Progress_Test` — JSON-RPC routing,
  notification ordering, progressToken plumbing
- `McpTools_Test` — action→endpoint mapping + progress sequences
- `McpRouter_Test` — OpenAPI search/invoke against a scripted spec

End-to-end: `scripts/mcp-smoke-test.sh` (HTTP + bridge + SSE round-trips
against a running emulator).
