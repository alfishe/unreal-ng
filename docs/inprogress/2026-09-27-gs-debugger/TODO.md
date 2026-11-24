# TODO — GS/NeoGS card-CPU debugger

> TTD index: [docs/emulator/design/debugger/time-travel-debug/README.md](../../emulator/design/debugger/time-travel-debug/README.md)

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
     check is the script's rebuild check (1.04 / 1.05b byte-exact), not a C++ test. Left: L5, Qt card labels. NeoGS (N5) done (uncommitted): see item 11.
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
     §6.3 "As built". Left: card-side hooks, block destinations of uploads.
   - D3 done (2026-11-20, worktree `scratch/wt-ttd-d3`): the log follows time travel (`history::ITimeFollower`,
     `DeviceHistory::Rewind` / `Truncate`): a move holds the entries after the moment back, a move forward brings
     them back, a live run from the past logs anew, a branch switch keeps the shared past; the decoder follows the
     machine (frame-start marks, replay decodes without logging). Design §6.3 "TTD (D3)"; tests
     `GSCommandLog_Test.Ttd*`, `DeviceHistory_Test.Rewind*` / `Truncate*`.
4. [x] **Protocol state and send with decoding (F3, F6).**
   - Done: `GSProtocolState` from the command log's decoder (`protocol` in the GS report), `GSHostDriver` (the
     debugger's send: each byte when the mailbox allows it, replies read, decoded; TTD input `GSReadData`), CLI /
     WebAPI / MCP / Lua / Python, `GSHostDriver_Test`. Design §6.4 "As built". Left: firmware objects (F5, phase
     6), a MultiSound card's send, Qt.
5. [x] **NeoGS boards, stats, SD (N4, N7, N8).** `GET /debug/boards`, the stats block, the SD board over `sd.ngs`.
   - 5a done (17c5a3e40): the boards and the SD board over `sd.ngs`, design §6.5.
   - 5b done (bbe0ec4aa, 2026-11-18): the statistics block, the `gs_stats` board, every plane; design §6.6,
     neogs-automation-design.md §10. Checked: `core-tests` (GS, NeoGS, boards, MCP filters), `unreal-qt` zero
     warnings, live `test_gs_debugger.py` 15/15; A/B benchmarks on .20 in neogs-automation-design.md §10 (frames
     unchanged, +0.5 ns per host `OUT`). Landed 628936545 (2026-11-18). Left: the gcc check (batched).

### Phase 4, revision 3 additions (M1-M8, AU6, the F4 extensions; design §8A.8-§8A.10) — branch `gs-firmware`

Done when the §8A.10 scenarios V-1 to V-6 pass with their golden logs (design §10).

6. [x] **Profile schema 2 (M1, M2, M6, M7).** Profiles as JSON files (`data/symbols/gs/profiles/`), the built-in tables in
   the same structure: `match` (ROM / upload digests), `dispatch`, per-command `handshake`, `structs`, `instances`
   bound to a physical page, `variables` (register in a PC range, SMC operand). Firmware objects read through
   `peekPage` on every plane (the data side of M3 and F5).
   Done (uncommitted): `gsfirmwareobjects.h`, `cardRamPage`, WebAPI `/state/audio/gs/firmware[/profiles]`, CLI
   `gs firmware`, MCP `audio_gs_firmware[.<object>]`, Lua / Python `gs_firmware`; design §8A.9 "As built".
7. [ ] **F4 extensions.** `reply_read_without_poll`, `data_before_command`, `command_while_busy`,
   `card_reply_stall`, `stale_reply_drain`, `upload_over_running_code`, `command_rejected_in_state`; the handshake
   (M6) suppresses legal firmware-specific ones; the same flags on LW (W4).
   Done (landed d770c9b67): the host status-read hook on LLE / LW / NeoGS; handshake `ack`, `barrier`, `strobe`,
   `replyWait`, `exit`; profile `modes`, `unsolicitedReplies`; design §6.3. Left: an A/B of the status-read hook
   (P2), batched with the next .20 run.
