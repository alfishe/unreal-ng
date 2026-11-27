# TODO — RE agents vs MCP / WebAPI efficiency (2026-11-12)

> TTD index: [docs/emulator/design/debugger/time-travel-debug/README.md](../../emulator/design/debugger/time-travel-debug/README.md)

**Status:** analysis done; F3, F6, V4 done. PLAN row **#122** (T2).

## Documents
- [analysis.md](analysis.md) — every fault, slow path, missing verb and ergonomics gap the reverse-engineering agents hit, with evidence in the reverse cases repo, the master code places, and proposals (§6 summary table).

## Proposals, in priority order

### P1
- [x] F1: no silent clamp in `run_frames` / `/steps` / `run_tstates`; answers carry `frames_run`, `frame`, `t`, `stop`; MCP text from the answer (S).
  As built (2026-11-19, uncommitted on `run-exact`): one implementation in `execution::RunControl` (REST, WebSocket,
  MCP, the `run.*` actions) with limits 1..200000 frames, 1..14000000000 t-states, 1..10000000 instructions; a larger
  count is refused (400 with `count`, `max`), nothing runs. `RunOutcomeNode` / `RunOutcomeText`: `count`, `frames_run`,
  `tstates_run`, `frame`, `t`, `stop {reason done|breakpoint|stopped, ...}`, `message`; the MCP text is `message`.
  `breakpoints: true` on run_frames / run_tstates. CLI same limits and text; Lua / Python return the answer as a
  table / dict. Docs: command-interface.md "Counted runs", plane docs, OpenAPI.
