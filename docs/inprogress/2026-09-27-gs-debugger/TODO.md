# TODO — GS/NeoGS card-CPU debugger

Requirements rev. 3 and design rev. 2 (2026-09-28): NeoGS in master, NeoGS target moved to phases 1-2, front-ends handed to the [debugger model](../2026-09-28-debugger-model/). (#44, the thread-safe GS control path, landed in `8dacbc82`.)

Plan: [PLAN.md](../PLAN.md) #45 (T2).

## Design §4 (card hooks) — in progress on `deck-controllers` (2026-10-27)

### A. Core: debug loop, card breakpoints, events, park — done

- [x] `IGSDebugHooks` (`core/src/emulator/sound/chips/gs/gsdebughooks.h`): `beforeStep`, `endRun`, bus reads /
  writes, port in / out, card events (`GSCardEvent`: int, nmi, reset, page, card_command, card_data, card_reply,
  host_command, host_data, host_reply).
- [x] `GSCardRunner::runToDebug`: the same loop compiled with `kDebug` (no idle sleep, `beforeStep` per
  instruction, INT / NMI events). `runTo` (the fast loop) is untouched.
- [x] GS and NeoGS: `setDebugHooks` swaps in the debug bus callbacks; `runTo` branches once per run; page mapping,
  page counts, registers, card time for the target.
- [x] `IDebugTarget` + `GSCardTarget` (`core/src/debugger/targets/`), owned by `DebugManager`; finds the card on each
  call; hooks on the card only while an active breakpoint asks for them; SoundManager tells it about a new card.
- [x] `BreakpointManager` target mode: pages and demand from the target, BRK_MEMORY / BRK_IO / BRK_EVENT only,
  conditions refused.
- [x] `BRK_EVENT` (protocol breakpoints): card-side kinds on the gs manager, host-side kinds (#B3 / #BB from the main
  CPU) on the main manager, value filter.
- [x] Hits: execute before the instruction; memory / port / event hits at the next card instruction boundary with
  the access PC; M1 reads excluded.
- [x] Park: `Emulator::OnTargetBreakpointHit` pauses the machine (or ends a direct step); the pause and breakpoint
  payloads carry `cpu` and `cpuPc`.
- [x] Tests: `core/tests/debugger/targets/gscardtarget_test.cpp` (GS and NeoGS firmware).

### B. Surfaces

- [x] CPU selector on breakpoint commands (CLI `--cpu` and `bevent`, Lua / Python `cpu` and `bp_event`, MCP `cpu`,
  REST `?cpu=` / body `cpu`, WebSocket), `cpu` in lists, `cpu` and `cpu_pc` in stop reports beside the main PC.
  Docs: [breakpoints-and-traces.md §8](../../emulator/design/debugger/breakpoints-and-traces.md#8-the-sound-card-cpu).
- [x] Tests: the machine's NeoGS (`TheMachinesCardStopsWhereItLoops`), live MCP
  `tools/verification/mcp/test_gs_debugger.py`.
- [ ] `GET /breakpoints/groups` per CPU (main only now).

### C. Read-value script callbacks (design §8A.7)

- [ ] Synchronous card memory / port read callbacks returning the value the card CPU sees, on `onMemoryRead` /
  `onPortIn`.

### Later

- [ ] Card breakpoint conditions (the condition engine is tied to the main Z80 registers and memory).
