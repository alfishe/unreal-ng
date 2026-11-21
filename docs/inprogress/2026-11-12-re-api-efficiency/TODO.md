# TODO — RE agents vs MCP / WebAPI efficiency (2026-11-12)

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
- [ ] S2: job model / long-poll for long verbs; clients drop `sleep(5)` (M).
- [ ] S3: server-side input script player with TTD recording (M).
- [ ] V1: L1 over several sessions (M).
- [x] V2: `screen_at` (shown / normal / shadow) at a past frame (S). As built 2026-11-19: verb `screen-at`
  (`TimeTravelController::ReadScreenAt`): `which` shown (the frame's final picture, composed as a seek by frame shows
  it, PNG via the Screenshotter, `area` full / screen) or normal / shadow (RAM page 5 / 7 at the frame's end, a 256 x
  192 PNG or `bitmap` / `attributes` / `raw` bytes, `encoding` base64 / hex); every answer names the screen shown at the
  frame's start and end (`#7FFD`, page), `changed_during_frame` and the flips (sampled at 312 points), the border.
  Refused: a frame that has not ended in the history, a barrier in the frame (409), shadow on a 48K. Planes: `POST
  /ttd/screen-at`, MCP `screen_at` (an image block), CLI `ttd screen-at` (`--save`), Lua, Python. Oracle: the PNG equals
  the live picture captured when the frame ended, the bytes equal the live RAM pages (a guest flipping the shown screen
  every 64 loops).
- [ ] V3: lab machine preset in one call; `keys set` (M).
- [ ] E1: unknown action / value lists the valid ones, "did you mean" (S).
- [ ] E2: features in MCP (`kempstonmouse` off), mouse / Kempston notes in tool descriptions, Bumpy retry (S).

### P3
- [ ] F7: GS COMRG value 0; NeoGS / GS in SZX (verify first) (M).
- [ ] F8: port history flush and frame counter (verify first) (S).
- [ ] F9: L1 `root` at a snapshot's first PC (verify first) (S).
- [ ] S4: history / trace filters, event-only trace (M).
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
- [ ] V5: L1 first / last frame per instruction; reader-PC filter (S).
- [ ] V6: GS firmware state view (M).
- [ ] V7: `--port-base auto` and a ports file (S).
- [ ] E3: `frame` in `/registers` and run answers (S).
- [x] E4: `memory_at` states the moment it read (S). As built 2026-11-19: memory-at adds `moment` (`frame_start`,
  `inside_frame`, `now`), `at_tinframe` (at a frame start: where the checkpoint's CPU stood past the boundary,
  `TimeTravelController::CheckpointMoment`) and `moment_text` ("the start of frame F, before its first instruction");
  the MCP text, the CLI dump header and the tool description say it; memory-series rows carry the same. Found on
  the way: the store files a write that the boundary-crossing instruction made after the boundary under frame F, so
  memory-at at F lacks it while a seek to F (and the live machine) has it; the text says so, the bytes are unchanged.
- [ ] E5: `capture.json` script start frame (S).
- [ ] E6: key script error explains the separator (S).
- [ ] E7: RZX seek discoverability (S).
- [ ] E8: capability check before a session (S).
