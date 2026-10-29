# Conditions and script callbacks on breakpoints and taps

**Status:** 2026-10-26: design, measured in [POC 039](../../../tools/poc/039-condition-engine-callbacks/README.md);
the decisions in §8 are taken. 2026-10-27: phases 1-5 (§9) are built, the GS read-value hooks included. Phase 1 (F1 core): [conditional breakpoints](../../features/conditional-breakpoints.md). It covers phase 5 of the [taps](README.md) and the condition engine F1 of
[conditional breakpoints](../2026-08-17-conditional-breakpoints/design.md).

## 1. What the numbers say

All numbers below are from POC 039 on an M-series Mac. The frame is 20 ms; 10 % of it is the deferred-tier budget of
R02 P-5.

| Item | Cost per hit | Verdict |
|:--|--:|:--|
| fast condition shape (`A==0`, `MWV==k`, a flag, a byte) | 1-2 ns | free |
| RPN VM, 4-22 ops | 9-16 ns | within R02 P-2 (≤ 30 ns); no JIT |
| full-range exec condition at 0.9 M instr/s | 1.4 % of a core | the worst case of performance.md, lower than its 3-8 % estimate |
| Lua call, a state owned by the calling thread, raw API | 19-31 ns (+8-10 ns with the count hook) | synchronous per hit is affordable |
| Lua call through the AutomationLua worker (round trip) | 3-4 µs p50, 16-18 µs p99 | never per hit |
| async push of a hit into a ring | 2-4 ns | free, for every language |
| Python, GIL free, kept thread state | 125 ns | fast, but only while no other Python thread runs |
| Python while the script thread runs Python | p99 6.3 ms | one hit can exceed the frame: never synchronous by default |
| per-call timing (two clock reads) | 34 ns | affordable; the instruction count is cheaper |

The engine itself is not the risk; where a callback runs is.

## 2. Requirement coverage

The audit is 2026-10-26: about 60 requirements from 15 documents. The full table is in
[requirements-audit.md](../../../tools/poc/039-condition-engine-callbacks/requirements-audit.md). The blocks below say what the engine covers, what needs an addition, and what
conflicts.

### 2.1 Covered by F1 as designed (with the measured cost)

- **Language and evaluation:**
  - expression compiled once to RPN, evaluated after the trigger matched;
  - registers including primed and I, R, IM; flag mnemonics; memory `(e)`, `(e).w`;
  - the access bus `MRA MWA MRV MWV PRA PWA PRV PWV`; paging `PG0-3 ROMPG DOS SHADOW`; `T FRAME DT IFF1 IFF2 HALTED`;
  - labels.
- **Tiers:** fast shapes plus the VM; differential tests; validation at entry with the column of the error; programs
  compiled off the emulation thread and swapped atomically.
- **Triggers:** exec, memory, ports, ranges, slots and physical pages (already built). Conditions on taps (taps
  phase 5) use the same evaluation after the match, with no extra work on the miss path.
- **Hit policies, error state, planes:** `condition`, `hits`, `set_condition`, `reset_hits`, CLI `bp … if`; DZRP
  condition strings.

### 2.2 Additions the engine can take without a change of model

Each is a new operand or opcode in the same VM, or a sampling point that reuses the evaluator.

| Requirement | Source | Addition |
|:--|:--|:--|
| WZ / MEMPTR, beam (line, T, `RAY(x,y)`), `HITS`, `FD`, device fields | use-cases §4.4, rules §5.3 | operands from an accessor table; device fields through the registry (R02 §3) |
| cross-CPU `main.X`, `gs.X`, card registers | rules §5.3, GS B4 | one context per CPU; an operand carries a CPU index |
| `memhash()`, `physhash(ram3:0000,len)` | metadata TR-7 | functions with a "deferred" cost class; the compiler refuses them inline (R02 §6) |
| `X@prev`, edge enter/leave, "on change" | metadata TR-8, BE, Xpeccy | a per-condition previous-value slot, updated after evaluation; edge = last result kept |
| frame-sampled conditions | TR-8, use-cases "conditions track" | the same program evaluated once per frame at the frame end (a watch with a condition) |
| address-less conditions (registerpoints) | BE §5, comparative analysis | evaluated per instruction: 1.4 % of a core at 16 ns; shown with the ⚠ badge |
| log actions with `{expr:02x}` placeholders | rules §5.4a, toolchain §9.2 | each placeholder is a compiled program; formatting happens at drain, the hit writes values to a ring (≤ 50 ns, R02 P-3) |
| struct and field access, dotted registry names | toolchain §7.2, R02 §3 | resolved at compile time to addresses or accessor indices |