- [x] F2: `RunNFrames` aims at an absolute frame boundary; split-independent runs, with a test (M).
  As built (2026-11-19, uncommitted on `run-exact`): `Emulator::RunTimeline` keeps the end position and the overshoot
  of a run that reached its target; the next `RunNFrames` / planes' `RunTStates(onTimeline)` starting exactly there
  credits the overshoot. Any other run moves the position and ends the timeline. Internal `RunTStates` callers (TTD,
  analysis) stay off it; `MachineStateTransfer` hands the source's timeline to the target (a clone continues
  alike). `core/tests/emulator/runsplit_test.cpp`: 100 == 30+70 == 1x100 == t-state slices, digest equal on
  Pentagon, 48K, General Sound and tight General Sound (a 47-T loop, so every run overshoots; a control test shows
  the old rule drifts). Tests that relied on the drift: ProfileAnalyzer InterruptsHaltMarksAndFrames (window started
  mid-frame; since 2026-11-20 the profile books the last instruction and its frame end at the frame end and at the
  run's end, so the test is back at t=0 and a run ending on a frame's first instruction shows N frames), GmxBoot LoaderSchemeFour (both machines start at the same t mod 4: HALT's 4-T phase), TTD CI
  gate bm7_file rows (1 / 4 / 18 bytes less: the saved tail is shorter).
- [x] F3: TTD dump failure reasons; chunk records over 1 GiB; partial file handling (M).
  As built (2026-11-16, uncommitted on `transfer-dump`): the cause was measured, the write journal written whole with
  the last part (8.3 KB raw per frame, 1 GiB at about 130 000 frames; SimCity's 139 382 frames refused as "disk full").
  Streams over the record limit are written as chunked twins (`S | 0x8000`, flag bit 2 continued), read back joined by
  the C++ reader, the recovery scan, `ttdlib` and the analyzer; older readers skip the twin. Writer failures carry a
  reason (`TTDWriteError`: io / no_space / too_large / layout); `ttd dump` writes `<path>.partial` and renames it,
  removes it on failure; WebAPI 409 `io` / `session`, 507 `no_space` (ENOSPC / EDQUOT only), 500 `too_large`; Lua
  `false, why`, Python `RuntimeError`. Spec: [ttd-container-format.md](../../emulator/design/debugger/time-travel-debug/ttd-container-format.md#chunked-streams-2026-11-16), `ttd.ksy`;
  tests `ttdcontainer_chunks_test.cpp`, `ttdsessionfile_chunks_test.cpp`, `AFailedDumpSaysWhyAndLeavesNoPartialFile`,
  `tools/python/ttd/tests/test_chunks.py`.
- [ ] F4: analysis of sessions recorded during RZX playback (bundle carries the RZX; PLAN #27 R5) (M).
- [x] S1: `memory_series` verb (frame range / step, several ranges, `changes_only`, base64) (M). As built
  2026-11-19: verb `memory-series` (TTDControl over `TimeTravelController::ReadMemorySeries`,
  `ttdcontrollerpastreads.cpp`): up to 16 ranges `[space:]offset:length` (or JSON objects `{space, address | offset,
  page, length}`; space `cpu` by default), 65536 bytes per frame, frames `from_frame` / `to_frame` / `step` (default the
  whole history) or an ascending `frames` list, `changes_only` (with `changed` per row), `format` base64 / hex /
  values, bounded by `limit` rows (1000, at most 100000) and `max_bytes` (1 MiB, at most 16 MiB) with
  `next_from_frame`. One forward walk: the tracked pieces' versions follow the change lists checkpoint by checkpoint
  (a full table or a jump over 64 checkpoints looks them up again), a piece unchanged since the frame before is not
  decoded again (`pieces_decoded`). `cpu` ranges read through the mapping stored with each checkpoint; without one (a
  loaded session, a thinned frame) the checkpoint is restored and the machine put back (`restored_frames`). Planes:
  `POST /ttd/memory-series` (+ socket call, OpenAPI), MCP `time_travel memory_series` (`memory_ranges`,
  `data_format`, hex by default; rows in the text), CLI `ttd memory-series` (`mser`), Lua `ttd_memory_series{}`,
  Python `emu.ttd_memory_series()`. Tests `TTDControl_PastReads_Test` (oracles: memory-at per frame and the live bytes
  at each frame start; step, list, changes_only, limits, refusals, a loaded session's restores),
  `McpTools_Test.TimeTravel_MemorySeriesScreenAtAndMoment`, `AutomationLuaPastReads_Test`,
  `OpenApiTTDPastReads_Test`, live `tools/verification/mcp/test_ttd_past_reads.py` (MCP, REST, socket, CLI, Python,
  Lua).

### P2
- [ ] F5: tape play after `$2A` / pause; play answer reports state and block; `$2A` 48K decision; last-block edge (M).
- [x] F6: `find_event` exec sentinel hits (S).
  As built (2026-11-19, uncommitted on `find-count`): reproduced. The coverage index answers "may contain" and, with
  no page, matches the same offset in any page, so false candidate frames came back; the exec path replayed only
  `limit` of them, dropped a miss silently and listed the rest as hits `{frame, 0}` with no text, so real hits were
  lost behind them and the answer changed with `limit`. Now every candidate is replayed (batch after batch of
  `QueryCoverageScan`) until `limit` hits are confirmed or the window ends; a candidate that does not run the range
  counts in `index_rejected` (the text: "N index candidate(s) did not run it"); `max_frames` bounds the replays and,
  when it runs out, `more` / `next_from_frame` point at the first candidate not replayed; without the index a window
  longer than `max_frames` also answers `more`. Test `TTDFindEvent_Test.T13_ExecIndexCandidatesThatDidNotRun`
  (the same offset #1000 in two pages; it fails on the old code with hits at `t` 0 and no text). Docs:
  [find-event design §exec](../2026-11-02-ttd-find-event/design.md), `command-interface.md` `ttd find-event`.
- [x] S2: job model / long-poll for long verbs; clients drop `sleep(5)` (M).
  As built (2026-11-22, uncommitted on `api-jobs`): one registry for every kind (`core/src/emulator/jobs/jobregistry.h`,
  the verbs `jobcontrol.h`; design [jobs.md](../../emulator/design/control-interfaces/jobs.md)). `async=true` on TTD
  `dump`, `load`, `probe-frames`, `memory-series`, `find-event`, `replay-check`, `export-clip`, `journal-build`,
  `branch-create` and analysis `build`, `trace`, `experiment`, `cover` answers 202 `{job, state, status_url}` at
  once; without it nothing changes. States queued / running / done / failed / cancelled; the TTD jobs of an instance
  run in order in one lane, its analysis jobs in another, 4 workers; `probe-frames`, `memory-series` (stop at the next
  frame, `cancelled: true` with what they got), `journal-build` and the build cancel, the others refuse (409). The
  `analysis build` job (L1Jobs) runs on the registry now (no thread of its own; `analysis.progress` /
  `analysis.ready` as before, state `queued` added). REST `GET /api/v1/jobs`, `GET|POST /api/v1/emulator/{id}/jobs`,
  `GET /api/v1/jobs/{job}?wait_ms=` (long-poll up to 60000 ms, more refused; a registry watch and an event-loop timer,
  no thread held; 256 at once), `DELETE /api/v1/jobs/{job}` or `POST .../cancel`; WebSocket: the same `call`s, topics
  `jobs.progress` (active jobs, progress throttled to 100 ms) and `jobs.done` (the last 8 ends); MCP `jobs`
  (list / status / wait / cancel / start) and `async` on `time_travel` / `analysis`; CLI `jobs`, Lua / Python
  `jobs()`. Retention 64 finished, 30 min, 64 MiB of results (the oldest lose their payload). Removing an instance
  cancels its jobs. `re_session.py` and `mcplib.py` (`wait_job`) long-poll instead of `sleep(5)`. Tests
  `JobRegistry_Test`, `JobControl_Test`, `JobsVerbs_Test`, `WebAPIJobsRoutes_Test`, `McpTools_Test.*Async*` /
  `Jobs_*`; live `tools/verification/mcp/test_jobs.py`.
- [ ] S3: server-side input script player with TTD recording (M).
- [x] V1: L1 over several sessions (M). As built (2026-11-25): `analysis build sessions=<list>` (`current` and
  `ttd dump` files, in order, at most 64) replays each session on its own clone into one `L1Builder`
  (`NextSession`: counts, routines, call graph and byte uses add up; the call stack, open loops and provenance are
  dropped at the boundary; a session's first instruction is a `resume`). `replay.sessions[]` gives each session's
  own frames and its `frame_base` on the build's one frame axis; an instruction row ends with `[session, frames,
  first_frame, last_frame]` per session it ran in. A build of one is unchanged byte for byte. No window; L3
  experiment / cover answer 409 on such a build. Planes: REST / MCP `sessions` (an array), CLI / Lua / Python
  `sessions=a.ttd,current`; `re_session build --sessions`. Tests `AnalysisL1_Test.SeveralSessionsFoldIntoOneL1`,
  `AnalysisControlL1_Test.BuildOverSeveralSessions`. Docs:
  [several sessions](../../emulator/analysis/analysis-layers.md#several-sessions).
- [x] V2: `screen_at` (shown / normal / shadow) at a past frame (S). As built 2026-11-19: verb `screen-at`
  (`TimeTravelController::ReadScreenAt`): `which` shown (the frame's final picture, composed as a seek by frame shows
  it, PNG via the Screenshotter, `area` full / screen) or normal / shadow (RAM page 5 / 7 at the frame's end, a 256 x
  192 PNG or `bitmap` / `attributes` / `raw` bytes, `encoding` base64 / hex); every answer names the screen shown at the
  frame's start and end (`#7FFD`, page), `changed_during_frame` and the flips (sampled at 312 points), the border.
  Refused: a frame that has not ended in the history, a barrier in the frame (409), shadow on a 48K. Planes: `POST
  /ttd/screen-at`, MCP `screen_at` (an image block), CLI `ttd screen-at` (`--save`), Lua, Python. Oracle: the PNG equals
  the live picture captured when the frame ended, the bytes equal the live RAM pages (a guest flipping the shown screen
  every 64 loops).
- [x] V3: machine preset in one call; `keys set` (M). As built (2026-11-25): one verb, `MachinePreset::Execute`
  (`core/src/emulator/preset/machinepreset.h`), on every plane - `POST /api/v1/emulator/preset` (+ socket call, OpenAPI), MCP
  `emulator_manage` `preset`, CLI `preset <model> [name=value ...]`, Lua `machine_preset{}`, Python `machine_preset()`. Arguments
  `model`, `ram` (zero), `cards` (`"edge.1=kempston"` or `{slot, card, options, adapter, replace}`), `features`
  (`{kempstonmouse: false}`), `muted` (true), `paused` (true), `fast_tape` (false: features `fasttape` and `turbotape`
  off), `take_control` (true), `select` (false: the selection is left alone, the owner's decision 2026-11-26), `replace` (false: created beside the others, as a create; true destroys them once the
  new machine is built). The cards are planned with the slot plug's planner (`SlotManager::PlanChanges`) inside the
  creation and the machine is built once with the planned set: no restart, nothing to poll. The machine is started and
  paused at power-on (frame 0, t 3: the reset's T-states, nothing executed); the caller holds the plane's own session (MCP: the agent's, set from the answer). What needs
  no machine is checked first (400 with the valid values); a refused plan, feature or session (409) removes the
  never-started machine: no refusal leaves an instance. The answer (fields and text) is read back from the machine:
  cards in their slots, features not at their default, muted, paused, fast_tape, the session, frame / t.
  `re_session.py` `capture` / `open` use it (`NEEDED_TOOLS` checks `emulator_manage:preset`). Docs: command-interface.md
  "Preset machine" and the plane docs. Tests `MachinePreset_Test`, `McpPreset_Test`, live
  `tools/verification/mcp/test_preset.py`.
  - `keys set` is built (2026-11-25): `DebugKeyboardManager::SetHeldKeys` (the given keys held, every other injected
    key released, a pending sequence dropped; an unknown name changes nothing) on every plane - `POST /keyboard/set`
    `{"keys": [...]}`, MCP `type_input` `set`, CLI `key set <k1+k2 | k1 k2 ...> | none`, Lua `key_set({...})`, Python
    `key_set([...])`.
- [x] E1: unknown action / value lists the valid ones, "did you mean" (S). As built (2026-11-25): one helper,
  `UnknownValueText` (`mcp-tool-utils.h`, with `EditDistance` / `NearestMatch` / `SetSchemaEnum`): "Unknown action
  'slots_plugg'. Did you mean 'slots_plug'? Valid actions: ..."; the nearest by Levenshtein distance within max(2,
  length / 3), else a name that contains the refused one. Every MCP tool but `analysis` answers with it, from the one
  list its schema `enum` is built from (`emulator_manage`, `control_execution`, `time_travel`, `type_input`,
  `mouse_input`, `joystick_input`, `memory_access`, `rzx_playback`, `companion_manage`, `capture_media`,
  `manage_symbols`, `asm_source`, `signature_info`, `streamdeck`, `audio_mixer`, `gs_analysis`, `controllers`,
  `bus_analyzer`, `device_history`, `jobs`, `gui_*`; `inspect_state` for an unknown aspect). `media` and `actions` forward
  the action to the WebAPI, which answers. `slots_unplug` is an alias of `slots_remove`. Tests:
  `McpToolUtils_Test.EditDistanceIsLevenshteinWithoutCase`, `.NearestMatchSuggestsOnlyACloseValue`,
  `.UnknownValueTextListsTheValidOnes`, `McpTools_Test.EmulatorManage_UnknownAction_ListsValidOnesAndSuggestsTheNearest`,
  `.EmulatorManage_SlotsUnplug_IsAnAliasOfSlotsRemove`, `.TimeTravel_UnknownAction_Errors`.
- [x] E2: features in MCP (`kempstonmouse` off), mouse / Kempston notes in tool descriptions, Bumpy retry (S). As built
  (2026-11-25): MCP `emulator_manage` `features` (the list as text: id, alias, on / off, mode, description) and
  `feature` (`name`, `enabled`; forwards to `PUT /feature/{name}`, which takes no mode; an unknown name answers with
  `UnknownValueText` over the route's `available_features`); `GET /features` now carries `alias`. The descriptions of
  `mouse_input`, `joystick_input` and `emulator_manage` say that the Kempston mouse is on by default and answers
  `#FADF` / `#FBDF` / `#FFDF` (standard decode A9 = 1, A5-A0 = `#1F`, outside TR-DOS), that `feature
  name=kempstonmouse enabled=false` frees those ports, and that a machine without the built-in port reads the stick
  at `#1F` only with the card (`slots_plug slot=edge.1 card=kempston`). The joystick status on every plane (`GET
  /joystick`, MCP `joystick_input status`, CLI `joystick status`, Lua / Python state) reports `port_decoded`,
  `port_source` (`board` / `card`), `kempston_mouse_feature`, `kempston_mouse_fitted` and `ports_text`
  (`DebugJoystickManager::DescribePorts`). Tests: `McpTools_Test.EmulatorManage_Features_ListsEveryFeatureAsText`,
  `.EmulatorManage_Feature_PutsEnabledAndSaysTheNewState`, `.EmulatorManage_Feature_UnknownNameListsTheValidOnes`,
  `.InputTools_DescriptionsSayHowToFreeTheKempstonMousePorts`,
  `McpJoystick_Test.Status_TextNamesThePortDecodeAndTheKempstonMouse`,
  `JoystickWeb_Test.Status_ReportsThePortDecodeAndTheKempstonMouse`; live `test_emulator_manage.py`, `test_input.py`.
  The Bumpy retry (play through the stick with the mouse off) is left to the reverse-cases work: it is outside this
  repository.

### P3
- [ ] F7: GS COMRG value 0; NeoGS / GS in SZX (verify first) (M).
- [x] F8: port history flush and frame counter (verify first) (S). Verified 2026-11-24, no change needed: the
  device histories are read while they record (memory: under the history's lock; disk: flushed every 100 ms or 256
  records), a record carries the emulator's frame counter, the one TTD uses. A snapshot load restarts that counter
  for the whole machine and ends the TTD session; the records before and after it share one buffer.
- [x] F9: L1 `root` at a snapshot's first PC (verify first) (S). Verified present 2026-11-24 (the replay's first
  instruction always got `root`), fixed: entry kind `resume` unless the first PC is the reset vector. Test
  `AnalysisL1_Test.CallGraphTailJump`.
- [ ] S4: history / trace filters, event-only trace (M). Verified 2026-11-24, partly there: the machine port trace
  (`/profiler/porttrace`) is event-only (frame, PC) with record-time filters (port, device, direction, PC range,
  value) on WebAPI, CLI, Lua, Python. Record-time filters of the device histories built 2026-11-26: `filter` in the
  history config on every plane (`side=host out=true`, `port=#B3,#BB pc=#8000-#9FFF`), the records left out counted
  and neither kept nor streamed (`historyfilter.h`, docs/emulator/design/debugger/device-history.md). The 1.7 GB
  history of the case was a capture with a stream and no filter, not a ring. Left: the filter on the MCP `porttrace`
  action, the instruction access trace.
- [x] V4: counting breakpoints with per-hit frames (with PLAN #6) (M).
  As built (2026-11-19, uncommitted on `find-count`): a breakpoint that only counts (`stop: false`, CLI `--count`)
  records its hits in a bounded `BreakpointHitLog` (`hit_log` N, default 1000, 0 none, at most 100000;
  `hit_log_keep` first / last): hit number, frame, T, PC, address, value, kind; a card CPU adds its own clock
  (`cpu_time`, `DebugTargetHitClock` from `GSCardTarget`). Written by the manager's own count sink at tap id 255, held
  in its slot only while a record takes hits: the walk is unchanged, a count-only breakpoint with no record or a full
  record of the first N calls nothing (the drop follows from the count and the policy). A/B `BM_BreakpointFrame_*`
  over 40 interleaved rounds: off / unarmed / armed and counting with a full record within noise (paired +-2 %);
  `--log-keep last` (a record written on every hit) +22 % on a whole-ROM breakpoint. Read without pausing (a spin lock per record / per 256 copied): monitoring
  verb `breakpoint_hits`, WebAPI `GET /breakpoints/{bp_id}/hits?cpu=&from=&limit=` (WebSocket `call`), CLI
  `bphits <id>`, Lua / Python `bp_hit_log`, MCP `control_execution` `bp_hits`; the hit reset empties it (Lua / Python
  `bp_reset_hits` take `cpu` now). Tests: `BreakpointHitLog_Test.*` (oracle: the counting run's frame / T / PC equal
  a stop-on-every-hit run's, across a frame boundary), `GSCardTarget_Test.ACountingBreakpointRecordsTheCardsMoments`,
  `tools/verification/mcp/test_breakpoint_hits.py`. Docs: [conditional-breakpoints](../../features/conditional-breakpoints.md#when-each-hit-came-the-per-hit-record).
  Qt (2026-11-20, uncommitted on `bp-hitlog-ui`): the breakpoint editor's "Record hits" (size) and "keep" (first /
  newest) under Count only, the core's defaults, an edit shows the breakpoint's own; the breakpoint list's counting
  rows expand to the record's summary and its first / newest 200 hits (hit, frame, T, PC, address, value, kind, card
  time), polled at 4 Hz through `GetHitLog` only while open, the records read only when the counts moved. Tests:
  `BreakpointHits_Test.*` (unreal-qt-tests).
- [x] V5: L1 first / last frame per instruction; reader-PC filter (S). Verified 2026-11-24, partly there: first /
  last frame per routine, byte, block copy, branch and replay. Per instruction 2026-11-24: `instructions[]` rows
  end with `first_frame, last_frame` (`[z80, page, code, class, count, tstates, smc, first_frame, last_frame]`,
  the recording's frames). Reader filter 2026-11-25: `analysis build
  ignore_readers=5B00-5BFF,...` (Z80 ranges of a depacker): the data reads of instructions there set no flag, reader
  or count on the bytes; their writes and code are folded as ever. Test `AnalysisL1_Test.IgnoredReadersLeaveNoReads`.
- [ ] V6: GS firmware state view (M).
- [x] V7: `--port-base auto` and a ports file (S). As built (2026-11-25, uncommitted on `re-backlog2`): unreal-qt
  had no port option (six `UNREAL_*_PORT` variables set by each script); it takes `--port-base <N | auto>` now and
  sets them for itself before automation starts (WebAPI N, CLI +100, MCP +200, GDB +300, DeZog +400, ZRCP +500).
  `auto`: the first base of 20000, 20600, ... 32000 whose six ports all bind (a real bind on all interfaces, then
  closed), the search starting at a base taken from the process id so instances started together do not race for
  one; none free = stderr and exit code 2. `--ports-file <path>` writes `{"port_base", "webapi", "cli", "mcp",
  "gdb", "dezog", "zrcp", "pid"}` by temp file + rename when the ports are decided. `mcplib.launch(binary, 'auto')`
  (`run_all.py --port-base auto`) uses both and gives `process.ports`. Tests: `PortPlan_Test.*` (core-tests). Docs:
  [environment-variables](../../emulator/environment-variables.md#all-six-ports-at-once---port-base-and---ports-file).
  Left: `re_session.py` and the other launchers still take a number.
- [x] E3: `frame` in `/registers` and run answers (S). Run answers in `c3ab886b3`; 2026-11-24: `special.frame` next
  to `special.t` in `GET /registers` (and the `regs` of `/debug/snapshot`), the MCP register text ends with
  "frame N T t". Test `DebugSnapshot_Test.RegistersCarryEveryGroup`.
- [x] E4: `memory_at` states the moment it read (S). As built 2026-11-19: memory-at adds `moment` (`frame_start`,
  `inside_frame`, `now`), `at_tinframe` (at a frame start: where the checkpoint's CPU stood past the boundary,
  `TimeTravelController::CheckpointMoment`) and `moment_text` ("the start of frame F, before its first instruction");
  the MCP text, the CLI dump header and the tool description say it; memory-series rows carry the same. Found on
  the way: the store files a write that the boundary-crossing instruction made after the boundary under frame F, so
  memory-at at F lacks it while a seek to F (and the live machine) has it; the text says so, the bytes are unchanged.
- [x] E5: `capture.json` script start frame (S). 2026-11-24: `re_session capture` writes `script_start_frame` (the
  emulator / TTD frame of the play's frame 0) and, for `--inputs`, `scripts`: each script's path, text and its own
  `start_frame`.
- [x] E6: key script error explains the separator (S). As built (2026-11-25): the script with spaces is
  `mm_session.py`'s (zx-reaper's format); its word "W+O" went to `type_input press` as one key name. Now `+` joins
  keys held together wherever a key text is taken: `DebugKeyboardManager::SplitKeyChord` ("w+o", "caps+1"; the key
  `+` stays a key, "ss++" is symbol shift and `+`), used by the WebAPI `keyboard/tap` / `press` / `release` (`key`)
  and `combo` (an item of `keys`), so by MCP `type_input`, and by the `input.key` action for an item of `keys` (its
  text form split at `+` already). The refusal says how keys are written (`DescribeUnknownKey`): "Unknown key 'W O':
  a key is one name (a, enter, caps); keys held together are joined with '+' (w+o); a space or a comma does not
  separate keys". CLI `key press` / Python / Lua key verbs still take one name. Tests:
  `DebugKeyboardManager_test.SplitKeyChord_PlusJoinsKeysHeldTogether`, `.SplitKeyChord_TheKeyPlusStaysAKey`,
  `.DescribeUnknownKey_SaysHowKeysAreWritten`,
  `HostActions_Test.AKeyTapsHoldsAndReleasesAndAnUnknownNameIsABadRequest`.
- [ ] E7: RZX seek discoverability (S).
- [x] E8: capability check before a session (S). 2026-11-24: `mcplib.launch(..., needs=)` compares the binary's
  `tools/list` with the tools the caller uses and refuses at launch ("has no MCP tool analysis: build current
  master"); `re_session` passes its set. A capabilities route stays with PLAN #9.