8. [x] **Profiles for uploaded code (M5).** The `#14` upload hashed as it arrives, `#13` pushes the matching
   profile, a card reset pops to the ROM's, the decoder asks the stack top-down; an unknown upload logged with its
   address, length and digest. Profiles: GP `gscode.bin`, the kernel's `ngssd.bin`, NeoTracker `ngsldr` / `ngsdrv`
   / `neopg2`.
   Done (landed d770c9b67, 2026-11-21; uploads and the stack follow TTD since): `GSCommandLog::Uploads()` (also `uploads` in the protocol state), the profile stack with
   `extends`, upload matches that cover a prefix (the kernel's padded 768 bytes), five NedoOS profiles in
   `data/symbols/gs/profiles/`, the blobs in `testdata/software/nedoos/card-code/`. The `#1E` sector transfer with its
   `#BB` strobes (M12): the profile transfer `sectors` (design §6.3), done with V-5.
9. [x] **Dispatch view and unknown commands (M4, AU6).** The dispatch table read and compared with the ROM / upload
   baseline (patched entries named with their upload); the dispatch PC as a probe filling `card_pc` /
   `card_label`; the unknown-command set with counts (LW default branch, LLE `unknownStub`).
10. [x] **Generator (M8).** `tools/gsfirmware/profile_from_source.py`: commands from `COMTAB` / `COMTABH`, structs
    from `STRUCT … ENDS` and `EQU` blocks, symbols; the v1.05a profile regenerated equals the committed one (a test).
    Done (uncommitted, items 9-10): `dispatch` in the firmware objects read (handler, ROM baseline, `patched_by`,
    `unknown`), `highFrom`, `stock_split` in JSON; `card_pc` / `card_label` from the table at the command write (no
    per-instruction probe); `UnknownCommands()` in the log; the generator with `gs105a.profile.json` and its check.
    Design §8A.9 "As built" (items 8-10).
11. [x] **Validation scenarios V-1 to V-6 (§8A.10).** NedoOS fixtures in `testdata/software/nedoos/` (README with the
    revision), `gs_nedoos_scenarios_test.cpp` against golden logs; the `SSTAT.MCRDY` polling and host `IN (#0F)`
    accuracy checks.
    Progress (uncommitted): the test boots NedoOS on the ZX-Evo from a host folder (long tier); V-1 gstest passes on
    the classic GS against `testdata/software/nedoos/golden/v1-gstest.txt`. It found and closed: the stock test mode
    (`selects` / `leaveFrom`), the `#F4` value poll (`valuePoll`), a transfer stall counted before the card took its
    command. V-2 passes: GP plays a `.mod` on the stock firmware to its end (`v2-gp-mod.txt`: one 61902-byte block,
    `#60` every frame, the position falls, `#F3`), and Esc during the load sends `#F4` after the first 32 KB chunk
    (`v2-gp-cancel.txt`; one `command_while_busy`, as F4 names it). A 128 KB card: with the ZX-Evo's 512 KB the stock
    RAM test takes 127 frames and GP gives up on the card after 50 (gshardreset), as on such a board. Keys go in as
    PS/2 keys: NedoOS for the ZX-Evo (`PS2KBD=1`) reads the AVR's PS/2 log, not the matrix. The module needs an 8.3
    name (`afterwr2.mod`): GP does not open a longer one from its command line. V-3 passes: GP streams an 8 s MP3
    on the NeoGS (`v3-gp-mp3.txt`): both streamer uploads select `nedoos-gp-gscode`, the data is one host data
    stream block (new profile flag `hostDataStream`, design §6.3; 162970 bytes with the padding), `#01` every frame,
    12 MHz while streaming and 20 MHz after `#00` (six clock switches), timer interrupts, no anomalies (GP reads
    `#01` at once: no `card_reply_stall` in this run). `SoundChip_NeoGS::counters()` for the test. V-4 passes:
    ngsplay loads an S3M through the NeoTracker BIOS (`v4-ngsplay-s3m.txt`): the uploads to `#5100`, `#0000` and
    `#D000` select `nedoos-ngsldr`, `nedoos-neotracker`, `nedoos-neopg2`; two jumps, the barrier `#00`, the `#E8`
    header as one block, `#E9` / `#EA` until done, `#E2`, `#E3`, `#E4` on [S] in PlayCykl, no anomalies. The release
    ngsplay never sent its `#E6` status poll in 3,000 frames after the autostart, though its loop took keys. It fixed the
    NeoTracker profile (`#E2` / `#E3` take module and sample / order, `#E8` `block256`, `#EA` the new
    `block_pages`) and the decoder, which ended a `block256` only at the next command.
    N5 (uncommitted): the NeoGS sources (NedoPC SVN r193) in `materials/neogs/ngs-firmware/`; `markup_symbols.py`
    rebuilds the main ROM with asl byte for byte and writes `neogs.map`, the `gs:neogs` bundle and `NEOGS-VS-GS.md`;
    the built-in profile `neogs` (v1.05a's tables, the same handlers) with `firmwarePage` (RAM pages 0-1 on NeoGS).
    Design §6.2 "As built (N5)". V-5 passes (`v5-kernel-ngssd.txt`, P2666 + NeoGS via `GeneralSoundFitScope`): needed the P2666 decoder to pass
    the GS mailbox (`#33` / `#B3` / `#BB`, low byte) to a ZX-BUS card (read mux row 13, every write). The release
    kernel hangs on the real board as well, a NedoOS bug: its NGS SD driver (`src/kernel/ngssddrv.asm`, `ISDD3`
    installer upload and `SDWRSN2` sector write) sends with `LD BC,GSDAT` : `OUTI`, so B counts down on A15..A8, and
    `#E0B3`..`#EFB3` match the board's `#EFF7` decode (`A15..13 = 111`, `A12 = 0`, `A5 = 1`, `A3 = 0`, `A0 = 1`,
    PaE.vhd:1280). After the installer `#EFF7` holds `#8F` (RAM 0 at `#0000`), and the next ISR's call into the DOS ROM
    through `#3D2F` runs RAM and ends at `#0000`. The emulator matches the VHDL. The test boots the same kernel rebuilt
    from source with `INC B` before each `OUTI` (`testdata/machines/p2666/software/nedoos/kernel-ngsfix/`, recipe
    `.recipe/software/nedoos-build.md`, `tools/nedoos/kernel.sh`; the unpatched build equals the release in every
    code byte). The log shows the installer upload and jump (`nedoos-ngssd`), `#1E` init (`#77`), then the sector reads
    of the MBR, the boot sector, the root directory and the file, each `sector read: 512 bytes` with its `#77` status
    (M12: profile transfer `sectors`, the `#BB` strobes inside it), no anomalies; `type n:/ngssd.txt` prints the file.
    A relative `n:ngssd.txt` fails ("Wrong file"): a NedoOS process has one current directory cluster for all drives
    (`app.dircluster`), so it searched `M:/bin`'s cluster on drive N. For the `IN (#0F)` check: INSTSDD's host
    `IN (C)` of `#000F` read something other than `#FF` on the P2666 (value and source not checked yet), so it took
    NEOGSINIT.
    V-6 passes (`GSNedoOsMoonRabbit_Test.V6MoonRabbitStreamsAModFromTheNetwork`, golden `v6-moon-rabbit-mod`):
    Moon Rabbit (`mrfue`, ZX-Evo UART + GS) on the ZX-Evo with `ComPort=AT` opens a `.mod` row of its home page, a
    scripted gopher server behind the virtual network sends the module, and Moon Rabbit streams the network's bytes
    into the stock firmware: `#F3`, `#30`, `#D1`, one `module upload: 61902 bytes` over more than 100 frames, `#D2`,
    data 1, `#31`, then `#60` from the play loop (`player/mod-processor.asm`, about every other frame), no anomalies.
    The test takes the ESP's power-up lines at the ERS menu (RTS up, FIFO reset), as a terminal session before Moon
    Rabbit would: the AVR holds RTS off from its start, the flow-controlled ESP keeps "ready" / WIFI CONNECTED /
    WIFI GOT IP (38 bytes) until Moon Rabbit's init raises RTS after its FIFO reset, they flood the 16-byte FIFO while
    it sends `ATE0`, the OK is lost and `checkOkErr` waits for ever (it does not resync).
    MCRDY (`GSNedoOsGp_Test.V3GpWithMcrdyPollingPlaysAnMp3OnNeoGs`): GP rebuilt from source at d1d011f8 (byte for
    byte the release) with the polling loop its TODO says fails in UnrealSpeccy (`in a,(SSTAT): and M_MCRDY: jr z`)
    inside `noper`, padded to the delay's 19 bytes (`testdata/software/nedoos/gp-mcrdy/`: `gp.plr`, the patch; 8 bytes
    differ), plays the MP3 to its end on the NeoGS: the decoder gets its SCI setup and the whole file.
    `IN (#0F)` (`gs_hostport0f_test.cpp`): no card answers it (the NeoGS FPGA decodes only `#B3` / `#BB` / `#33`,
    `fpga/current/zxbus/zxbus.v:217-223`), the value is the machine's and the same without a card, with NeoGS and with
    the classic GS: `#FF` on the Pentagon, ZX-Evo (BaseConf, TS-Conf), ATM 2 / 2+, 128K (the floating bus in the
    border), Scorpion; `#25` on the P2666 and `#84` on the Profi, both powering up in DOS, where `#0F` is the VG93's
    status. The P2666 kernel's INSTSDD reads it so (DOS on, PC `#10D0`): its NeoGS test passes with or without a card,
    and without one the `#F3` wait ends the probe.

