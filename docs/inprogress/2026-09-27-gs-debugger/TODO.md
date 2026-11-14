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
- [x] The card CPU's registers, memory, disassembly and windows on every plane (2026-11-02, branch `gs-cpu-surfaces`):
  core `CpuView` (`core/src/debugger/targets/cpuview.h`), `MemoryRead` spaces `gs` / `ms`, REST `?cpu=` on
  `/registers`, `/memory`, `/disasm`, `/memory/map`, `/memory/info`, CLI `--cpu`, Lua / Python `cpu`, MCP
  `inspect_state` `cpu`, `memory_access` spaces `gs` / `ms`. Tests: `CpuView_Test.*`.
- [x] Card CPU step in / over / out on every plane (2026-11-02, branch `gs-cpu-surfaces`): `GSCardTarget::ArmStep`
  (a one-shot park at a card boundary), `RunControl::CardStep` (resume, wait, timeout), the pause reason `step` with
  the CPU (`PauseCause::Step`, `Emulator::OnTargetStep`); REST `?cpu=` on `/step`, `/steps`, `/stepover`,
  `/stepout`, CLI `--cpu`, Lua / Python `cpu`, MCP `control_execution` steps `cpu`. Tests:
  `GSCardTarget_Test.AStepParksAtItsBoundary`, `ABreakpointEndsAStepOver`, `TheMachinesCardStepsThroughRunControl`,
  `McpDebugTools_Test.CardCpu_StepsGoToTheCardsRoutes`. No lock-step: the main CPU runs on to the card's catch-up.
- [x] The Qt debugger's card CPU over the WebAPI (2026-11-02, branch `gs-cpu-surfaces`): `RemoteCardZ80` in
  `debuggers/qt/adapter/remotemachine.cpp` (registers, 64 KB, windows over `?cpu=`, once per snapshot; its own
  breakpoints and run-to; the card's RAM / ROM / flash as paged memories; the `<slot>.cpu` history); a step of a
  card's tab is the card's step in both adapters (embedded: `RunControl::CardStep`); a card's stop brings its tab
  to the front. `unreal-debugger --self-check` (and `--connect ... --self-check`) checks the card step and step over.
- [x] Scripts sent over `/lua/exec`, `/python/exec` (and `/file`) act in the sender's session
  (`execution::script::BorrowedSessions`): an MCP agent's Lua card step is no longer refused by the agent's own
  implicit session. Test: `ExecutionArbiterMachine_Test.AClientsScriptActsInTheClientsSession`.

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

### D. Card writes from a hook (Q3, owner 2026-10-29: the fork's track records them; design §8A.7)

- [x] `poke` / `set_reg` in a card hook (either role, at the card's next boundary: `GSCardTarget::QueueHookEdits`);
  a read's substitution allowed while recording for `logic` (2026-10-29).
- [x] TTD: card edits as `DebuggerEdit` inputs (`kEditCardEdit`) placed by the card frame and time; the replay hands
  a frame's edits to the card target (`DeliverCardEdits`), applied at that card time without the hook.
- [x] Test: `LuaGSCardHook_Test.ALogicCardHookIsRecordedAndReplayedWithoutRunningIt` (record a substitution, a poke
  and a register, seek back, replay: the same card state, the hook not called).

## Design §5 (one time line) — phase 2, branch `gs-phase2` (2026-11-13)

- [x] Tight mode (§5.2): a stopping card breakpoint or an armed card step sets `Z80::cardTight`
  (`FeatureManager::setCardTightHold`, `GSCardTarget::WantsTight`, `DebugManager::UpdateCardTight`); every main
  instruction start catches the card up (`DebugManager::CatchUpCards` -> `GeneralSoundCard::catchUpToHost`), never
  past the frame's end, never inside the card's own parked run. The main CPU keeps its memory path and debug mode
  (risk 1's fallback: at every memory access, which needed the debug memory path, it cost +44 % over lazy).
  Test: `GSCardTarget_Test.ACardBreakpointParksTheMainCpuNearby` (tight: main within an
  instruction of the card's stop; lazy: up to a frame).
- [x] Main position in a card's pause (§5.4, first part): `MainPosition` (`pc`, `t_in_instruction` from
  `Z80::instrStartT`, `gap_t` from `GeneralSoundCard::hostLead`, `frame_end`) in `Emulator::BreakpointStop`, the
  pause payload, the `debug` topic's `paused` event and the card step's `stop` (`main`).
- [x] A card step never runs ahead of main (§5.3): an armed step turns tight mode on; the stop's `gap_t` is never
  negative (`TheMachinesCardStepsThroughRunControl`). Paused mid-frame, the card is still lazily behind: the steps walk
  it towards main.
- [x] Determinism (§5.6): `GSCardTarget_Test.TightModeRunsTheCardAsLazyDoes`, the machine's NeoGS through its loader's
  clock switch, every card-side event identical lazy and tight.
- [x] A/B benchmark: `BM_HostFrame_NeoGS_Idle_CardLazy` / `_CardTight` beside `_Idle_Fast` (hostbusoverlay_benchmark).
  M-series Mac, 8 interleaved rounds, CPU time per host frame (median): all off 648 us, main debug path 766 us, card
  breakpoint lazy 715 us, card breakpoint tight 889 us (+24 % over lazy). The profile splits the rest about evenly:
  the catch-up calls (`CatchUpCards`, `catchUpToHost`, `endRun`, about 7 %) and the cost of running both CPUs in
  ~7000 short pieces per frame (cache and branch history, main and card loops alike). A cached card rate and an early
  return when the card is already at the target are in. Only while a stopping card breakpoint or a card step is
  active; with none the instruction start tests one more flag (Pentagon fast 458 us, unchanged).
- [ ] The rest of §5.4: the instruction's total T, the bus cycle under way (opcode M-cycle tables), the UI's progress.
- [ ] Tight mode while the machine is paused with the card selected in a front-end (§5.2 third trigger), card run-to
  (§5.3).

### Later

- [x] Card breakpoint conditions (2026-10-27): the card's registers, memory and pages through `IDebugTarget::Registers`
  / `Peek` / `MapAddress`; `T`, `FD`, `DOS` and labels refused; `cpu` on `bp_condition` (Lua, Python). Tests:
  `GSCardTarget_Test.ACondition*`, `AFalseConditionCountsMatchesWithoutStopping`, live `test_gs_debugger.py`.
