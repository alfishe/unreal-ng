# Phase 6 inventory: what still depends on TTD v1

What has to change when `TimeTravelManager` (v1) is deleted - not before 2026-10-22 (owner decision 2026-10-08: the
engine runs as the default for two weeks first). Taken 2026-10-09 on master `2c61f1b7d` with the preparation done
the same day (below), so the deletion itself is mechanical. [TODO](TODO.md), Phase 6.

## 1. Done ahead (2026-10-09)

- **21 test files moved to the engine** (208 tests): they ran against v1 only because their fixture took
  `pTimeTravelManager`; on the controller (the same API) they pass unchanged and left `ttdv1tests.h`:
  `timetravelframecache_test.cpp`, `timetravelhooks_test.cpp`, `timetravelmanager_hostwrites_test.cpp`, `timetravelmanager_resumesave_test.cpp`, `timetravelmanager_screenshot_test.cpp`, `timetravelmanager_servicestate_test.cpp`, `ttdclipexport_test.cpp`, `ttdcoverageintegration_test.cpp`, `ttdcoveragequery_test.cpp`, `ttdfeaturegating_test.cpp`, `ttdinputapply_test.cpp`, `ttdinputjournal_test.cpp`, `ttdinputplayback_test.cpp`, `ttdmodelstatecontract_test.cpp`, `ttdprobesafety_test.cpp`, `ttdreplaymode_test.cpp`, `ttdserializationrobustness_test.cpp`, `ttdsessionlifecycle_test.cpp`, `ttdstatecompleteness_test.cpp`, `ttdthinning_test.cpp`, `ttdwritejournal_test.cpp`.
- **Leftover includes of `timetravelmanager.h`** removed where nothing of v1 was used: tape, WD1793, the debug
  keyboard, the WebAPI memory routes, the CLI memory commands, DeZog's forward declaration.
- **zxdlss-render / zxdlss-bench** (`tools/verification/zxdlss`) read session files only through v1, so a recording
  made by the application today (the engine's format, since 2026-10-05) did not load. They now open an engine file
  with the engine's controller and a v1 file with v1 (`WithTimeTravelSession`); frames 150-157 of
  `demo_across-the-edge-second` render byte-identical from both files.

## 2. Production code

| Where | What depends on v1 | On deletion |
|---|---|---|
| `emulator/emulator.cpp` (Init / Release) | creates `pTimeTravelManager` next to the controller; `SetDefaultTimeTravelBackend(V1)` selects it | create the controller only; drop the backend switch (`Emulator::TimeTravelBackend`) |
| `emulator/emulatorcontext.h` | `pTimeTravelManager` | remove the field |
| `debugger/ttd/ttdsession.h` | `HasTimeTravelSession`, `WithTimeTravelSession`, `TTDSessionRef` pick either | keep the API, one implementation (or call the controller directly) |
| `debugger/ttd/ttdcontrol.{h,cpp}` | `TTDControlBackend<TimeTravelManager>`, `StatusBody(const TimeTravelManager*)`, `AddWriteJournal(..., TimeTravelManager&)` | one backend; the template can stay or go |
| `debugger/ttd/ttdclipexport.cpp` | v1's `VisitComposedFrames` / `ExportClip` (the controller has its own, `ttdcontrollerclipexport.cpp`) | delete |
| `debugger/ttd/timetravelmanager.{h,cpp}` | v1 itself (about 9,800 lines) | delete |
| `debugger/ttd/bench/ttdbench.cpp`, `ttdv1feeder.{h,cpp}` | the benchmark's v1 backend; the feeder reads a v1 file through v1 and feeds the engine | see §5 (the v1 file reader) |
| Comments naming v1 in about 30 headers (`ttdcheckpoint.h`, `ttdserializable.h`, `ttddumpformat.h`, ...) | text only | the TDD truth pass |

v1-only types to check when deleting: `TTDCheckpoint`'s v1 fields (`portReadCursor` / `portWriteCursor`, the page
references), the v1 page store (`ttdcodecpagestore`), the v1 dirty tracker, `ttddumpformat.h` (the v1 file
format), `ttdv1events.h` (the v1 event feed), `ttdwritejournal` as v1's ring.

## 3. Tests still on v1 (`core/tests/_helpers/ttdv1tests.h`, 18 files)

### 3.1 Oracles: the engine checked against v1 (10 files)

`timetravelcontroller_test.cpp`, `timetravelcontroller_corpus_test.cpp`, `timetravelmanager_engineseek_test.cpp`,
`timetravelmanager_shadow_test.cpp`, `timetravelmanager_shadowfile_test.cpp`, `timetravelmanager_shadowmodels_test.cpp`,
`timetravelmanager_shadowregions_test.cpp`, `bench/ttdv1feeder_test.cpp`, `engine/ttdsessionfile_test.cpp` (sessions
fed from v1 files), `ttdcontrol_test.cpp` (both backends). On deletion: drop the v1 side; keep the assertions that hold
on the engine alone (seek landing, find-last answers, file round trips) against recorded expectations or the engine
fixture corpus (`testdata/ttd/engine/`).

