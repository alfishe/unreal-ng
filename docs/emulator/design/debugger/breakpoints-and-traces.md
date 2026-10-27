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

## 6. The debug path follows the breakpoints

`FeatureManager::debugModeInEffect` is true while the `debugmode` feature is on, a replay or taps hold it, or
(`setBreakpointDebugHold`) an active stopping breakpoint exists and the `breakpoints` feature is on.
`BreakpointManager::ApplyTapDemand` pushes that last hold at each repaint (`BreakpointTapDemand::stop`), and Memory's
breakpoint gate follows it. So a breakpoint is never accepted and inert because `debugmode` went off (the hidden Qt
debugger, a client); without breakpoints `debugmode` alone decides and the fast path stays.

## 7. Conditions

A memory or port breakpoint can carry a condition (`core/src/debugger/breakpoints/breakpointcondition.h`); the
language is on [docs/features/conditional-breakpoints.md](../../../features/conditional-breakpoints.md), the design
and measurements on [conditions-and-callbacks.md](../../../inprogress/2026-10-26-breakpoint-taps/conditions-and-callbacks.md).

- **Compiled once.** `BreakpointConditionProgram::Compile` tokenizes, parses (the Unreal order, or the `c` dialect)
  and emits a stack-machine program, checks every access operand against the breakpoint's trigger
  (`BRK_COND_EXEC`/`READ`/`WRITE`/`PORT_IN`/`PORT_OUT`), resolves labels, and with a machine runs it once on the
  current state. A refusal carries the reason and the column; nothing is added or changed. Common shapes (a register,
  a variable or a byte against a constant) run without the stack machine and fall back to it when their context is
  missing.
- **Only after a match.** The filters and candidate sets of the hot path are unchanged: the condition runs in
  `WalkCandidates` for the breakpoint that matched, with the access the hot path saw (`ResolveMemory` /
  `ResolvePort` pass the value: the byte read or written, the OUT's byte; an IN has none). Bytes are read with
  `DirectReadFromZ80Memory`, without side effects.
- **The hit order.** `++matchCount`, the condition, then `++hitCount` and the hit policy on `hitCount`. A breakpoint
  without a condition counts both. A tap with a condition tells its sink only on condition-true hits.
- **Never shared.** A conditional breakpoint is never plain (`IsPlainMemory` / `IsPlainPort`): it is not in the key
  maps and no request adopts it; setting a condition later moves a plain one out of them.
- **The error mark.** An evaluation error sets `conditionFailed`, writes `conditionError` (the reason, the frame, the
  PC), bumps the revision and posts one `NC_BREAKPOINTS_CHANGED` with the id; the breakpoint is skipped until
  `ActivateBreakpoint` or `SetBreakpointCondition` clears the mark. The machine never stops on it.

| Plane | Set | Change, clear | Read |
| :--- | :--- | :--- | :--- |
| CLI | `bp`, `wp`, `bport ... [--dialect c] if <cond>` | `bpcond <id> [cond]` | `bplist` |
| Lua | `condition=`, `dialect=` in the options of `bp`, `bp_read`, `bp_write`, `bp_port_in/out` | `bp_condition(id, text[, dialect])` | `bp_status()` |
| Python | `condition=`, `dialect=` | `emu.bp_condition(id, text, dialect)` | `emu.bp_status()` |
| MCP | `control_execution` `bp_add` `condition`, `condition_dialect` | `bp_condition` | `bp_list` |
| REST | `POST /breakpoints {condition, condition_dialect}` | `PUT /breakpoints/{bp_id}/condition {condition, dialect}` | `condition`, `match_count`, `condition_error` |
| WebSocket | the REST routes as `call`s | the same | `breakpoints_changed` |
| Catalogue | `dbg.bp_add` (condition, dialect) | - | - |

## 8. The sound card CPU

The General Sound / NeoGS card's Z80 has its own breakpoints (`GSCardTarget`, `core/src/debugger/targets/`, design
on [gs-debugger §4](../../../inprogress/2026-09-27-gs-debugger/design.md)). `DebugManager::GetBreakpointsManager(cpu)`
picks the manager: `main` (also `z80` or nothing) the machine's, `gs` (or `neogs`) the card's, `ms` (or `multisound`)
the General Sound on a MultiSound slot card (a second `GSCardTarget`, the card found through
`ICard::DebugGeneralSound`; `SlotManager` tells it when the cards are built or released). Everything below holds for
both card CPUs; `--cpu ms`, `cpu="ms"`, `?cpu=ms` name the MultiSound's.