### 2.3 Conflicts the owner decides (§8)

1. **Precedence and literals.**
   - F1 (design.md §4.2): bitwise binds tighter than comparisons, and literals are decimal by default.
   - The debugger model (rules.md §5.3) and the TUI prototype: the Unreal order (comparisons tighter than `&`), with
     hex by default and `123d` for decimal.
   - The expression-evaluator design: C order, with octal literals.
   - ZRCP and DZRP add their own dialects.
2. **Operand names.** F1 `MRA/MWA/MRV/MWV` vs `RD/WR/MDT/IN/OUT/VAL` (rules, TS-Labs, Xpeccy), plus the ZXSpin
   aliases. `PG0-3` vs `SLOT0-3`. Memory `(x)` vs `M(x)`, `W(x)`, `mem[x]`, `PEEK`. Aliases can accept all of these;
   only the printed (canonical) form needs a decision.
3. **Hit counting order.** The code counts every match before any condition (`WalkCandidates`); performance.md S9
   counts only condition-true hits. Decided (§8): both, as `matchCount` and `hitCount`.
4. **Errors at run time.**
   - F1: ERROR, notify, do not pause.
   - rules: stop and ERROR.
   - ZRCP: stop.
   - The breakpoint-enhancements doc: treat as true.
   - Decided (§8): ERROR plus one notification, never a stop.
5. **Callbacks.**
   - GS design: on the emulation thread, 1 ms, then logged and disabled.
   - R02: deferred to a safe point, a per-frame budget, overrun puts the trigger in ERROR.
   - §3-§5 below replace both with one model.
6. **`when:` in game profiles.** The key already means `decreased / increased / changed / ==` (haptics.md §5). A
   condition gets its own key, `if: "A==3"`, and `when:` keeps its meaning. `decreased` and `increased` need the
   replaced byte (`before`, designed in taps §3, not built). F1's `@prev` covers it.

## 3. Where a callback runs

There are three modes, chosen per callback when it is registered. The default depends on the language.

| Mode | Where | Hit path cost | Who may use it | Default for |
|:--|:--|--:|:--|:--|
| **deferred** | on the script's own thread, drained from a per-script ring of hits | 2-4 ns (push) | Lua, Python, WebAPI / WebSocket clients, MCP | Python, all remote clients |
| **frame** | on the emulation thread at the frame end (a safe point), from the same ring | 2-4 ns plus the callback at the frame end | Lua | Lua |
| **sync** | on the emulation thread inside the hit | 20-40 ns plus the body | Lua only, explicit flag | — |

