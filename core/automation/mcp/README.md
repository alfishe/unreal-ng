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
             ├── mcp-tools.cpp         6 core smart tools (drogon-free)
             ├── mcp-symbols.cpp       manage_symbols (Phase 2)
             ├── mcp-analysis.cpp      debug_code / analyze_performance (Phase 2)
             ├── mcp-media.cpp         capture_media (Phase 2)
             ├── mcp-router.cpp        search_api / invoke_api + OpenAPI cache
             ├── mcp-resources.cpp     7 resources (6 embedded + dynamic)
             └── target-resolver.cpp   "auto" → create/reuse/refuse
                       │
                webapi-client.cpp      IApiCaller → drogon HttpClient
                       ▼
        GET/POST http://127.0.0.1:8090/api/v1/...   (existing WebAPI)
```

Everything below the adapter is drogon-free and depends only on jsoncpp, so
`core-tests` exercises the whole protocol stack against a `FakeApiCaller`.
Emulator state is served by the shared WebAPI handlers and forwarded
verbatim, so MCP clients see exactly what WebAPI clients see (parity rule:
all automation modules report the same information from the same source).

### Start/stop ordering

`drogon::app()` is process-global: `Automation::start()` calls
`startMCP()` **before** `startWebAPI()` (the :8092 listener must exist before
WebAPI's thread calls `run()`), and `stopMCP()` runs **after** `stopWebAPI()`.

## Tools

### Phase 1 (core 6 + time_travel + rzx_playback + router 2)

| Tool | Purpose |
|:--|:--|
| `emulator_manage` | create/list/status/start/stop/pause/resume/reset/destroy, list_models, server (build fingerprint + models_creatable); GS card actions gs_reset/gs_reset_card/gs_nmi/gs_send_command/gs_send_data/gs_read_status/gs_read_data (`value` = byte 0-255; writes/resets/NMI apply at the next instruction boundary and are journaled for TTD, reads are side-effect-free peeks), gs_switch_personality (`personality`: z80/lle/lw/lightweight/ngs/neogs; a slot change applied by a machine restart, Q10), gs_dump_module (optional `path`); NeoGS: gs_sd_insert (`path`), gs_sd_eject, gs_flash_save, gs_stereo_mode (`mode`: separated/gs/mono) (applied on the machine thread at the next instruction boundary; insert/eject refused while TTD records); ZX-bus slots: slots_catalog, slots_matrix (`table`), slots_plug (`slot`, `card`, `options`), slots_remove (`slot`), slots_set (`slot`, `options`) with `replace_if_incompatible`, `dry_run`, `media_disposition` - planned first, refused with the plan when a card would be removed without the flag, applied by a machine restart (new id); create takes `slots` ([SLOTS] keys) |
| `load_software` | load `.sna/.z80/.szx` snapshots, `.rzx` input recordings (via `rzx/play`: switches to the recording's model), `.tap/.tzx` tapes (auto-play flag), `.trd/.scl/.fdi/.udi/.dsk/.td0/.mgt/.img` disks (`autostart` flag: drive A only, quick-reset into TR-DOS and run the disk) |
| `control_execution` | run/pause/resume/step/step_n/step_over/step_out, run_frames/run_tstates/run_to_interrupt, breakpoints (add/remove/enable/disable/clear/list); the raw `skip_until` endpoint is reachable via `invoke_api` |
| `inspect_state` | aspects fan-out: machine, registers, memory, disasm, stack, breakpoints, memory_banks, screen_ocr, screen_image, screen_digest, timing (beam + the layer pixel under it), video_layout (mode layers and beam windows), video_text (exact text of ATM / ZX-Evo text modes), rom, audio_ay (every AY/SSG chip decoded), audio_fm (TurboSound FM board + both YM2203 halves: mode, timers, channels, operators, envelopes, key-on), audio_gs (General Sound card: mailbox flags, MPAG page, DAC channels, coprocessor core; unavailable when not fitted), audio_multisound (the ZX-MultiSound: options, CPLD, YM pair, SAA, GS, DACs, MIDI summary), audio_midi (its MIDI line and SAM2695: parts, programs, notes sounding, counters; panic via invoke_api POST /control/audio/midi), slots (the ZX-bus slot report), fdc (Beta Disk WD1793 registers, status, FSM, drives), ttd (time-travel session state, recorded range, checkpoints, current position), video_changes (the video change log: latch changes with T / line / PC, palette and mode table writes), memory_region (a device memory region: `region`, `address`, `size`), audio_mixer (per-device mixer), sprinter / sprinter_ports / sprinter_text / sprinter_video / sprinter_palette / sprinter_sound_ring / sprinter_bios / sprinter_zx_mode (which Spectrum mode, launcher options, port decodes) / sprinter_pld_journal (who changed CNF / #1FFD / the port table, when; `pld_journal_kinds`, `pld_journal_source` = live / ttd) (the Sprinter Sp2000) |
| `type_input` | type (tokenized BASIC entry), tap/press/release, combo, macro, release_all, status, list_keys |
| `joystick_input` | Kempston joystick: press / release (names `up+fire` or an array), set (exact `state` byte or a `buttons` list), tap (hold N frames), status (byte, `IN #1F` value, wired / fitted, host keys, pending tap); forwards to `/joystick/*` |
| `mouse_input` | The machine's own mouse (Kempston, Sprinter serial, ZX-Evo PS/2; 409 when none is fitted): move (relative dx/dy, +dy = up), glide (up to ±4096, stepped per frame, later input queues), press/release, click (hold N frames, optional dx/dy pre-move; beyond ±127 it glides), buttons (exact pressed set), wheel, release_all, status (optional `device`); counters override stays on `invoke_api` |
| `time_travel` | time-travel debugging: status/start/stop/invalidate/position/markers, seek, step_back_frame/step_forward_frame, step_back_instruction/step_forward_instruction, reverse_step, reverse_continue, find_last, resume, dump/load/file_info (`.ttd`), bookmarks, coverage_probe/scan/summary. Walkthrough: [docs/features/mcp/README.md](../../../docs/features/mcp/README.md#time-travel-debugging) |
| `rzx_playback` | RZX input recordings: play (path, desync_mode, conventions, switch_model), stop, status, seek (frame) |
| `search_api` | keyword search over the OpenAPI spec (scored), optional `auto_invoke` |
| `invoke_api` | direct WebAPI call with `{id}` target substitution |

### Phase 2 (4 more)

| Tool | Purpose |
|:--|:--|
| `manage_symbols` | load_labels / list / resolve / load_listing / source_at / step_line / run_to_line (sjasmplus `.lst` support) |
| `debug_code` | disassemble / assemble (`Z80TextAssembler`) / find_bytes / trace (calltrace sessions) / porttrace |
| `analyze_performance` | coverage_* (executed-address map + gaps), frame_cost, profiler suites (calltrace/porttrace/memory), vdac2_line_budget / vdac2_line_budget_set (TS-Conf VDAC2: FT812 per-line cost against the line period via `/vdac2/metrics`; `lines`, `in_flight`, `margin`, `measure_always`) |
| `capture_media` | screenshot (`area` `full` default = the whole frame with border / `screen` = the working picture; `format` `png` default / `gif`; `source` `presented` default / `live` (the frame as drawn now); `path` or `filename` to save; the answer carries the frame geometry: `frame`, `screen_window`, `crop`), screen_digest, record_video (GIF; `every_nth:"auto"` samples the digest quantum), audio_capture (RMS/peak/dominant-Hz, WAV), temporal_status / temporal_set (ZX DLSS de-flicker via GET / PUT `/video/temporal`; `algorithm` = name or `"off"`; summary names the video / audio delay it causes), vdac2_capture_start / _stop / _status (TS-Conf VDAC2 card: FT812 bus to an .evr replay stream via `/vdac2/capture/*`; `filename` = the file), framebuffer (raw pixels: `format` rgba / index, base64 data with `include_image`); audio_capture takes `source` (a mixer key) |
| `media` | every media slot (floppy drives, SD card, ...): list / info / formats / targets (where a file can go: what it is, the slots that take it, the default or the refusal) / insert / swap / eject / save / export / discard / rescan / create / protect over the WebAPI `/media` routes; `slot` takes `A`, `fdd.b`, `sd`, `tag:…` or `auto`; a dirty medium leaves only with `save` / `export` / `discard` ([docs/features/media.md](../../../docs/features/media.md)) |

Every tool accepts `target` (emulator id or `auto`; `auto` creates a 128K
machine when none exists, refuses when several exist) and answers with dual
content: `content[]` text summary + `structuredContent` machine data.
Tool-level failures are `isError: true` results with remediation hints —
never JSON-RPC errors.

## Resources

`unreal://keyboard-layout`, `unreal://basic-reference`, `unreal://z80-isa`,
`unreal://trdos-commands`, `unreal://memory-map`, `unreal://machine/profi`, `unreal://machine/tsconf` (embedded markdown) and
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
| Backend | direct core calls | loopback WebAPI (shared handlers — identical information) |
| Tool count | 48 flat tools | 8 Phase 1 (12 after Phase 2) + schema-driven router |
| Context cost | ~4k tokens | ~1.6k tokens |
| Resources | — | 6 (keyboard/BASIC/Z80/TR-DOS/memory map/state) |
