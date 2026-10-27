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
- [Frame and sync modes: the hook state](#frame-and-sync-modes-the-hook-state)
- [The hit](#the-hit)
- [The sound card's CPU](#the-sound-cards-cpu)
- [Lua](#lua)
- [Python](#python)
- [Remote planes: list and manage](#remote-planes-list-and-manage)
- [Budgets and statistics](#budgets-and-statistics)
- [Over budget: the policy](#over-budget-the-policy)
- [Lifetime](#lifetime)
- [Cost](#cost)
- [Limits of this version](#limits-of-this-version)

## Deferred mode

A callback given a function is **deferred**:

- The emulation thread only notes the hit in the callback's **ring**, together with a snapshot of the registers. It
  never waits for the script.
- The script's own thread takes the hits from the ring and calls the function: the Lua interpreter's thread, or the
  Python thread holding the GIL. The function runs between the script's other tasks, never on the emulation thread.
- A full ring keeps the oldest hits and drops the new ones, and counts each one in `dropped`. The ring holds 256 hits
  by default; `ring` sets 16 to 4096 (rounded up to a power of two).
- The function cannot change the instruction that caused the hit. Whatever it writes (memory, registers) takes effect
  later, at the point where the emulator is when the function runs.

## Frame and sync modes: the hook state

A callback given a **chunk** (a string of Lua source that returns the function) runs in the emulator's **hook state**:
a Lua state of its own, separate from the script's, on the emulation thread.

```lua
-- sync: inside the access, before the next instruction
on_tap({write = 0x5C78, ["if"] = "MDT < MDT@prev"}, [[
    local lost = 0
    return function(hit)
        lost = lost + 1
        shared_set("lost", lost)
        if lost == 3 then poke(0x5C78, 5) end   -- infinite lives, at the instruction that took the last one
    end]], {mode = "sync", instructions = 20000})

-- frame (the default for a chunk): the frame's hits, at the frame's end
on_tap({port_out = 0xFE, mask = 0x00FF}, [[return function(hit) shared_set("border", hit.value & 7) end]])
print(shared_get("lost"), shared_get("border"))
```

| Mode | When the function runs |
|:--|:--|
| `sync` | inside the access, right after the instruction that made it: what it writes is there for the next instruction |
| `frame` | at the end of the hit's frame, every hit of the frame in order; the hit's `frame` is the frame that just ended |

A breakpoint stop or a game event runs a frame or sync callback at once (the machine is at a frame end or stopped).

What a hook can use:

| Function | Does |
|:--|:--|
| `peek(addr)`, `peek16(addr)` | read memory as the CPU sees it now |
| `poke(addr, value)` | write memory as a CPU write would (ROM stays write-protected; TTD sees the page) |
| `reg(name)`, `set_reg(name, value)` | a register, as the debugger names them (any case): `a f b c d e h l i r`, `a'` .. `l'`, `ixh ixl iyh iyl`, `af bc de hl ix iy sp pc ir memptr im iff1 iff2` |
| `frame()`, `tstate()` | the frame counter and the T-state in the frame |
| `shared_get(key)`, `shared_set(key, value)` | values shared with the scripts (`shared_get` / `shared_set` in Lua, `emu.shared_get` / `emu.shared_set` in Python): nil, a boolean, a number or a string |

The libraries are `string`, `math`, `table`, `utf8` and the base functions without `pcall`, `xpcall`, `load`,
`loadfile`, `dofile`, `collectgarbage`, `print`, `setmetatable` and `getmetatable`. There is no `io`, `os`, `package`,
`debug` or `coroutine`: a hook works on the machine only, nothing in it can catch the budget's error, and no
finalizer (`__gc`, which Lua runs with the count hook off) can run out of its reach.

**The instruction budget.** Each call may run `instructions` Lua VM instructions (100000 by default), counted in
steps of 1000. A call over it stops with `instruction budget of N exceeded`: it is counted in `aborted` and in
`errors`, and the policy applies at once (a `suspend` callback is suspended with the reason `a call ran out of its N
instructions`). The count does not depend on the host, so a runaway hook stops at the same point everywhere. A C
function (`string.rep`, a pattern match) counts as one instruction.

**The time budget** (`budget_us`) and its [policy](#over-budget-the-policy) apply as for deferred callbacks, measured
on the emulation thread. Each call holds the machine for its time.

**Roles: view and logic.** A hook is a `view` (the default) or a `logic` callback (`role = "logic"`).

| Role | `poke`, `set_reg` | Under time travel |
|:--|:--|:--|
| `view` | at once: the next instruction sees it | refused while a TTD session records or is browsed (`a view callback does not change the machine while time travel records or browses (role logic does)`); reading is fine |
| `logic` | kept, made at the **next instruction boundary**: after the instruction whose access ran the hook (an `exec` hit: after that instruction); a frame hook's at the first instruction of the next frame | allowed while TTD records: the writes are an edit at that boundary in the recording, and a replay or seek applies them there **without running the hook**; refused while a session is browsed |

```lua
-- Infinite lives that TTD records: the hook runs live, the replay repeats its write
on_tap({write = 0x5C78, ["if"] = "MDT < MDT@prev"}, [[return function(hit) poke(0x5C78, 5) end]],
       {mode = "sync", role = "logic"})
```

- In a logic call, `peek` and `reg` still read the machine as it is: the call's own writes are not there yet.
- A logic call that raises or runs out of instructions makes none of its writes. At most 65536 `poke`s per call.
- A replay never runs a hook, so a logic hook's own state (its upvalues, `shared_set`) is not rewound by a seek.
- No host randomness or time: `math.random` starts from a fixed seed in every hook state, and `math.randomseed` takes
  an explicit seed (with none it raises). There is no `os`.
- TTD v1 (`UNREAL_TTD_BACKEND=v1`, the fallback recorder) does not record these edits: a logic write while it records is refused.
- Deferred callbacks have no role: their writes go through the scripts' memory and register calls, which TTD records
  as tool edits already.

**The `unpaced` policy** is for hooks that may need more than a frame's time. Calls over the budget are counted, and
the callback stays active. When frames run longer than 120 % of their period for 3 frames in a row while unpaced
hooks run, the emulator goes **unpaced**: the host audio output is held (`unpaced` in the [audio mixer](../../.recipe/peripherals/audio-mixer-and-capture.md)'s
holders), so the sound does not break up, and the machine runs as fast as the hooks let it. After 50 frames whose
unpaced hooks took at most a quarter of the period, it goes back to real time and the sound comes back. Each change
publishes `SCRIPT_CALLBACKS_CHANGED` with callback id 0.

The hooks' time per frame: `callback_hooks()` (Lua), `emu.callback_hooks()` (Python), `hooks` in `GET /callbacks`
and the `Hooks:` line of the CLI and MCP lists.

| Field | Meaning |
|:--|:--|
| `unpaced` | the emulator is unpaced now |
| `unpaced_spans` | times it went unpaced |
| `last_frame_us`, `max_frame_us` | the hooks' time in the last frame, the most in one frame |
| `frames_over_share` | frames whose hooks took over 10 % of the frame |

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
| `kind` | `exec`, `read`, `write`, `port_in`, `port_out`, `card_event` (a sound card event), `stop` (a breakpoint stop) or `event` (a game event) |
| `event` | the game event's or the card event's name (game event and card event callbacks only) |
| `cpu` | `main`, or `gs` / `ms` for a tap of a sound card's CPU (then `pc` and `regs` are the card's; `ms` the MultiSound's General Sound) |
| `arg` | a card event's detail: the DAC channel of `dac_fetch`, the clock in MHz of `clock_change`, 1 (finished) or 0 (aborted) of `dma_done`; absent (Lua) / not in the dict (Python) otherwise |
| `regs` | `af bc de hl af_ bc_ de_ hl_ ix iy sp pc i r a f` at the hit |

The hit is a snapshot taken when it happened. Live state read inside the function (`memory`, `get_registers`) is the
state of the machine now, which is later.

## The sound card's CPU

A tap with `cpu = "gs"` watches the CPU of the General Sound / NeoGS card ([gs-debugger](../emulator/design/debugger/breakpoints-and-traces.md#8-the-sound-card-cpu)):
its memory reads, writes and executions (the card's addresses), its port accesses, and its events (`event`: `int`,
`nmi`, `reset`, `page`, `card_command`, `card_data`, `card_reply`, `dac_fetch`, `clock_change`, `dma_start`,
`dma_done`, `gscfg0`). A write tap on a card RAM page (`page`) also hears the NeoGS DMA's writes there (the hit's
address is the offset in the page). The card runs its debug loop while such a tap is
armed; a condition is refused.

- `pc` and `regs` in the hit are the card's; `frame` and `t` stay the machine's.
- In a card hook `peek` reads the card's memory and `reg` its registers. A card hook does not write the card:
  `poke` and `set_reg` raise.
- **A read's byte (fault injection).** A sync hook on the card's `read` or `port_in` may return a byte (0..255):
  the card's CPU sees it instead of the one it read. Returning nothing (or a non-integer) keeps the byte. Another
  number is an error. Frame and deferred callbacks cannot change it. While a time travel session records or is
  browsed the byte is kept and the call counts an error: a replay could not repeat it.

```lua
-- The card sees "no command" in its status port while the hook says so
on_tap({port_in = 4, mask = 0xFF, cpu = "gs"}, [[return function(hit) return hit.value & 0xFE end]], {mode = "sync"})
-- Count the card's interrupts
on_tap({event = "int"}, [[return function(hit) shared_set("ints", (shared_get("ints") or 0) + 1) end]], {mode = "sync"})
```

The named callbacks are the same taps, one per event: `gs_on_command`, `gs_on_data`, `gs_on_reply`, `gs_on_int`,
`gs_on_nmi`, `gs_on_page`, `gs_on_dac_fetch`, `gs_on_clock_change`, `gs_on_dma_start`, `gs_on_dma_done`, `gs_on_gscfg0` take
`(fn [, options])`; `gs_on_port_in(port, fn [, options])` and `gs_on_port_out` watch a card port (its 8-bit decode).
`cpu = "ms"` in the options (Python: `cpu="ms"`) puts them on the MultiSound card's General Sound.
Python takes the same keywords as `on_tap` (`policy`, `budget_us`, `ring`, `label`, `mode`, `instructions`, `role`).

```lua
gs_on_dac_fetch([[return function(hit) shared_set("ch" .. hit.arg, hit.value) end]], {mode = "sync"})
gs_on_port_in(4, [[return function(hit) shared_set("status", hit.value) end]], {mode = "sync"})
```

```python
emu.gs_on_clock_change(lambda hit: print("card clock", hit["arg"], "MHz"))
emu.gs_on_dma_done(lambda hit: print("DMA", hit["value"], "finished" if hit["arg"] else "aborted"))
```

## Lua

```lua
-- A tap: one of exec, read, write, port_in, port_out names the address
id, err = on_tap({write = 0x5C78, to = 0x5C79, ["if"] = "MDT < 5"}, function(hit) print(hit.value) end)
id, err = on_tap({port_out = 0xFE, mask = 0x00FF}, fn)       -- any high byte
id, err = on_tap({exec = 0xC000, page = "ram3"}, fn)         -- page: as for breakpoints (ram<N>, rom<N>, ...)
id, err = on_tap({read = 0x8000, ["if"] = "a==7", dialect = "c"}, fn)
id, err = on_tap({read = 0x4000, cpu = "gs"}, fn)              -- the sound card CPU's read of #4000
id, err = on_tap({event = "card_reply"}, fn)                   -- a card event (cpu "gs" by default)

-- A stop of breakpoint 3 (it still stops), a game event of the profile
id, err = on_breakpoint(3, function(hit) print("stop at", hit.pc) end)
id, err = on_game_event("died", function(hit) print("died, value", hit.value) end)

-- The options (every registration): policy, budget_us, ring, label; a chunk also mode, instructions, role
id = on_tap({write = 0x9000}, fn, {policy = "ignore", budget_us = 500, ring = 1024, label = "score"})
id = on_tap({write = 0x9000}, "return function(hit) end", {mode = "sync", instructions = 5000, policy = "unpaced"})
id = on_tap({write = 0x9000}, "return function(hit) poke(0x9001, 0) end", {mode = "sync", role = "logic"})

callbacks()                -- every callback of the emulator: id, on, owner, label, target, policy, state, stats
callback_resume(id)        -- a suspended one again, with fresh statistics
callback_policy(id, "disable")
callback_remove(id)
callbacks_run()            -- run the waiting hits now (a script that loops without returning)
callback_hooks()           -- the hooks' time per frame and the unpaced state
shared_set("lives", 3)     -- shared with the hooks (nil, boolean, number, string)
shared_get("lives")
```

- A registration returns the id, or `-1` and the reason: an unknown name in the condition, a breakpoint that does
  not exist, a full table (256 callbacks per emulator), a chunk that does not compile or does not return a function
  (`hook: ...`), a function with `mode = "frame"` or `"sync"`.
- An error the function raises is counted in `errors` and kept as `last_error`. The callback stays active.
- The interpreter runs the waiting hits whenever it is idle. A script that loops for a long time calls
  `callbacks_run()` in its loop.

## Python

```python
import unreal_emulator as ue

emu = ue.emu_get_selected()
id = emu.on_tap(lambda hit: print(hit["value"]), "write", 0x5C78, condition="MDT < 5")
id = emu.on_tap(fn, "port_out", 0xFE, mask=0x00FF)    # kind: exec, read, write, port_in, port_out; to=, page=, dialect=
id = emu.on_tap(fn, "read", 0x4000, cpu="gs")         # the sound card CPU's read
id = emu.on_tap(fn, "event", event="int")             # a card event (cpu "gs" by default)
id = emu.on_breakpoint(fn, 3)
id = emu.on_game_event(fn, "died")
id = emu.on_tap(fn, "write", 0x9000, policy="ignore", budget_us=500, ring=1024, label="score")
id = emu.on_tap("return function(hit) shared_set('v', hit.value) end", "write", 0x9000, mode="sync", instructions=5000)

emu.callbacks()                  # a list of dicts, as in Lua
emu.callback_resume(id)          # False for an unknown or disabled one
emu.callback_policy(id, "disable")
emu.callback_remove(id)
ue.callbacks_run()               # run the waiting hits now
emu.callback_hooks()             # the hooks' time per frame and the unpaced state
emu.shared_get("v")              # a value a hook or a script shared (None: not set)
```

- A refused registration raises `ValueError` with the reason.
- The Python thread runs the waiting hits every 10 ms when no task is running, under the GIL.
- An exception from the function is counted in `errors` and kept as `last_error`.
- A `str` instead of a function is a chunk of Lua source for the [hook state](#frame-and-sync-modes-the-hook-state)
  (`mode="frame"`, the default, or `"sync"`): Python has no hook state of its own.

## Remote planes: list and manage

A function lives in a script, so only Lua and Python register callbacks. Every other plane lists them with their
statistics and resumes, removes or re-policies one, with the semantics of `callbacks()`, `callback_resume`,
`callback_remove` and `callback_policy` and the same field names:

| Plane | List | Resume, remove, policy |
|:--|:--|:--|
| CLI | `callback list` (`callbacks`) | `callback resume <id>`, `callback remove <id>`, `callback policy <id> <suspend\|disable\|ignore\|unpaced>` |
| REST | `GET /api/v1/emulator/{id}/callbacks` | `POST .../callbacks/{cb_id}/resume`, `DELETE .../callbacks/{cb_id}`, `PUT .../callbacks/{cb_id}/policy {"policy": "ignore"}` |
| WebSocket | every REST route as a `call`; `callbacks_changed` on `debug.events` | the same `call`s |
| MCP | `control_execution` `callbacks` | `callback_resume`, `callback_remove`, `callback_policy` (`callback_id`, `policy`) |

The CLI and MCP print one line per callback:

```
Script callbacks: 1
#3 tap write #5C78 (lua "slow") suspended, policy suspend, hits 3, dropped 0, pending 0, calls 3, errors 0, overruns 3, max 3012 us, p99 4095 us, total 9030 us, budget 1000 us, ring 256, last overrun 3012 us at frame 412; reason: 3 calls over 1000 us within 50 frames
```

A frame or sync callback adds its mode after the state, and `aborted N, instructions N` after the ring. Once there
are such callbacks the list has the hooks' line after the header:

```
Hooks: paced, last frame 12 us, max 340 us, frames over 10 % 0, unpaced spans 0
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
| `mode` | `deferred`, `frame` or `sync` |
| `aborted`, `instructions` | frame and sync: calls stopped by the instruction budget; the budget |

## Over budget: the policy

| Policy | When calls go over the budget |
|:--|:--|
| `suspend` (default) | after 3 calls over the budget within 50 frames, or one call over 10 times the budget, the callback is **suspended**: it notes no more hits until `callback_resume(id)` (statistics start again) |
| `disable` | the same rule removes the callback's tap; the callback stays in the list as `disabled`; register again to get it back. A frame or sync callback disabled on the emulation thread keeps its tap, silent, until it is removed |
| `ignore` | counted in `overruns` only |
| `unpaced` | frame and sync only: counted, and the emulator [slows down](#frame-and-sync-modes-the-hook-state) with its audio held instead |

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
- **A hook:** its call's time on the emulation thread, every hit (sync) or at the frame end (frame). With no frame
  or sync callback the frame end tests one counter.

## Limits of this version

- Only a card read's byte can be replaced; the main CPU's reads cannot.
- A card tap takes no condition; a card hook does not write the card.
- One hook state per emulator for every script: two scripts' hooks share its globals.
- No script manifest yet: callbacks are registered one call at a time.
- Only Lua and Python register callbacks; CLI, REST, WebSocket and MCP list and manage them
  ([above](#remote-planes-list-and-manage)). No monitoring topic or readout carries them yet.
