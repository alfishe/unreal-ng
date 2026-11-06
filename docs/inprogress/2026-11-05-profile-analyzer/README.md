# CPU time profile (ProfileAnalyzer)

| | |
|:--|:--|
| **Status** | Implemented (2026-11-05): design v2 after [POC 046](../../../tools/poc/046-profile-analyzer/README.md), decisions in [§14](#14-decisions-after-poc-046), final numbers in [§15](#15-final-measurements). Module doc: [cpu-profile.md](../../emulator/analysis/cpu-profile.md) |
| **Owner request** | 2026-11-05: "run the game, enable the profile, fetch it after N frames": where the CPU time goes per routine, live, without a TTD recording and an L1 build |
| **Code** | `core/src/debugger/analyzers/profile/` (analyzer id `profile`), planes in `core/automation/*` |
| **Related** | [analysis layers](../../emulator/analysis/analysis-layers.md) (L1 per-routine `tstates` / `tstates_inclusive`), [coverage analyzer](../../../core/src/debugger/analyzers/coverage/coverageanalyzer.h), [performance guidelines](../../guidelines/performance-guidelines.md), [automation planes](../../guidelines/automation-planes-guidelines.md) |

## Contents

- [1. Why](#1-why)
- [2. What it answers](#2-what-it-answers)
- [3. What L1 already gives, and how the two relate](#3-what-l1-already-gives-and-how-the-two-relate)
- [4. The measurement model](#4-the-measurement-model)
- [5. Routines without a map: discovery and the shadow stack](#5-routines-without-a-map-discovery-and-the-shadow-stack)
- [6. Data structures](#6-data-structures)
- [7. Memory budget](#7-memory-budget)
- [8. Counter width and overflow](#8-counter-width-and-overflow)
- [9. Hot-path cost model](#9-hot-path-cost-model)
- [10. Threads](#10-threads)
- [11. Interaction with the rest of the emulator](#11-interaction-with-the-rest-of-the-emulator)
- [12. Output: the profile JSON and the kit export](#12-output-the-profile-json-and-the-kit-export)
- [13. Automation planes](#13-automation-planes)
- [14. Decisions after POC 046](#14-decisions-after-poc-046)
- [15. Final measurements](#15-final-measurements)
- [16. Tests](#16-tests)
- [17. Left for later](#17-left-for-later)

## 1. Why

The reverse-engineering cases (`unrealng-reverse-cases`, e.g. Zynaps `docs/02-architecture.md` §2.7 and the kit's
`fig_budget.py`) show "where the CPU time goes" per routine. Today this takes a TTD recording, a replay and an L1 build
(`analysis start record`, `analysis build`): for Zynaps `r1` a 158 MB session, a 15.6 MB `l1.json`, minutes of
replay. The question is a counter: how many T-states each instruction and each routine took. The profile answers it
live, on the running machine, at a cost close to nothing when off and small when on.

## 2. What it answers

| Question | Where |
|:--|:--|
| T-states and executions per executed instruction (page, address) | flat cells (§6.1) |
| Per routine: self time, inclusive time, entries (calls), callers | routine table (§6.3), shadow stack (§5) |
| Totals: frames, T-states, instructions, interrupts, interrupt acknowledge, HALT | window header (§6.6) |
| Since the profile was enabled (cumulative, never reset by a mark) | the counters themselves |
| Since a mark (named, several) | mark baselines (§6.5): `since = cumulative - baseline` |
| Per frame: each routine's self / inclusive T in each frame (the frames where a routine spikes) | per-frame series (§6.7): ring of the last N frames, optional JSON-lines stream to a file |
| When in the frame a routine runs ("pass vs beam") | beam bins (§6.4) |
| Routine entries found at run time, for labels / L1 / a case's `.ann` | `entries` export (§12.3) |
| Where the attribution may be wrong | anomalies (§5.3): tail jumps, dispatch, SP resyncs, patched returns, nested-interrupt returns |

It works on a fresh program without a code/data map and without labels: counts exist only for executed addresses,
routines are found from the CALL / RST / interrupt targets as they execute, names come from the loaded labels when
there are any, else `sub_XXXX` (`sub_XXXX_pN` for banked code).

## 3. What L1 already gives, and how the two relate

L1 (`core/src/debugger/analyzers/analysis/l1builder.cpp`) replays a recording on a muted clone with an
instruction-start hook and the bus hook, decodes every instruction from its bus cycles and builds the CFG, routines,
blocks, loops, data flow. Its timing facts:

- per instruction (`instructions[]` rows `[z80, page, code, class, count, tstates, smc]`): `tstates` = the sum over
  the executions of (next instruction start T - this instruction start T). An accepted interrupt's acknowledge falls
  into the interrupted instruction; a HALT's repeated idle fetches into the HALT (the repeats do not count as
  executions);
- per routine `profile.tstates` (self) = the sum over the instructions the routine *owns* in the CFG (blocks reached
  from its entry without crossing another entry), `tstates_inclusive` = the sum over its frames of (exit T - entry T),
  from a shadow stack of call / RST / interrupt / dispatch frames; `abandoned` and `patched_returns` count frames that
  ended without a matching return;
- `replay` totals: frames, instructions, T-states, interrupts.

The profile measures the same quantity with the same definition (T between instruction starts), live, without the
bus hook, the decoder of bus events, the CFG or the recording:

| | L1 | profile |
|:--|:--|:--|
| Needs | a TTD recording + a replay (minutes, GB on long runs) | nothing: a callback on the running machine |
| Per-instruction T and count | yes (exact) | yes, the same definition (§4); the acknowledge is booked apart (§4.2) |
| Routine self | CFG ownership | stack top (the routine whose frame is running), and by address (nearest entry at or before) at export |
| Routine inclusive | stack frames | stack frames (the same rules, §5) |
| Per frame | no (sums over the window) | yes (§6.7) |
| Windows | one build per window | marks, live |
| Data flow, loops, blocks, accesses | yes | no |

The two complement each other: the profile is the quick answer and the per-frame view; L1 is the deep one. The profile
exports L1's `instructions[]` / `replay` / `pages` shape (§12.2), so a kit figure made from an L1 can be made from a
profile with no change of the script; and its discovered entries can seed labels for L1 or a case's `.ann`.

## 4. The measurement model

### 4.1 The hook point and the delta

The analyzer subscribes to the CPU step event of the `AnalyzerManager` (`subscribeCPUStep`, the same raw-pointer hot
path as the coverage analyzer). It fires in `Z80::RunInstructionStartHooks`, at the start of every instruction, before
its M1, after the interrupt decision of the boundary. At each call the analyzer adds `t - lastT` to the cell of the
*previous* instruction, then counts one execution of the current one and remembers it:

```text
step(pc):  d = cpu.t - lastT            // the previous instruction's T (with its contention, its waits)
           cell(lastPc).t += d
           cell(pc).count += 1
           lastPc = pc; lastT = cpu.t
```

This is exactly L1's per-instruction definition. Contention, wait states, the Scorpion's Even M1, turbo waits are
part of the delta: they are time the instruction took on this machine. The cell is keyed by the **physical** byte
(the offset in the emulator's one memory buffer, `Memory::_memory`: page = offset >> 14, the same page numbers L1
uses: RAM 0-255, cache 256-259, ROM from 261), so banked code at the same Z80 address on different pages is
counted apart. The Next's 8K slots are resolved per slot.

### 4.2 Interrupts, NMI, HALT

- **Acknowledge.** An INT or NMI accepted at the boundary runs its acknowledge (IM1 13 T, IM2 19 T, NMI 11 T, plus a
  wait) between two instruction starts. The CPU records the acknowledge's length (`Z80::lastAcceptT`) and a counter
  (`Z80::accepts`); when the counter moved since the last step, the analyzer books the acknowledge apart (window
  total `interrupt_ack`, and per interrupt entry), and the previous instruction gets `d - ack`. The kit export (§12.2)
  folds it back into the interrupted instruction to stay identical to L1.
- **HALT.** While halted, the CPU repeats idle fetches at the HALT. With a step subscriber they are steps (the halted
  idle "in one go" is refused while a subscriber exists, `Z80::IdleStepsInert`), so each repeat is a callback at the
  same PC: its time goes to the HALT, as in L1. The repeats are not counted as executions (one execution per HALT
  entered, as L1). *POC*: whether letting the halted idle run in one go while only the profile listens is worth it
  (the delta needs no per-fetch callback: the time after the stretch lands on the HALT at the next call).

### 4.3 Frame wrap and discontinuities

`Core::AdjustFrameCounters` subtracts the frame length from `cpu.t` at every frame end. The step sees `t < lastT`
(an unsigned `d` far above any instruction): the slow path adds the frame length, closes the frame (per-frame series,
beam bins' frame, pending marks / resets, §10), and goes on. A `d` above the frame length otherwise means the
machine was moved under the profile (a snapshot load, a TTD seek, a reset): the delta is dropped and counted
(`discontinuities`), the stack is cleared (`resyncs`).

## 5. Routines without a map: discovery and the shadow stack

### 5.1 Entries

Every executed CALL target, RST target, interrupt entry (IM1 `#0038`, the IM2 vector's target, NMI `#0066`) and
dispatch target (§5.3) becomes a routine entry, keyed by its physical byte. The first instruction executed after the
profile starts is the root routine (`root`). Names at export: a loaded label at the entry (bank-aware: a label with a
bank names only that page) wins, else `sub_XXXX` (`sub_XXXX_pN` when the page is not the one a 48K maps there,
`rom_XXXX_pN` in ROM).

### 5.2 The shadow stack

The stack moves only at CALL / RST / RET-family instructions and interrupt acceptances, so only those pay for it
(owner, 2026-11-05: "the shadow stack must not cost per instruction"). The CPU raises a **flow event** for them
(`Z80::FlowHook`, null when nobody listens):

| Source | Event | Arguments |
|:--|:--|:--|
| `CALL nn`, `CALL cc,nn` taken (`op_CD`, `op_C4`..`op_FC`) | `Call` | target, slot = SP after the push, return address pushed |
| `RST n` (`op_C7`..`op_FF`) | `Rst` | the same |
| `RET`, `RET cc` taken, `RETN`, `RETI` and their ED aliases (`op_C9`, `op_C0`..`op_F8`, `ope_45`, `ope_4D`) | `Ret` | target, slot = SP before the pop |
| DD / FD before any of them | the same (the prefixed tables reuse the handlers) | |
| an engine CPU (`ICpuEngine`: the Next's Z80N, the Sprinter's Z84C15) | the same, decoded by `Z80::EngineStep` from the engine's opcode word and the SP it moved, only while a hook is set (the libraries have no hook) | |
| INT / NMI acceptance | not an event: `Z80::acknowledges` moved; the next step pushes the interrupt frame (it already tests the counter to book the acknowledge apart) | |

Each handler tests the hook pointer after its stack access (one load and a predicted branch, only in those 26
handlers and 2 ED bodies); no other opcode pays anything. The event comes at the instruction's end: the analyzer
first books the instruction's T-states to the cell and routine it ran in (as the next step would), then pushes or
pops, so the attribution is the same as decoding at the next step. Per instruction only the cells and the jump-in
test on an entry cell remain (the entry test reads the bits the count update already loaded).

Two per-step decoders are kept for the A/B of POC 046 only (`Settings::flowSource`): 1 decodes the previous
instruction from `Z80::opcode` / `prefix` (already fetched, no memory read) and 2 re-reads the instruction's byte
from memory (the first version). Both store the instruction's SP and PC at every step and resolve the transfer at the
next step:

| Previous | Taken when | Action |
|:--|:--|:--|
| CALL / CALL cc / RST | `SP = SP0 - 2` | push a call frame: entry = new PC, return slot = SP, return address = PC0 + length |
| interrupt (accepts moved) | always | push an interrupt frame: entry = new PC, return slot = SP, return address = the word at SP |
| RET / RET cc / RETI / RETN | `SP = SP0 + 2` | pop (below) |

Pop rules (the same as L1's `Return`):

1. Frames whose return slot is below SP0 were left without a return (the program moved SP over them, or discarded
   the return address): popped as **abandoned**, counted on their routine (`resyncs`).
2. The top frame's slot is SP0 and its return address is the new PC: a normal return (several frames with the same
   slot pop together: dispatch frames, §5.3).
3. The top frame's slot is SP0, the address differs: a **patched return** (counted), popped.
4. No frame at SP0 (the top frame's slot is above it): the RET took an address the code pushed itself: a **dispatch**
   (PUSH + RET, RET chains like Zynaps' DrawPlayfield): a dispatch frame for the target with the top frame's slot and
   return address, so the target gets its own time and the final return to the caller pops it with the caller's
   frame. When the top frame is itself a dispatch frame (the second RET of a chain, a RET-driven main loop, a
   coroutine switching stacks: Zynaps' scenery script), the new target **replaces** it: the dispatched routine
   returned into the next one. A chain is one frame deep, whichever stack its RETs run on. (Stacking them instead,
   the first core build, overflowed the 1024 frames 84 000 times in 3000 Zynaps frames: DrawPlayfield's chain ran on
   the span nodes under a frame of the script's stack, which no RET of the main stack ever popped.)

Self time is booked lazily, at the transitions: a push or pop adds `now - topSince` to the routine that was on top
and restarts the stretch (readers add the open stretch of the top routine), so no step adds anything per routine.
Inclusive: at a pop, `now - entryT` is added to the routine, only when the routine is not on the stack below
(recursion and nested interrupts of the same handler are counted once; L1 adds every frame, a difference only for
recursive code). The stack keeps 1024 frames; a deeper push drops the bottom frame (counted, `stack_overflows`). A
push on the slot of a frame (or below it) leaves that frame (abandoned); a dispatch from the root replaces the root
dispatch frame before it.

### 5.3 Failure cases, detected and reported

| Case | What the profile does | Reported as |
|:--|:--|:--|
| JP-entered routine, tail call, jump table (`JP (HL)`) | the time stays with the routine whose frame runs (the caller) | `jump_entries` per routine: arrivals at a known entry not by a CALL / RST / interrupt / dispatch; a routine with them says its self time is partly in its callers' |
| PUSH + RET dispatch, RET chains | dispatch frames (§5.2 rule 4) | `dispatches` per target routine |
| Return from a nested interrupt into an interrupted pass | an interrupt frame per acceptance; a RETI / RET matches its own frame by slot and address, the interrupted pass's frame stays below | `nested_interrupts` (max interrupt depth), `patched_returns` if the handler returns elsewhere |
| SP moved for drawing (stack blitters), return addresses discarded | frames below the new SP are abandoned at the next RET (rule 1); an interrupt during a stack blit pushes its frame on the blit's stack and matches it on return | `resyncs` per routine, `abandoned` frames |
| Code before the profile started returning | a RET with no frame: dispatch rule, or nothing to pop | `unmatched_returns` |

### 5.4 Self-modifying code

A cell counts the instruction that *executed* at that byte; when the code there changes, the cell keeps summing
(L1 marks such an instruction `smc`). The kit export reads the opcode bytes at export time.

## 6. Data structures

### 6.1 Flat cells

```cpp
struct Cell { uint64_t count; uint64_t tstates; };   // 16 B (POC: 8 B variant with 32-bit halves and a spill)
std::atomic<Cell*> _pages[MAX_PAGES];                 // 389 pointers, a page block allocated on first execution
```

A page block is `16384 x sizeof(Cell)` = 256 KiB, allocated when an instruction first executes there (lazy), zeroed,
published with a release store; never freed while the analyzer lives (reset zeroes it).

### 6.2 Opcode classes

`static const uint8_t kFlowClass[256]` (+ the ED row): read only when the stack is on.

### 6.3 Routine table

`std::vector<Routine>` indexed by a dense id; `std::unordered_map<uint32_t physOffset, uint32_t id>` looked up on a
push only (a CALL, an interrupt). Per routine: `self`, `inclusive`, `entries`, `jumpEntries`, `dispatches`, `resyncs`,
`patchedReturns`, `onStack` (the recursion count), callers (up to 8 `(caller id, site)` pairs with counts, then
`more`), `firstFrame`, `lastFrame`, `activeFrames`.

### 6.4 Beam bins

Per routine `B` bins (default 16) of the frame's T: bin = `(lastT * B) / frameT`, `beam[id * B + bin] += d`. Gives
when in the frame a routine runs, relative to the beam (a 48K frame: 312 lines, a bin of 16 = 19.5 lines).

### 6.5 Marks

A mark is a baseline: at the frame boundary after the request the emulation thread copies the cumulative counters
(cells of the allocated pages, the routine table) into the mark's buffers. `since(mark) = cumulative - baseline`
(a page allocated after the mark has a zero baseline). Up to 8 named marks; a mark with an existing name moves.
Cumulative counters are never reset by a mark; `reset` zeroes everything (marks too).

### 6.6 Window header

`frames`, `tstates` (sum of the deltas), `instructions`, `interrupts`, `nmis`, `interrupt_ack`, `halt_tstates`,
`discontinuities`, `first_frame`, `last_frame`, frame length, CPU clock; published at every frame end under a seqlock
(readers copy 128 bytes and retry on a torn read, never waiting on the writer).

### 6.7 Per-frame series

At each frame end the emulation thread compares each routine's `self` / `inclusive` with the values at the previous
frame end (a second array) and appends the routines that changed as sparse records `(id u32, self u32, inclusive
u32)` behind a frame header `(frame u64, count u32, total u32)` into a ring of bytes (default 4 MiB). The open frames'
inclusive time is added for the routines on the stack (walk of the stack, ≤ 256). Readers copy from the ring by
sequence number and drop what the writer overwrote meanwhile (§10). Optional: the same records as JSON lines to a
file (`stream=path`), buffered by stdio, written on the emulation thread at the frame end (§9).

## 7. Memory budget

A page block is 16 384 cells. With 16 B cells: 256 KiB a page; with 8 B cells: 128 KiB. Executable pages = RAM pages +
ROM pages of the model's ROM set (+ the Sprinter's 4 cache pages). The bound is what every page would cost if all
executed; lazily, only the pages code runs from are allocated.

| Machine | RAM pages | ROM pages | All pages | Bound, 16 B cells | Bound, 8 B cells | Typical game (lazy) |
|:--|--:|--:|--:|--:|--:|:--|
| 48K (+ TR-DOS) | 3 | 1 (+1) | 4-5 | 1.00-1.25 MiB | 0.50-0.63 MiB | 4 pages = 1 MiB |
| 128K / +2 (+ TR-DOS) | 8 | 2 (+1) | 10-11 | 2.50-2.75 MiB | 1.25-1.38 MiB | 4-8 pages = 1-2 MiB |
| Pentagon 512 | 32 | 4 | 36 | 9.00 MiB | 4.50 MiB | 4-8 pages |
| Pentagon 1024 | 64 | 4 | 68 | 17.00 MiB | 8.50 MiB | 4-8 pages |
| Scorpion ZS256 (64K ROM) | 16 | 4 | 20 | 5.00 MiB | 2.50 MiB | 4-8 pages |
| Scorpion + ProfROM 256K | 16 | 16 | 32 | 8.00 MiB | 4.00 MiB | |
| TS-Conf 4 MB (512K ROM) | 256 | 32 | 288 | 72.00 MiB | 36.00 MiB | an OS: 10-30 pages |
| ZX Next 2 MB (64K ROM) | 128 | 4 | 132 | 33.00 MiB | 16.50 MiB | |
| Sprinter 4 MB (+ cache) | 256 | 32 + 4 | 292 | 73.00 MiB | 36.50 MiB | |
| the whole buffer (MAX_PAGES 389) | | | 389 | 97.25 MiB | 48.63 MiB | |

The other structures are small: routine table ~96 B a routine (2 000 routines: 188 KiB), beam bins `16 x 8 B` a
routine (2 000: 250 KiB), the stack 256 x 32 B = 8 KiB, the per-frame snapshot arrays 16 B a routine, the ring 4 MiB
(default), a mark = a copy of the allocated cells (4 pages: 1 MiB) + the routine table.

Per-frame ring: a frame costs 16 B + 12 B per active routine. *POC*: active routines per frame on Zynaps / Nether
Earth; at 100 active: 1.2 KiB a frame, 4 MiB = ~3 400 frames (68 s at 50 Hz).

## 8. Counter width and overflow

A cell's T-states grow at most at the CPU clock (a HALT or a tight loop takes nearly the whole frame), its count at
most at clock / 4 (an instruction is at least 4 T).

| Clock | 32-bit T full after | 32-bit count full after | 64-bit T full after |
|:--|--:|--:|--:|
| 3.5 MHz (48K, 128K, Pentagon) | 1 227 s = 20.5 min | 81.8 min | 167 000 years |
| 7 MHz (turbo, ATM, Profi) | 614 s = 10.2 min | 40.9 min | 83 500 years |
| 14 MHz (TS-Conf, Evo, Sprinter 14) | 307 s = 5.1 min | 20.5 min | 41 800 years |
| 21 MHz (Sprinter) | 205 s = 3.4 min | 13.6 min | 27 800 years |
| 28 MHz (Next) | 153 s = 2.6 min | 10.2 min | 20 900 years |

(2^32 = 4 294 967 296; 2^64 / 28e6 s = 6.6e11 s.) Turbo emulation reaches these in less wall time (Zynaps at 20x
real time fills a 32-bit HALT cell in a minute). "Hours of run" therefore needs 64-bit time; the options:

- **A, 16 B cells**: `u64 count, u64 t`. No overflow in practice, 256 KiB a page.
- **B, 8 B cells + spill**: `u32 count, u32 t` and a per-page spill array touched only on a carry (`if (t < old)`):
  half the memory, one more predictable branch per step. *POC* measures A against B.

Routine counters, totals and the per-frame records are 64-bit (per-frame records 32-bit: a frame's T fits).

## 9. Hot-path cost model

> **As built** (numbers: §15): cells with T-states +17-19 % (8.6 ns an instruction, about twice the estimate below);
> the stack from the CPU flow events adds nothing measurable over the cells (the "stack (option)" row below is the
> per-step decoder, kept only for the A/B); off is within the noise of master.

Off: no subscriber, nothing registered; the CPU loop pays its existing `hasCPUStepSubscribers()` test (two loads, one
branch), as today. **Zero cost**: proven by the A/B of §15 against master.

On, per instruction (the callback is a call through `AnalyzerManager::dispatchCPUStep`'s vector, ~2-3 ns):

| Part | Work | Estimate |
|:--|:--|:--|
| dispatch | the vector loop, an indirect call | 2-3 ns |
| delta | load `t`, subtract, compare with the frame length | < 0.5 ns |
| cells | bank pointer → physical offset, page pointer, count++ and the previous cell's T += d | 1-2 ns |
| interrupt check | load `accepts`, compare | < 0.3 ns |
| stack (option) | opcode byte + class table, store PC / SP / class; the rare push / pop | 1-2 ns |
| beam (option) | multiply, shift, add | < 0.5 ns |
| per frame (option) | nothing per step; O(routines) at the frame end | 2-5 µs a frame |

A classic machine's instruction costs ~60-100 ns of host time with everything around it (`BM_HostFrame_*`: 1.1-1.9
ms per 17 500 instructions), so the expectation is +5-10 % for counts and T, +10-15 % with the stack. A side effect
to measure: with a subscriber the halted idle is stepped fetch by fetch; a HALT-paced game pays for every idle fetch
(§4.2).

## 10. Threads

As built (decided after POC 046, §14):

- **Writer**: the emulation thread only (the step and its frame-end slow path). Every counter (cells, routines,
  totals, the series ring) is a `std::atomic` written by a relaxed load + store (a plain store on arm64 and x86-64, no
  read-modify-write), so a reader on another thread sees each counter whole and never parks the emulation thread.
- **Readers** (`status`, `report`, `frames`, `export`, the `debug.cpuprofile` topic) take no lock against the
  writer: a report is a few instructions skewed at most. Cell pages and routine chunks (1024 routines) are published
  by release / acquire pointers and live until the analyzer goes. The routine on top of the stack has an open
  stretch since its last transition: `_top` / `_topSince` are published so a reader adds it.
- **Series ring**: a single-writer overwrite ring of 32-bit words plus a frame index (16384 entries); a reader copies
  a frame's words and re-checks both heads after the copy (the writer did not lap it). The stream thread is such a
  reader, polling every 100 ms.
- **Marks** copy the counters on the caller's thread under the marks' own mutex (the writer never takes it): 9-16 µs
  for Zynaps' two pages.
- **Control**: `start`, `stop` and `reset` hold the CPU (`Emulator::CpuHold`) while they subscribe, unsubscribe or
  zero the counters: no request queue and no frame-boundary handshake (rejected: more code for no gain, the switch is
  a human-paced action).

## 11. Interaction with the rest of the emulator

- **Turbo**: the step and the deltas are the same at any host speed; the turbo render decimation does not touch the
  CPU. Turbo makes the profile reach N frames sooner.
- **CPU clock changes** (hardware turbo, the Next's 28 MHz, the Sprinter's accelerator): `t` counts CPU T-states of the
  current clock; the window header keeps the frame length in CPU T at each frame end, so the total T of the window
  is the sum of the frames' lengths at their clocks. Per-routine T stays in CPU T-states (what the code cost).
- **TTD recording**: independent; the profile is not recorded state (not in snapshots or checkpoints).
- **TTD replay / seek**: a replay pass (`ttdReplayActive`) is not live execution: the profile skips it (one byte test in
  the slow path, a seek shows as a discontinuity). Profiling a recording is L1's job, or the analysis clone can host
  its own `profile` analyzer (the POC does that for the accuracy check).
- **Snapshot load, reset, model switch**: a discontinuity (§4.3); a model switch recreates the memory: the analyzer is
  told through `onDeactivate` / re-activation by the `AnalyzerManager` of the new machine (no stale page pointers).
- **Several instances**: each instance has its own `DebugManager` and `AnalyzerManager`, hence its own profile.
- **Debug mode, breakpoints**: independent; a pause between two steps is time that does not pass in `t`.

## 12. Output: the profile JSON and the kit export

> **As built** (the fields: [cpu-profile.md](../../emulator/analysis/cpu-profile.md#operations)): the kit format is
> `format=kit` (not `l1`), the report sorts by `self` / `inclusive` / `calls` (no `attribution=address`: the kit
> export with a `.ann` gives that through `fig_budget.py`), routines carry `kinds` and the attribution counters
> (`jump_ins`, `dispatches_in` / `_out`, `abandoned`, `patched_returns`) plus a `notes` text; the series is read by
> `frames last= top=`; the `sym` export is `name = $ADDR` lines, `ann` an `.ann` seed. The v1 sketch below is kept.

### 12.1 Report

`GET /cpuprofile/report?since=<mark>&top=24&attribution=stack|address&beam=1&callers=1`:

```json
{
  "format": "unreal-ng cpu profile", "version": 1,
  "machine": {"model": "48K", "frame_tstates": 69888, "cpu_hz": 3500000},
  "window": {"since": "start", "first_frame": 0, "last_frame": 3000, "frames": 3000, "tstates": 209664000,
             "instructions": 25370000, "interrupts": 3000, "nmis": 0, "interrupt_ack": 57000, "halt_tstates": 0,
             "discontinuities": 0},
  "options": {"stack": true, "beam_bins": 16, "frames_ring_bytes": 4194304},
  "routines": [
    {"entry": "$B30C", "page": 2, "name": "EffectPlay", "kind": "call", "self": 47160000, "self_share": 0.225,
     "inclusive": 52000000, "inclusive_share": 0.248, "entries": 3000, "jump_entries": 0, "dispatches": 0,
     "resyncs": 0, "patched_returns": 0, "callers": [{"routine": "$8D00", "site": "$8D3A", "count": 3000}],
     "beam": [0, 0, 1200, ...]}
  ],
  "anomalies": {"unmatched_returns": 0, "abandoned_frames": 0, "max_depth": 7, "max_interrupt_depth": 2,
                "stack_overflows": 0}
}
```

`share` is of the window's T. `attribution=address` sums the cells by the nearest entry at or before the address
(labels first, then the discovered entries) as `fig_budget.py` does with a `.ann`.

### 12.2 Kit export (L1-compatible)

`POST /cpuprofile/export {"format": "l1", "path": "work/p1/profile-l1.json", "since": "play"}` writes the subset of
L1's `l1.json` the kit's figure scripts read:

```json
{"format": "unreal-ng analysis L1", "version": 1, "source": "cpu profile",
 "replay": {"first_frame": 0, "last_frame": 3000, "frames": 3000, "instructions": 25370000, "tstates": 209664000,
            "interrupts": 3000},
 "pages": [{"page": 0, "name": "RAM 0"}, {"page": 261, "name": "ROM 0"}],
 "routines": [{"entry": "$B30C", "page": 2, "kinds": "call",
               "profile": {"tstates": 47160000, "tstates_inclusive": 52000000, "entries": 3000}}],
 "instructions": [["$8544", 2, "228885", "", 12253, 196048, false]]}
```

`instructions` rows are `[z80, page, code, class, count, tstates, smc]` as in L1 (`code`: the opcode bytes read at
export time, `class` empty, `smc` false); the interrupt acknowledge is folded into the interrupted instructions'
total as L1 does (per instruction it is added to the handler's first instruction, which keeps every routine's share
within the acknowledge's 13-19 T a frame). `kit/fig_budget.py work/p1/profile-l1.json out.png --ann ...` draws
from it unchanged.

### 12.3 Entries export

`POST /cpuprofile/export {"format": "entries"}`: one row per discovered entry `{entry, page, kind (call / rst /
interrupt / nmi / dispatch / root), name, calls, callers [{routine, site, count}], jump_entries, dispatches}`, and
`format: "sym"` writes them as a `.sym` (`sub_XXXX: EQU $XXXX`) for the label manager, L1 runs or a case's `.ann`.

### 12.4 Per-frame series

`GET /cpuprofile/frames?from=<frame>&limit=&routine=<entry|name>`: `{frames: [{frame, total, routines: [[id, self,
inclusive], ...]}], routines: [{id, entry, page, name}], dropped}`; with `routine` only that routine's column (a series
to plot). The stream file: JSON lines, one per frame, `{"f":123,"r":[[id,self,incl],...]}`, and a last line with the
routine table.

## 13. Automation planes

> **As built**: the table below with these changes: `start` takes `nostack` / `beam` / `noframes` / `reset` /
> `stream=` / `ring_kib=` (no `keep`: a start goes on with the window unless `reset`), `status` is
> `GET /cpuprofile/status`, `frames [last] [top=]`, MCP sends the series switch as `series` (its `frames` is
> porttrace's frame count). Not done: the `trace cpuprofile` alias (§17).

One verb, `cpuprofile`, with operations; every plane (automation planes guidelines §1):

| Operation | CLI | Python / Lua | MCP (`analyze_performance`) | REST (`/api/v1/emulator/{id}/…`) |
|:--|:--|:--|:--|:--|
| start | `cpuprofile start [stack] [beam=16] [frames=4M] [stream=path] [keep]` | `cpuprofile("start", opts)` | `cpuprofile` op `start` | `POST /cpuprofile/start` |
| stop | `cpuprofile stop` | `cpuprofile("stop")` | op `stop` | `POST /cpuprofile/stop` |
| reset | `cpuprofile reset` | `cpuprofile("reset")` | op `reset` | `POST /cpuprofile/reset` |
| mark | `cpuprofile mark <name>` | `cpuprofile("mark", {name})` | op `mark` | `POST /cpuprofile/mark` |
| status | `cpuprofile status` | `cpuprofile("status")` | op `status` | `GET /cpuprofile` |
| report | `cpuprofile report [since=] [top=] [attribution=] [beam]` | `cpuprofile("report", opts)` | op `report` | `GET /cpuprofile/report` |
| frames | `cpuprofile frames [from=] [limit=] [routine=]` | `cpuprofile("frames", opts)` | op `frames` | `GET /cpuprofile/frames` |
| export | `cpuprofile export <l1\|json\|entries\|sym> [path] [since=]` | `cpuprofile("export", opts)` | op `export` | `POST /cpuprofile/export` |

WebSocket: every route is a `call`; the state topic `debug.cpuprofile` (`{available, running, frames, tstates,
routines, marks, options}`, per instance, frames cadence) pushes what a client would poll. The verb also joins the
`trace` table (`trace cpuprofile start|stop|clear`) and `debug.analyzers`.

## 14. Decisions after POC 046

| Question | Measured ([POC 046](../../../tools/poc/046-profile-analyzer/README.md)) | Decision |
|:--|:--|:--|
| 8 B cells (32-bit T with a spill) vs 16 B cells | 8 B not cheaper (+15.7 % vs +15.2 % on Zynaps, the same on Nether) | 16 B cells: bits 0-39 executions, 40-63 the routine id + 1 of an entry there (the entry test costs no extra load) |
| counts only | +3.4 % Zynaps, +7.8 % Nether | not an option of its own: T-states always (+15 %) |
| the shadow stack | +23 % Zynaps (10.8 ns an instruction), +21-25 % Nether | on by default (`stack=true`), `nostack` keeps the cells only |
| beam bins | +2-5 % over the stack | off by default (`beam=true`) |
| per-frame series | +1-3 %, 1.1-1.3 µs a frame, 30 records a frame on Zynaps (52 at most) | on by default |
| halted idle stepped vs in one go | a HALT-paced Pentagon demo +95-115 % stepped, +3 % in one go | the subscriber is idle-tolerant (`subscribeCPUStep(..., idleTolerant=true)`, `AnalyzerManager::hasIdleStepSubscribers`): the delta model books the stretch to the HALT exactly |
| interrupt acknowledge | L1 books it to the interrupted instruction | the core books it to the handler's first instruction (the interrupt's routine); the kit export says so in `acknowledge_tstates` |
| control | | `CpuHold` for start / stop / reset, relaxed atomics for reads (§10) |
| a push on a slot a frame holds | the POC left the frame (the stack grew under POP + JP loops) | the frame is abandoned at the push too, counted; a dispatch from the root replaces the root dispatch frame before it |
| the shadow stack's source | per-step decode: from memory +3 ns, from `opcode` / `prefix` +1 ns an instruction over the cells; CPU flow events: +0 (K1 8.58 vs K0 8.57 ns on Zynaps) | `Z80::FlowHook` raised by the CALL / RST / RET handlers (and `EngineStep` for the engines); `Settings::flowSource` 1 / 2 kept internal for the A/B and the `FlowSourcesAgree` test, chosen by template at subscription (no per-step branch) |
| a RET chain (dispatch from a dispatch frame) | stacking the frames: 83 821 overflows of the 1024 frames in 3000 Zynaps frames | the new target replaces the dispatch frame (§5.2 rule 4): 0 overflows, max depth 14 on Zynaps r1 |
| TTD replay | | skipped live; `Settings::countReplay` books it on an analysis clone (the accuracy check, a future "profile a recording") |

## 15. Final measurements

Details and raw data: [POC 046 measurements](../../../tools/poc/046-profile-analyzer/measurements-2026-11-05-cost-accuracy.md), [analysis](../../../tools/poc/046-profile-analyzer/analysis.md),
[conclusions](../../../tools/poc/046-profile-analyzer/conclusions.md). macOS, Apple Silicon, release build with LTO,
a shared machine (load 90-240): differences under ~3 % are noise.

**Off** (the flow hook's null test in the handlers present, the profile off), `core-benchmarks` against master
`8801b5e64`, 20 interleaved rounds, thread CPU time:

| Benchmark | master | this tree | paired median | this tree slower in |
|:--|--:|--:|--:|--:|
| BM_HostFrame_48K_Fast | 481.3 µs | 488.5 µs | +0.15 % | 10 / 20 |
| BM_HostFrame_Pentagon_Fast | 459.2 µs | 452.9 µs | -1.87 % | 8 / 20 |
| BM_HostFrame_Sprinter_Fast | 1949.3 µs | 1966.6 µs | +0.04 % | 11 / 20 |
| BM_NextRuntime/cpu3_busy | 15429 µs | 15319 µs | -0.39 % | 9 / 20 |

**On**, the core analyzer, 1500 turbo frames, 10 interleaved rounds (% over off, ns per instruction):

| Variant | Zynaps | Nether play | 7th Reality (HALT-paced) |
|:--|--:|--:|--:|
| cells + T-states (`stack=false`) | +18.6 % (8.6) | +17.1 % (6.9) | +4.7 % |
| + the stack from the flow events | +18.6 % (8.6) | +16.8 % (6.8) | +4.0 % |
| ... decoded per step from `opcode` (`flowSource` 1) | +21.1 % (9.7) | +14.9 % (6.0) | +1.6 % |
| ... decoded per step from memory (`flowSource` 2) | +25.2 % (11.6) | +24.1 % (9.7) | +1.7 % |
| + the per-frame series (the default) | +23.2 % (10.7) | +13.4 % (5.4) | +0.9 % |
| + beam bins | +25.2 % (11.6) | +19.0 % (7.7) | +1.3 % |

A frame record 1.1-1.3 µs (30 routines a frame on Zynaps), a mark 9-22 µs.

**Accuracy**, the core's kit export of the replays the reverse-cases L1 files were built from:

| | Nether Earth play (16 514 frames) | Zynaps r1 (75 003 frames) |
|:--|:--|:--|
| window frames, T-states, instructions | equal to L1 | equal to L1 |
| per instruction: counts | 4918 / 4918 equal | 5265 / 5265 equal |
| per instruction: T-states | equal but the acknowledges (in the handler, not the interrupted instruction): 150 860 T moved = 2 x 75 430 | 1 461 746 T = 2 x 730 873 |
| self by address, top 24 | 0.000 ‰ of the window | 0.000 ‰ |
| `fig_budget.py --ann game.ann` | | the same 24 bars, shares within 0.01 % |
| L1's CALL / RST / interrupt entries found | 100 % | 100 % |
| stack: max depth, overflows | 19, 0 | 14, 0 |


## 16. Tests

- Unit (`core/tests/debugger/analyzers/profile/`): a hand-written program on a bare 48K (CALL / RET, RST, a JP-entered
  routine, PUSH + RET dispatch, a RET chain, an IM2 handler with a nested interrupt, a stack blit with an interrupt in
  it, a discarded return address, a HALT loop, a frame wrap): exact T and counts per cell against hand-counted
  timings, routines, anomalies, marks, per-frame records, the ring's overwrite, the kit export's shape; off = no
  subscription.
- Planes: CLI, REST (and the WebSocket call conformance), Lua, Python, MCP unit tests (`*Mcp*`) and a live script in
  `tools/verification/mcp/`.

As built: `ProfileAnalyzer_Test` (14 tests: off = no subscriber, CALL / RET, JP into a routine, PUSH + RET dispatch,
a RET chain one frame deep, a discarded return address, a root dispatch, the three flow sources agree, interrupts
with a halted CPU + marks + the per-frame series, a stop closing the last frame, bad input, report and exports, reset,
the CLI words), the MCP tool tests, `tools/verification/mcp/test_cpu_profile.py` (live); the accuracy against L1 is
the POC's `replay --core 1` + `compare_kit.py` (not a unit test: it needs the reverse-cases recordings).

## 17. Left for later

- `report attribution=address` (the nearest entry at or before, with labels): today the kit export + a `.ann` +
  `fig_budget.py` gives it.
- The `trace cpuprofile start|stop|clear` alias (§13).
- `Settings::flowSource` 1 / 2 (per-step decoders): internal, for the A/B and the `FlowSourcesAgree` test; remove
  once a recorded fixture replaces that test.
- The kit export's `class` column (only `halt`, which `fig_budget.py` reads) and `smc` (always false): L1 has them
  from its decoder.
- Profiling a recording from the planes (an analysis clone with `countReplay`, as the POC's `replay --core 1`).
- Counts + T-states cost +15-19 %, above the 10 % hoped for; a cheaper cell update (no entry bits, the routine id
  table apart) was not tried.
