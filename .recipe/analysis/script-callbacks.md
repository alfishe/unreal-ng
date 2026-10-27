# Script callbacks: run a Lua or Python function on a tap, a stop or a game event

A breakpoint that stops is the wrong tool when you want to **watch**: count every life lost, log every write to the
score, react to a game event, all without pausing the game. A **script callback** calls your function for each hit,
on the script's own thread, with a snapshot of the registers at the hit. The machine never stops and never waits for
the script. Reference: [docs/features/script-callbacks.md](../../docs/features/script-callbacks.md).

| You want to... | Lua (Python: `emu.on_tap(fn, kind, address, ...)`) |
|:--|:--|
| count the writes of one value to a byte | `on_tap({write = 0x5C78, ["if"] = "MDT == 0"}, fn)` |
| see each life lost (a byte going down) | `on_tap({write = LIVES, ["if"] = "MDT < MDT@prev"}, fn)` |
| log each border colour change | `on_tap({port_out = 0xFE, mask = 0x00FF}, fn)` (`hit.value & 7`) |
| run code each time a routine is entered | `on_tap({exec = 0x8000}, fn)` (`hit.regs` at entry) |
| react to a breakpoint's stop (it still stops) | `on_breakpoint(id, fn)` |
| react to a profile's game event | `on_game_event("died", fn)` (a controller profile with `game.events`) |

## Steps (Lua over WebAPI)

```bash
BASE=http://localhost:8090/api/v1
EMU=$(curl -s -X POST $BASE/emulator/start -H 'Content-Type: application/json' -d '{"model":"48K"}' | jq -r .id)
curl -s -X PUT $BASE/emulator/$EMU/audio/mixer/master -H 'Content-Type: application/json' -d '{"muted":true}'
curl -s -X PUT $BASE/emulator/$EMU/feature/breakpoints -H 'Content-Type: application/json' -d '{"enabled":true}'

lua() { curl -s -X POST $BASE/lua/exec -H 'Content-Type: application/json' -d "$(jq -n --arg c "$1" '{code:$c}')"; }

# FRAMES (#5C78) is written on every interrupt: count the writes that make its low byte 0, and keep the frames
lua 'zeros = {}
id, err = on_tap({write = 0x5C78, ["if"] = "MDT == 0"}, function(hit) zeros[#zeros + 1] = hit.frame end,
                 {label = "frames-wrap"})
print(id, err)'
#   1	nil

# ... the low byte wraps every 256 frames (~5 s): after ~27 s the function has run 6 times
lua 'for _, cb in ipairs(callbacks()) do print(cb.id, cb.label, cb.target, cb.state, cb.hits, cb.calls, cb.max_us) end
print(#zeros, zeros[1], zeros[2])'
#   1	frames-wrap	tap write #5C78 if MDT == 0	active	6	6	60
#   6	31	338
```

- A refusal comes back as `-1` and the reason: `on_tap({write = 0x5C78, ["if"] = "FOO == 1"}, f)` gives
  `-1	condition: unknown name 'FOO' at column 1`.
- `callbacks()` lists every callback of the emulator, from every script, with its statistics: `hits`, `dropped`,
  `pending`, `calls`, `errors`, `overruns`, `total_us`, `max_us`, `p99_us`, `last_error`.
- An error in the function is counted, not fatal: `errors` grows and `last_error` holds the message.

## Python

```bash
py() { curl -s -X POST $BASE/python/exec -H 'Content-Type: application/json' -d "$(jq -n --arg c "$1" '{code:$c}')"; }
py 'import unreal_emulator as ue
emu = ue.emu_get_selected()
frames = []
fc = emu.on_tap(lambda hit: frames.append((hit["frame"], hit["value"], hit["regs"]["pc"])), "write", 0x5C78,
                label="frames")
print(fc)'
#   1
py 'print(len(frames), frames[:2])
c = [c for c in emu.callbacks() if c["id"] == fc][0]; print(c["hits"], c["calls"], c["errors"], c["owner"])'
#   26 [(19, 2, 4574), (31, 1, 4586)]
#   27 26 0 python                      (one hit was still in the ring)
```

The code of every `python/exec` runs in `__main__`: the names it defines stay for the next call, and the callback's
function sees them. A refused registration raises `ValueError` with the reason. The Python thread runs the waiting hits every 10 ms
under the GIL.

## A slow callback: suspended, then resumed

Each call has a time budget (`budget_us`, 1000 by default). Under the default policy `suspend`, a callback that goes
over it 3 times within 50 frames (or once by 10 times) is **suspended**: it notes no more hits.

```lua
slow = on_tap({write = 0x5C78}, function(hit) local t = os.clock() while os.clock() - t < 0.003 do end end,
              {budget_us = 1000})
-- ... a few frames later
for _, cb in ipairs(callbacks()) do print(cb.id, cb.state, cb.reason, cb.overruns) end
--   1	active	nil	0
--   3	suspended	3 calls over 1000 us within 50 frames	3      (ids are never reused: a refusal takes one too)
callback_resume(slow)          -- active again, statistics reset
callback_policy(slow, "ignore")  -- or: count the overruns and keep calling
callback_remove(slow)
```

