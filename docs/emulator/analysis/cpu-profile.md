# CPU time profile

Where the CPU's T-states go, per executed instruction and per routine, live on the running machine: start the
profile, run the program, read the report. No TTD recording, no L1 build, no code map and no labels are needed.

| | |
|:--|:--|
| **Code** | `core/src/debugger/analyzers/profile/` (`ProfileAnalyzer`, analyzer id `profile`; `profilecontrol.h`: the `cpuprofile` verb every plane calls) |
| **Planes** | [command interface](../design/control-interfaces/command-interface.md#55b-cpu-time-profile) (`cpuprofile`), CLI, Lua, Python, MCP (`analyze_performance` action `cpuprofile`), REST `/api/v1/emulator/{id}/cpuprofile/{op}`, WebSocket (every route a `call`, topic `debug.cpuprofile`) |
| **Design** | [docs/inprogress/2026-11-05-profile-analyzer](../../inprogress/2026-11-05-profile-analyzer/README.md), measurements in [POC 046](../../../tools/poc/046-profile-analyzer/README.md) |
| **Related** | [analysis layers](analysis-layers.md) (L1 has the same per-routine `tstates` / `tstates_inclusive` from a recording) |

## Contents

- [Quick start](#quick-start)
- [What is counted](#what-is-counted)
- [Routines without a map](#routines-without-a-map)
- [Attribution notes](#attribution-notes)
- [Marks and windows](#marks-and-windows)
- [The per-frame series](#the-per-frame-series)
- [Operations](#operations)
- [Exports](#exports)
- [Cost](#cost)
- [Threads and other subsystems](#threads-and-other-subsystems)

## Quick start

```text
cpuprofile start                  # cells + call stack + per-frame series
run frames 500
cpuprofile report 10              # the top 10 routines by self time
cpuprofile mark level2            # a baseline
run frames 500
cpuprofile report since=level2    # the time after the mark only
cpuprofile export kit work/p1/profile-l1.json   # for kit/fig_budget.py
cpuprofile stop
```

```text
CPU profile since the start: 500 frame(s), 34944000 T, 118 routine(s) found
  self    incl    calls  routine (entry, page, kinds)
   22.5%   24.8%    500  sub_B30C ($B30C, RAM 2, call)
   ...
```

## What is counted

One CPU step subscriber (`AnalyzerManager::subscribeCPUStep`) books, at every instruction start, the T-states since the
previous start to the previous instruction:

| Counter | Where |
|:--|:--|
| executions and T-states per executed byte | cells keyed by physical page and offset (`Memory::ExecPhysOffset`), a 16 KiB page of cells (256 KiB) allocated at its first execution |
| self / inclusive T-states, entries by kind, callers, notes | per routine (the call stack, `stack=true`) |
| self T-states per 1/64 of the frame | per routine (`beam=true`) |
| every frame's self / inclusive per active routine | the series ring (`frames=true`) and an optional JSONL stream |
| frames, T-states, instructions, acknowledges, discontinuities | the totals |

- **Interrupt acknowledge** (INT / NMI): booked to the handler's first instruction, so to the interrupt's routine,
  and summed in `acknowledge_tstates`. L1 books it to the interrupted instruction; per routine the difference is the
  acknowledge (13-19 T a frame).
- **HALT**: a halted CPU's idle fetches add their T-states to the HALT, not its execution count. The profile lets the
  CPU run a halted stretch in one go (the subscriber is idle-tolerant), so a HALT-paced program costs about nothing
  more.
- **Frame wrap**: the delta crosses the frame end (`t` restarts); the frame counter must advance by one.
- **Discontinuities** (a snapshot load, a reset, a TTD seek or replay pass, a frame counter jump): not booked,
  counted; the call stack is cleared and rebuilt from the next calls.
- **CPU clock changes**: T-states are CPU T-states of the clock at the time.
- **The window's end**: while running, the last instruction's T-states and a frame that just ended are booked at the
  next instruction start; `stop` books them (the CPU is held at an instruction start), so N frames run and stopped
  are N frames and N frame lengths, as L1 counts a replay window.

## Routines without a map

A routine is an executed CALL target, RST target or interrupt entry (IM 1 `$0038`, the IM 2 vector's target, NMI
`$0066`), or the target of a RET no frame pushed (a dispatch). It is keyed by its physical byte, so banked code at one
Z80 address is several routines. Names: a loaded label at the entry (with a bank, only on that page), else
`sub_XXXX`; when one 16K window ran several pages, `sub_XXXX_rN` (RAM page N) or `sub_XXXX_romN`. Code outside any
routine found (before the first call, the main loop) is the root, `(outside any call)`.

The shadow stack follows the stack pointer, not the instructions. The CPU tells it of every transfer that moves the
stack (`Z80::FlowHook`, set only while the stack runs): a CALL / RST pushes a frame `{routine, slot = SP, return
address}`, a taken RET / RETI / RETN pops by the slot it read. Only those instructions test the hook; every other
instruction pays nothing for the stack. An interrupt pushes a frame at the slot of the address it pushed (the step
sees `Z80::acknowledges` move). The Next's Z80N and the Sprinter's Z84C15 run in libraries without the hook:
`Z80::EngineStep` decodes their instruction after it ran (opcode word, SP moved) while the hook is set.

| Case | Rule |
|:--|:--|
| normal return | the top frame's slot is the popped slot: pop it (frames of the same slot and return address pop with it) |
| return address changed in the slot | popped, counted as a patched return |
| frames below the popped slot | left without a return (SP moved past them, the address was discarded): popped, counted abandoned |
| a push on the slot of a frame (or below it) | the same: that frame was left |
| RET with no frame at its slot (PUSH + RET dispatch) | a dispatch frame for the target with the parent's slot and return address: the target has its own time, the final return pops both; the routine that dispatched counts it |
| a dispatched routine dispatches again (a RET chain, a RET-driven main loop, a coroutine switching stacks) | the new target replaces it (it returned into the next one): a chain is one frame deep over the routine that started it, on whatever stack its RETs run |
| JP to a known entry, tail calls, jump tables | the time stays with the routine that jumped; the entry counts a jump-in |

Self time is the time a routine was on top of the stack; inclusive is the time it was on the stack (once for
recursion and nested interrupts of the same handler). The stack keeps 1024 frames (deeper: the oldest dropped,
counted); 65536 routines (more: booked to the caller, counted).

## Attribution notes

Each report row carries what the rules above did for that routine, so a share that is partly elsewhere is said:

| Field | Meaning |
|:--|:--|
| `jump_ins` | arrivals at the entry not by a call, return or interrupt: its time then stayed with the jumper |
| `dispatches_in` / `dispatches_out` | entered by a RET dispatch / dispatched out by PUSH + RET |
| `abandoned` | frames of the routine left without a return |
| `patched_returns` | returns to an address other than the one pushed |
| `notes` | the same as text, only when one is non-zero |

## Marks and windows

The counters are cumulative from `start` (or `reset`). `mark <name>` copies them as a baseline (at most 8; a mark of
the same name replaces it); `since=<name>` on `report` and `export` reads the counters minus the baseline. A mark
copies the cells and the routine counters (tens of microseconds for a game's pages), the emulation thread is not
held.

`stop` keeps the counters readable; the next `start` goes on with the same window unless `reset=true` or the
`stack` setting changes. `reset` starts a fresh window and drops the marks.

## The per-frame series

With `frames=true` (the default) every frame end appends a record: the frame number, the T-states booked and, per
routine whose counters moved, `[id, self, inclusive]` of that frame. The ring (`ring_kib`, default 4096 KiB) keeps the
last frames (16383 at most); `frames [last] [top=]` reads them. `stream=<file>` also writes them as JSON lines
(`{"frame":<frame>,"t":<T>,"r":[[id,self,incl],...]}`) from a writer thread that polls the ring every 100 ms; frames the
writer could not keep up with count in `frames_lost`. Each routine is introduced by a line
`{"routine":<id>,"entry":"$XXXX","page":<page>}` before the first frame record that names it, so the file reads alone.

## Operations

| Operation | Options | Answer |
|:--|:--|:--|
| `start` | `stack` (true), `beam` (false), `frames` (true), `ring_kib` (4096, 64..262144), `stream` (file), `reset` | the status; the CPU is held for the switch |
| `stop` | | the status |
| `reset` | | the status (a fresh window, marks dropped) |
| `mark` | `name`, `drop` | the status; 404 dropping an unknown mark, 409 past 8 |
| `status` | | `active, settings, start_frame, frames, tstates, instructions, acknowledges, acknowledge_tstates, discontinuities, stack_overflows, routine_overflows, frame_tstates, pages, routines, depth, frames_kept, frames_streamed, frames_lost, marks` |
| `report` | `since` (a mark), `top` (24), `sort` (self, inclusive, calls), `callers` (true), `beam` | the status plus `window {frames, tstates, instructions, acknowledges, acknowledge_tstates}` and `routines [{id, name, entry, page, page_name, kinds, self, inclusive, self_permille, inclusive_permille, calls, rsts, interrupts, dispatches_in, jump_ins, dispatches_out, abandoned, patched_returns, max_depth, on_stack, notes, callers [{id, name, entries}], callers_more, beam}]` |
| `frames` | `last` (50), `top` (8) | `frames [{frame, tstates, active, routines [[id, self, inclusive]]}]`, `names {id: name}`; 409 with the series off |
| `export` | `format` (kit, json, entries, sym, ann), `path`, `since` | `format, routines, instructions, bytes` and `path` or `content` |

Every answer has `text`, the lines the CLI prints. Refusals: 400 a bad option or value, 404 an unknown mark, 409 not
possible now, 503 no debug manager.

## Exports

| Format | Content |
|:--|:--|
| `kit` | the subset of L1's `l1.json` the case kit's figure scripts read (`kit/fig_budget.py`): `replay {first_frame, last_frame, frames, instructions, tstates, interrupts, acknowledge_tstates, frame_tstates}`, `pages`, `routines [{entry, page, name, kinds, profile {tstates, tstates_inclusive, tstates_permille, entries, ...}}]`, `instructions [["$ADDR", page, code, class, count, tstates, smc]]` (code: the instruction's bytes now; class `halt` or empty; smc false) |
| `json` | the same document with `format: "unreal-ng cpu profile"` |
| `entries` | the routine entries found: `{entry, page, page_name, name, kinds, calls, rsts, interrupts, dispatches_in, jump_ins, callers [{entry, page, entries}]}` |
| `sym` | `name = $ADDR ; page, kinds, entries` lines |
| `ann` | an `.ann` seed: `$ADDR c name ; page, kinds, entries` lines |

## Cost

Off: no subscriber, nothing runs; the CALL / RST / RET handlers test a null flow hook (the A/B against master is
within the noise). On (POC 046, 48K, turbo, the core analyzer): the cells with T-states +17-19 % (8.6 ns an
instruction on Zynaps); the call stack adds nothing measurable (it moves only in the CALL / RST / RET handlers, from
the CPU's flow events); the per-frame series +0-5 %, beam bins +2-5 %; a HALT-paced program +1-5 % (the idle
stretch runs in one go). A frame record costs ~1 µs, a mark 10-20 µs. Memory: 256 KiB per executed page, 1 KiB per
routine, the ring.

## Threads and other subsystems

- The step runs on the emulation thread and is the only writer; every counter is a relaxed atomic written by load +
  store, so a reader never parks the emulation thread and never sees a torn value (a report may be a few instructions
  skewed). `status`, `report`, `frames`, `export` and `mark` take no lock against it; `start`, `stop` and `reset` hold
  the CPU (`Emulator::CpuHold`) for the switch.
- The profile is no machine state: not in snapshots, TTD checkpoints or recordings. TTD replay passes are skipped.
- Each instance has its own profile.