### 3.2 v1's own internals and file format (8 files): delete with v1

They do not compile against the controller - v1's page store (`GetPageStore`), its key frame every 50 frames
(`kKeyFrameInterval`), its DebuggerLive mode, its file format: `ttdmanager_test.cpp`, `ttdresume_test.cpp`,
`ttdformatv2_test.cpp`, `ttdfullrestore_test.cpp`, `ttdseekexhaustive_test.cpp`, `ttddumpformat_test.cpp`,
`ttdcorpus_test.cpp`, `timetravelmanager_recordingguard_test.cpp`. Before deleting, look through `ttdresume`,
`ttdseekexhaustive` and `ttdfullrestore` for behavior tests worth porting (the engine has equivalents for most:
`ttdresume` → `TTDControl_Test` resume, `ttdseekexhaustive` → the engine's seek tests).

### 3.3 Behavior tests written to v1's rules: moved to the engine (2026-10-09, 28 files)

All 28 run against the engine's controller now (344 tests) and left `ttdv1tests.h`; 18 files stay there (§3.1, §3.2).
Their expectations follow the engine's rules:
- a seek, step, reverse query or find-last while recording pauses the recording and runs (D8) - v1 refused;
- tape and disk markers are crossed (the journals carry the data); a debugger edit without its bytes, a reset and an
  unclassified marker still stop a seek - the barrier tests use those;
- a frame's final picture is its end (D13): a seek to `{f, 0}` shows frame f-1's;
- the write journal is the engine's write index (`_helpers/ttdwriterecords.h`: the ring holds one frame);
- the history limit drops whole segments (decision 41): up to an eighth of the window more is kept;
- one checkpoint per frame: a test that calls `OnFrameBoundary` by hand moves the frame counter first;
- device states and RAM at a checkpoint come from the engine (`DeviceState`, `RestoreRegion`), not v1's blobs and pages;
- the two port-journal fixtures read in the engine's format (`testdata/ttd/port-journals/engine/`, converted once by
  `ConvertV1Session`, which now writes the controller's facts; the file searches answer `expected.json` exactly).
Tests of v1's file layout (cut sections, flipped header flags) are gone; the container's integrity has its own tests.

Found on the way and fixed in the engine's controller:
- **A seek stopped at a barrier named no marker** (`blockingMarker` empty: kind unknown, no reason), live and loaded
  - so WebAPI / CLI / MCP reported an anonymous `blocking_marker`. It is the recorded marker now.
- **Memory after a session ends:** the piece store kept its 64 KB arena chunk and the session's tables their
  capacity, and status reported about 80 KB with no session. A store nobody shares is cleared when the session ends;
  without a session the session's memory is 0 (as B7 states).
- **`SetReplaySource(nullptr)`** left the controller without a replay engine (the next seek crashed); null is the
  session's own engine now.

## 4. Benchmarks and tools

| Where | v1 use | On deletion |
|---|---|---|
| `core/benchmarks/debugger/ttd/ttd_{coverage,frame_overhead,portjournal,reverse,serialize}_benchmark.cpp` | drive `pTimeTravelManager` | move to the controller (or `TTDControl`); their baselines change (v1 → engine numbers) |
| `ttdbench` (`UNREAL_TTD_BENCH_ENGINE=v1/all`), `testdata/ttd/bench/v1-*.json`, `v1-ci-gate.txt` | v1 as a backend and the reference figures | drop the v1 backend; the engine baselines (`engine-phase*-full.json`) stay |
| `tools/verification/ttd-analyzer` (Python: `ttd_format.py`, `record_port_journal_fixtures.py`) | parses v1 files; records fixtures with v1 | keep the parser for old files or retire it; record fixtures with the engine |
| `tools/verification/zxdlss` | the v1 branch for v1 files (§1) | drop the branch, or keep v1 files readable through a converter |
| `tools/poc/010-ttd-gui`, `01-ttd-compression`, `010-ttd-compression`, `011-ttd-v2-capture-analysis` | proofs of concept on v1 data | retire (Phase 6) |

## 5. Owner decisions (2026-10-09)

Nothing is kept for v1: every artifact moves to the engine ("v1 goes, everything is on v2").

1. **No v1 file reader after the deletion.** The v1 fixtures are converted to the engine's format once, now, while v1
   can still read them (`testdata/ttd/*.ttd`, `testdata/ttd/port-journals/`, `testdata/machines/*/ttd/`; seven are
   in `testdata/ttd/engine/` already). zxdlss's v1 branch, the v1 feeder and the Python analyzer's v1 parser go
   with v1.
2. **The oracle tests lose their v1 side.** What they assert of the engine stays, against the converted fixtures or
   recorded expectations; a check that only compares with v1 goes.
