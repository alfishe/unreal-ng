# Breakpoints, profilers and traces on every plane

The debugger's breakpoints (`BreakpointManager`), its profilers and traces, and the machine-state edits a debugger
makes, as every automation plane reaches them. The verb table with the status per plane is
[command-interface.md §4.3a](../control-interfaces/command-interface.md#43a-debug-tools-on-every-plane-status);
this page is the design behind it.

## 1. Selections: `BreakpointSelection`

`core/src/debugger/breakpoints/breakpointselection.h` names a set of the user's breakpoints in one text form that
every surface takes:

| Form | Selects |
| :--- | :--- |
| `all` | every user breakpoint |
| `5`, `0x05`, `#05` | one breakpoint by id |
| `exec`, `read`, `write`, `mem` | memory breakpoints by access (`mem`: any) |
| `port`, `in`, `out` | port breakpoints (`port`: either direction) |
| `nextreg`, `nextreg_read`, `nextreg_write`, `beam`, `copper`, `magic` | the ZX Next kinds |
| `keyboard` | keyboard breakpoints |
| `group:<name>` | a group (the name keeps its case) |

Only the user's breakpoints are selected (`BreakpointManager::IsUserBreakpoint`): the BASIC typer's control points,
the debugger adapters' and the step traps stay with their owners, as `ClearUserBreakpoints` leaves them. `SetActive`,
`Toggle` (all on when one is off, else all off) and `Remove` run in one `BreakpointManager::Batch`: one repaint of
the hot path and one `breakpoints_changed` for the whole selection. `IsActive` is the state a toggle key shows (null
when nothing is selected).

| Plane | Switch / toggle / remove a selection | Groups |
| :--- | :--- | :--- |
| CLI | `bpon`, `bpoff`, `bpclear`, `bptoggle <select>` | `bpgroup on\|off\|toggle\|list\|set\|remove` |
| Lua | `bp_select(select, op)` | `bp_groups()`, `bp_group(id, name)` |
| Python | `emu.bp_select(select, op)` | `emu.bp_groups()`, `emu.bp_group(id, name)` |
| MCP | `control_execution` `bp_enable` / `bp_disable` / `bp_toggle` / `bp_remove` with `select` | `bp_groups`, `bp_group_set`, `bp_note` |
| REST | `PUT /breakpoints/active {select, active}`, `GET /breakpoints/active?select=`, `POST /breakpoints/remove {select}` | `GET /breakpoints/groups`, `PUT /breakpoints/{bp_id}/group`, `PUT /breakpoints/{bp_id}/note` |
| WebSocket | the REST routes as `call`s; every change is pushed as `breakpoints_changed` on `debug.events` | the same |
| Catalogue | `dbg.bp_enable`, `dbg.bp_disable`, `dbg.bp_toggle` (readout `toggle.bp.<select>`), `dbg.bp_remove` | `dbg.bp_group_toggle` (readout `toggle.bp_group.<group>`) |

## 2. Keyboard breakpoints

`BRK_KEYBOARD` existed as a type but could not be added (no case in `AddBreakpointAdopting`). It now stops on the
**first keyboard read that sees the key change**: `Keyboard::HandlePortIn` (every model's matrix read, `IN #xxFE`)
hands the rows it selects and the matrix it returns to `BreakpointManager::HandleKeyboardRead`. A keyboard breakpoint
remembers, per half-row, what the program last saw of its key (`keySeenRows`), so a scanning loop that reads the key
thousands of times stops once per press (or release), at the instruction that read it: the program's key handling,
not the host's key event. The key is a key of the matrix (`space`, `enter`, `caps`, `symbol`, `a`..`z`, `0`..`9`) or
`any` (any key of the rows the read selects); the edge is `press`, `release` or `both`. A gate (`hasKeyboard`) keeps
the cost at one load and one test per keyboard read while no keyboard breakpoint is set; replays and tool reads never
hit (`ttdReplayActive`, `toolAccessActive`). The stop is reported with `BreakpointHitKind::Keyboard` ("keyboard").

| Plane | Add |
| :--- | :--- |
| CLI | `bkey <key\|any> [press\|release\|both] [--hits] [--group] [note]` |
| Lua / Python | `bp_key("space", {on = "release"})` / `emu.bp_key("space", on="release")` |
| MCP | `control_execution` `bp_add` `type: keyboard`, `key`, `on` |
| REST / WebSocket | `POST /breakpoints {"type": "keyboard", "key": "space", "on": "press"}` |
| Catalogue | `dbg.bp_add` `kind: keyboard`, `key`, `on` |
| Qt | the breakpoint editor's Keyboard kind |

## 3. Profilers and traces: `tracecontrol`

`core/src/debugger/tracecontrol/tracecontrol.h` drives every profiler, trace and capture with one verb set: `start`,
`stop`, `toggle`, `pause`, `resume`, `pause_toggle`, `clear`, `status`, on `opcode` (the opcode profiler), `memory`
(memory tracking counters), `calltrace`, `coverage`, `porttrace`, `gsporttrace` (General Sound), `pchist`, `aylog`,
`audio` (the audio capture: `seconds`, `source`) and `all` (memory + call trace + opcode, as `POST /profiler/start`).
A start switches on the features it needs, as the REST start routes do. `State` reads the session flags and atomics
only, so a key may show it at the readout rate.

| Plane | Calls |
| :--- | :--- |
| Catalogue | `prof.toggle` (readout `toggle.profiler.<name>`), `prof.pause_toggle` (`toggle.profiler_paused.<name>`), `prof.clear`; `analyzer.toggle` (`toggle.analyzer.<name>`), `analyzer.clear` |
| Lua / Python | `trace(name, op, opts)` / `emu.trace(name, op, **opts)`; Lua `analyzer_list/enable/disable/active` |
| MCP | `analyze_performance` `profiler` {profiler, op}, `gs_trace`, `aylog`, `pchist`, `analyzers` {op, name} |
| CLI | `profiler`, `coverage`, `porttrace`, `gsporttrace`, `pchist`, `aylog`, `audiocapture`, `analyzer` |
| REST / WebSocket | the per-profiler routes (`/profiler/opcode/*`, `/profiler/*`, `/coverage/*`, `/control/audio/gs/porttrace`, `/debug/pchist`, `/ay/log`, `/audio/capture`, `/analyzer/{name}`) |

## 4. Machine state and symbols

The catalogue adds `dbg.register_set` (`Emulator::SetRegisterFromTool`), `mem.write` (bytes, or a fill),
`port.out` (`PortWrite::Write`, with the port presets `fe`, `7ffd`, `1ffd`, `fffd`, `bffd`) and the symbol verbs
`sym.import`, `sym.export`, `sym.import_source`, `sym.import_live`, `sym.drop`, `sym.set_toggle` (readout
`toggle.sym_set.<id>`, `LabelManager::IsSymbolSetEnabled`). MCP gains `control_execution` `set_register`. The readout
`debug.breakpoints` shows `{total, active, inactive, owned, by_kind, groups, last_hit}` (the monitoring value, `GET /api/v1/monitor/debug.breakpoints`).

## 5. Port presets

| Preset | Port | Mask | What |
| :--- | :--- | :--- | :--- |
| `fe` | `#00FE` | `#00FF` | the ULA: border, beeper, keyboard |
| `7ffd` | `#7FFD` | `#8002` | 128K paging (A15 = 0, A1 = 0) |
| `1ffd` | `#1FFD` | `#F002` | +3 paging |
| `fffd` | `#FFFD` | `#C002` | AY register select |
| `bffd` | `#BFFD` | `#C002` | AY data |
