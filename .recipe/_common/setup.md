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
emulator_manage {"action":"preset","model":"48K","cards":["edge.1=kempston"]}   # or: the whole lab machine in one call (below)
control_execution {"action":"run_frames","frames":100}   # pause + synchronous advance
control_execution {"action":"step"}                      # single instruction
control_execution {"action":"resume"}                    # free-run
inspect_state    {"aspects":["screen_ocr"]}              # "did the menu appear?"
inspect_state    {"aspects":["screen_digest"]}           # deterministic frame digest
capture_media    {"action":"screenshot"}                 # PNG (default) or GIF + metadata; area full|screen
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
emulator. In an agent session prefer `tools/build/build.sh` (build slots); either
way the compile goes through ccache by itself, with the checkout's own cache in
`scratch/ccache` and the host's master cache read only (`AGENTS.md`, "Compiler
cache").

### 2. Launch with a clean slate

Only **one process can bind port 8090** — a stale instance silently wins or
loses the race and everything downstream gets confusing. Always start with:

```bash
# 1. Check if port 8090 is already in use
if lsof -i :8090 >/dev/null 2>&1; then
  echo "WARNING: Port 8090 in use. Another session may own this emulator."
  echo "Use that instance, or ask the user before killing it."
  lsof -i :8090
  exit 1
fi

# 2. Start the freshly built emulator (macOS path; Linux: bin/unreal-qt)
./cmake-build-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt &
sleep 4   # WebAPI + MCP listeners come up with the app

# 4. Smoke-test both automation surfaces
curl -s http://localhost:8090/api/v1/emulator | jq '.emulators | length'
curl -s -X POST http://localhost:8092/mcp -H 'Content-Type: application/json' \
     -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}' | jq '.result.tools | length'
```

**unreal-qt starts with no machine**, with a window or headless (`-platform offscreen`): `.emulators | length` is 0
until the user presses Start or a script creates one (step 4, `POST /emulator/start`). Do not wait for a machine
to appear: create it. The first instance created is the one the window shows (and the one `source=window`
screenshots and window recordings use); a second one runs beside it, not in the window.

Port map (all bind without authentication — trusted network only):

| Port | Surface |
|:--|:--|
| 8090 | WebAPI `/api/v1/...` (`/mcp` also reachable here) |
| 8092 | MCP Streamable HTTP (`POST /mcp`, `GET /mcp` keepalive) |
| 8765 | CLI automation (telnet-style, `ttd`/`port-trace` commands) |

iOS/embedded hosts may use different ports (e.g. the cube app uses 8091) —
a port collision means a second host silently serves nothing.

A **second instance** beside one that owns the defaults (another checkout, a
recipe check) gets its own ports from the environment, without stopping the
first one:

```bash
UNREAL_WEBAPI_PORT=8190 UNREAL_CLI_PORT=8191 UNREAL_MCP_PORT=8192 \
  ./cmake-build-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt &
curl -s -X POST http://localhost:8192/mcp -H 'Content-Type: application/json' \
     -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}' | jq '.result.tools | length'
```

Check the ports with `lsof -i :<port>` first; stop only that instance's PID.