- **Deferred** keeps today's threading: the AutomationLua worker and the Python thread call the function with the
  state and GIL they already own. A full ring drops hits and counts `dropped` (the planes' rule). The emulator never
  waits. The callback sees the hit's snapshot `{frame, T, pc, address, value, kind, regs}`, not live state.
- **Frame** and **sync** need a Lua state owned by the emulation thread: a **hook state** per emulator, separate from
  the script's state. A script registers a callback as a chunk of source (a string or a file), not as a closure:
  `on_tap("jump", "return function(hit) … end", {mode = "sync"})`. The hook state compiles the chunk. The two states
  share data only through emulator APIs (memory, registers, the game-event log, a small key/value table). This makes
  the cost of `sync` the measured 20-40 ns instead of a 3-18 µs round trip, and keeps the hook state serialisable
  (R02 L-1: no closures in checkpoints).
- **Sync** may change emulator state (a poke, a register write, a replacement read value for the GS hooks).
  **Deferred** and **frame** may not change the instruction being executed; changes take effect after it.
- **Python never runs sync** in the first version. The GIL wait (up to 6.3 ms) is outside the callback's control. A
  later opt-in "sync while no other Python thread runs" can use a kept thread state (125 ns per call).

### 3.1 The script manifest

A script declares its callbacks in a manifest instead of registering them one call at a time. The manifest is a
table in the script (or a YAML file beside it). The emulator reads it before the script runs, so it can do four
things:

- validate every trigger and condition at load (errors with positions, before anything runs);
- compile the hook-state chunks for frame and sync callbacks;
- refuse a `logic` callback while TTD records unless the manifest declares it;
- show the user what the script will do (which events, which modes, which budgets, which policies) on every plane.

```lua
manifest = {
  name = "dizzy-lives",
  role = "view",                       -- view (read-only, skipped on replay) | logic (mutates, journaled)
  budget = { instructions = 100000, us = 1000, frame_pct = 10 },   -- defaults for the callbacks below
  policy = "suspend",                  -- suspend | unpaced | disable | ignore
  callbacks = {
    { on = "game_event", event = "died", mode = "deferred", fn = "on_died" },
    { on = "tap", write = "lives", ["if"] = "MWV < (lives)", mode = "sync",
      chunk = "return function(hit) emu.mark('life lost') end", policy = "unpaced" },
  },
}
```

- `fn` names a function of the script itself: deferred mode only, run on the script's thread.
- `chunk` is source for the hook state: frame and sync modes.
- `role`, `budget` and `policy` are the defaults for the callbacks, and each callback can override them.
- The calls `on_tap` and `on_game_event` stay available for scripts without a manifest. They register one callback
  with the same fields.
- `callback list` shows which manifest each callback came from.

## 4. Budgets: what is measured

Each callback has three budgets.

| Budget | Unit | Default | Measured by |
|:--|:--|:--|:--|
| per call, **instructions** (Lua) | VM instructions | 100 000 (~0.23 ms here) | the count hook every 1000 instructions: deterministic, the same on every host |
| per call, **time** (all languages) | µs of wall clock | 1 ms (GS design) | two clock reads around the call (34 ns) |
| per frame, **time** | % of the frame period | 10 % (R02 P-5) | the sum of a frame's callback time on the emulation thread (sync + frame modes) |

- The **instruction** budget stops a runaway exactly: the hook raises an error, the call unwinds, and the hit is
  marked `aborted`.
- The **time** budgets cannot stop a call (except Lua through the same hook, which checks the clock every 1000
  instructions). They only detect an overrun, after the call returns.
- Deferred callbacks run on the script's own thread and never hold the emulator, so their time budget guards the
  script's backlog (ring fill, drops), not the frame.
- Statistics per callback: `calls`, `aborted`, `overruns`, `dropped`, `totalUs`, `maxUs`, `p99Us` (a small
  histogram), `lastOverrun {frame, us, instructions}`. They are on every plane (§7).

## 5. Over budget: the policy

The **policy** is set per callback (or per script, as its default), from the four options the owner listed.

| Policy | On an overrun | Emulation | Resume |
|:--|:--|:--|:--|
| **suspend** (default) | the callback is suspended; the trigger state is `SUSPENDED(reason)`; an event goes out on every plane | keeps its pace | the user re-enables it (`callback resume <id>`), with stats reset |
| **unpaced** (the user allows it) | nothing is suspended; the emulator stops pacing to wall clock and runs as fast as the callbacks let it, a slow-motion mode, with audio muted rather than stuttering | slower than real time, with the same results | the user switches back, or the policy returns to paced once frames fit again |
| **disable** | the callback is removed for the session (not restored from a saved set); an event is published | keeps its pace | register again |
| **ignore** | counted in the stats only | may stutter | — |

- **The instruction budget applies under every policy** (even `ignore` and `unpaced`): a `while true` in a sync
  callback must never hang the emulation thread. Under `unpaced` the user may raise the instruction budget, but not
  remove it.
- **`unpaced` and determinism.** With the instruction budget as the only limit, callbacks end at the same points on
  every host, so a run with slow callbacks gives the same frames as a fast one, only later in wall-clock time. TTD,
  replays and CI see identical results. This is why the deterministic budget is the instruction count and not
  milliseconds.
- **One overrun or a rate.** Proposal: `suspend` triggers on 3 overruns within 50 frames, or on one call over 10× its
  budget. A single GC stall (≤ 43 µs measured) must not suspend a callback.
- An aborted or suspended callback never stops the emulator and never changes a breakpoint's stop behaviour: a
  stopping breakpoint with a callback still stops.

## 6. TTD and determinism

- Taps and breakpoints are muted on TTD replay today (taps §2), so callbacks do not run on replay or seek.
- R02 §5 action classes apply to callbacks:
  - A **view** callback (read-only, the default) is skipped on replay.
  - A **logic** callback (declared at registration) may mutate state. Its effects are journaled as
    `TTDExternalEvent` and replayed from the journal, never re-run. It gets no host time and no host RNG
    (R02 L-2): in the hook state, `os.time` / `os.clock` / `math.random` are replaced by emulator time and a seeded
    RNG.
- A mutating sync callback is refused while TTD records unless it is declared `logic` (rules §6: edits are refused
  while recording).

## 7. Planes

Every plane gets the same verbs (automation-planes guidelines):

- `on_tap` / `on_breakpoint` / `on_game_event` with `{mode, policy, budget}`;
- `callback list` (with stats), `callback resume`, `callback remove`, `callback policy`.

The WebSocket topic `callbacks` carries the state changes (suspended, disabled, unpaced on/off). Remote clients
(WebAPI, WebSocket, MCP) are always deferred: a hit becomes a topic event, which is the at-most-once, bounded queue of
R02 §11.

Built 2026-10-27 (list and manage; registration stays with the scripts): CLI `callback list | resume | remove |
policy`, REST `GET /callbacks`, `POST /callbacks/{cb_id}/resume`, `DELETE /callbacks/{cb_id}`,
`PUT /callbacks/{cb_id}/policy`, MCP `control_execution` `callbacks`, `callback_resume`, `callback_remove`,
`callback_policy`. The state changes go out as `callbacks_changed` on `debug.events` (beside `breakpoints_changed`)
rather than a topic of their own; hits are not pushed to remote clients yet. One report and one text line in core
(`callbackcontrol.h`, `callbacktext.h`) for every plane.

## 8. Decisions for the owner

1. **Dialect: decided 2026-10-26, the Unreal order is canonical** (rules.md §5.3, the TUI prototype): comparisons
   bind tighter than `&`, `^`, `|`; `&&` above `||`; numbers are hex by default, `123d` is decimal. F1's form and
   C are accepted as parse dialects; conditions print in the canonical form. design.md §4.2 follows this.
2. **Callback thread model: decided 2026-10-26, as §3 describes**: a Lua hook state per emulator for sync and frame
   modes (callbacks registered as source, not closures; frame is Lua's default), and deferred on the script's thread
   for Python and remote clients.
3. **Over-budget policy: decided 2026-10-26**: `suspend` is the default; `unpaced` is a per-callback flag set at
   registration (no session opt-in). The emulator publishes `unpaced on` / `off` when it starts and stops falling
   behind wall clock.
4. **Hit counting: decided 2026-10-26, two counters**: `matchCount` counts every trigger match (before the
   condition), `hitCount` counts condition-true hits; the `hits` policy works on `hitCount`. The false path pays one
   increment.
5. **Script manifest: agreed 2026-10-26** (§3.1).
6. **Condition errors: decided 2026-10-26, mark, never stop.**
   - At run time, an evaluation error puts the breakpoint or tap in `ERROR(reason, frame, pc)`, skips it from then on
     (as built: its filter bits stay, a match costs one test of the mark; enabling it again or a new condition clears
     the mark) and publishes one `breakpoints_changed` on every plane. The machine
     keeps running, whether the breakpoint is a stopping one or a tap.
   - **Validation at creation catches the obvious mistakes:**
     - the parse, with the error column;
     - every operand checked against the current machine (an unknown register, an absent device or card field, a
       label that does not resolve, a page beyond the machine's RAM: refused, "never silently inert", R02 §3);
     - the stack depth;
     - the cost class (a deferred function refused inline);
     - a dry run on the current state.
   - What is left for run time are only states validation cannot see: a device removed after the breakpoint was set,
     a model change, or a computed address outside the space. A model or device change re-validates the armed set
     and marks the breakpoints that no longer fit.

## 9. Phases

Each phase ships its documentation with the code. A phase is not done until the recipe and the public page describe
what it built.

1. **F1 core.**
   - The tokenizer, the compiler, fast shapes plus the VM, the `ERROR` state, and differential tests.
   - Planes: `condition` / `if` (design.md phase 1). Hit counting after the condition.
   - An A/B on the hit path and the miss path.
   Measured 2026-10-27 (core-benchmarks, 12 interleaved rounds against master, cpu time medians, load 50-70):
   `BM_BreakpointResolve_Hit` +1.9 %, `_Miss` +1.0 %, `BM_BreakpointFrame_*` within -1.0..+2.2 % (the A/A bias is
   1.8 %). A first build had the hit path at +15.1 %: the condition check inlined into the candidate walk grew it.
   `ConditionHolds` is now out of line (`UNREAL_NOINLINE`, behind an `[[unlikely]]` test of the program pointer), and
   the 120-byte error text moved to the end of the descriptor.
2. **Conditions on taps:** the profile key `if:`, and `@prev` with the `before` byte for `decreased` / `increased`.
   Built 2026-10-26: `if:` on every source (a breakpoint probe's tap condition, else a frame-end condition), checked
   at profile parse and at arm, the reason in `game.events` `condition_error`; `x@prev` in the language (any operand,
   16 slots). An `each_write` probe with `if:` keeps the byte of the last write the condition held for as `before`.
3. **Deferred callbacks** on Lua and Python: the per-script ring, the stats, and the policies without `unpaced`;
   plus `on_game_event`.
   Built 2026-10-27: `ScriptCallbackManager` per emulator (owned by the debug manager): a tap callback owns a hidden
   tap, a breakpoint callback hears the stop, a game event callback the frame's fired events; an SPSC ring per
   callback (16..4096, a full one drops and counts), a register snapshot per hit, a wake on the empty-to-not-empty
   step; the script's thread drains and reports each call's time; `suspend` / `disable` / `ignore` with the §5 rule.
   Lua and Python `on_tap`, `on_breakpoint`, `on_game_event`, `callbacks`, `callback_resume` / `_remove` /
   `_policy`, `callbacks_run`. Public page: [script-callbacks.md](../../features/script-callbacks.md).
4. **The Lua hook state:** frame and sync modes, the instruction budget, and `unpaced` pacing.
   Built 2026-10-27: `IScriptHookHost` (core) with `LuaHookHost` installed by Lua automation: one sandboxed Lua
   state per emulator (base, string, math, table, utf8; no `pcall` / `load` / metatables), a chunk compiled at
   register, a count hook per 1000 instructions for the budget (an overrun aborts and applies the policy at once),
   `peek` / `poke` / `reg` / `set_reg` / `frame` / `tstate`, a shared value store. Sync hooks run inside the access,
   frame hooks at the frame end (not on TTD replay); writes refused while TTD is not idle. `unpaced` holds the host
   audio (`HostHoldReason::Unpaced`) after 3 long frames, releases after 50 calm ones; `hooks` status on every plane.
5. **Logic callbacks** with TTD journaling; the GS read-value hooks.
   Built 2026-10-27 (logic callbacks): `CallbackRole` `view` / `logic` per frame or sync callback (`role =`
   on Lua and Python, `role` on every list). A logic call's writes are kept (`HookEdits`) and made at the next
   instruction boundary (step-work bit `kStepWorkScriptEdits`), so live and replay agree. The engine records them as
   a `DebuggerEdit` with a lean payload (kind 9: bytes through the CPU's view; the CPU when registers were set), which
   the replay applies without running the hook. TTD v1 refuses them. The hook state has no host randomness
   (`math.random` from a fixed seed; `math.randomseed` needs a seed). A failed or aborted call writes nothing.
   Built 2026-10-27 (the GS read-value hooks, after gs-debugger design §4 gave the card CPU its debug loop and
   taps): `on_tap` with `cpu = "gs"` on the card's memory, ports and card events, the card's registers in the hit,
   `peek` / `reg` of the card in a hook. A sync hook on a card read or port in may return the byte the card CPU sees
   (`BreakpointManager::HandleReplaceableRead` / `HandleReplaceablePortIn`); while TTD records or is browsed the byte
   is kept and the call counts an error. Since 2026-10-29 (Q3) a card hook writes the card (`poke` / `set_reg` at
   the card's next boundary) and a `logic` one is recorded by TTD (gs-debugger design §8A.7). Public page:
   [script-callbacks.md](../../features/script-callbacks.md#the-sound-cards-cpu).

### 9.1 Documentation per phase

| Phase | Public documentation | Recipes |
|:--|:--|:--|
| 1 | new `docs/features/conditional-breakpoints.md`: the language (operands, precedence, literals, dialects), examples, errors, cost; `docs/emulator/design/debugger/breakpoints-and-traces.md` (the engine, the hit order); `command-interface.md`, then the plane docs (CLI `bp … if`, REST, WebSocket, MCP `condition`, Lua, Python) | new `.recipe/analysis/conditional-breakpoints.md`: set a condition on every plane, read its state and errors, typical conditions (a register value, a page, a written value, a score threshold); update `breakpoints-and-events.md` and `waiting-and-watching.md` (run until an expression instead of polling) |
| 2 | `haptics.md` / `mapping-and-profiles.md` §7: `if:` on profile events, `@prev` | `.recipe/input/game-haptics.md`: an event with a condition |
| 3 | new `docs/features/script-callbacks.md`: modes, budgets, policies, stats; `lua-interface.md` and the Python docs (drop the `set_breakpoint_callback` that does not exist; document `on_tap`, `on_breakpoint`, `on_game_event`, `callback …`) | new `.recipe/analysis/script-callbacks.md`: a deferred Lua and Python callback, reading its stats, resuming a suspended one |
| 4 | `script-callbacks.md`: the hook state, sync and frame modes, the instruction budget, `unpaced` | the recipe: a sync Lua hook with a budget; what happens to a runaway |
| 5 | `script-callbacks.md` and the TTD docs: view vs logic, journaling | the recipe: a logic callback under TTD recording |