- `disable` instead of `suspend` removes the callback's tap for good (register again to get it back).
- A slow deferred callback never slows the emulator: the cost is its backlog (`pending` grows, then `dropped`).

## Sync and frame hooks: the Lua hook state

A function runs on the script's thread, later. To change the machine **at** the hit (patch a value, skip a routine)
or to sum per frame, give the callback as a **chunk** (Lua source returning the function) and a `mode`: `sync` runs
it inside the access, `frame` at the end of the hit's frame. It runs in a small Lua state on the emulation thread with
`peek`, `peek16`, `poke`, `reg`, `set_reg`, `frame`, `tstate`, and `shared_get` / `shared_set` to talk to the script.

```lua
sync = on_tap({write = 0x5C78}, [[
    local n = 0
    return function(hit)
        n = n + 1
        shared_set("writes", n)
        shared_set("same_frame", frame() == hit.frame)
    end]], {mode = "sync", label = "frames-sync"})
framed = on_tap({write = 0x5C78}, [[return function(hit) shared_set("framed", peek(0x5C78)) end]], {mode = "frame"})
-- ... later, on the script's thread
print(shared_get("writes"), shared_get("same_frame"), shared_get("framed"))   --  24  true  21
bad, why = on_tap({write = 0x5C78}, "return function(", {mode = "sync"})
print(bad, why)                    --  -1  hook: chunk:1: <name> or '...' expected near <eof>
```

- Each call has an instruction budget (`instructions`, 100000 by default). A runaway is cut and handled by its
  policy at once: `return function() while true do end end` with `instructions = 5000` is `suspended`, reason
  `a call ran out of its 5000 instructions`, `aborted` 1. `pcall`, `load`, `setmetatable`, `print` are not there.
- `poke` and `set_reg` fail while TTD records or replays: the hook would change the machine behind the journal.
- Policy `unpaced` (hooks only): a heavy hook may make frames long; after 3 frames over 1.2 times the period the
  audio is held (`audio mixer` holders `"unpaced": 1`) instead of crackling, released after 50 calm frames or when
  the callback goes. `callback_hooks()` (REST `hooks`) shows `unpaced`, `unpaced_spans`, `last_frame_us`,
  `max_frame_us`: an 8M-iteration loop on `exec = 0x0038` gave `unpaced true 1`, `last_frame_us` ~286000.
- Python: pass the chunk as a `str`, `emu.on_tap("return function(hit) ... end", "write", 0x5C78, mode="sync")`.

## From outside the script: REST, CLI, MCP, WebSocket

Only a script registers a callback, but any client lists them and resumes, removes or re-policies one (same fields
as `callbacks()`):

```bash
curl -s $BASE/emulator/$EMU/callbacks | jq '.callbacks[] | {id, label, target, state, reason, hits, calls, max_us}'
#   {"id": 3, "label": "slow", "target": "tap write #5C78", "state": "suspended",
#    "reason": "3 calls over 1000 us within 50 frames", "hits": 3, "calls": 3, "max_us": 3012}
curl -s -X POST $BASE/emulator/$EMU/callbacks/3/resume | jq -r .message          # Callback 3 resumed
curl -s -X PUT  $BASE/emulator/$EMU/callbacks/3/policy -H 'Content-Type: application/json' \
     -d '{"policy":"ignore"}' | jq -r .callback.policy                           # ignore
curl -s -X DELETE $BASE/emulator/$EMU/callbacks/3 | jq -r .message               # Callback 3 removed
```

- CLI: `callback list`, `callback resume 3`, `callback policy 3 ignore`, `callback remove 3` (one line per callback).
- MCP: `control_execution` with `action` `callbacks`, `callback_resume`, `callback_policy` (`policy`) or
  `callback_remove`, and `callback_id`; the text has the CLI's lines.
- WebSocket `/api/v1/ws`: subscribe to `debug.events` and get `callbacks_changed` with the callback when one is
  suspended over its budget, instead of polling.
- A disabled callback cannot be resumed (409): the script registers it again.

## Gotchas

- The hit is a **snapshot**. `memory` or `get_registers()` inside the function read the machine **now**, frames
  later perhaps. Use `hit.value` and `hit.regs`.
- `hit.pc` is the PC **after** the instruction that made the access.
- A script that loops without returning (a long `for` with waits) must call `callbacks_run()` in its loop, or its
  hits only run when it returns.
- Callbacks go with their interpreter: a Lua stop or restart, the end of Python, removes them on every emulator.
- Taps are muted while TTD replays or seeks: no callbacks run on replay.
- A tap callback's hidden tap does not show in breakpoint lists; `callbacks()` shows it as `target`.