### Phase 5 (U1, U1a, U2, B3; design §10) — the GUI skin

12. [ ] **Main debugger** per [gui-main-debugger.md](../2026-09-28-debugger-model/gui-main-debugger.md), its
    acceptance checklist.
13. [ ] **Card debugger** per [gui-card-debugger.md](../2026-09-28-debugger-model/gui-card-debugger.md) (GS, then
    NeoGS): two Qt windows paused together, its acceptance checklists.

Owner decisions 2026-11-21: phase 5 extends the two debuggers that exist (the embedded one in unreal-qt and eCat,
debuggers/qt), not a new window system; both get each step in parallel. Both already debug the card's CPU (a tab per
CPU, registers, disassembly with the firmware's symbols, steps, exec breakpoints, TTD on the card). The steps, each in
both debuggers, each pane a view of a report every automation plane serves (the Next panes' rule: the debugger shows
what an agent reads):

- [x] **P5.1 The card zone (A5.2).** Done 2026-11-21: embedded `CardPanes` (unreal-qt/src/debugger/cardpanes.cpp, test
  `cardpanes_test.cpp`), eCat `CreateCardTabs` (nextviews.cpp, `--self-check` "card tabs"), catalog
  `devicecatalog_test.cpp`. Side tabs shown while a GS or NeoGS is fitted, as the Next tabs are: Log (the
  `gs.commands` history, decoded), Protocol (the `gs` report's `protocol`, `last_send`), Firmware
  (`/state/audio/gs/firmware`: objects, dispatch, unknown commands, the profile stack), Hardware (the `gs` report:
  MPAG, mailbox, channels, NeoGS registers), Stats (`/state/audio/gs/stats`). New device catalog entries `gs-commands`,
  `gs-firmware`, `gs-stats` (eCat reads a pane by its catalog entry, in process or over the WebAPI).
- [x] **P5.2 Whose pause and where the other CPU is (A6 S1-S3, A4).** The card strip / inside-an-instruction strip
  (MainPositionNode: the main instruction, elapsed / total T, the bus cycle), the paused-by line naming the CPU, the
  card cycle and Δ in the card's registers, the profile line (`Symbols: GS v1.05a`).
  - [x] 2026-11-22: the `gs` report's `strip` and `cpu.clock_hz` / `cpu.frame_cycle` (DeviceState::GeneralSoundStrip,
    `GeneralSoundCard::cardCpuClockHz`): "GS Z80 12 MHz · PC #0C32 · cycle 119,985 · MPAG #03 · Symbols: GS v1.05a".
    Embedded: the card line under the stop banner at every stop, with "paused by the card (gs)" and the main CPU's
    MainPositionText after a card's stop (CardPanes::StripText). eCat: the same line in the state label.
  - [x] 2026-11-22: embedded, the card CPU's T field is its cycle in the frame and Δ since the value shown before
    (`c119985 Δ12`, GeneralSoundCard::cardCycles).
  - [x] 2026-11-23: the S3 strip over the main registers (embedded: "in #8000 LD A,(HL) · T 6 of 7 + 3 T wait ·
    memory read", the card line then drops the position; eCat: the state label), eCat's card `T` row `c2015 Δ13`
    (remote: Δ within one host frame, the report has no running card total), a live card line while running
    (embedded 4 Hz, eCat 4 Hz in process / 1 Hz remote; side-effect-free reads, nothing parks). Tests
    `CardPanes_Test.TheMainStripShowsTheWaitOfTheInstruction`, `.TheCardLineIsLiveWhileTheMachineRuns`, eCat
    `--self-check`.
- [x] **P5.3 Breakpoints (A4 B1-B4, A6 S5).** The event kinds (INT, NMI, card reset, page switch) and the protocol
  kinds on the card side and the host side in both editors; card breakpoints listed with a CPU column.
  Done 2026-11-22. Embedded: the editor's Event kind (grouped Card / Protocol, card side / Protocol, host side /
  Machine, plain words, value / mask) and a CPU row for memory and port kinds; the list shows every CPU's
  breakpoints with a CPU column and filter; event hits in plain words in the stop banner, its Edit on the stopping
  CPU (test `breakpointcardevents_test.cpp`). eCat: the shim's event breakpoints per CPU (in process and over the
  WebAPI), the breakpoint list as a tree with a CPU column, an Events menu (`--self-check` "event bp", also run
  with `--connect`).
  Left: the hit's own byte in the banner (the stop carries no value: `BreakpointTriggeredPayload` /
  `Emulator::BreakpointStop` would need it; now the filter's value is named); eCat's CPU filter;
  `unreal-debugger --no-gs` still has the Pentagon config's NeoGS.