- **Kinds.** Execute, memory read / write (addresses of the card's CPU, `--page romN` / `ramN` its flash / ROM and RAM
  pages), port in / out, and the protocol events (`BRK_EVENT`). The card refuses the rest (NextREG, beam, keyboard).
- **Conditions.** A memory or port breakpoint of the card takes a condition (§7) on the card's registers, memory and
  pages: `IDebugTarget::Registers` (the card's register file in the `Z80Registers` layout, copied per evaluation),
  `Peek` and `MapAddress` behind `BreakpointConditionContext::target`. The compiler gets `BRK_COND_TARGET` and refuses
  the machine's `T`, `FD` and `DOS`; labels are not passed. See
  [conditional-breakpoints.md](../../../features/conditional-breakpoints.md#the-sound-cards-cpu).
- **Events.** The card's: `int`, `nmi`, `reset`, `page` (a page switch), `card_command`, `card_data` (the card read the
  host's command / data byte), `card_reply` (it wrote its reply), and the card's hardware: `dac_fetch` (the player
  fetched a sample; the detail is the DAC channel 0..3), `clock_change` (NeoGS GSCFG0 changed the clock; the detail is
  the clock in MHz, the value GSCFG0), `dma_start` / `dma_done` (a NeoGS DMA module, the value its number; the detail of
  `dma_done` is 1 for a finished block, 0 for an abort), `gscfg0` (every GSCFG0 write of the NeoGS CPU, the value the
  byte; a reset's GSCFG0 is the `reset` event). The host's: `host_command` (#BB), `host_data` (#B3 written),
  `host_reply` (#B3 read), on the main CPU's manager; each kind on the other CPU is refused. `value` / `vmask` filter
  the byte.
- **DMA writes.** A NeoGS DMA module's write to the card's RAM (ZX-DMA, a byte the host wrote; SD, a 512-byte block)
  goes through the write watchpoints tied to that RAM page (`wp 0x2005 --page ram4 --cpu gs`), not through those on a
  CPU address: the card CPU is off the bus. The stop's access is `dma_write`, its address the offset in the page
  (`GSCardTarget::Stop` also keeps the module and the page). A flash write is the CPU's own: a write watchpoint on a
  `romN` page sees it.
- **Where it stops.** An execute breakpoint before the instruction; a watchpoint, a port or an event at the card's next
  instruction boundary, with the PC of the instruction that did it. The machine parks (pause) inside the card's
  catch-up; a direct step of the main CPU ends after its instruction.
- **Cost.** The hooks are on the card only while an active card breakpoint exists; without them the card runs its
  fast loop unchanged.
- **Stops.** The pause, the breakpoint payload, a step's stop and `wait` carry `cpu` (`main` or `gs`) and, for a card
  stop, `cpu_pc` (the card instruction) beside the main PC.

| Plane | The CPU | Event breakpoint |
| :--- | :--- | :--- |
| CLI | `--cpu gs` on `bp`, `wp`, `bport`, `bplist`, `bpclear`, `bpon`, `bpoff`, `bptoggle`, `bphits`, `bpgroup`, `bpnote` | `bevent <kind> [--value V [--vmask M]] [--hits ...] [--cpu ...]` |
| Lua | `cpu="gs"` in the options; `bp_remove`, `bp_enable`, `bp_disable`, `bp_clear`, `bp_count`, `bp_list` take it last | `bp_event(kind, {value=, vmask=, hits=, cpu=})` |
| Python | `cpu="gs"` keyword on `bp`, `bp_read`, `bp_write`, `bp_port_*`, `bp_remove`, `bp_enable`, `bp_disable`, `bp_clear`, `bp_count`, `bp_list` | `emu.bp_event(kind, value=, vmask=, hits=, cpu=)` |
| MCP | `control_execution` `cpu` on every `bp_*` action | `bp_add` `type` `event`, `event` |
| REST | `?cpu=gs` or the body's `cpu` on `/breakpoints...` (400 for another name) | `POST /breakpoints {type: "event", event, value, value_mask}` |
| WebSocket | the REST routes as `call`s; `cpu` / `cpu_pc` in `breakpoint` events | the same |

The event kind's default CPU is its side (`bevent card_reply` and `bp_event("int")` go to the card). The groups'
listing takes the CPU too (`GET /breakpoints/groups?cpu=gs`, `bp_groups("gs")`, `emu.bp_groups(cpu="gs")`, MCP
`bp_groups` with `cpu`) and names it in the answer (`cpu`).

The card's events have named callbacks too (`gs_on_command`, `gs_on_data`, `gs_on_reply`, `gs_on_int`, `gs_on_nmi`,
`gs_on_page`, `gs_on_dac_fetch`, `gs_on_clock_change`, `gs_on_dma_start`, `gs_on_dma_done`, `gs_on_gscfg0`, `gs_on_port_in(port)`,
`gs_on_port_out(port)` in Lua and Python): taps on the card CPU, the hit's `arg` the event's detail
([script-callbacks.md](../../../features/script-callbacks.md#the-sound-cards-cpu)).

## 9. Sound chip registers

A chip register breakpoint (`BRK_CHIPREG`, `core/src/debugger/breakpoints/chipregister.h`) stops the main CPU after
its OUT wrote a sound chip's register: the chip, one unit of it or any, a register or a range, and a value filter
(`value` / `vmask`, as for NextREG and events). It is the semantic form of a port breakpoint: `ay 7` is "the mixer",
whatever port pair, latch order or board the program reaches it through.

| Chip | Registers | Where it is reported |
| :--- | :--- | :--- |
| `ay` | 0-15 | AY / YM2149: the 128K's and TurboSound's (units 0-1), the Next's three (0-2), the Fuller box; the SSG of a YM2203 (TS-FM, MultiSound) |
| `fm` | #10-#FF | the FM part of a YM2203 (TS-FM, MultiSound; units 0-1) |
| `saa` | 0-#1F | SAA1099 (MultiSound) |
| `opl4` | #000-#1FF | MoonSound OPL4 FM: bank 1 as #100-#1FF |
| `opl4wave` | 0-#FF | MoonSound OPL4 wave part (while NEW2 is set) |
| `dac` | 0-3 (the channel) | Covox / SounDrive, the MultiSound's SounDrive; the value is the sample |

- **Cost.** A chip reports only while `EmulatorContext::chipRegisterWatch` is set: the main CPU's breakpoint manager
  sets it while an active chip register breakpoint exists. Otherwise one predictable branch per OUT to a chip.
- **What is not a program's write.** A snapshot load, a reset or a tool's access (`toolAccessActive`) writes the
  registers without the port path and is not reported; nor is a write during a time travel replay.
- **The stop.** The access is `chip_register`, the address the register (`BreakpointHitKind::ChipRegister`).
- **Not yet.** The Sprinter's Covox Blaster ring (P7) and taps (script callbacks) on chip registers.

| Plane | Chip register breakpoint |
| :--- | :--- |
| CLI | `bchip <chip> <reg>[-<end>] [--unit N] [--value V [--vmask M]] [--hits ...] [note]` |
| Lua | `bp_chipreg(chip, reg, {to=, unit=, value=, vmask=, hits=})` |
| Python | `emu.bp_chipreg(chip, reg, end=, unit=, value=, vmask=, hits=)` |
| MCP | `control_execution` `bp_add` `type` `chipreg`, `chip`, `address`, `address_end`, `unit`, `value`, `value_mask` |
| REST | `POST /breakpoints {type: "chipreg", chip, address, address_end, unit, value, value_mask}`; the listing has `chip`, `unit` |
| WebSocket | the REST route as a `call` |
