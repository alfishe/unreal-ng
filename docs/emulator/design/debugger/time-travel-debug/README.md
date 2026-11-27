# Time-travel debugging (TTD): index

One page for everything about time travel in this repository: the designs, the plan folders and their status, the
automation planes, the code, the tests and tools, and the open tasks. Read this first instead of scanning the tree.
Updated 2026-11-24.

## What TTD is here

- TTD records the running machine (a checkpoint per frame, memory as versioned pieces, device states, inputs, bus
  journals) so any recorded moment can be reached again exactly: seek, step back, reverse continue, "who wrote this",
  and the agent queries built on top (cause chain, find event, explain frame, diff, backtrace, timeline).
- One implementation: the engine `ttd::TimeTravelEngine` (TTD v2), driven by `ttd::TimeTravelController`. Every plane
  (CLI, WebAPI / WebSocket, MCP, Lua, Python, GDB, DeZog, Qt, eCat) goes through the `ttd::TTDControl` verbs
  (`core/src/debugger/ttd/ttdcontrol.h`).
- v1 (`TimeTravelManager`, its file format and reader) was deleted on 2026-10-30 (`d4d7bc34a`); the v2 migration was
  closed on 2026-10-31. Documents written before that describe v1 and say so.
- Branches: acting in the past forks a new line of history instead of cutting the future
  ([TTD branches](../../../../inprogress/2026-11-08-ttd-branches/README.md), closed 2026-11-16).
- Card CPUs: seek, steps both ways and reverse continue on a sound card's CPU (GS, NeoGS, MultiSound GS), landed
  2026-11-18/20 with the [GS debugger](../../../../inprogress/2026-09-27-gs-debugger/TODO.md).
- Key decisions: [engine-decisions.md](../../../../inprogress/2026-09-25-ttd-v2-migration/engine-decisions.md)
  (D1-D43; D13 a frame is its end, D22 machine groups, D42 a load ends the session),
  [requirements.md](../../../../inprogress/2026-09-25-ttd-v2-migration/requirements.md),
  [target-architecture.md](../../../../inprogress/2026-09-25-ttd-v2-migration/target-architecture.md),
  [d23-device-keys.md](../../../../inprogress/2026-09-25-ttd-v2-migration/d23-device-keys.md) (device ids frozen up
  to 65), [branches architecture](../../../../inprogress/2026-11-08-ttd-branches/architecture.md).

## Design docs (this folder)

