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

Memory breakpoints need debug mode and the `breakpoints` feature. The feature can be off while debug mode is on
(seen 2026-10-10 on a `PENTAGON` created through the WebAPI): the breakpoint is added `active` but its `hit_count`
stays 0. Switch it on with `PUT /feature/breakpoints {"enabled":true}` (it switches debug mode on too; `PUT /debugmode` alone
does not switch the feature on) and check `GET /features` when a breakpoint
never fires. A memory breakpoint stops during the access, so the PC is inside or just past the writing instruction
([articles/program-state-reverse.md](../articles/program-state-reverse.md)).

**Ranges.** Any execution, read or write breakpoint can cover a range: `address` + `address_end`. A
range is checked as fast as one address, and so are ten thousand of them (the emulator paints them
into a per-address table when they are set).

**On a page (physical).** A breakpoint can name a page - `{"kind":"ram","page":32}` in JSON,
`ram32` / `rom3` / `cache0` in text. It then watches that page at offset `address & #3FFF`, through
whatever slot shows the page: code a TS-Conf or 128K program pages into `#C000` one moment and `#8000`
the next, RAM 5 that a 128K shows at `#4000` and, when paged in, at `#C000`. `slot_only` keeps it to the
slot of `address`. A page the machine does not have is refused.

**On the Sprinter's video RAM.** `vram0`..`vram15` (`{"kind":"vram","page":1}`) names a 16 KB page of the
256 KB video RAM, which the CPU never sees at a fixed address: a read or write watchpoint there fires
when a graphics window (or the accelerator) touches that byte, whatever `PORT_Y` and window it went through. The
address is the offset in the page: `wp 0x0805 w --page vram1` is video RAM `#4805`. The stop names the CPU
address of the access. Execution breakpoints are refused there. The fast RAM needs nothing new: it is the
`cache0`..`cache3` pages.

**Masked ports.** `port_mask`: the breakpoint matches every port where `(port & mask) == (address &
mask)`. A Spectrum decodes ports partly, so `#FE` with mask `#00FF` catches the keyboard read whatever
the high byte.

**Hit counts.** Every matching access counts (`hit_count`); `hits` decides which ones stop: `"5"` the
5th only, `">=5"` from the 5th on, `"%5"` every 5th. Stepping on from the breakpoint the machine stopped
at is not counted again. `POST /breakpoints/hits/reset` (CLI `bphits reset`) starts the counts over.

```bash
# writes anywhere in the screen bitmap
curl -s -X POST $BASE/emulator/$EMU/breakpoints -H 'Content-Type: application/json' \
     -d '{"type":"write","address":"0x4000","address_end":"0x57FF"}'
# code in RAM page 32 wherever it is paged in; the 3rd time it runs
curl -s -X POST $BASE/emulator/$EMU/breakpoints -H 'Content-Type: application/json' \
     -d '{"type":"exec","address":"0xC000","page":{"kind":"ram","page":32},"hits":"3"}'
# the keyboard read on any high byte
curl -s -X POST $BASE/emulator/$EMU/breakpoints -H 'Content-Type: application/json' \
     -d '{"type":"port_in","address":"0xFE","port_mask":"0x00FF"}'
curl -s $BASE/emulator/$EMU/breakpoints | jq '.breakpoints[] | {id, address, address_end, page, port_mask, hit_mode, hit_target, hit_count}'
```

| Surface | Range | Page | Mask | Hits |
|:--|:--|:--|:--|:--|
| CLI | `bp 0x8000-0x80FF`, `wp 0x4000-0x57FF w` | `--page ram32 [--slot-only]` | `bport 0xFE i --mask 0x00FF` | `--hits >=5`; `bphits reset [id]` |
| Lua | `bp(a, {to=e})` | `bp(a, "ram32")`, `{page=, slot_only=}` | `bp_port_in(p, {mask=})` | `{hits=">=5"}`; `bp_reset_hits([id])` |
| Python | `emu.bp(a, to=e)` | `page="ram32", slot_only=True` | `emu.bp_port_in(p, mask=)` | `hits=">=5"`; `emu.bp_reset_hits([id])` |
| MCP | `bp_add` `address_end` | `page`, `slot_only` | `port_mask` | `hits`; action `bp_reset_hits` |
| unreal-qt | Breakpoint editor: "to", "Page", "This slot only", "Mask", "Hits" fields; the list shows them | | | |

**Notes and groups.** `POST /breakpoints` takes `note` and `group` (MCP `bp_add` too); the CLI takes the
note after the address (and page), `bpgroup` manages groups; Lua / Python set them with `bp_note(id, text)`
and `bp_group(id, name)`. A group is switched on and off as one.

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
