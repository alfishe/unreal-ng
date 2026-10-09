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

## 3. Tests still on v1 (`core/tests/_helpers/ttdv1tests.h`, 46 files)

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

### 3.3 Behavior tests whose expectations are v1's (28 files): adapt to the engine

On the controller they compile; these fail, mostly because the engine behaves differently by design:
- a seek while recording pauses the recording and runs (D8) - v1 refused it;
- an external-event marker (tape, disk write) is no barrier: the engine's journals carry the data (Phase 3) - v1
  stopped there;
- a frame alone is the frame's end on the engine (D13);
- tests reaching into v1's checkpoint blobs (`peripheralBlobs`) or v1's port journal gate.

| File | Tests | On the engine |
|---|---|---|
| `timetravelmanager_display_test.cpp` | 8 | 5 pass, 3 fail |
| `timetravelmanager_historylimit_test.cpp` | 3 | 1 pass, 2 fail |
| `timetravelmanager_journalcapacity_test.cpp` | 1 | 0 pass, 1 fail |
| `timetravelmanager_journalsegments_test.cpp` | 9 | 8 pass; `BuildingTheWholeSessionEqualsTheRecordedJournal` crashes: it reads v1's write ring (`GetWriteJournal()`), which the controller does not keep with the journal off (built records go to the engine's write index) |
| `timetravelmanager_portjournal_test.cpp` | 18 | 11 pass, 7 fail |
| `timetravelmanager_publishedinfo_test.cpp` | 5 | 4 pass, 1 fail |
| `timetravelmanager_regeneratewrites_test.cpp` | 2 | 0 pass, 2 fail |
| `timetravelmanager_rzxplayback_test.cpp` | 5 | 1 pass, 3 fail |
| `timetravelmanager_savedinputs_test.cpp` | 16 | crashes (an exception or abort on the first test) |
| `timetravelmanager_turboclock_test.cpp` | 5 | 0 pass, 1 fail |
| `ttdautomationcontract_test.cpp` | 20 | 18 pass, 2 fail |
| `ttdbookmarks_test.cpp` | 26 | 25 pass, 1 fail |
| `ttddevicereplay_test.cpp` | 3 | 3 pass, 1 fail |
| `ttddivergencecorpus_test.cpp` | 6 | 2 pass, 4 fail |
| `ttdexternalevents_test.cpp` | 41 | 39 pass, 2 fail |
| `ttdexternaleventshooks_test.cpp` | 12 | 11 pass, 1 fail |
| `ttdfileinfo_test.cpp` | 7 | 4 pass, 3 fail |
| `ttdfindlastall_test.cpp` | 20 | 17 pass, 3 fail |
| `ttdlifecyclestress_test.cpp` | 6 | 4 pass, 2 fail |
| `ttdmodelpagebounds_test.cpp` | 4 | 24 pass, 8 fail |
| `ttdpage255_test.cpp` | 9 | 7 pass, 2 fail |
| `ttdrestore_test.cpp` | 10 | crashes (an exception or abort on the first test) |
| `ttdreverseexecutor_test.cpp` | 28 | 25 pass, 3 fail |
| `ttdseek_test.cpp` | 29 | 28 pass, 1 fail |
| `ttdstatusendpoint_test.cpp` | 9 | 6 pass, 3 fail |
| `ttdstepinstruction_test.cpp` | 7 | 5 pass, 2 fail |
| `ttdsubsystemrestore_test.cpp` | 8 | crashes (an exception or abort on the first test) |
| `ttdwritejournale2e_test.cpp` | 9 | 3 pass, 6 fail |


Each needs its expectations rewritten for the engine (or the test dropped where it only checks v1's rule); a
failure that is not one of the reasons above is a bug to look at. The three that crash need a look first.

## 4. Benchmarks and tools

| Where | v1 use | On deletion |
|---|---|---|
| `core/benchmarks/debugger/ttd/ttd_{coverage,frame_overhead,portjournal,reverse,serialize}_benchmark.cpp` | drive `pTimeTravelManager` | move to the controller (or `TTDControl`); their baselines change (v1 → engine numbers) |
| `ttdbench` (`UNREAL_TTD_BENCH_ENGINE=v1/all`), `testdata/ttd/bench/v1-*.json`, `v1-ci-gate.txt` | v1 as a backend and the reference figures | drop the v1 backend; the engine baselines (`engine-phase*-full.json`) stay |
| `tools/verification/ttd-analyzer` (Python: `ttd_format.py`, `record_port_journal_fixtures.py`) | parses v1 files; records fixtures with v1 | keep the parser for old files or retire it; record fixtures with the engine |
| `tools/verification/zxdlss` | the v1 branch for v1 files (§1) | drop the branch, or keep v1 files readable through a converter |
| `tools/poc/010-ttd-gui`, `01-ttd-compression`, `010-ttd-compression`, `011-ttd-v2-capture-analysis` | proofs of concept on v1 data | retire (Phase 6) |

## 5. Questions for the owner (before 2026-10-22)

1. **The v1 file reader "kept for verification"** (TODO, Phase 6). Its users today: the v1 feeder (the oracle tests
   and the benchmark read v1 fixtures through it), zxdlss for old files, the Python analyzer (independent of the
   C++ reader). Options: keep a minimal v1 reader that converts a v1 file into an engine session (the feeder without
   v1's manager), or convert the v1 fixtures once (`testdata/ttd/*.ttd`, `testdata/ttd/port-journals/`,
   `testdata/machines/*/ttd/`) and drop v1 files entirely.
2. **The oracle tests (§3.1)**: keep their engine-side assertions against recorded expectations, or rely on the
   engine's own suites and the engine corpus.