- [x] **P5.4 Run control (A4 S5/S7, A6 S6).** Step main / step other, Run to: card cycle, main T, next card
  interrupt, next host command; the card CPU by its shortcut.
  Done 2026-11-22. Core: `RunControl::CardRunTo(requester, cpu, until, value)` with `until`
  `cycle` / `int` / `command` / `main_t` (`GSCardTarget::ArmUntil`, StepKind Cycle / Int / Command; main_t a direct
  `RunTStates` run) on every plane: CLI `card_run_to`, Lua `card_run_to`, Python `emu.card_run_to`, MCP
  `control_execution` `card_run_to`, REST / WebSocket `POST /card_run_to {cpu, until, value}`; every card step's reply
  adds `card_cycle`, `main_t`, `frame`. "Step main" from a card view is the main CPU's own step (`POST /step` without
  `cpu`, `RunControl::StepIn`): no new call. Tests `GSCardRunTo_Test.*`.
  Embedded (`debuggercardrun.{h,cpp}`): the four Run-to entries in "Adv Step ▾", Step other (Alt+F11; "Step main" with
  the card in front, "Step GS" with main), Ctrl+1 / Ctrl+2 and the CPU tab bar's menu, the run's message in the stop
  line (test `debuggercardrun_test.cpp`). eCat: the shim's `debug_card_run_to` (in process and `POST /card_run_to`),
  a "Run to ▾" button, Step other, Ctrl+1 / Ctrl+2 (`--self-check` "run control", also over `--connect`).
  Found on the way: a card fitted with the machine got its firmware symbols only after a card switch
  (`Emulator::ApplySymbolBundles` now applies the cards' too), `GET /symbols/sets` ignored `cpu`, MCP `sets` had no
  list in its text.
  Left: `int` / `command` reached only in core tests (the idle NeoGS loader has interrupts off); Step other from the
  main tab steps the card live, not through the TTD history; the GUI control plane has no verbs for these.
  Ctrl+1 / Ctrl+2 vs the disassembly's marks (owner, 2026-11-22): the focused disassembly keeps Ctrl / Alt+1..5
  (`DisassemblyView::event`, ShortcutOverride); elsewhere they switch the CPU.
- [x] **P5.5 NeoGS and the rest of the slot (B3, A10).** NeoGS DMA, SD · MP3 · Flash tabs; the LW player inspector;
  the MultiSound's GS.
  Core data (2026-11-22): the `gs` report on the LW card carries `interpreter` (state idle / params / command /
  upload / reply / covox, the command, the state's byte counts, the queues, the upload's received of total from the
  module header) and `player` (position, pattern, row, tick, speed, BPM, the four channels with period, note and
  rate, the module row (P) / (S), the samples with the Unreal GS dialog's columns, the FX count); the strip adds both;
  `gs firmware` answers the v1.05a window variables the interpreter mirrors (`source: mirror`), so the Firmware pane
  works on LW unchanged. The MultiSound's GS: its `gs` report part was already the GS report; the catalog adds
  `ms-gs-commands` (history `<slot>.gs.commands`), `ms-gs-firmware`, `ms-gs-stats`; firmware and statistics take the
  card by CPU (`--cpu ms`, `?cpu=ms`, Lua / Python `cpu`, MCP `gs_cpu`; the card publishes its own statistics at its
  frame ends). Fields: [command-interface `state audio gs`](../../emulator/design/control-interfaces/command-interface.md).
  Tests `SoundChip_GSLightweight_Test.Inspector_*`, `GSModPlayer_Notes.*`,
  `MultiSoundDeviceState_Test.TheCardsGsHasTheGsReportAndTheCardPanes`.
  UI done 2026-11-22, both debuggers on the same sources: the NeoGS board, "NeoGS DMA" and "SD · MP3 · Flash" read
  the debug boards (`neogs`, `neogs_dma`, `neogs_sd`, `neogs_mp3`, `neogs_flash`) plus the `gs_stats` counters, with
  the boards' actions (SD insert image / folder, eject, save flash). Embedded: `boardpane.{h,cpp}` renders any board;
  `gslwpane.{h,cpp}` the LW "GS interpreter" (state chips, the queues of 16, the upload's progress) and "GS player"
  (song, module, channels, samples); the card panes follow the card CPU tab in front (ms in front or no slot card:
  the MultiSound's GS) — tests `CardPanes_Test.*`. eCat: the shim's `read_board` / `board_action` (in process, `GET
  /debug/boards/{id}`), the two LW tabs, the ms tabs (same rule) — `--self-check` with neogs, gs, gs-lw, multisound,
  in process and over `--connect`.
  Fixed on the way: `neogs_dma` named the selected module one off (DMA_MOD 1 ZX, 2 SD, 3 MP3); MCP
  `gs_reset_counters` takes `gs_cpu`.
  ZX-DMA trace (N6, 2026-11-23): every byte the Spectrum moves through the window with the host frame and T, the
  card cycle, the address, the value, the /WAIT T-states and dropped bytes, in a ring of its own on the NeoGS (the
  port trace's DAC fetches evicted them within half a second); report `gs-zxdma` on every plane (REST
  `state/audio/gs/zxdma?last=&since=`, CLI `gs zxdma`, Lua / Python `gs_zxdma`, MCP `audio_gs_zxdma`); "Trace ▸" on
  the NeoGS DMA pane / tab in both debuggers (Start / Stop / Clear, rows appended by `since`, double-click to the
  host PC's disassembly / the card memory). Tests `GSZxDmaTrace_Test.*`, `NeoGSZxDma.TheTrace*`,
  `CardPanes_Test.TheDmaPaneTracesTheZxDmaBytes`, eCat `--self-check` (17 real bytes, in process and `--connect`).
  Core data for MPAGEX, the banner (S4) and the wait (S3) (2026-11-23, uncommitted): `NeoGSMemory::lastMpagEx()`
  (the last #10 write that took effect, TTD fixed byte 66, a layout 4 blob from before reads 0), NeoGSStateInfo
  `mpagex` / `extendedPaging`, the `gs` report's `neogs.mpag` / `mpagex` / `extended_paging`, the `neogs` board's
  Paging group (`mpagex`, `extended_paging`, header `MPAG=#03 MPAGEX=#05` while EXPAG), the strip `MPAG #03 · MPAGEX
  #05` while EXPAG; `neogs.dma.zx` `window_start`, `window_end`, `card_address`, `next_byte`, `rom_paged`, `banner`
  ("ZX-DMA active: reads at #0000-#3FFF return card RAM @#012345; next byte #A7"), in process
  `DeviceState::ZxDmaBanner(context)`; `Z80::instrWaitTicks` / `InstructionWaitT()`, MainPosition `waitT`, the text
  `... T 17 of 16 + 1 T wait, ...` and `wait_t` in MainPositionNode. Tests `NeoGSMemory.MpagExShadow*`,
  `SoundChip_NeoGS_Ttd.TheMpagExShadowTravelsInTheBlob`, `DeviceStateGs_Test.NeoGsReportNamesMpagEx` /
  `TheClassicCardHasNoZxDmaBanner`, `DebugBoards_Test.TheNeoGSPagingGroupNamesMpagEx`,
  `Modes/NeoGSZxDma_Test.HostReadsABlockOfCardRam` (banner), `.AWriteWaitShowsInTheMainPosition`. The two
  debuggers' UI (2026-11-23): the banner over the main CPU's memory view and disassembly (Divert / Watch), the rows
  #0000-#3FFF marked "card bytes at run time" in Divert with the ROM paged (embedded and eCat, in process and
  remote); "Open media panel" on the SD card opens the media panel on slot `sd.ngs` (embedded; eCat has no media
  panel). Tests `CardPanes_Test.TheZxDmaBannerShowsWhileTheModuleDiverts`, `.TheSdCardOpensTheMediaPanelOnItsSlot`,
  eCat `--self-check`.
  Left: the trace restarted from outside (CLI / REST) past the
  panes' cursor before their next refresh loses that session's first rows; a live "+ N T wait" after a card's stop not produced in
  eCat's self-check (the field only); the disassembly's run-time marks refresh at stops only; the LW panes do not mark
  the values changed since the last stop; the card strip follows a CPU-tab switch only at the next stop; LW checked
  live only idle (an upload and a module in the Qt test); gs + ms together not run; the LW FX upload counter is not
  in the TTD state.
- [ ] **P5.6 Later (P1 / P2, on the owner's call):** the twin timeline as a canvas, the log row actions, the Send
  console, the protocol monitor, the analysis tabs (A5.2a).
  - Protocol monitor prototype, embedded debugger (2026-11-23; owner decision: a card side tab "GS monitor", not a
    floating window): `unreal-qt/src/debugger/gsmonitorpane.h`, after "GS protocol" in `CardPanes::Create`, every card
    (`Card::Any`, the MultiSound's GS through the follow). The strip (the `gs` report's protocol, status, mailbox, last
    send), the error tile (anomalies of the command log and `gs-firmware` `unknown_commands` since its Clear, the last
    anomaly with its frame, tinted), the log (`history read`, newest 200, appended after the last row shown, anomalies
    tinted, Follow / scroll up), Pause / Continue (the window's actions); log off: Turn on (history `<slot>.commands`
    mode ring). Live through the card panes' timer (200 ms) while the tab is in front of a shown window. Tests
    `CardPanes_Test.TheMonitor*`. eCat: `debuggers/qt/ecat/dialogs/debugger/gsmonitor.h`, the same tab after "GS
    protocol" in both adapters (shim `read_gs_monitor`, `gs_command_log`; remote plain GETs), 4 Hz in process, 1 Hz
    remote; self-check `gs monitor:` on every card, in process and --connect. Left: the log row actions (A5.4); rows lost when more than 200 records arrive
    between two refreshes (the history read has no since cursor).

## Design §8 (TTD positions on the card's CPU) — branch `card-positions` (2026-11-17), landed (core `3945ca4f3`, UI `a6be6598d`, card tails `61276899f`, F4 and D-3 `e97d71a26`)

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
  PC #...; main CPU PC #..., +N T into the next instruction`); Embedded's moment label shows a card position (and,
  since its processor tabs, its time group is the card's too: the item below). [TTD branches debuggers.md §8](../2026-11-08-ttd-branches/debuggers.md#8-positions-on-a-sound-cards-cpu-2026-11-18).
  Tests: `DebuggerTimeTravelCard_Test`, `unreal-debugger --self-check` (both modes).
- [x] Embedded: processor tabs (2026-11-18): Main CPU plus one per card CPU fitted (GS / NeoGS, MultiSound GS), no bar
  with the main CPU alone. The tab in front retargets the registers (card edits through `CpuView::SetRegister`), the
  stack, the memory pages (the card's windows by name), the memory view (the card's 64 KB, 16 KB at a time), the
  disassembly (`debuggercpu::CardSource`: the card's bytes, labels and breakpoints; F2 toggles the card's), the steps
  and run to cursor (`RunControl::CardStep`, also Debug menu's and the main window's keys) and the time group
  (`DebuggerTimeTravel::setCpu`: the card verbs, as eCat). A card's stop brings its tab forward once per
  `LastStop().serial`; a move in time does not. Guiplane: `debugger.select_cpu {cpu}`, `cpu` / `cpus` in the
  `debugger` state, `cpu` in the views'. Code: `unreal-qt/src/debugger/debuggercpu.cpp`. Tests:
  `DebuggerCpuTabs_Test` (unreal-qt-tests).
- [x] The card tab's open ends (2026-11-19, landed 61276899f / 065836119):
  - Labels: the card's own `LabelManager` (`DebugManager::GetLabelManager(cpu)`) in the Embedded memory view's
    tooltip and Label Manager, and in eCat's card disassembly (`Z80DisAsm` with the card's labels); no markup and no
    main CPU labels on a card's tab.
  - Step over back on the card: `TimeTravelController::ReverseStepOverCard` (one card instruction back, or past the
    whole routine when that instruction is a return that lifted the stack to the level now: a replay scan
    backwards with the card's SP at each boundary, `GSCardTarget::ScanStackLevel`, at most 1000 frames; out of
    reach the card stays on the return, `call_out_of_reach`). `reverse-step-over {cpu}` on every plane
    (CLI `ttd reverse-step-over --cpu`, Lua / Python `ttd_card_reverse_step_over`, MCP `card_reverse_step_over` and
    `reverse_step_over` with `cpu`, REST / WebSocket). Shift+F8 on the card's tab in both debuggers.
  - Step over / step out forward in the past: `TimeTravelController::StepCardForward` (a forward replay scan that
    decides each card boundary as a live card step does, `GSCardTarget::ScanStep`; the card's breakpoints end it
    first; not ended in the history: to its end, `history_end`; 1000 frames at most, `truncated`).
    `step-instruction {cpu, kind: over | out}` on every plane (CLI `ttd si-forward --cpu gs --over` / `--out`,
    Lua / Python `ttd_card_step_over` / `ttd_card_step_out`, MCP `card_step_over` / `card_step_out`). F8 /
    Ctrl+F8 on the card's tab in the past in both debuggers.
  - F7 / F8 / Ctrl+F8 at the history's end (`at_end`) run the card's live step, unrecorded (D-3, as the main CPU).
  - [x] Run to cursor (F4) on a card's tab in the past (2026-11-20). **As built:**
    `TimeTravelController::RunCardToCursor`, the same forward replay scan as the step over / out
    (`CardForwardScan`, `GSCardTarget::ScanStep` with `StepKind::To` and the cursor): the card's first boundary at
    the cursor after its position; its breakpoints end it first; not reached in the history or in `max_frames`
    (default 1000) frames: `no_hit`, nothing moved, the live machine untouched (`truncated` when the frame limit
    ended the search); at the history's end `at_end` and the debuggers run the live card run. `step-instruction
    {cpu, kind: to, pc, max_frames}` on every plane (CLI `ttd si-forward --cpu gs --to <pc> [--max-frames N]`, Lua /
    Python `ttd_card_run_to_cursor`, MCP `card_run_to_cursor` with `addr`, REST / WebSocket, OpenAPI); the reply adds
    `cursor` and `hit`. F4 on the card's tab in both debuggers.
  - [x] Moves right after a live card step at the history's end (D-3, 2026-11-20): they were refused (409) while the
    machine stood inside the card's run; now the machine leaves it first (`Emulator::LeaveCardRun`, TTD branches
    debuggers.md §7). A card step back from past the history's end lands at the history's newest card instruction
    (`history_end`).
  - Tests: `TTD_CardReverseStepOver_*`, `TTD_CardStepForward_*`, `TTD_CardRunToCursor_*` (trace oracles: the recording's own card PC / SP at
    every boundary), `TTDControl_CardPositions_Test`, `CliTTDCards_Test`, Lua, MCP and OpenAPI tests,
    `DebuggerTimeTravelCard_Test.TheCardsTabStepsOverBackAndThroughThePast`, `unreal-debugger --self-check`, live
    `test_ttd_card_oracle.py` (step over back against single steps back, step over / out against single steps
    forward). The firmware right after its boot (the classic card's RAM test, the NeoGS's command wait loop) runs
    no CALL for frames on end, so these tests give the card nested routines of its own in its RAM, interrupts off
    (`ttdtest::LoadCardRoutines`, the live script's `plant_card_routines`).

### Later

- [x] Card breakpoint conditions (2026-10-27): the card's registers, memory and pages through `IDebugTarget::Registers`
  / `Peek` / `MapAddress`; `T`, `FD`, `DOS` and labels refused; `cpu` on `bp_condition` (Lua, Python). Tests:
  `GSCardTarget_Test.ACondition*`, `AFalseConditionCountsMatchesWithoutStopping`, live `test_gs_debugger.py`.
