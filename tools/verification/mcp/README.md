# MCP verification

Black-box checks of the emulator's MCP server as an agent sees it: JSON-RPC over HTTP to a running `unreal-qt`, one
script per block of tools. Standard library only. How and when to run them:
[docs/guidelines/mcp-testing-guidelines.md](../../../docs/guidelines/mcp-testing-guidelines.md).

## Run

```bash
# Launch a fresh instance on ports of its own (WebAPI 8095, MCP 8295), run everything, stop it
tools/verification/mcp/run_all.py --launch cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt

# Against a running server, some scripts only, the tests whose name contains "bp"
tools/verification/mcp/run_all.py --mcp-url http://127.0.0.1:8295/mcp test_control_execution -k bp

# One script on its own
tools/verification/mcp/test_symbols.py --mcp-url http://127.0.0.1:8295/mcp
```

Each script destroys every instance on the server it talks to and creates its own machine (`--model`, default
PENTAGON; `test_input.py` takes a 48K for the BASIC prompt). Do not point it at an emulator someone works in: launch one
(`--launch`, `--port-base`; a port another process holds is refused, so the scripts never talk to someone else's
build). Exit code 0 = every test passed. The whole set takes about 5 seconds.

## Scripts

| Script | Tools | What it checks |
|--------|-------|----------------|
| `test_protocol.py` | the server | handshake and version fallback, every tool has a description and `response_format`, text-only by default, the `json` echo equals `structuredContent`, bad arguments are tool errors (the server survives), resources |
| `test_emulator_manage.py` | `emulator_manage` | list / status / models / server in the text, reset, unknown action, `target: auto` refuses two machines |
| `test_inspect_state.py` | `inspect_state` | every aspect answers with its data in the text (none says "see structuredContent"), registers, memory hexdump at a `"0x…"` address, disasm listing, the attribute grid, key=value aspects |
| `test_memory_access.py` | `memory_access` | write / read round trip with the bytes in the text, `address` = `offset`, both = error, the 256-line cut, a region read, bad hex |
| `test_control_execution.py` | `control_execution` | step / step_n / run report PC, a breakpoint is listed, skipped by `run_frames` and hit by `resume` + `wait` (bp_add switches the breakpoints feature on); a conditional write breakpoint: refusals with the reason, `hits 1 matched 2 if VAL == 6`, `bp_condition` set / refuse / clear; a Lua `on_tap` callback over its budget: `callbacks` shows it suspended with the reason, `callback_policy` / `callback_resume` / `callback_remove` (and `callbacks_changed` on the WebSocket's `debug.events`) |
| `test_debug_code.py` | `debug_code` | assemble then disassemble, find_bytes, the call trace one event per line, a bad address |
| `test_symbols.py` | `manage_symbols` | resolve by name and by address, list by name / range / limit, a bad range |
| `test_capture_media.py` | `capture_media` | screenshot metadata by default, the picture as an MCP image block (real PNG / GIF, no base64 in the JSON echo), a stable digest, status calls |
| `test_input.py` | `type_input`, `mouse_input`, `joystick_input` | a typed BASIC line reaches the screen, key names, device status in the text, wrong arguments name the right ones |
| `test_next.py` | `inspect_state`, `invoke_api`, `control_execution` | ZX Next: the `next_nmi` aspect and its journal, the NMI trigger and its refusals, the NextREG / beam / copper breakpoint types in `bp_add` and `bp_list` |
| `test_bus.py` | `bus_analyzer`, `memory_access`, `invoke_api`, `control_execution` | Bus analyzer on TS-Conf: the bus list, enable with checks, a program that reads `#57` during its own SPI DMA (a `dma_collision` finding with the PC of the IN, the DMA's bytes as the DMA's), the SD timing profile, the VCD export, the refusals (unknown bus, profile, kind) |
| `test_cli_server.py` | the CLI directly | The CLI server keeps each connection's socket its own: 100 connections back to back and 20 beside five open ones each get the greeting and an answer (the accept loop used to close sockets their threads also closed; a reused number was closed under the next client) |
| `test_companion.py` | `companion_manage`, `control_execution`, `inspect_state`; the WebAPI and the CLI directly | Companion clients: with the companion `mcplib.launch` provisions (`UNREAL_COMPANION_TOKENS`), resume / keys / reset / speed open no session, debugger actions are `companion_forbidden`, no token = the gate as before, an MCP marker or an unknown token is refused, an agent's session refuses the companion until it takes control back (the agent's next call is the stop error), `allow-again`, the CLI's `companion auth`, `me`, the pairing routes, and a revoke over MCP stops the token. The other scripts use no companion; the revoke holds for the server process, so a second run needs a fresh one (`--launch` starts one) |
| `test_actions.py` | `actions`; the WebAPI and `/api/v1/ws` directly | The host actions: the list names every action with its kind and why one cannot run (a stub, "needs a pause"), a step on the paused machine reports its pc, a step while running is `not_now`, unknown ids and wrong values; the same catalogue and the same errors over REST and over WebSocket calls, the implicit session the socket's call opened named in its result and ended by a later call; the narrow companion refused a step (debug scope), allowed turbo (run scope). Needs `websockets` (or `websocket-client`) |
| `test_streamdeck.py` | `streamdeck`; the WebAPI, `/api/v1/ws` and the CLI directly | The Stream Deck daemon as unreal-qt supervises it (Stream Deck SD7): `mcplib.launch` sets `UNREAL_STREAMDECK_SUPERVISOR=off`, so the launched unreal-qt starts no daemon and the user's setting stays: the status text (state, mode, port), the settings and the udev rule read, `pair` / `pairing` / `brightness` refused with 409 "not running", `revoke` 404, a bad mode 400; the same status over REST, a socket `call`, the `streamdeck.status` topic (one value, no instance) and the CLI `streamdeck`. The supervision itself is in core-tests and the SD7 live check |
| `test_surfaces.py` | `actions`, `time_travel`; the WebAPI and `/api/v1/ws` directly | The control-surface actions (control surfaces S2): the surface context the same on REST, the socket and MCP, set by a companion (no scope needed), then by the agent (the last setter wins), a bad name refused, `""` back to the default; `ttd.seek_by` over a 160-frame recording: 2 s at full speed = 98 frames with a second mark, clamped to the first and the last frame with `edge` marks, the socket's call, MCP's text; `run.step_by` back through the history and forward (16 at most); the targets of Stream Deck SD4: the main window is a view showing the instance, `views` the same on REST, the socket and MCP, a second instance and a second (API) window: "no id" (`/controllers/actions`, `/surfaces/context`) follows the focused window, the catalogue's `description` / `toggle` / `rev`, a session pinned to the main window and then to an instance (MCP `pin`), the readouts, the activity of an action (REST and MCP text), the window closing and the instance going (the pin falls back with a notice), the client's window not removable; the TTD position polled at 50 Hz beside a machine recording at 1x: the same frames per second, the poll latency printed. Needs `websockets` (or `websocket-client`) |
| `test_ws_surface.py` | `time_travel`, `actions`, `companion_manage`; `/api/v1/ws` directly | The topics and the `surface` topic of `/api/v1/ws` (control surfaces S4), the conformance suite: `companion_required` (then `auth` and `join` on the same socket), `companion_not_for_agents`, `companion_unknown` + close, one surface per connection, `welcome` (instance, context, scopes, target), a view target `not_now`, a pin by index; `run.pause_toggle` by a companion (no session) with the `run.state` readout following, unknown id / bad value / unknown op, `companion_forbidden` for the narrow token; `ctx.set` from one session reaching another and the `surfaces.context` topic, the agent's set too; the default context flipping to `ttd` when a recording leaves a history (`by: default`); `ttd.seek_by` with marks and the `ttd.position` readout following, the clamp's `edge`; the `instances` topic (`companion_required` without a token, `hello` equal to the events socket's entry, `emulator` at once on a resume, `snapshot`); the `debug.*` topics without a token from this host, with the same `seq` as `/api/v1/websocket`; a client that stops reading (a 4 KiB receive window, 1500 `snapshot` requests) closed as `slow_consumer` while another is served (`mcplib.launch` sets `UNREAL_WS_HIGH_WATER_BYTES=16384`, `UNREAL_WS_SLOW_CONSUMER_MS=1500`); one and ten 50 Hz readout subscribers beside a machine running at 1x: the same frames per second (printed with readouts per second); revoking the companion closes its socket. Uses the `surface` companion `mcplib.launch` provisions; stdlib only (`mcplib.WebSocket`) |
| `test_audio_mixer.py` | `audio_mixer`; the WebAPI and `/api/v1/ws` directly | The sound mixer: the rows in the text (the master muted by the session), a row's channels (`ay1.a`..`c`), a channel's solo silencing the others of its row, an unknown channel naming the ones there are; the levels, the sound settings (`ay_stereo` set and back, a bad value refused); a channel over REST (`PUT` / `GET /audio/mixer/ay2.c`, `/audio/levels`), the socket `call` answering as REST, the `audio.mixer` topic carrying the channel state and pushing its change |
| `test_controllers.py` | `controllers` | The host side of the controller manager: the devices and the probe in the text (each backend running or why not, the `devices.user.json` path), the device database (the Elite 2 by VID:PID, an unknown one said so, a malformed one an error), a RetroArch file imported with `save: false` (the map, RetroPad B = `south`, the SDL line; nothing written), `role` / `pin` / `identify` naming an unknown device, `learn` refused with the REST / CLI / Python spellings |
| `test_time_travel.py` | `time_travel` | record, stop, the range in status, seek, an out-of-range seek is reported |
| `test_media.py` | `media`, `load_software`, `rzx_playback` | slots and formats in the text, a tape from `testdata/` loads into its slot, a missing file is named |
| `test_router.py` | `search_api`, `invoke_api` | search by words, a GET by path, a missing path |
| `test_analysis.py` | `analyze_performance` | frame cost and profiler status in the text, coverage over a run |
| `test_asm_source.py` | `asm_source` | dialects and formats, an encode / decode round trip |
| `test_signature.py` | `signature_info`, `controllers` (`match`, `context`), `media` | The media signatures: a tape's content signature and hashes in the text, a disk's TR-DOS file set, compare (exact; a tape against a disk: none), a missing file named, the signature of a disk inserted into a drive, the game-profile match of a signature and of a path (the Qt client computes it), context naming the matches and the haptics reason |
| `test_text_coverage.py` | many | the share of the `structuredContent` values the default text carries, per call, against a floor: a call whose text falls back to a headline fails |

The disassembler's quality against z88dk / sjasmplus is [`../disasm/`](../disasm/README.md) (needs those tools).

## Writing a test

A script is `test_*` functions taking a `Session`; `mcplib.main(globals())` runs them. An optional `setup(s)` replaces
the default fresh machine.

```python
from mcplib import expect, main

def test_registers_in_the_text(s):
    r = s.call('inspect_state', {'aspects': ['registers']})   # a tool error fails the test (ok=False to allow it)
    expect('PC=0x' in r.text, 'PC in the text', r.text)        # r.text, r.structured, r.blocks('image'), r.is_error

if __name__ == '__main__':
    sys.exit(main(globals()))
```

Check what the model reads, `r.text`, first: a value only in `structuredContent` is invisible to clients that pass only
`content[]` to the model. Check `r.structured` for exact values. Make a test independent of the machine's past (write the
code it runs, set PC), as `test_breakpoint_listed_and_hit` does.