| File | What it answers | Status |
|------|-----------------|--------|
| [time-travel-debugging-tdd.md](time-travel-debugging-tdd.md) | The TTD design; [§0](time-travel-debugging-tdd.md#0-as-built-2026-10-31) says what the engine built | current (as-built notes 2026-10-31) |
| [ttd-use-cases.md](ttd-use-cases.md) | What you can do with a `.ttd` recording, on every surface | current (costs in §9 measured on v1) |
| [ttd-container-format.md](ttd-container-format.md) | The session file: numbered streams, chunks; schema in `core/src/debugger/ttd/ttd.ksy` | as-built section current; section 2 is v1 history |
| [ttd-port-read-journal.md](ttd-port-read-journal.md) | Port journals: replay without the outside world, "when did the program see ..." | built; v1 file layout is history |
| [gdb-reverse-debugging-tdd.md](gdb-reverse-debugging-tdd.md) | GDB RSP stub and the reverse commands (`bs`, `bc`) | built (`core/automation/gdb/`); design names are v1-era |
| [time-travel-ux.md](time-travel-ux.md) | The Qt timeline panel, marks, search views, personas | design 2026-07-19; built panel is `ttdwidget.*` |
| [overhead-and-gating.md](overhead-and-gating.md) | Costs and runtime gates | history (measured on v1) |
| [implementation-plan.md](implementation-plan.md) | Order of the v1 and GDB work | history (v1) |
| [ttd-v1-architecture-and-format.md](ttd-v1-architecture-and-format.md) | TTD v1 as built, its file format | history (v1 deleted 2026-10-30) |

Related design docs next door: [device-history.md](../device-history.md) (per-device histories, not TTD),
[breakpoints-and-traces.md](../breakpoints-and-traces.md).

## Plan and history folders (docs/inprogress)

"Read" is the canonical file for the status. PLAN rows are in [PLAN.md](../../../../inprogress/PLAN.md).

| Folder | Topic | Status | Read | PLAN | Landed |
|--------|-------|--------|------|------|--------|
| [2026-07-19-time-travel](../../../../inprogress/2026-07-19-time-travel/README.md) | v1 engine sprints, GDB plan | historical (v1 deleted) | [DONE.md](../../../../inprogress/2026-07-19-time-travel/DONE.md) | - | - |
| [2026-08-20-ttd-reverse-search-index](../../../../inprogress/2026-08-20-ttd-reverse-search-index/README.md) | coverage index, reverse search pruning, journal compression | done 2026-08-21 (v1 era) | [DONE.md](../../../../inprogress/2026-08-20-ttd-reverse-search-index/DONE.md); leftovers README §5 | #20 (retired) | - |
| [2026-09-10-ttd-registry-integration](../../../../inprogress/2026-09-10-ttd-registry-integration/TODO.md) | peripheral registry fixes | closed 2026-10-30 (phases 2-5 went with v1) | [TODO.md](../../../../inprogress/2026-09-10-ttd-registry-integration/TODO.md) | - | - |
| [2026-09-23-ttd-qt-toolbar-widget](../../../../inprogress/2026-09-23-ttd-qt-toolbar-widget/DONE.md) | Qt toolbar toggle and `TtdWidget` | done | [DONE.md](../../../../inprogress/2026-09-23-ttd-qt-toolbar-widget/DONE.md) | #36 (retired) | `b4afd9a3` |
| [2026-09-25-ttd-v2-migration](../../../../inprogress/2026-09-25-ttd-v2-migration/README.md) | the engine, phases 0-6, v1 removal | done 2026-10-31 | [DONE.md](../../../../inprogress/2026-09-25-ttd-v2-migration/DONE.md) (README = roadmap; an untracked `TODO.md` there is a stale copy) | #40 | `d4d7bc34a` (v1 deleted) |
| [2026-09-28-ttd-positioning-and-display](../../../../inprogress/2026-09-28-ttd-positioning-and-display/DONE.md) | one path per responsibility for seek and display | done 2026-09-28 (built in v1) | [DONE.md](../../../../inprogress/2026-09-28-ttd-positioning-and-display/DONE.md) | #40 | - |
| [2026-10-03-qt-ttd-heap-crash](../../../../inprogress/2026-10-03-qt-ttd-heap-crash/DONE.md) | heap crash in the toolbar tooltip, published session info | done (v1-era names) | [DONE.md](../../../../inprogress/2026-10-03-qt-ttd-heap-crash/DONE.md) | - | `1de1b07bc` (Qt) |
| [2026-10-28-ttd-analysis-feedback](../../../../inprogress/2026-10-28-ttd-analysis-feedback/README.md) | query costs from game analysis, in-place replay | done 2026-10-31 | [README.md](../../../../inprogress/2026-10-28-ttd-analysis-feedback/README.md), [phase-2-plan.md](../../../../inprogress/2026-10-28-ttd-analysis-feedback/phase-2-plan.md) | - | `c35d12fd8` (phase 1) |
| [2026-10-31-ttd-timeline-summary](../../../../inprogress/2026-10-31-ttd-timeline-summary/README.md) | TD-5 `GET /ttd/timeline` | done 2026-11-01 | [README.md](../../../../inprogress/2026-10-31-ttd-timeline-summary/README.md) | #7 | `b0615018a` |
| [2026-11-02-ttd-cause-chain](../../../../inprogress/2026-11-02-ttd-cause-chain/README.md) | where a value came from | done 2026-11-02 | [README.md](../../../../inprogress/2026-11-02-ttd-cause-chain/README.md) | #112 | - |
| [2026-11-02-ttd-find-event](../../../../inprogress/2026-11-02-ttd-find-event/README.md) | the moment something happened | done 2026-11-02 | [README.md](../../../../inprogress/2026-11-02-ttd-find-event/README.md) | #113 | - |
| [2026-11-02-ttd-explain-frame](../../../../inprogress/2026-11-02-ttd-explain-frame/README.md) | what one frame did | done 2026-11-02 | [README.md](../../../../inprogress/2026-11-02-ttd-explain-frame/README.md) | #114 | - |
| [2026-11-03-ttd-diff-moments](../../../../inprogress/2026-11-03-ttd-diff-moments/README.md) | diff of two moments | done 2026-11-03 | [README.md](../../../../inprogress/2026-11-03-ttd-diff-moments/README.md) | #115 | - |
| [2026-11-03-ttd-agent-bookmarks](../../../../inprogress/2026-11-03-ttd-agent-bookmarks/README.md) | bookmarks with notes, context, export | done 2026-11-03 | [README.md](../../../../inprogress/2026-11-03-ttd-agent-bookmarks/README.md) | #116 | - |
| [2026-11-05-ttd-replay-check](../../../../inprogress/2026-11-05-ttd-replay-check/README.md) | replay reproduces the recording | done 2026-11-06 | [README.md](../../../../inprogress/2026-11-05-ttd-replay-check/README.md) | - | `124fb512f` |
| [2026-11-07-ttd-backtrace](../../../../inprogress/2026-11-07-ttd-backtrace/README.md) | the call stack at a moment | done 2026-11-07 | [README.md](../../../../inprogress/2026-11-07-ttd-backtrace/README.md) | #119 | - |
| [2026-11-08-ttd-branches](../../../../inprogress/2026-11-08-ttd-branches/README.md) | branched history, two tiers, surfaces, `ttdtool` | closed 2026-11-16 (P4 not planned) | [DONE.md](../../../../inprogress/2026-11-08-ttd-branches/DONE.md) | #76 (W1) | `e0ac62a53`, `912e8dd68`, `47ebc9dd3` |
| [2026-11-12-re-api-efficiency](../../../../inprogress/2026-11-12-re-api-efficiency/TODO.md) | reverse-engineering agents vs MCP / WebAPI (TTD items among others) | open | [TODO.md](../../../../inprogress/2026-11-12-re-api-efficiency/TODO.md), [analysis.md](../../../../inprogress/2026-11-12-re-api-efficiency/analysis.md) | #122 | F1-F3, S1, V2, E4 (`e918537f5`), F6, V4; S2 (jobs: TTD long verbs with `async`, [jobs.md](../../control-interfaces/jobs.md)) |
| [2026-09-27-gs-debugger](../../../../inprogress/2026-09-27-gs-debugger/TODO.md) | card CPU debugger; TTD positions on the card's CPU (§8) | open (TTD part landed, D3 done 2026-11-20) | [TODO.md](../../../../inprogress/2026-09-27-gs-debugger/TODO.md), [design.md §8](../../../../inprogress/2026-09-27-gs-debugger/design.md#8-ttd-d1-d4) | #45 | `3945ca4f3` / `7dce0959a`, `a6be6598d`, `61276899f`, `e97d71a26` |
| [2026-09-29-rzx-replay](../../../../inprogress/2026-09-29-rzx-replay/TODO.md) | RZX playback, recording, TTD interop | open (R0-R3 done; R4, R5 left) | [TODO.md](../../../../inprogress/2026-09-29-rzx-replay/TODO.md) | #27 | R0-R3 2026-09-29 |
| [2026-09-29-model-what-if](../../../../inprogress/2026-09-29-model-what-if/TODO.md) | machine state transfer, branches (W1), forks to other models | open (W3 postponed) | [TODO.md](../../../../inprogress/2026-09-29-model-what-if/TODO.md) | #76 | W0 `6f5759f3`, `570ca3ce`, `7ddbef11` |

TTD files in other folders:

| File | What | Status |
|------|------|--------|
| [turbosound-fm/ttd-fm-state-gap.md](../../../../inprogress/2026-09-10-turbosound-fm/ttd-fm-state-gap.md) | TSFM FM synth state in TTD | fixed 2026-09-23 |
| [moonsound/...-opl4-ttd-integration-tdd.md](../../../../inprogress/2026-09-13-moonsound/2026-09-13-0217-opl4-ttd-integration-tdd.md) | MoonSound in TTD (tier A blob, tier B wave SRAM) | tier A built |
| [automation-triage-gaps/designs/ttd-coverage-query-design.md](../../../../inprogress/2026-09-14-automation-triage-gaps/designs/ttd-coverage-query-design.md) | TD-7 coverage index query | not planned (see open tasks) |
| [automation-triage-gaps/designs/ttd-timeline-summary-design.md](../../../../inprogress/2026-09-14-automation-triage-gaps/designs/ttd-timeline-summary-design.md) | TD-5 first design | superseded by 2026-10-31-ttd-timeline-summary |
| [automation-triage-gaps/ttd-coverage-evaluation.md](../../../../inprogress/2026-09-14-automation-triage-gaps/ttd-coverage-evaluation.md) | TTD for AI reverse-engineering, evaluation | history (2026-09-15, v1) |
| [debugger-family/rzx-ttd.md](../../../../inprogress/2026-09-28-debugger-family/rzx-ttd.md) | RZX vs TTD, bridging them | research; RZX playback built since |
| [debugger-family/ttd-offline-analysis.md](../../../../inprogress/2026-09-28-debugger-family/ttd-offline-analysis.md) | record cheap, analyze offline (O-*, OR-*) | proposal; partly built by the analysis layers |
| [sprinter/s7-ttd-outcome.md](../../../../inprogress/2026-09-28-sprinter/s7-ttd-outcome.md) | TTD on the Sprinter | done 2026-10-02 |
| [storage-manager/integration-ttd-snapshots.md](../../../../inprogress/2026-09-28-storage-manager/integration-ttd-snapshots.md) | time travel and snapshots / media | reviewed 2026-09-28 |
| [model-what-if/reference-branched-ttd-ui.md](../../../../inprogress/2026-09-29-model-what-if/reference-branched-ttd-ui.md) | a branched TTD UI in another emulator | reference |
| [zx-next/design-ttd.md](../../../../inprogress/2026-10-07-zx-next/design-ttd.md) | TTD on the ZX Next | done with the Next |
| [controller-manager/debug-and-ttd.md](../../../../inprogress/2026-10-17-controller-manager/debug-and-ttd.md) | game controllers as debugger and TTD surfaces | design |
| [tsconf-vdac4/ttd-design.md](../../../../inprogress/2026-11-15-tsconf-vdac4/ttd-design.md) | TTD on TSL-VDAC4: the ESP CPU's keyframes + journal, the FT812, replay points | built 2026-11-23, Quake II §11, open items §10 |
| [tsconf-vdac4/quake2.md §8.1](../../../../inprogress/2026-11-15-tsconf-vdac4/quake2.md#81-time-travel-and-session-files) | TTD of the Quake II core (`q2`): bomzh state, 8 MB SDRAM region at the ESP's keyframes, the core rebuilt by the journal replay ([ttd-design.md §11](../../../../inprogress/2026-11-15-tsconf-vdac4/ttd-design.md#11-the-quake-ii-core-between-keyframes-2026-11-27)), live numbers | built 2026-11-27; demo 58 MB/min |

## Automation planes

- Command interface (the spec all planes follow):
  [§8 Time-Travel Debugging](../../control-interfaces/command-interface.md#8-time-travel-debugging-ttd), with
  [card CPUs](../../control-interfaces/command-interface.md#ttd-on-a-sound-cards-cpu),
  [position check](../../control-interfaces/command-interface.md#ttd-position-check),
  [session rules](../../control-interfaces/command-interface.md#ttd-session-rules),
  [status fields](../../control-interfaces/command-interface.md#status-fields).
- CLI: [cli-interface.md](../../control-interfaces/cli-interface.md#time-travel-debugging-ttd-commands)
  (`core/automation/cli/src/commands/cli-processor-ttd.cpp`).
- WebAPI: [webapi-interface.md](../../control-interfaces/webapi-interface.md#time-travel-debugging)
  (`core/automation/webapi/src/api/ttd_api.cpp`; OpenAPI `core/automation/webapi/src/openapi/openapi_ttd.inc`,
  `openapi_ttdfile.inc`, `openapi_ttdfile_schemas.inc`).
- WebSocket: [websocket-interface.md §2](../../control-interfaces/websocket-interface.md#2-calls) (TTD routes as calls,
  the `ttd.position` and `ttd.branches` topics).
- MCP: the `time_travel` tool, [docs/features/mcp/README.md](../../../../features/mcp/README.md#time-travel-debugging)
  (`core/automation/mcp/src/mcp-tools.cpp`).
- Lua: [lua-interface.md](../../control-interfaces/lua-interface.md#time-travel-debugging); Python:
  [python-interface.md](../../control-interfaces/python-interface.md#time-travel-debugging).
- GDB reverse commands: [gdb-protocol.md](../../control-interfaces/gdb-protocol.md#reverse-execution-time-travel-debugging);
  UDB: [udb-protocol.md](../../control-interfaces/udb-protocol.md#time-travel-commands); overview:
  [control-interfaces README](../../control-interfaces/README.md#time-travel-debugging-ttd).
- Debuggers: [debuggers/qt/README.md](../../../../../debuggers/qt/README.md#time-travel) (eCat time group).
- Recipes: [ttd-recording.md](../../../../../.recipe/analysis/ttd-recording.md),
  [ttd-write-journal.md](../../../../../.recipe/analysis/ttd-write-journal.md),
  [ttd-reverse-debugging.md](../../../../../.recipe/analysis/ttd-reverse-debugging.md),
  [ttd-gameplay-automation.md](../../../../../.recipe/analysis/ttd-gameplay-automation.md),
  [ttd-visual-inspection.md](../../../../../.recipe/analysis/ttd-visual-inspection.md),
  [sprinter-ttd.md](../../../../../.recipe/analysis/sprinter-ttd.md),
  [articles/bug-hunt-ttd.md](../../../../../.recipe/articles/bug-hunt-ttd.md),
  [articles/game-reverse-pipeline.md](../../../../../.recipe/articles/game-reverse-pipeline.md).

## Code map

`core/src/debugger/ttd/` (about 200 files):

| Role | Files |
|------|-------|
| Controller (the one entry point) | `timetravelcontroller.{h,cpp}`; one file per verb group: `ttdcontroller{backtrace,bookmarks,branches,cardpositions,causechain,clipexport,diffmoments,explainframe,findevent,pastreads,replaycheck,sessionfile,timeline}.cpp` |
| Verbs for every plane | `ttdcontrol.{h,cpp}` (`TTDControl`) |
| Session plumbing | `ttdsession.h`, `ttdsessiontypes.h`, `ttdsessionport.*` (coordinator sink), `ttdsessionfacts.*`, `ttdrecordingfolders.*`, `ttdcursorcell.*` (published cursor), `timetravelframecache.h`, `timelinemarks.*`, `ttddisplayparticipant.h`, `ttdscreenlayout.h` |
| Query results | `ttdbacktrace.h`, `ttdcausechain.*`, `ttddiffmoments.h`, `ttdexplainframe.h`, `ttdfindevent.h`, `ttdreplaycheck.h`, `ttdtimeline.*`, `ttdbookmarks.*`, `ttdbookmarkexport.*` |
| Engine | `timetravelengine.{h,cpp}`; `engine/`: piece store, payload store, arena, device table, event log, media journal, write index, branch table, container, session file, recording writer, stream registry, time, config fingerprint |
| Checkpoints, pages, codec | `ttdcheckpoint.*`, `ttdcodecpagestore.*`, `ttdcompression.h`, `ttddirtytracker.*`, `ttdphyspage.h`, `machinestatehash.*`, `ttdconfigcapture.*`, `ttdfileinfo.*`, `ttddumpformat.h` |
| Hooks, journals, search | `timetravelhooks.*`, `ttdexternalevents.*`, `ttdinputjournal.*`, `ttdinputapply.*`, `ttdv1events.*` (input journal and events into the engine's log), `ttdwritejournal.*`, `ttdportjournal.*`, `ttdportsearch.*`, `ttdportdescribe.*`, `ttdportclassifier.h`, `ttdcoverageindex.*`, `ttdprobe.*` |
| Device serializers | `ttdperipheralregistry.*`, `ttdmachineperipherals.*`, `ttdperipheralmask.h`, `ttdserializable.h`, chips at the top level (`ttdds12887`, `ttdpit8253`, `ttdppi8255`, `ttdusart8251`, `ttdwd1793context`); per platform in `atm/`, `gmx/`, `ide/`, `network/`, `next/`, `p2666/`, `plus3/`, `profi/`, `scorpion/`, `sprinter/`, `tsconf/` |
| Benchmark harness | `bench/` (`ttdbench.*`, `ttdtimelinefixture.*`) |
| File format | `ttd.ksy` (Kaitai, schema 2, `legacy_id` device table, branch and holder streams, a segment file's piece tables and seek index; checked against the fixtures by `tools/verification/ttd-analyzer/scripts/check_ksy.py`) |

Outside it:

- Core: `core/src/emulator/actions/` (`branchactions.*`, stepping and host actions behind Qt, Stream Deck, controllers),
  `core/src/emulator/slots/slotttd.cpp`, `core/src/emulator/slots/cards/multisound/multisoundttd.*`,
  `core/src/emulator/rzx/rzxttdstate.*`, `core/src/emulator/coordinator/machinepolicy.cpp` (session effects),
  `core/src/emulator/zxpoly/zxpolygroup.cpp`; the analysis layers replay recordings in
  `core/src/debugger/analyzers/analysis/`.
- Automation: `core/automation/cli/src/commands/cli-processor-ttd.cpp`, `cli-ttd-cards.h`;
  `core/automation/webapi/src/api/ttd_api.cpp`; `core/automation/mcp/src/mcp-tools.cpp` (`time_travel`);
  `core/automation/lua/src/emulator/lua_emulator.h`; `core/automation/python/src/emulator/python_emulator.h`;
  `core/automation/gdb/src/gdbserver.cpp`; `core/automation/dezog/src/dezogdebugadapter.cpp`.
- Qt: `unreal-qt/src/widgets/ttdwidget.*` (panel, branch lanes), `ttdbranchcommands.*`,
  `unreal-qt/src/emulator/ttdsessionobserver.*`, `unreal-qt/src/debugger/debuggertimetravel.*`,
  `porteventsdialog.*`.
- eCat: `debuggers/qt/ecat/dialogs/debugger/ttdcontrols.*`, `debuggers/qt/adapter/unrealmachine.cpp`. The TUI
  debuggers have no TTD (frozen).
- Stream Deck: `companions/streamdeck/` actions and the TTD strip (`data/streamdeck/themes/unrealng/ttd_strip.png`).

Tests:

- `core/tests/debugger/ttd/` (about 160 files): `engine/` (engine units: piece store, container, session file and
  fuzz, branch table, segments); `timetravelcontroller_*_test.cpp` (controller behaviour: branches, history limit, two
  tiers, journals, display, RZX playback, teardown, corpus, black box); `ttdcontrol_*_test.cpp` (verbs); one test per
  query (`ttdbacktrace`, `ttdcausechain`, `ttddiffmoments`, `ttdexplainframe`, `ttdfindevent`, `ttdreplaycheck`,
  `ttdtimeline*`, `ttdagentbookmarks`, `ttdclipexport`, `ttdcardpositions`); per-device serializers (platform
  subfolders and `ttd*serializer_test.cpp`); contracts and robustness (`ttdautomationcontract`,
  `ttdstatecompleteness`, `ttdmodelstatecontract`, `ttddivergencecorpus`, `ttdlifecyclestress`,
  `ttdserializationrobustness`).
- Helpers: `core/tests/_helpers/ttd*` (synthetic sessions, divergence harness, slot cards, card bench).
- Elsewhere: `core/tests/automation/` (`cli-ttd-cards_test.cpp`, `webapi/openapi-ttd-*_test.cpp`,
  `mcp-tools-test.cpp`, `dezog/`), `core/tests/gdb/gdbtarget_ttd_test.cpp`, per-machine and per-device
  `*ttd*_test.cpp` under `core/tests/emulator/`; Qt: `unreal-qt/tests/qt/ttdwidget_test.cpp`,
  `ttdwidget_branches_test.cpp`, `ttdsessionobserver_test.cpp`, `debuggertimetravel_test.cpp`.
- Benchmarks: `core/benchmarks/debugger/ttd/` (capture, frame overhead, matrix, reverse, serialize, coverage, port
  journal, timeline, branches, analysis queries).
- Live MCP: [tools/verification/mcp/](../../../../../tools/verification/mcp/README.md) `test_time_travel.py`,
  `test_ttd_past_reads.py`, `test_ttd_card_positions.py`, `test_ttd_card_oracle.py`; TTD calls also in
  `test_surfaces.py`, `test_ws_surface.py`.

Tools, POCs and data:

- [tools/python/ttd](../../../../../tools/python/ttd/README.md): `ttdtool.py` and `ttdlib` (info, check, lineage,
  prune, join, recover a recording folder).
- [ttd-analyzer](../../../../../tools/verification/ttd-analyzer/README.md) (reads schema 2 files),
  [ttd-bench](../../../../../tools/verification/ttd-bench/README.md) (benchmark matrix compare),
  [ttd-surface-contract](../../../../../tools/verification/ttd-surface-contract/README.md) (same answers on every
  plane), [ttd-scrubber](../../../../../tools/verification/ttd-scrubber/README.md) (PySide6 scrubber over WebAPI).
- POCs ([index](../../../../../tools/poc/README.md)):
  [010](../../../../../tools/poc/010-ttd-compression/README.md) codec (retired),
  [011](../../../../../tools/poc/011-ttd-v2-capture-analysis/README.md) v2 capture analysis (retired),
  [027](../../../../../tools/poc/027-ttd-gui/README.md) GUI (retired),
  [038](../../../../../tools/poc/038-ttd-replay-query-engine/README.md) replay query engine on clones (go, not built),
  [040](../../../../../tools/poc/040-ttd-inplace-replay-profile/README.md) in-place replay profile,
  [048](../../../../../tools/poc/048-ttd-async-compress/README.md) async compression (no-go); TTD is also used by
  019 (zxdlss capture) and 035 (debugger GUI bench).
- Fixtures: [testdata/ttd](../../../../../testdata/ttd/README.md) (`engine/`, `v2/`, `branches/`, `port-journals/`,
  `bench/`), `testdata/sound/multisound/ttd/`.

## Open tasks

Collected 2026-11-20 from every plan doc; statuses brought up to date 2026-11-27 (open without RZX: S3 alone). Not TTD, kept because they sit next to it: F8, E3. Pure non-TTD items of
the same TODO (V6 and the rest) are left out.

Open, by priority:

| Id | Title | Priority | Status | Source |
|----|-------|----------|--------|--------|
| F4 | Analysis of sessions recorded during RZX playback (bundle carries the RZX; PLAN #27 R5) | P1 | open | [re-api TODO](../../../../inprogress/2026-11-12-re-api-efficiency/TODO.md) |
| S2 | Job model / long-poll for long verbs (TTD dump, analysis build, memory series) | P2 | done 2026-11-23 (`c86776e1c`) | [re-api TODO](../../../../inprogress/2026-11-12-re-api-efficiency/TODO.md) |
| S3 | Server-side input script player with TTD recording | P2 | open, waits for the owner's choice of the runner (Lua + C or WASM) | [re-api TODO](../../../../inprogress/2026-11-12-re-api-efficiency/TODO.md) |
| V1 | L1 analysis over several TTD sessions | P2 | done 2026-11-25 (`96aea1d42`: build option `sessions`, one frame axis) | [re-api TODO](../../../../inprogress/2026-11-12-re-api-efficiency/TODO.md) |
| R4 | RZX recording (writer: `IN` values, fetch counts, frame ends) | T2 (PLAN #27) | open, nobody on it | [rzx TODO](../../../../inprogress/2026-09-29-rzx-replay/TODO.md) |
| F9 | L1 marks a false root at a snapshot's first PC | P3 | done 2026-11-24 (`resume` kind) | [re-api TODO](../../../../inprogress/2026-11-12-re-api-efficiency/TODO.md) |
| S4 | History / trace filters, event-only trace | P3 | done 2026-11-27 (`c2395b749` record-time filter of the device histories, `a11ec13f1` MCP port trace filter and the access trace filter) | [re-api TODO](../../../../inprogress/2026-11-12-re-api-efficiency/TODO.md) |
| V5 | L1 first / last frame per instruction; reader-PC filter | P3 | done (`2683ddc69` 2026-11-24 the frames per instruction, `96aea1d42` 2026-11-25 `ignore_readers`) | [re-api TODO](../../../../inprogress/2026-11-12-re-api-efficiency/TODO.md) |
| E5 | `capture.json` records the script start frame | P3 | done 2026-11-24 | [re-api TODO](../../../../inprogress/2026-11-12-re-api-efficiency/TODO.md) |
| E7 | RZX seek discoverability | P3 | open | [re-api TODO](../../../../inprogress/2026-11-12-re-api-efficiency/TODO.md) |
| E8 | Capability check before a session | P3 | done 2026-11-24 (tool list checked at launch) | [re-api TODO](../../../../inprogress/2026-11-12-re-api-efficiency/TODO.md) |
| F8 | Port history flush and frame counter reset on snapshot load (next to TTD) | P3 | verified 2026-11-24, no change needed | [re-api TODO](../../../../inprogress/2026-11-12-re-api-efficiency/TODO.md) |
| E3 | Frame counter in `/registers` (next to TTD; run answers done in `c3ab886b3`, `special.frame` 2026-11-24) | P3 | done | [re-api TODO](../../../../inprogress/2026-11-12-re-api-efficiency/TODO.md) |
| P1.S10 | Real-use sessions over days; quiet-machine A/B of C1d | - | done 2026-11-21 (A/B within noise; 100-min soak, three bugs fixed; days of real use stay the owner's) | [v2 DONE, Phase 5](../../../../inprogress/2026-09-25-ttd-v2-migration/DONE.md#phase-5--switch-the-emulator-to-the-engine) |
| P1.S10 seek | Long-session seek and timeline (the soak's 10-23 s seeks at 239k frames) | - | done 2026-11-21 (pause + seek 11 s -> 19 ms, timeline 16 s -> 0.37 s at 240k frames; a lost frame after a resume fixed; both closed by P1.S10 seek tails) | [v2 DONE, Phase 5](../../../../inprogress/2026-09-25-ttd-v2-migration/DONE.md#phase-5--switch-the-emulator-to-the-engine) |
| P1.S10 recheck | TSL seek-check divergences, ATM3 determinism | `ttdsoak_test.cpp`, `ttddeterminism_test.cpp` | done 2026-11-22 (12-min TSL and PENTAGON soaks: 16,169 seek checks, 0 divergences, 2,856 loaded compares, 0 mismatches; the old divergences were the read-back bug; ATM3 counts vary by the host-clock RTC anchor and power-on RAM, by design; a frame replay that slowed the clock ran three frames on, fixed) | [v2 DONE, Phase 5](../../../../inprogress/2026-09-25-ttd-v2-migration/DONE.md#phase-5--switch-the-emulator-to-the-engine) |
| P1.S10 seek tails | Seeks into evicted history, a pause and the timeline with coverage, coverage capture | `ttdseekscaling_test.cpp`, `ttdpiecestore_test.cpp`, `ttdportjournal_test.cpp` | done 2026-11-22 (240k frames: an evicted seek p50 / p95 11.4 / 43 -> 4.1 / 5.5 ms by a prefix read back, a window seek 1.74 -> 1.2 ms, the timeline with coverage 13.3 s -> 0.29 s, a first pause with coverage 413 -> 9 ms, coverage capture 19.3 -> 13.0 us a frame; left: PR-6 1 ms is 1.2 ms at 240k, the floor a full decode plus one frame) | [v2 DONE, Phase 5](../../../../inprogress/2026-09-25-ttd-v2-migration/DONE.md#phase-5--switch-the-emulator-to-the-engine) |
| P1.S10 seek A/B | A/B of the seek tails against their parent; stepping back and the timeline with coverage | `frame_profile_benchmark.cpp`, `ttdseekscaling_test.cpp`, `ttdcoverageindex_test.cpp` | done 2026-11-22 (recording with TTD off / on / full inside the A/A; stepping back 1.33 -> 0.93 ms by a decoded copy of memory, the timeline with coverage 287 -> 167 ms at 240k by run unions of coverage blocks; two attempts reverted; left: evicted p95 6.3 ms, PR-6 1.3 ms at 240k) | [v2 DONE, Phase 5](../../../../inprogress/2026-09-25-ttd-v2-migration/DONE.md#phase-5--switch-the-emulator-to-the-engine) |
| P1.S10 seek format | Seek to the requirements at 240k frames, in memory and evicted (the format may change) | `ttdseekscaling_test.cpp`, `ttdsegmentwindow_test.cpp`, `ttdpiecestore_test.cpp` | done 2026-11-24 (240k frames: a window seek p50 1.31 -> 0.70 ms, an evicted seek p50 4.7 -> 1.3-2.4 ms, p99 1.8-2.5 ms at moderate load; a seek index and piece tables in segment files, skip-stride chains, baselines reuse unchanged versions; the folder +6 %) | [v2 DONE, Phase 5](../../../../inprogress/2026-09-25-ttd-v2-migration/DONE.md#phase-5--switch-the-emulator-to-the-engine) |
| RSI-cond-ecat | eCat Run Back passes breakpoint conditions to `reverse-continue` (`condition`; the Qt Run Back does) | - | done 2026-11-21 (embedded and remote; `--self-check`) | [RSI README §5](../../../../inprogress/2026-08-20-ttd-reverse-search-index/README.md#5-not-done-and-why) |
| TD-5 file | Timeline of a `.ttd` file without loading it (option `file`, `branch`) | - | done 2026-11-21 | [timeline design §8](../../../../inprogress/2026-10-31-ttd-timeline-summary/design.md#8-a-ttd-file-without-loading-it) |
| TD-5 blobs | Device-state blobs in the timeline (`state` entries, `states_every_frame`) | - | done 2026-11-21 | [timeline design §1](../../../../inprogress/2026-10-31-ttd-timeline-summary/design.md#1-what-the-recording-already-holds) |
| DM limit | Diff of two moments: name the fields that changed inside a device | - | done 2026-11-21 (`TTDDescribeFields`, counters apart) | [diff plan](../../../../inprogress/2026-11-03-ttd-diff-moments/plan.md#device-fields-2026-11-21-the-dm-limit) |
| PD-Q2 | "Same T-state in the next frame" as an explicit operation | - | closed 2026-11-21: `seek` frame + 1 with the T wanted, one call on every plane | [positioning design §7](../../../../inprogress/2026-09-28-ttd-positioning-and-display/design.md#7-open-questions) |
| R5 | RZX and TTD interop (player state blob, TTD while playing, import, export) | low | open | [rzx TODO](../../../../inprogress/2026-09-29-rzx-replay/TODO.md) |

Blocked or deferred (waiting for a trigger):

| Id | Title | Status | Source |
|----|-------|--------|--------|
| D22 | ZX-Poly machine groups over a shared piece store: one group seek, one file | deferred by the owner until the rest is done | [v2 DONE, Later](../../../../inprogress/2026-09-25-ttd-v2-migration/DONE.md#later) |
| P3.S3a | CPU table, clock map (`ClockChange` facts), per-checkpoint CPU counter residuals | deferred (owner 2026-10-03); card positions landed without it, no current user | [v2 DONE, Phase 3](../../../../inprogress/2026-09-25-ttd-v2-migration/DONE.md#phase-3--everything-a-replay-needs) |
| W3 | Forks to other models (`RestoreInto`, lockstep fork runner), `switch_model` with state; W4 depends on it | blocked (owner: no linked sessions, 2026-10-05) | [what-if TODO](../../../../inprogress/2026-09-29-model-what-if/TODO.md) |
| B-incr | Incremental consecutive seeks (memory known across operations) | deferred until a measurement asks | [phase-2-plan](../../../../inprogress/2026-10-28-ttd-analysis-feedback/phase-2-plan.md#b-in-place-replay-poc-040-causes-1-4-6) |
| RZ-N1/N2 | RZX benchmark gate (zero cost off, overhead while playing) | waits for a quiet machine | [rzx TODO](../../../../inprogress/2026-09-29-rzx-replay/TODO.md) |
| RZX corpus | Real RZX recordings for +3 and Scorpion | none found yet | [rzx TODO](../../../../inprogress/2026-09-29-rzx-replay/TODO.md) |

Not planned (owner decisions; do not propose again):

| Id | Title | Decision | Source |
|----|-------|----------|--------|
| P4 | Media in the stream, version tree, write-through following the active moment | owner 2026-11-16 | [branches DONE](../../../../inprogress/2026-11-08-ttd-branches/DONE.md#not-planned-owner-2026-11-16) |
| Qt-rest | Main-window status bar, lane filters, edits in the past from the panel | owner 2026-11-16 | [branches DONE](../../../../inprogress/2026-11-08-ttd-branches/DONE.md#not-planned-owner-2026-11-16) |
| tdx-update | Rename / note / tag / pin of a folder session written to `branches.tdx` | owner 2026-11-16 | [branches DONE](../../../../inprogress/2026-11-08-ttd-branches/DONE.md#not-planned-owner-2026-11-16) |
| Dial | Surface Dial binding for branches | owner 2026-11-16 | [branches DONE](../../../../inprogress/2026-11-08-ttd-branches/DONE.md#not-planned-owner-2026-11-16) |
| S-3 | Stream Deck long touch on a fork tick | not built, work closed by the owner | [branches surfaces.md](../../../../inprogress/2026-11-08-ttd-branches/surfaces.md) |
| out-of-scope | Forks to other machines, loads as branch points (D42), merging branches | out of scope from the start | [branches DONE](../../../../inprogress/2026-11-08-ttd-branches/DONE.md#not-planned-owner-2026-11-16) |
| P3.S2a | `rzx/seek` through the engine, `RzxKeyframeStore` removed | owner 2026-11-17 (`7b623ce69`) | [v2 DONE, Phase 3](../../../../inprogress/2026-09-25-ttd-v2-migration/DONE.md#phase-3--everything-a-replay-needs) |
| P3.S2b | Session replayable without the RZX file | owner 2026-11-17 | [v2 DONE, Phase 3](../../../../inprogress/2026-09-25-ttd-v2-migration/DONE.md#phase-3--everything-a-replay-needs) |
| P5.S3c | Offer to open a crashed recording at the next start | owner 2026-11-17 | [v2 DONE, Phase 5](../../../../inprogress/2026-09-25-ttd-v2-migration/DONE.md#phase-5--switch-the-emulator-to-the-engine) |
| W2 | History across snapshot / tape / disk loads | D42, owner 2026-10-06, final | [what-if TODO](../../../../inprogress/2026-09-29-model-what-if/TODO.md) |
| Clone pool | Replay query engine on cloned machines (POC 038) | owner 2026-10-28, until a measurement asks | [phase-2-plan](../../../../inprogress/2026-10-28-ttd-analysis-feedback/phase-2-plan.md#why-no-clone-pool-now) |
| TD-7 / G-5 | Dedicated coverage index query endpoint | deferred, PLAN #20 retired; coverage-scan / coverage-rare cover it | [RSI DONE](../../../../inprogress/2026-08-20-ttd-reverse-search-index/DONE.md) |
| Registry 2-5 | Peripheral registry phases 2-5 | went with v1 (closed 2026-10-30) | [registry TODO](../../../../inprogress/2026-09-10-ttd-registry-integration/TODO.md) |
| C-mem | Separate TTD-only memory path | left out by the plan (no explicit owner veto) | [phase-2-plan](../../../../inprogress/2026-10-28-ttd-analysis-feedback/phase-2-plan.md#c-halt-idle-while-ttd-records-and-replays) |
| EF limit | Explain frame across frames (call graph) | design scope | [explain design §7](../../../../inprogress/2026-11-02-ttd-explain-frame/design.md#7-what-it-does-not-do) |

Done since the index was collected: D3, the GS command log follows TTD moves (2026-11-20,
[gs-debugger design §6.3](../../../../inprogress/2026-09-27-gs-debugger/design.md#63-the-command-log-f1-f1c-f3-f4-w1-w4),
the generic part in [device-history.md](../device-history.md)). P4.S6, the Kaitai-generated parser run against `ttd.ksy` (2026-11-20: 38 fixtures, 0 disagree after four schema fixes; repeatable with `tools/verification/ttd-analyzer/scripts/check_ksy.py`, which skips without the compiler; [v2 DONE, Phase 4](../../../../inprogress/2026-09-25-ttd-v2-migration/DONE.md#phase-4--the-session-file)). RSI-wide, RSI-bpset and RSI-cond (2026-11-21): wide and wrapping ranges are pruned with an exact offset set (a wrapping read 148.98 to 3.49 ms), reverse continue decodes a frame once for all its PCs, `reverse-continue` takes a `condition` on every plane, and read watchpoints already used read coverage ([RSI README §5](../../../../inprogress/2026-08-20-ttd-reverse-search-index/README.md#5-not-done-and-why)); eCat Run Back passes the conditions too (RSI-cond-ecat). P1.S10 (2026-11-21): the quiet-machine A/B of C1d is within noise, and a 100-minute soak on four models over WebAPI and MCP found and fixed three bugs: a save after eviction with a branch, a resume at an evicted trunk end losing a segment's file (and cutting the trunk on a failed fork), and the Qt panel lifting a script's history limit ([v2 DONE, Phase 5](../../../../inprogress/2026-09-25-ttd-v2-migration/DONE.md#phase-5--switch-the-emulator-to-the-engine)). Its follow-up the same day removed the costs that grew with the session (a pause rewriting the write journal and the coverage index, the timeline reading evicted segments back and decoding every frame): at 240k frames a pause and seek 11 s -> 19 ms, the timeline 16 s -> 0.37 s, seeks flat at 1.5-1.7 ms p50, and a frame lost after a resume in a recording folder fixed (requirements.md PR-7 measured note). P1.S10 recheck (2026-11-22): 12-minute soaks on TSL and PENTAGON (folder, 64 MiB, branches, trunk-end resumes, dump and load) found no seek-check divergence, so the 100-minute soak's TSL divergences are taken for its read-back bug; the ATM3 timeline counts vary with the host-clock RTC anchor and random power-on RAM, both recorded, and every model is the same twice with them fixed; a frame replay now stops at the frame's end when the guest slows its clock inside it (an ATM3 boot ran three frames on, `TTDDeterminism_Test`) ([v2 DONE, Phase 5](../../../../inprogress/2026-09-25-ttd-v2-migration/DONE.md#phase-5--switch-the-emulator-to-the-engine)). DM limit (2026-11-21): the diff of two moments names the fields that changed inside a device (`TTDSerializable::TTDDescribeFields`; AY, TurboSound, TSFM, WD1793 and its drives, +D, Covox) and lists free-running counters apart, `only counters` for a device whose counters alone moved ([diff plan](../../../../inprogress/2026-11-03-ttd-diff-moments/plan.md#device-fields-2026-11-21-the-dm-limit)).

Checked and closed (stale notes; nothing left to do): branches on resume / edit in the past, a file continuing
across a resume, branch operations in the UI (all done by TTD branches, ticked in the v2 DONE.md); the GS debugger's
§8 heading (all landed); the 2026-07-19 index "Not started" rows (v1 history); "forks here" for eCat remote
(`forks_here` is in the `branches` answer); PD-Q1 frame start vs end (answered by D13); PD-Q2 the same T-state in the next frame (2026-11-21: one `seek` with frame + 1 and the T wanted, [positioning design §7](../../../../inprogress/2026-09-28-ttd-positioning-and-display/design.md#7-open-questions)); P7-info, `ttdtool info` without memory / disk tiers (2026-11-20: the tiers are runtime state since P3, no file records them; the live split is in `ttd status`, [branches tools.md](../../../../inprogress/2026-11-08-ttd-branches/tools.md)). RSI-models (2026-11-21): the v1 blockers are gone, the bound comes from the model table, the lost warning for a page past it is back as a counter, and a sweep covers every creatable model. Not worth it: RSI-zone, zone maps and a blocked columnar journal (the worst case they would remove is a full journal scan, about 50 ms for the whole 64 MB ring; port searches take under 1 ms), and the rest of RSI-bpset (offset aliasing past a few PCs: 16 PCs 286 against 332 ms) ([RSI README §5](../../../../inprogress/2026-08-20-ttd-reverse-search-index/README.md#5-not-done-and-why)).

## Keeping this index current

Any new TTD plan folder, design doc, recipe, tool, POC or major code area adds a line here, with a back-link to this
page near the top of its main file. A landed TTD task updates its row (status, commit) here and in the open-task
tables; an owner "not planned" moves the item to that table.