Or let the emulator pick free ports itself, so parallel agents and tests need no bases handed out
([environment-variables](../../docs/emulator/environment-variables.md#all-six-ports-at-once---port-base-and---ports-file)):

```bash
./cmake-build-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt --port-base auto --ports-file scratch/ports.json &
# wait for the file (written whole, by rename), then:
jq . scratch/ports.json
# {"port_base":20600,"webapi":20600,"cli":20700,"mcp":20800,"gdb":20900,"dezog":21000,"zrcp":21100,"pid":4242}
```

`--port-base N` takes a number instead (WebAPI N, CLI N+100, MCP N+200, GDB N+300, DeZog N+400, ZRCP N+500). The
servers come up a moment after the file appears: poll the port. No free base: stderr and exit code 2.

### 3. Pick a machine model

Runtime-authoritative list with `creatable` flags:

```bash
curl -s http://localhost:8090/api/v1/emulator/models | jq '.models[] | {name, creatable}'
```

The creatable list changes as decoders land, so read it from the runtime
list; the current table with notes is in [machines.md](machines.md). A create request for a non-creatable model
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

### 4a. A lab machine in one call: `preset`

A reverse-engineering or verification run wants the same machine every time: RAM zeroed, a card or two fitted, sound
muted, paused before the first instruction, run control held. `preset` does all of it in one call and answers when
the machine is ready: the cards are planned inside the creation (no restart, no new id, nothing to poll). Reference:
[command-interface.md, Preset machine](../../docs/emulator/design/control-interfaces/command-interface.md#preset-machine).

```text
emulator_manage {"action":"preset","model":"48K","cards":["edge.1=kempston"],"features":{"kempstonmouse":false}}
#   Preset machine 6f2c... ready: 48K (48 KB), RAM zero, paused at frame 0 t 3.
#   Cards: edge.1 = kempston.
#   Features not at their default: fasttape off, kempstonmouse off (asked), turbotape off.
#   Sound: master muted. Fast tape: off (fasttape and turbotape off: a tape loads in real machine time).
#   Run control: held by you (mcp, session 0d3e...).
```

```bash
curl -s -X POST "$BASE/emulator/preset" -H 'Content-Type: application/json' \
     -d '{"model":"48K","cards":["edge.1=kempston"],"features":{"kempstonmouse":false}}' \
  | jq '{id, paused, muted, fast_tape, cards, session: .control.session, position}'
# 201; send the session id as the X-Unreal-Session header of the calls that change this machine
```

| Argument | Default | Note |
|:--|:--|:--|
| `model` | required | as for `create` |
| `ram` | `zero` | `random`: power-on noise in the screen pages |
| `cards` | none | `"<slot>=<card>"` or `{slot, card, options, adapter, replace}` |
| `features` | none | `{name: bool}`, by id or alias |
| `muted`, `paused`, `take_control` | `true` | paused at power-on: frame 0, t 3 |
| `fast_tape` | `false` | `false` switches `fasttape` and `turbotape` off (a tape loads in real machine time); `true` leaves both on |
| `replace` | `false` | `true` destroys every other instance once the new one is built |
| `select` | `false` | the new machine is created beside the existing ones and the selection (what a GUI shows, the default target of calls without one) is left alone; `true` selects it |

Other planes: CLI `preset 48K card=edge.1=kempston feature=kempstonmouse=off`, Lua
`machine_preset{model = "48K", cards = {"edge.1=kempston"}}`, Python `machine_preset("48K", cards=["edge.1=kempston"])`.
A refusal (an unknown card, a slot plan that does not fit, a feature that cannot be switched) names the valid values
and leaves no instance behind. With other instances up, pass the new id as `target` (it is not selected by itself).

### 5. Instance lifecycle

| Operation | WebAPI | MCP action |
|:--|:--|:--|
| List instances | `GET /api/v1/emulator` | `emulator_manage` `list` |
| One instance | `GET /api/v1/emulator/{id}` | `emulator_manage` `status` |
| Pause / resume | `POST .../{id}/pause` / `resume` | `emulator_manage` `pause` / `resume` |
| Reset | `POST .../{id}/reset` | `emulator_manage` `reset` |
| Hard stop | `POST .../{id}/stop` | `emulator_manage` `stop` |
| Destroy (free memory) | `DELETE /api/v1/emulator/{id}` | `emulator_manage` `destroy` |
| Switch model | `POST .../{id}/model {"model":"48K"}` (media follow; `"stranded"` for dirty media without a slot) | `emulator_manage` `switch_model` (`model`, optional `stranded`) |

Notes agents trip over:

- `Pause` is **asynchronous** — the Z80 thread parks at the next frame
  boundary. State-mutating endpoints (TTD seek, tape snapshot) do their own
  pause+confirm internally; only roll-your-own loops need to care.
- `stop` freezes the instance; `destroy` deletes it. TTD history, traces and
  counters die with the instance — dump them to `scratch/` first.
- Emulator ids are UUIDs; MCP `target` takes the full id (or `"auto"`); id prefixes are not matched.
- **Instances have no owner and no expiry**: nothing removes them but `DELETE`. A script that creates one
  deletes it on every exit path (bash `trap 'curl -s -X DELETE "$BASE/emulator/$EMU_ID"' EXIT`, Python
  `try/finally`), and keeps the ids it created: other sessions may have instances on the same app, so never
  clean up by deleting everything in `GET /api/v1/emulator`. Name them at create (`{"symbolic_id":"<task>-<n>"}`):
  the list and `GET /api/v1/emulator/{id}` show `symbolic_id`, so a leftover is recognised as yours
  (`jq '.emulators[] | select(.symbolic_id | startswith("<task>-")) | .id'`).

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
`step_n`, `run_frames`, `run_tstates`, `run_to_interrupt`. A counted run answers what it ran (`frames_run`,
`frame`, `t`, `stop`); a count over the limit (200000 frames) is refused, never cut silently.

An unknown action or value on any MCP tool answers with the valid ones and the nearest match ("Unknown action
'slots_plugg'. Did you mean 'slots_plug'? Valid actions: ..."): read the refusal instead of guessing again. Long
verbs (a TTD dump or load, an analysis build) take `async: true` and are waited for with the `jobs` tool
([ttd-recording.md](../analysis/ttd-recording.md#long-verbs-as-jobs)).

### 7. Verify what is on screen (assertion primitives)

```bash
# Screen text via OCR — great for "did the menu appear?"
curl -s "$BASE/emulator/$EMU_ID/capture/ocr" | jq '.text'

# Deterministic content digest — great for "same frame as last run?"
# (.combined is the hash to compare; also banks[], active_surface, changed, previous_digest)
curl -s "$BASE/emulator/$EMU_ID/state/screen/digest" | jq '.combined'

# Screen image capture of the presented frame: PNG and the whole frame with border by default
# (area=screen = the working picture only, format=gif = 256 colors): media/agent-screenshot-view.md
curl -s "$BASE/emulator/$EMU_ID/capture/screen" | jq '{format, width, height, size}'
# the base64 image bytes are in the "data" field of the same response
```

MCP: `inspect_state` with `aspects: ["screen_ocr"]` /
`["screen_digest"]` / `["screen_image"]`. Read the digest from
`structuredContent.screen_digest.combined` (the text summary line shows the same combined value).
To compare digests across instances, create them with `ram_power_on:"zero"`
(see [machines.md](machines.md)) — the default random power-on noise in screen
pages 5 and 7 makes digests differ run to run.

### 8. Teardown

```bash
curl -s -X DELETE "http://localhost:8090/api/v1/emulator/$EMU_ID" | jq .
pkill -9 unreal-qt 2>/dev/null || true
```

Kill the app when a recipe sequence is finished; long-lived instances with
TTD recording or traces accumulate hundreds of MB before you notice.
