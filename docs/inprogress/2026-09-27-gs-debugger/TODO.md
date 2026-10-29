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
- [x] `GET /breakpoints/groups` per CPU (`?cpu=gs`, Lua / Python `bp_groups(cpu)`, MCP `bp_groups` `cpu`), 2026-10-27.

### C. Read-value script callbacks (design §8A.7)

- [x] Card taps for script callbacks (`cpu = "gs"`): memory, ports and card events (`event = "int"`, ...), the card's
  registers in the hit, `peek` / `reg` of the card in a hook; Lua and Python `on_tap`, `cpu` in the lists.
- [x] A sync hook on a card read or port in returns the byte the card CPU sees (`BreakpointManager::
  HandleReplaceableRead` / `HandleReplaceablePortIn`); refused while time travel records or browses (the hook gets
  an error and the card reads the real byte: a replay could not repeat the substitution).
- [x] Tests: `LuaGSCardHook_Test` (gs105a's `IN A,(4)` takes the hook's byte), live `test_gs_debugger.py` (Python).
  Docs: [script-callbacks.md](../../features/script-callbacks.md#the-sound-cards-cpu).
- [x] Callbacks named after the events (X13, 2026-10-27): `gs_on_command`, `gs_on_data`, `gs_on_reply`, `gs_on_int`,
  `gs_on_nmi`, `gs_on_page`, `gs_on_dac_fetch`, `gs_on_clock_change`, `gs_on_dma_start`, `gs_on_dma_done`,
  `gs_on_port_in` / `gs_on_port_out` (Lua, Python). New card events `dac_fetch` (GS and NeoGS), `clock_change`
  (GSCFG0), `dma_start` / `dma_done` (`NeoGSDma::Host` hooks); the hit's `arg` carries the detail. Tests:
  `GSCardTarget_Test.TheDacFetchAndTheClockChangeAreCardEvents`, `NeoGSDma_Test`, `LuaGSCardHook_Test.Named*`.
- [x] The headless runner (2026-10-27): `unreal-qt --headless --script f.lua --exit-on-script` (`HeadlessScriptRunner`,
  `AutomationLua::runFile`): the chunk's result is the exit code, the emulator muted, the window closed unattended.
  Tests: `AutomationLua_Test.RunFileAnswersTheScriptsExitCode`, `tools/verification/headless/test_headless_runner.py`.
- [x] The MultiSound card's General Sound as the `ms` CPU (2026-10-27): a second `GSCardTarget` found through
  `ICard::DebugGeneralSound`; `--cpu ms`, `cpu="ms"`, `?cpu=ms`. Test: `GSCardTarget_Test.TheMultiSoundsGeneralSoundIsTheMsCpu`.

### Later

- [x] Card breakpoint conditions (2026-10-27): the card's registers, memory and pages through `IDebugTarget::Registers`
  / `Peek` / `MapAddress`; `T`, `FD`, `DOS` and labels refused; `cpu` on `bp_condition` (Lua, Python). Tests:
  `GSCardTarget_Test.ACondition*`, `AFalseConditionCountsMatchesWithoutStopping`, live `test_gs_debugger.py`.
