# Script callbacks

A Lua or Python script can ask to be called when something happens in the machine:

- a **tap**: a memory access or a port access, with an optional [condition](conditional-breakpoints.md). A tap
  never stops the machine;
- a **breakpoint stop**: an existing breakpoint stops the machine (it still stops);
- a **game event** of the controller profile (`game.events`, [haptics](../inprogress/2026-10-17-controller-manager/haptics.md)) fires on a frame.

Example (Lua): count the lives lost, without stopping the game.

```lua
lost = 0
on_tap({write = 0x5C78, ["if"] = "MDT < MDT@prev"}, function(hit) lost = lost + 1 end, {label = "lives"})
```

Design and measurements: [conditions-and-callbacks.md](../inprogress/2026-10-26-breakpoint-taps/conditions-and-callbacks.md).
Task recipe: [.recipe/analysis/script-callbacks.md](../../.recipe/analysis/script-callbacks.md).

## Contents

- [Deferred mode](#deferred-mode)
- [The hit](#the-hit)
- [Lua](#lua)
- [Python](#python)
- [Remote planes: list and manage](#remote-planes-list-and-manage)
- [Budgets and statistics](#budgets-and-statistics)
- [Over budget: the policy](#over-budget-the-policy)
- [Lifetime](#lifetime)
- [Cost](#cost)
- [Limits of this version](#limits-of-this-version)

## Deferred mode

Every callback of this version is **deferred**:

- The emulation thread only notes the hit in the callback's **ring**, together with a snapshot of the registers. It
  never waits for the script.
- The script's own thread takes the hits from the ring and calls the function: the Lua interpreter's thread, or the
  Python thread holding the GIL. The function runs between the script's other tasks, never on the emulation thread.
- A full ring keeps the oldest hits and drops the new ones, and counts each one in `dropped`. The ring holds 256 hits
  by default; `ring` sets 16 to 4096 (rounded up to a power of two).
- The function cannot change the instruction that caused the hit. Whatever it writes (memory, registers) takes effect
  later, at the point where the emulator is when the function runs.

The frame and sync modes (a function on the emulation thread with an instruction budget) come in the next phase.

## The hit

The function gets one argument: the hit, a table (Lua) or a dict (Python).

| Field | Meaning |
|:--|:--|
| `frame` | the frame of the hit |
| `t` | the T-state in the frame |
| `pc` | PC after the instruction that made the access |
| `address` | the memory address or the port; 0 for a game event |
| `value` | the byte read, written, input or output; a game event's value; absent (Lua) / `None` (Python) at an exec tap or a stop |
| `breakpoint` | the id of the tap or of the breakpoint |
| `kind` | `exec`, `read`, `write`, `port_in`, `port_out`, `stop` (a breakpoint stop) or `event` (a game event) |
| `event` | the game event's name (game event callbacks only) |
| `regs` | `af bc de hl af_ bc_ de_ hl_ ix iy sp pc i r a f` at the hit |

The hit is a snapshot taken when it happened. Live state read inside the function (`memory`, `get_registers`) is the
state of the machine now, which is later.

## Lua

```lua
-- A tap: one of exec, read, write, port_in, port_out names the address
id, err = on_tap({write = 0x5C78, to = 0x5C79, ["if"] = "MDT < 5"}, function(hit) print(hit.value) end)
id, err = on_tap({port_out = 0xFE, mask = 0x00FF}, fn)       -- any high byte
id, err = on_tap({exec = 0xC000, page = "ram3"}, fn)         -- page: as for breakpoints (ram<N>, rom<N>, ...)
id, err = on_tap({read = 0x8000, ["if"] = "a==7", dialect = "c"}, fn)

-- A stop of breakpoint 3 (it still stops), a game event of the profile
id, err = on_breakpoint(3, function(hit) print("stop at", hit.pc) end)
id, err = on_game_event("died", function(hit) print("died, value", hit.value) end)

-- The options (every registration): policy, budget_us, ring, label
id = on_tap({write = 0x9000}, fn, {policy = "ignore", budget_us = 500, ring = 1024, label = "score"})

callbacks()                -- every callback of the emulator: id, on, owner, label, target, policy, state, stats
callback_resume(id)        -- a suspended one again, with fresh statistics
callback_policy(id, "disable")
callback_remove(id)
callbacks_run()            -- run the waiting hits now (a script that loops without returning)
```

- A registration returns the id, or `-1` and the reason: an unknown name in the condition, a breakpoint that does
  not exist, a full table (256 callbacks per emulator).
- An error the function raises is counted in `errors` and kept as `last_error`. The callback stays active.
- The interpreter runs the waiting hits whenever it is idle. A script that loops for a long time calls
  `callbacks_run()` in its loop.

## Python

```python
import unreal_emulator as ue

emu = ue.emu_get_selected()
id = emu.on_tap(lambda hit: print(hit["value"]), "write", 0x5C78, condition="MDT < 5")
id = emu.on_tap(fn, "port_out", 0xFE, mask=0x00FF)    # kind: exec, read, write, port_in, port_out; to=, page=, dialect=
id = emu.on_breakpoint(fn, 3)
id = emu.on_game_event(fn, "died")
id = emu.on_tap(fn, "write", 0x9000, policy="ignore", budget_us=500, ring=1024, label="score")

emu.callbacks()                  # a list of dicts, as in Lua
emu.callback_resume(id)          # False for an unknown or disabled one
emu.callback_policy(id, "disable")
emu.callback_remove(id)
ue.callbacks_run()               # run the waiting hits now
```

- A refused registration raises `ValueError` with the reason.
- The Python thread runs the waiting hits every 10 ms when no task is running, under the GIL.
- An exception from the function is counted in `errors` and kept as `last_error`.

## Remote planes: list and manage

A function lives in a script, so only Lua and Python register callbacks. Every other plane lists them with their
statistics and resumes, removes or re-policies one, with the semantics of `callbacks()`, `callback_resume`,
`callback_remove` and `callback_policy` and the same field names:

| Plane | List | Resume, remove, policy |
|:--|:--|:--|
| CLI | `callback list` (`callbacks`) | `callback resume <id>`, `callback remove <id>`, `callback policy <id> <suspend\|disable\|ignore>` |
| REST | `GET /api/v1/emulator/{id}/callbacks` | `POST .../callbacks/{cb_id}/resume`, `DELETE .../callbacks/{cb_id}`, `PUT .../callbacks/{cb_id}/policy {"policy": "ignore"}` |
| WebSocket | every REST route as a `call`; `callbacks_changed` on `debug.events` | the same `call`s |
| MCP | `control_execution` `callbacks` | `callback_resume`, `callback_remove`, `callback_policy` (`callback_id`, `policy`) |

The CLI and MCP print one line per callback:

```
Script callbacks: 1
#3 tap write #5C78 (lua "slow") suspended, policy suspend, hits 3, dropped 0, pending 0, calls 3, errors 0, overruns 3, max 3012 us, p99 4095 us, total 9030 us, budget 1000 us, ring 256, last overrun 3012 us at frame 412; reason: 3 calls over 1000 us within 50 frames
```

- A disabled callback cannot be resumed (REST 409, CLI and MCP say so): the script registers it again.
- A callback removed from outside is gone for its script too: the function is never called again.
- `callbacks_changed` carries `{id, callback}` (the entry as `GET /callbacks` shows it) on a registration, a
  resume, a policy change, a suspension or a disable, and `{id, removed: true}` on a removal. A hit is not an event:
  poll `GET /callbacks` for the statistics.
- The changing verbs pass the automation gate as the breakpoint verbs do (under another client's run control they
  are refused). There is no catalogue action: a callback id lives only as long as the script that registered it, so
  a key bound to one would point at nothing after the next run.

## Budgets and statistics

Each callback has a **time budget** per call, 1000 µs by default (`budget_us`). The script's thread measures each
call. The budget cannot stop a call that runs too long; it only detects it. A deferred callback never holds the
emulator, so a slow one costs the script its backlog (the ring fills, hits drop), not the frame rate.

The statistics of each callback, in `callbacks()`:

| Field | Meaning |
|:--|:--|
| `hits` | hits noted in the ring |
| `dropped` | hits lost because the ring was full |
| `pending` | hits waiting in the ring now |
| `calls` | calls of the function |
| `errors` | calls that raised |
| `overruns` | calls over the budget |
| `total_us`, `max_us`, `p99_us` | call times (p99 from a histogram of powers of two) |
| `last_overrun_frame`, `last_overrun_us` | the last call over the budget |
| `last_error` | the last error's text |

## Over budget: the policy

| Policy | When calls go over the budget |
|:--|:--|
| `suspend` (default) | after 3 calls over the budget within 50 frames, or one call over 10 times the budget, the callback is **suspended**: it notes no more hits until `callback_resume(id)` (statistics start again) |
| `disable` | the same rule removes the callback's tap; the callback stays in the list as `disabled`; register again to get it back |
| `ignore` | counted in `overruns` only |

A single slow call (a garbage collection) does not suspend a callback. The `reason` field says why one was
suspended or disabled. Every change of state publishes `SCRIPT_CALLBACKS_CHANGED` with the emulator and the callback
id.

## Lifetime

- A tap callback owns a hidden tap (a breakpoint that never stops, owner `callback:<id>`). Removing the callback
  removes the tap. The tap is not shown in breakpoint lists.
- When the interpreter stops (Lua stop or restart, the end of Python), every callback it registered is removed on
  every emulator.
- A callback of an emulator that was closed goes with it.
- Taps and breakpoints are muted while TTD replays or seeks, so callbacks do not run on replay.

## Cost

- **A machine with no callbacks:** nothing is added to the hit path. A tap callback is an ordinary tap.
- **A hit:** the tap's cost (the condition included, [measured](conditional-breakpoints.md)), plus a register
  snapshot and a ring push of a few nanoseconds.
- **A breakpoint stop or a game event:** one test of a counter when no callback asks for them.
- **The script's side:** a call of the function per hit, on the script's thread; Python adds the GIL.

## Limits of this version

- Deferred mode only. The frame and sync modes, the instruction budget and the `unpaced` policy come in phase 4;
  callbacks that change the machine under TTD recording (`logic`) come in phase 5.
- No script manifest yet: callbacks are registered one call at a time.
- Only Lua and Python register callbacks; CLI, REST, WebSocket and MCP list and manage them
  ([above](#remote-planes-list-and-manage)). No monitoring topic or readout carries them yet.
