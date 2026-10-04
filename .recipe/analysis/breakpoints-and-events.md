# Breakpoints, stepping and debugger events

Stop the machine where something happens, step through the code, and be told about pauses and
steps instead of polling. Works on every machine (main Z80).

## What stops where

| Breakpoint | Stops a running machine | Stops a step / `steps` run |
|:--|:--|:--|
| execution (`exec`) at an address | before the instruction there | before the instruction there |
| memory read / write | during the access (the instruction completes after resume) | after the instruction |
| port in / out | during the access | after the instruction |

A step from the execution breakpoint the machine is stopped at runs that instruction (stepping
on from a breakpoint). A machine that sits at an address with a breakpoint without having stopped
there (fresh reset, a jump done by a seek) stops on the first step, without executing.

Memory breakpoints need debug mode (and the `breakpoints` feature, on with debug mode).

## WebAPI

```bash
BASE=http://localhost:8090/api/v1
EMU=$(curl -s -X POST $BASE/emulator/start -H 'Content-Type: application/json' -d '{"model":"48K"}' | jq -r .id)
curl -s -X PUT  $BASE/emulator/$EMU/debugmode   -H 'Content-Type: application/json' -d '{"enabled":true}'
curl -s -X POST $BASE/emulator/$EMU/breakpoints -H 'Content-Type: application/json' -d '{"type":"exec","address":"0x38"}'

# The machine runs into the breakpoint and pauses. Then:
curl -s -X POST $BASE/emulator/$EMU/step  | jq '{pc, executed, stop}'
#   {"pc":57,"executed":1,"stop":{"cpu":"main","reason":"step"}}
curl -s -X POST $BASE/emulator/$EMU/steps -H 'Content-Type: application/json' -d '{"count":100000}' | jq '{pc, executed, stop}'
#   {"pc":56,"executed":2844,"stop":{"cpu":"main","reason":"breakpoint","breakpoint_id":1,"address":56,"access":"execute"}}
```

`stop.reason` is `step` (the run did all it was asked) or `breakpoint` with `breakpoint_id`,
`address` (PC, memory address or port) and `access` (`execute`, `read`, `write`, `port_in`,
`port_out`). `executed` counts the instructions that ran: an execution breakpoint's own
instruction does not.

## Events: `/api/v1/websocket`

Subscribe once; every pause, resume, finished step and breakpoint set change of that emulator
arrives as a JSON message (protocol: `docs/inprogress/2026-09-28-debugger-model/protocol.md` §5).

```python
import json, websocket                        # pip install websocket-client
ws = websocket.create_connection("ws://localhost:8090/api/v1/websocket")
ws.send(json.dumps({"op": "subscribe", "id": "s1", "emulator": EMU, "topics": ["debug"]}))
print(ws.recv())   # {"op":"subscribed","id":"s1","seq":4}
while True:
    print(ws.recv())
# {"op":"event","topic":"debug","seq":5,"event":"paused","emulator":"…","cpu":"main",
#  "reason":"breakpoint","breakpoint_id":1,"address":56,"access":"execute"}
```

| Event | When | Fields |
|:--|:--|:--|
| `paused` | every pause | `reason`: `breakpoint` (+ `breakpoint_id`, `address`, `access`) or `pause` |
| `resumed` | resume / run | - |
| `step_done` | the end of a step, `steps` or other direct run | `reason`: `step` or `breakpoint` (+ fields) |
| `breakpoints_changed` | a breakpoint added, removed, enabled, disabled | - |

- `emulator` omitted: events of every emulator. `topics` omitted: every published topic. Topics
  not published yet (`timeline`, `cmdlog`, `stats`, `ttd`) are listed under `unsupported` in the
  reply.
- `seq` counts per emulator; the `subscribed` reply carries the current one. A gap means a lost
  event: read the state again.
- `{"op":"unsubscribe","id":"s1"}` ends a subscription; closing the connection ends all.

## CLI, MCP, Lua, Python

| Surface | Step | Result |
|:--|:--|:--|
| CLI | `stepin`, `steps N`, `run_ncycles N` | a line `Stopped at breakpoint #1 (execute) at $0038` when one ended the steps |
| MCP | `control_execution` actions `step`, `step_n` | the summary names the breakpoint; the structured result carries `stop` |
| Lua | `emu:step(false)`, `emu:steps(n)` | `{executed, stopped, breakpoint_id, address, access}` |
| Python | `emu.step(False)`, `emu.steps(n)` | the same as a dict |

Events are pushed on the WebAPI WebSocket only; the other surfaces return the outcome with the
step.
