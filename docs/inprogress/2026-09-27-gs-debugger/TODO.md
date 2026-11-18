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
- [x] The rest of §5.4 (branch `gs-progress`, 2026-11-14): `Z80MCycleDecoder` (debugger/disassembler/z80mcycles)
  gives an instruction's machine cycles, both ways of a conditional; checked against every FUSE vector's bus trace
  (`Z80MCycles_Test`, 1341 cases one instruction, 15 compared to the instruction's end). Tight mode's catch-up runs
  before the next instruction start is taken, so a card's stop sees the instruction its moment is in (`on_boundary`).
  `main` gains `on_boundary`, `elapsed_t`, `total_t`, `total_t_alt`, `contended`, `bus_cycles`, `bus_cycle`,
  `bus_cycle_index` and `text` (`execution::MainPositionNode` / `MainPositionText`, one source for the card step's
  `stop`, the `debug` topic's `paused` event and the snapshot's and `wait`'s `pause`). MCP `wait` adds the text; the
  debugger's status line shows it after a card's stop (`ecatshim::Emulator::debug_stop_main_text`, both adapters).
  Contention: the measured T beside the table's (`contended`), the cycle is the uncontended one.
- [x] Tight mode while the machine is paused with the card selected in a front-end (§5.2 third trigger, 2026-11-14):
  `DebugManager::SelectCpu` (one per machine, the last front-end's), tight mode for a direct run of the paused machine
  (`Emulator::DirectStepScope` -> `DebugManager::OnDirectRun`) and a catch-up at its end, so the card stands at the
  main CPU's moment after each step. Not on pause edges: `applyDebugMode` takes a `CpuHold`, the edge comes from inside
  the arbiter. Planes: `CpuView::SetSelected` / `Selected`; REST `GET` / `PUT /debug/selected_cpu`, CLI
  `selected-cpu`, Lua / Python `select_cpu` / `selected_cpu`, MCP `control_execution` `select_cpu`; the Qt debugger's
  CPU tab (`ecatshim::Emulator::debug_select`, both adapters; main again at detach). Tests:
  `GSCardTarget_Test.TheSelectedCardFollowsTheMainCpusSteps`, live `test_gs_debugger.py`.
- [x] Card run-to (§5.3) on the automation planes (2026-11-15): a step kind `to` (`GSCardTarget::StepKind::To`, the
  cursor as the step's target; at the card's PC: its next run) through `RunControl::CardStep(..., "to", 1, address)`,
  the reply adds `hit`. Planes: the main CPU's run until PC with a card CPU: REST / WebSocket `POST /skip_until?cpu=gs`,
  CLI `skip_until <pc> --cpu gs`, Lua `skip_until(pc, "gs")`, Python `skip_until(pc, cpu="gs")`, MCP
  `control_execution` `run_until_pc` `cpu`. A breakpoint on the way always ends it; no t-state budget (5 s). The Qt
  debugger keeps its own breakpoint. Tests: `GSCardTarget_Test.AStepParksAtItsBoundary` (to),
  `TheMachinesCardStepsThroughRunControl` (to, to at the cursor, a breakpoint first), live `test_gs_debugger.py`.

## Design §6 (firmware knowledge) — phase 4, branch `gs-firmware` (2026-11-16)

Findings before the work: the in-tree sources (`materials/gs/gs-firmware`) assemble with sjasmplus 1.23.1 into
`gs105b.rom` byte for byte, and without `patch/patch.a80` into `gs104.rom` byte for byte; `gs105a.rom` differs from
v1.05b in 63 bytes. So v1.04 and v1.05b take the reference build's labels exactly, and v1.05a takes them outside its
63 changed bytes. sjasmplus's SLD output marks EQUs (`+equ`) and gives each label its 16K page (0: low ROM, 1: the
fixed RAM window, 3: high ROM at `#C000`).

1. [x] **Card symbols (L1-L4, R1).** A label store per card CPU (`LabelManager` with the card's page map), page-tied
   labels (`gs.rom0` / `gs.rom1` / the fixed RAM window), the card's disassembly and breakpoints by name;
   `tools/gsfirmware/markup_symbols.py` (sjasmplus reference builds, SLD) writes `data/symbols/gs/<id>.map` and a
   mark-up report; `data/symbols/gs/manifest.json` bundles matched by the card's ROM page SHA-256 load at card boot;
   a test checks every shipped label's bytes against the ROM; R1's build check (v1.05b from the sources = the
   shipped file). Planes: `cpu` on the label and symbol verbs (REST `/labels`, `/symbols`, CLI, Lua, Python, MCP).
   - Done (uncommitted, worktree `scratch/wt-gs-firmware`): store and bundles as in design §6.2 "As built", `cpu`
     on every plane, card breakpoint conditions by name, tests `GSCardTarget_Test.TheFirmwareSymbolsNameTheCardsCode`,
     `TheMachinesCardLoadsItsFirmwareSymbols`, `McpTools_Test.ManageSymbols_Cpu_NamesTheCardsLabels`. The label byte
     check is the script's rebuild check (1.04 / 1.05b byte-exact), not a C++ test. Left: L5, NeoGS (N5), Qt card labels.
2. [x] **Firmware profiles (F2).** `GSFirmwareProfile` tables for v1.04 / v1.05a / v1.05b from `COMTAB` / `COMTABH`,
   selected by the ROM image SHA-256; the card names its firmware; self-check test: every non-stub handler in the
   ROM's tables has a named command.
   - Done (worktree `scratch/wt-gs-firmware`): `gsfirmwareprofile.h/.cpp` (129 / 131 commands, variables, stream
     commands), `firmware_profile` in the GS report, `GSFirmwareProfile_Test` (self-check against the ROM tables and
     labels). Design §6.1 "As built". Left: `rom.cpp` known-ROM entries, profiles from files.
3. [x] **Command log (F1, F4, P2, F7 basics).** `GSCommandLog` (ring, decoder by profile, anomalies), owned by
   `SoundManager`, fed by LLE and LW; start / stop / clear / read / export on every plane; on / off benchmark.
   - Done: the device history `<slot>.commands` (every plane through the history verbs; the stream file is the
     export), fed by LLE, LW and NeoGS, owned by `SoundManager` / the MultiSound card; four anomalies;
     `GSCommandLog_Test`, `BM_GSCommandLog_HostTraffic_*`, MCP `test_the_command_log_is_a_device_history`. Design
     §6.3 "As built". Left: TTD seek cut-off (D3), card-side hooks, block destinations of uploads.
4. [x] **Protocol state and send with decoding (F3, F6).**
   - Done: `GSProtocolState` from the command log's decoder (`protocol` in the GS report), `GSHostDriver` (the
     debugger's send: each byte when the mailbox allows it, replies read, decoded; TTD input `GSReadData`), CLI /
     WebAPI / MCP / Lua / Python, `GSHostDriver_Test`. Design §6.4 "As built". Left: firmware objects (F5, phase
     6), a MultiSound card's send, Qt.
5. [ ] **NeoGS boards, stats, SD (N4, N7, N8).** `GET /debug/boards`, the stats block, the SD board over `sd.ngs`.

## Design §8 (TTD positions on the card's CPU) — branch `card-positions` (2026-11-17), not landed

Owner decision 2026-11-17: either CPU is stepped both ways; the other CPU is computed by replay from the nearest
checkpoint; the main CPU is shown at its last completed instruction plus a T offset; a run first lets the main CPU
reach its boundary.

- [x] Core: the card's instruction count in its TTD blob (GS bytes 59-67, NeoGS 240-248, outside the hash);
  `GSCardTarget` scans (`ScanScope`: capture at a count, breakpoint hits by replay, hit counts kept);
  `TimeTravelController::SeekToCardInstruction` / `StepCardInstructions` / `ReverseContinueCard` /
  `CardPositionNow` / `PublishedCardPositions` (`ttdcontrollercardpositions.cpp`); the card position (`CardStand`)
  left at the first run, carried by `LiveStateSnapshot`.
- [x] Every plane: `TTDControl` `cpu` on `seek` / `step-instruction` / `reverse-step` / `reverse-continue`, `cards[]`
  in `position` / `status`; CLI `--cpu`, Lua / Python `ttd_card_*`, MCP `card_seek` / `card_step` /
  `card_reverse_continue` / `card_position`, REST / WebSocket and OpenAPI. Docs:
  [command-interface.md](../../emulator/design/control-interfaces/command-interface.md#ttd-on-a-sound-cards-cpu).
- [x] Tests: `TTD_CardPositions_*` (GS, NeoGS, MultiSound GS), `TTDControl_CardPositions_Test`, `CliTTDCards_Test`,
  `OpenApiTTDCards_Test`, MCP and Lua tests, live `tools/verification/mcp/test_ttd_card_positions.py`.
- [x] UI (landed a6be6598d 2026-11-18): eCat's time group on the card's tab (Step Back, F7 in the past,
  Run Back to the card's breakpoints; in process and over REST), the moment naming both CPUs (`gs at instruction N,
  PC #...; main CPU PC #..., +N T into the next instruction`); Embedded has no card CPU view, its moment label shows a
  card position. [TTD branches debuggers.md §8](../2026-11-08-ttd-branches/debuggers.md#8-positions-on-a-sound-cards-cpu-2026-11-18).
  Tests: `DebuggerTimeTravelCard_Test`, `unreal-debugger --self-check` (both modes).
- [ ] Embedded: a CPU selector (Main / GS / MS) for its registers and disassembler (proposed, not built).

### Later

- [x] Card breakpoint conditions (2026-10-27): the card's registers, memory and pages through `IDebugTarget::Registers`
  / `Peek` / `MapAddress`; `T`, `FD`, `DOS` and labels refused; `cpu` on `bp_condition` (Lua, Python). Tests:
  `GSCardTarget_Test.ACondition*`, `AFalseConditionCountsMatchesWithoutStopping`, live `test_gs_debugger.py`.
