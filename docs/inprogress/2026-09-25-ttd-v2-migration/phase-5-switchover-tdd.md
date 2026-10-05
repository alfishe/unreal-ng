# Phase 5 — Switch the emulator to the engine: technical design

Status: **in progress (2026-10-04)**: item 1 done - 1a (the hooks interface, §4.2.2), 1b (every TTD verb of the WebAPI, CLI, Lua and Python through `TTDControl`), 1c (Qt, GDB, ZX-Poly, Sprinter; DeZog with Step 2). Item 3 (the clean stop, FR-17) done. Next: item 2 as the playback controller C1-C5 (owner decision 2026-10-04, §7). Roadmap: [README §2, Phase 5](README.md#phase-5--switch-the-emulator-to-the-engine). Decisions: [engine-decisions.md](engine-decisions.md) D7–D13, D29, D30, D32, D33 (and D11, D17, D19, D26 where the switch touches them). Requirements: QR-8, FR-17, FR-24, FR-9 and FR-8 on the engine, FR-7 on every surface, PR-1 / PR-2 with v1 out of the frame, and the acceptance criteria of [requirements §6](requirements.md#6-acceptance-criteria).

Code references are to master at `8ddaf708e`. `TTM` = `core/src/debugger/ttd/timetravelmanager.cpp`, `TTM.h` = its header, `CTX` = `core/src/emulator/emulatorcontext.h`, `EMU` = `core/src/emulator/emulator.cpp`.

What this phase does **not** design: the engine's internals (Phases 1–4: `phase-1-memory-regions-tdd.md`, `phase-2-device-state-tdd.md`, `phase-3-replay-inputs-tdd.md`, `phase-4-session-file-tdd.md`). Phase 5 assumes they are done and checked by D33, and wires the emulator, the surfaces and the UI to them.

## 1. Glossary

| Term | Meaning |
|---|---|
| Surface | A way to control the emulator from outside the core: WebAPI, MCP, CLI, Lua, Python, GDB stub, DeZog, the Qt UI |
| Call site | A line of code outside TTD that calls the TTD object (for example a loader telling TTD a tape was inserted) |
| Hook | A call site the emulator itself makes while running: at a frame boundary, on a memory write, on a reset |
| Branch | A second history that starts at a past position of an existing one. The first history is the *trunk* (branch 0) |
| Fork position | The position where a branch leaves its parent |
| Paused recording | A recording that is not capturing right now because the user moved into the past; resuming at its end continues it |
| Black box | A recording that is always on in the background, keeping the last N minutes, so a problem can be inspected after it happened |
| Session file | The file the engine writes the session into as it records (D28); memory is only a cache of it |
| Clean stop | Ending a recording so that everything recorded up to that instant stays valid and the session file is complete |
| Guarded action | An action v1 refuses while recording because it would end or corrupt the history (`TTDGuardedAction`, TTM.h:324-337) |
| Verification tools | `core-tests`, `core-benchmarks` and the comparison scripts; never the application |
| Go/no-go gate | The list of checks that must all pass before users get the engine |

## 2. What changes, in one example

A user plays a game in unreal-qt on a Pentagon (about 49 frames per second) with the black box on. Seven minutes in, a sprite glitches. The user drags the TTD slider back 30 seconds (about 1,465 frames), finds the frame where it starts, types a POKE into the memory view, and presses "Resume from here". Later they load a snapshot of another level, then switch debug mode off.

| Moment | Today (v1) | After Phase 5 |
|---|---|---|
| Starting the game | Nothing is recorded unless the user pressed Record before the glitch | The black box records from the start; it keeps the last N minutes (D11, D29). File: `scratch/ttd/2026-10-02-141503-pentagon.ttd` (D30), growing about 3 MB per minute idle up to about 37 MB per minute on heavy content (E6, D28) |
| Dragging the slider back | Refused while recording: the WebAPI answers 409 (`ttd_api.cpp:393-411`), the core rejects the seek (TTM:2261-2273). The user must stop first | The recording pauses and the machine shows the past (D8). Status: `detached`, `recording_paused: true` |
| The POKE in the past | Memory edit is a "debugger edit" marker (`EMU:680-697`); DeZog wipes the history and restarts (`dezogdebugadapter.cpp:1023-1048`) | A branch starts at that frame and the POKE is its first event (D9). The trunk keeps all 7 minutes |
| "Resume from here" | Deletes the last 30 seconds of history (`ResumeRecordingFrom`, TTM:2849, TTM.h:1124-1152) | Branch 1 continues from there. The trunk's last 30 seconds are still there and reachable by `seek {branch: 0, …}` (D7, FR-24) |
| Loading a snapshot | Refused while recording (`RecordingGuard`, TTM:842); on a stopped session it drops the whole history (`EMU:1702`) | An event on the timeline: the state before and after the load are both kept (D10) |
| Switching debug mode off | Refused while recording (B9, `featuremanager.cpp:308-313`) | The recording stops cleanly at that instant; history and file stay valid (FR-17) |
| "Jump to start" after 20 minutes | Seeks the first recorded frame | Seeks the earliest position still kept, minute 10 with a 10-minute black box (D12) |
| Seek to "frame 1000" | Machine state at the start of frame 1000, picture at its end (TTM:2280-2284) | State and picture both at the end of frame 1000 (D13) |

## 3. How it works today

### 3.1 One manager per emulator, one pointer

- `Emulator::Init` creates one `ttd::TimeTravelManager` per instance (`EMU:284-287`) and stores it in `EmulatorContext::pTimeTravelManager` (CTX:225, forward-declared at CTX:53). It is deleted in `EMU:482-485`.
- Per-event hooks call the manager directly, each behind a check:
  - memory write: `memory.cpp:448-451` (`RecordMemoryWrite`, debug write path only);
  - port write: `portdecoder.cpp:461-463` (`RecordIoWrite`);
  - coverage: `memory.cpp:329-331`, `z80.cpp:1137-1140`, both behind `ttdCoverageActive` (CTX:300);
  - input playback: `z80.cpp:782-783` (`ServiceInput`, behind the step-work bit `kStepWorkTtdInput`).
- The port journals already use a different pattern: the context holds the journals themselves (`ttdPortReads`, `ttdPortWrites`, CTX:281-282), not the manager.
- Per-frame hook: `MainLoop` calls `OnFrameBoundary` (`mainloop.cpp:460-464`).

### 3.2 Call sites, by surface

Counted with `grep -E 'pTimeTravelManager|TimeTravelManager'` (lines, comments included) and, for "methods", the distinct public `TimeTravelManager` methods each surface calls. `TTM.h` has 101 public method names.

| Surface | Files | Lines | Methods | Entry points users see |
|---|---|---|---|---|
| WebAPI | `ttd_api.cpp` (12), `state_memory_api.cpp` (5), `state_audio_api.cpp` (1) | 18 | 38 | 25 routes under `/ttd` (`emulator_api.h:485-513`); one shared resolver `resolveTTD` (21 uses) and `rejectIfRecording` (9 uses) |
| MCP | none: `time_travel` and `inspect_state` aspect `ttd` call the WebAPI routes (`mcp-tools.cpp:3147-3668`) | 0 | — | 1 tool, about 25 actions; behavior text in `mcp-tools.cpp:3148-3153` and the server instructions (`mcp-dispatcher.cpp:22-26`) |
| CLI | `cli-processor-ttd.cpp` (23), `cli-processor-memory.cpp` (3), `cli-processor-gs.cpp` (2) | 28 | 35 | `ttd <subcommand>`: 22 subcommands plus aliases |
| Lua | `lua_emulator.h` | 77 | 35 | 29 `ttd_*` functions |
| Python | `python_emulator.h` | 75 | 36 | 29 `ttd_*` functions |
| DeZog | `dezogdebugadapter.{h,cpp}` | 13 | 14 | reverse debugging through `DebuggerLive` mode and the frame cache |
| GDB stub | `gdbserver.cpp` | 14 | 10 | reverse step / continue, `monitor` commands |
| Qt | `ttdwidget.cpp` (22), `toolbarmanager.cpp` (4), `statusbarmanager.cpp` (1), `menumanager.cpp` (1) | 28 | 16 | the TTD panel, toolbar button, status bar, menu locks |
| Core (not a surface) | 35 files outside `core/src/debugger/ttd/` (loaders, devices, input managers, ZX-Poly, RZX, feature manager) | 164 | 29 | — |
| Tests | 83 files | 569 | — | — |
| Benchmarks and tools | 5 in `core/benchmarks/debugger/ttd/`, `core/src/debugger/ttd/bench/`, `tools/verification/zxdlss/tool/ttd_source.cpp`, 4 POC 011 files | — | — | — |

The four scripting surfaces (WebAPI, CLI, Lua, Python) call nearly the same 35–38 methods, each with its own copy of the glue: status assembly (`GetSessionInfo` 3–6 times each), guards, error texts, position parsing. The core call sites use a different, small set: `IsRecording` (21), `RecordExternalEvent` (13), `InvalidateSession` (12), `SubmitLiveInput` (10), `UpdateFeatureCache` (10), `RecordingGuard` (9), `StopRecording` (6), `OwnsInput` (6), and a few more.

### 3.3 Behavior Phase 5 changes

| Today | Where |
|---|---|
| Resume from a past position deletes the later history | `ResumeRecordingFrom`, TTM:2849; Qt `onResumeFromHere`, `ttdwidget.cpp:709-720` |
| Seek, step, find-last and reverse queries are refused while recording | TTM:2261-2273; WebAPI 409 via `rejectIfRecording`, `ttd_api.cpp:393-411` |
| Loads, ROM reload, media change, GS card switch, model transfer and speed change drop the history | `InvalidateSession` at `EMU:650, 969, 1702, 1973, 2102, 2168, 3081`, `mediamanager.cpp:973`, `soundmanager.cpp:1637`, `rzxsession.cpp:372, 484`, `machinestatetransfer.cpp:1059` |
| While recording, those actions are refused instead | `TTDGuardedAction` (TTM.h:324-337), `RecordingGuard` (TTM:842) |
| Switching `timetravel` or `debugmode` off is refused while recording (B9, `e175ff42`) | `featuremanager.cpp:306-313` |
| A reset stops the recording | `EMU:940-944`, autostart `EMU:876-877` |
| A tool's memory edit is a marker that replay cannot cross | `Emulator::EditMemoryFromTool`, `EMU:680-697` |
| DeZog wipes the history on an edit and records a separate "live" history | `dezogdebugadapter.cpp:1006-1048`; `TTDRecordMode::DebuggerLive`, TTM.h:113-117 |
| A seek to `{frame: N}` means T 0 of frame N; the WebAPI turns a missing `tinframe` into 0 | `ttd_api.cpp:730-731`; display rule TTM:2280-2284 |
| The design doc of the Qt toolbar seeks frame 0 for "Jump to start"; the code already uses `sessionStartFrame` | [toolbar design](../2026-09-23-ttd-qt-toolbar-widget/ttd-qt-toolbar-widget-design.md) line 36; `ttdwidget.cpp:653-659` |

### 3.4 Settings in unreal-qt

- User settings are `QSettings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG")`: `mainwindow.cpp:130` (last folders, autostart), `toolbarmanager.cpp:661-674` (toolbar, mouse gate). The video recorder uses its own store (`recordingsettings.h:37`).
- Edit > Preferences exists but is disabled (`menumanager.cpp:229-236`, "TODO: Implement preferences dialog").
- `MainWindow::adoptEmulator` knows where an instance came from (`EmulatorOrigin`, `mainwindow.h:69-73`): `CreatedByGui` (user preferences apply) or `Adopted` (created by WebAPI / MCP / CLI, left as it is). This is exactly the line D29 draws.

### 3.5 Where v1 is built

- `core/src/CMakeLists.txt:103` compiles every `.cpp` under `core/src` into the `core` library, so `TimeTravelManager` and the benchmark harness (`core/src/debugger/ttd/bench/`) are inside the application binary today.
- One folder is already kept out the same way: `/recording/` (`core/src/CMakeLists.txt:109`, "Moved to separate library").
- `core-tests` and `core-benchmarks` link `core` (`core/benchmarks/CMakeLists.txt:151-160`; `core/tests/CMakeLists.txt` adds `core/src` as a subdirectory).

## 4. Design

### 4.1 Overview: three layers between the emulator and TTD

```
  WebAPI  CLI  Lua  Python  GDB  DeZog  Qt          MCP ──► WebAPI routes
     │     │    │     │      │     │     │
     └─────┴────┴─────┴──────┴─────┴─────┘
                     │  (3) TTDCommands: one implementation per command,
                     │      results as StateNode + error code
                     ▼
              ttd::TimeTravelEngine  (pTimeTravelEngine, concrete)
                     ▲
                     │  (2) ITimeTravelHooks: what the core calls
                     │      (frame boundary, reset, loads, edits, input, guards)
   loaders, devices, MainLoop, FeatureManager, ZX-Poly, RZX
                     ▲
                     │  (1) per-event sinks on the context (journals, coverage):
   Memory, PortDecoder, Z80         one pointer check, as the port journals today
```

- **(1) Per-event sinks** stay concrete and cheap. The pattern is the existing one for port journals (CTX:281-282): the context holds the sink, which is null when nothing records.
- **(2) Hooks** are an abstract interface with about 20 methods, all called per frame or on rare events. In the application they are implemented by the engine. In the verification tools they can be implemented by v1 (§4.5), which is what lets v1 leave the application while it keeps recording live in tests and benchmarks.
- **(3) Commands** are the behavior every surface shares: one function per command, used by all seven surfaces.

The switch from v1 to the engine is then one line for each layer: the object `Emulator::Init` creates and the pointers it sets.

### 4.2 Step 1 — The emulator runs on the engine

#### 4.2.1 The interface boundary: what was considered

| Option | For | Against | Verdict |
|---|---|---|---|
| A. One abstract `TimeTravel` interface over the whole API, implemented by v1 and the engine | Switching and rolling back are one line | v1's API is the thing that changes: positions without a branch, a truncating resume, `DebuggerLive`, internals the tests reach into (`GetPageStore`, `GetCheckpoint`, `GetFrameCache`). An interface would freeze v1's shape into the engine, or need a v1 adapter deleted again in Phase 6 | Rejected |
| B. Rename the pointer and let every surface call the engine directly | Least new code | The glue duplicated four times (§3.2) is rewritten four times, and behavior can drift between surfaces, which QR-8 and FR-7 forbid | Rejected |
| C. Three layers (§4.1): cheap sinks, a small hooks interface, one shared command layer | The core switch is one line; surfaces are switched once, in one place; v1 can still record in the verification tools | One new module (`TTDCommands`) and one interface to write | **Chosen** |

#### 4.2.2 Layer (2): the hooks interface

`core/src/debugger/ttd/timetravelhooks.h`:

```cpp
namespace ttd
{
/// What the emulator core calls into TTD. Per frame or on rare events, never per instruction
class ITimeTravelHooks
{
public:
    virtual ~ITimeTravelHooks() = default;

    // Running
    virtual void OnFrameBoundary() = 0;                         // MainLoop, once per frame
    virtual void ServiceInput() = 0;                            // Z80, only when kStepWorkTtdInput is set
    virtual void OnMachineReset() = 0;                          // a reset ends the session; optionally a new one starts (D39)

    // Things that happen to the machine (D10, D25, D26): events, not the end of the session
    virtual void OnLoad(TTDLoadKind kind, const std::string& source) = 0;   // snapshot, tape, disk, disk create, media change
    virtual void OnConfigurationChange(TTDConfigChangeKind kind, const char* reason) = 0;  // GS card, speed, ROM
    virtual void OnModelTransfer(const char* reason) = 0;       // D26: a new session linked to its parent
    virtual void OnToolEdit(const char* source) = 0;            // D9: called by Emulator::EditMemoryFromTool
    virtual void RecordExternalEvent(TTDExternalEventKind kind, const char* reason) = 0;

    // Input (SubmitLiveInput, OwnsInput, the ZX-Poly interceptor, SubmitMachineTask keep their v1 signatures)
    virtual bool SubmitLiveInput(const TTDInputEvent& ev) = 0;
    virtual bool OwnsInput() const = 0;
    virtual void SetLiveInputInterceptor(std::function<bool(const TTDInputEvent&)> interceptor) = 0;
    virtual MachineTaskResult SubmitMachineTask(std::function<void()> task) = 0;

    // State the core asks about
    virtual bool IsRecording() const = 0;
    virtual bool IsTimelineBound() const = 0;                   // recording, replaying or detached (acceleration lock)
    virtual std::string RecordingGuard(TTDGuardedAction action) const = 0;   // what is still refused (§4.3.6)
    virtual void SetUnavailableReason(const std::string& reason) = 0;        // ZX-Poly members

    // Clean stop (FR-17)
    virtual void StopForFeatureChange(const char* feature) = 0;
    virtual void UpdateFeatureCache() = 0;
};
}
```

- `CTX` gains `ttd::ITimeTravelHooks* pTimeTravelHooks` and `ttd::TimeTravelEngine* pTimeTravelEngine`; `pTimeTravelManager` is removed. The 35 core files switch to `pTimeTravelHooks`. Every `InvalidateSession(reason)` call in §3.3 becomes `OnLoad` / `OnConfigurationChange` / `OnModelTransfer`, so the reason strings survive as event reasons.
- Why virtual is acceptable here: these calls happen per frame, on loads and resets, or behind the step-work bit. The per-event calls are not on this interface.

#### 4.2.3 Layer (1): per-event sinks

The four per-event sites of §3.1 stop naming a manager:

| Site | Today | After |
|---|---|---|
| `memory.cpp:448-451` | `pTimeTravelManager->RecordMemoryWrite(...)` | `if (ctx->ttdWriteSink) ctx->ttdWriteSink->OnMemoryWrite(...)` |
| `portdecoder.cpp:461-463` | `pTimeTravelManager->RecordIoWrite(...)` | `if (ctx->ttdWriteSink) ctx->ttdWriteSink->OnIoWrite(...)` |
| `memory.cpp:329-331`, `z80.cpp:1137-1140` | `ttdCoverageActive && pTimeTravelManager` | `ctx->ttdCoverage` (null when off) |

`TTDWriteSink` is a small concrete class owned by whichever engine records; its shape is Phase 3's event stream (`phase-3-replay-inputs-tdd.md`, D24). The sink pointer is set when a recording starts and cleared when it stops, once, not per event. The cost is one pointer check per event, the same check v1 pays today (§5).

#### 4.2.4 Layer (3): the command layer

`core/src/debugger/ttd/ttdcommands.{h,cpp}`, following the precedent of `core/automation/temporalstatus.h` (one report as a `StateNode`, each surface converts it with its own converter):

```cpp
namespace ttd
{
enum class TTDCommandError : uint8_t
{
    None, NotAvailable, BadRequest, OutOfRange, Conflict, Degraded, Io
};

struct TTDCommandResult
{
    TTDCommandError error = TTDCommandError::None;
    std::string message;            // one sentence a user can act on; shown verbatim by every surface
    StateNode body;                 // the same fields on every surface
};

class TTDCommands
{
public:
    explicit TTDCommands(Emulator& emulator);
    TTDCommandResult Status() const;
    TTDCommandResult Start(const TTDStartOptions& options);   // mode, journal, retention, file (§4.4)
    TTDCommandResult Stop();
    TTDCommandResult Invalidate(const std::string& reason);
    TTDCommandResult Seek(const TTDSeekRequest& request);     // frame [, T] [, branch] | bookmark
    TTDCommandResult Step(TTDStepKind kind, uint64_t count);
    TTDCommandResult Resume(const std::optional<TTDSeekRequest>& from);
    TTDCommandResult Position() const;
    TTDCommandResult Markers() const;
    TTDCommandResult Branches() const;                        // read-only list (§4.3.1)
    TTDCommandResult Streams() const;                         // D19
    TTDCommandResult SetStream(const std::string& name, bool enabled);
    // find-last, port-events, reverse-continue, coverage, bookmarks, dump, load, export-clip, file-info: same shape
};
}
```

- Each surface keeps its routes, commands and function names (QR-8) and only maps `TTDCommandError` to its own form: WebAPI HTTP status (`Conflict` → 409, `OutOfRange` → 409 as today, `BadRequest` → 400, `NotAvailable` → 501/503 as today), CLI `Error: <message>`, Lua `false, message`, Python `RuntimeError(message)`, GDB `E` packets, Qt a dialog.
- The table of [command-interface.md → TTD Session Rules](../../emulator/design/control-interfaces/command-interface.md#ttd-session-rules) becomes the specification of `TTDCommands`; it is updated in the same commit as each behavior change (§4.6).
- **Degraded restores (FR-7, requirements §7 row "Phase 5, Step 1")**: `Seek`, `Step`, `Resume`, `Load` put the engine's restore report (Phase 2, Step 3) into `body` as `exact: false` plus `degraded: [{device, reason}]`, and a partially damaged session (integrity I-4, Phase 4) as `damaged_from` / `damaged_to`. Because every surface goes through `TTDCommands`, the report reaches all of them with one implementation, and one test per surface checks that the field arrives.
- **Optional streams (D19)**: `GET /ttd/streams`, `PUT /ttd/streams/{name}` (`{"enabled": bool}`), CLI `ttd stream [<name> on|off]`, Lua / Python `ttd_streams()` / `ttd_stream_set(name, enabled)`, MCP `time_travel` actions `streams` / `stream_set`, and a checkable list in the Qt TTD panel menu. All additive. The first stream is the per-frame screenshot (Phase 4, Step 4).

#### 4.2.5 Per-surface switch

| Surface | What changes |
|---|---|
| WebAPI | `resolveTTD` returns the command layer; the 25 handlers become thin `TTDCommands` calls; `rejectIfRecording` (9 uses) is deleted, the commands decide (D8). OpenAPI (`openapi_ttd.inc`): new optional fields only |
| MCP | No code path change; the tool description (`mcp-tools.cpp:3148-3153`) and server instructions (`mcp-dispatcher.cpp:22-26`) are rewritten for D7–D13; new actions `branches`, `streams`, `stream_set` |
| CLI | `cli-processor-ttd.cpp` calls `TTDCommands`; `cli-processor-memory.cpp` and `cli-processor-gs.cpp` lose their guards (edits and card switches become events, §4.3) |
| Lua, Python | The 29 functions each call `TTDCommands`; results keep their keys, new keys are added |
| GDB | Reverse step / continue through `TTDCommands`; `monitor load` loses the recording refusal for snapshots (D10) |
| DeZog | `BeginDebuggerLiveHistory` / `EndDebuggerLiveHistory` become plain start / pause, because the engine allows seeking while recording (D8); `onDebuggerEdit` stops wiping (D9). `TTDRecordMode::DebuggerLive` is not carried into the engine |
| Qt | `TtdWidget` and the managers call `TTDCommands` (the widget is a surface like the others); new: branch indicator, paused-recording state, black-box settings (§4.4) |

#### 4.2.6 What the engine must offer before the switch

These v1 features have users today; the switch is blocked until the engine provides each, with the same result on the same session (checked by the surface contract test, §6):

| v1 feature | Used by |
|---|---|
| `ExportClip`, `VisitComposedFrames` (`ttdclipexport.cpp`) | WebAPI `export-clip`, `tools/verification/zxdlss/tool/ttd_source.cpp` |
| `GetFrameCache` (instruction-level browsing) | DeZog, step-instruction |
| `SearchPortEvents`, `SearchPortEventsInFile` | `port-events` on every surface |
| Coverage probe / scan / summary, `FindLastAccess`, `ReverseContinue`, `ReverseStepInstructions`, `ReverseStepTStates` | every surface |
| Bookmarks, markers, `GetSessionInfo` fields (`write_journal_complete`, `write_journal_gap`, `last_drop_reason`, `page_store_used_bytes`, …) | every surface |
| `SubmitMachineTask`, live input interceptor, unavailable reason | NeoGS media, ZX-Poly |
| RZX interplay (`rzxsession.cpp:365-372, 455-484`) | RZX playback; RZX's own key-frame store retires later (D14), not in this phase |

Status fields that only describe v1's storage (`page_store_used_bytes`, key-frame counts) keep their names and report the engine's equivalent (pieces, regions); renamed or new fields are additive.

#### 4.2.7 Clean stop when TTD or debug mode is switched off (FR-17)

Today B9 (`e175ff42`) refuses both switches while recording (`featuremanager.cpp:308-313`, texts at TTM:868-873 in `RecordingGuard`). FR-17 asks for a clean stop instead. Phase 5 replaces the refusal:

1. `FeatureManager::setFeature("timetravel" | "debugmode", false)` sees a running recording and calls `pTimeTravelHooks->StopForFeatureChange(feature)` **before** the flag changes.
2. The engine pauses the emulator (as `StopRecording` does today, TTM:360-367), so no write can slip between the stop and the flag.
3. It records a `stop` event at the current position (frame and T), so the partly recorded frame stays replayable up to that instant, hands the last items to the file writer and waits for them to be appended (D28). The file is then complete.
4. The flag changes; the emulator resumes if it was running.
5. Status reports `last_stop_reason: "feature-off:debugmode"` (additive field). The history stays browsable. With `timetravel` off the session is closed and can be reopened from its file; with `debugmode` off it stays in memory, and a later seek or resume switches debug mode back on, as `StartRecording` does today (TTM:183-197).

Switching the **write journal** off or on during a recording no longer needs a refusal either: the journal is recorded in segments (D40, Phase 3, Step 7), so the switch ends or starts a segment and queries outside the segments use the coverage index and one replayed frame. `TTDGuardedAction::ChangeWriteJournal` is removed.

**Conflict with B9, to note in the migration docs.** [current-state.md](current-state.md) lists B9 as fixed by refusing these switches, and the command-interface rules say "Refused while recording". After Phase 5 they are not refused: they stop the recording. The B9 row gets a note "superseded by Phase 5, Step 1 (FR-17): clean stop instead of refusal", the refusal tests in `timetravelmanager_recordingguard_test.cpp` are rewritten as clean-stop tests, and the session-rules table moves the two rows from "refused" to "stops the recording". The B9 guarantee itself, "no corrupt history", is kept and tested (§6).

### 4.3 Step 2 — History is never cut short

#### 4.3.1 Resume from the past starts a branch (D7, FR-24, FR-9)

- `Resume(from)` at the end of the current branch continues it. Anywhere else, the engine creates branch `k` with `fork = from` and records into it. Nothing is deleted.
- Results gain `branch`, `branched` and `fork: {branch, frame, tinframe}`. `resumed` and `state` keep their meaning.
- Every position the API returns gains `branch` (default: the current one). `Seek` accepts an optional `branch`; without it, it seeks in the current branch.
- `Branches()` (`GET /ttd/branches`, CLI `ttd branches`, Lua / Python `ttd_branches()`, MCP action `branches`) lists `{id, parent, fork, end}`. This is the minimum that keeps the later history of the trunk reachable after a resume. Naming, switching in the UI, comparing and deleting branches stay in [PLAN #76](../2026-09-29-model-what-if/design.md).
- Qt: "Resume from here" no longer warns about losing the future; the panel shows "branch 1 of 2"; the slider covers the current branch, with its fork marked.
- FR-9 on the engine: after resuming from a past position, the new branch's first checkpoint must equal the restored state byte for byte (test in §6).

#### 4.3.2 Seek while recording pauses the recording (D8)

| Action while `recording` | Today | After |
|---|---|---|
| seek, step back / forward, step-instruction, reverse-step, reverse-continue, find-last | 409 / refused | Allowed. The recording pauses: state `detached`, `recording_paused: true` |
| `resume` at the paused end | — | Continues the same recording |
| `resume` in the past | — | Starts a branch (§4.3.1) |
| run the emulator from a past position | Plays the recorded history forward and auto-pauses at its end | Same; reaching the paused end continues the recording |
| `stop` while paused | — | Ends the recording; state `idle` with history |
| `port-events` | Refused while recording (the journal is being written) | Allowed while paused; still refused while capturing |

The states (`idle`, `recording`, `detached`) keep their names (`TTDSessionStateToString`, TTM.h:98); `recording_paused` is a new field.

#### 4.3.3 An edit in the past starts a branch (D9)

- All tool edits already go through `Emulator::EditMemoryFromTool` (`EMU:680-697`) or the DeZog adapter. Both call `OnToolEdit(source)`.
- At the present position the edit is recorded as an event with its content (bytes, registers, paging), so replay reproduces it and it is no longer a barrier. The event kind is Phase 3's (`phase-3-replay-inputs-tdd.md`, D24).
- At a past position (`detached`) the engine first starts a branch at that position, then records the edit as the branch's first event.
- DeZog's wipe-and-restart (`dezogdebugadapter.cpp:1044-1047`) is deleted.

#### 4.3.4 Loading a snapshot is an event (D10, D25, D26)

| Action | Today (`InvalidateSession` reason) | After |
|---|---|---|
| Snapshot load, RZX snapshot | `snapshot-load`, `rzx-snapshot` | `OnLoad`: an event, followed at once by a checkpoint of the loaded state. A seek before it replays up to it; a seek after it restores from that checkpoint; it is not a replay barrier |
| Tape load, disk load, disk create, media change | `tape-load`, `disk-load`, `disk-create`, `media-change` | `OnLoad`: an event that switches the medium version the next checkpoints refer to (D25) |
| GS card switch | `gs-card-switch` | The device set is fixed for a session (D38): refused while recording; otherwise a new session linked to its parent, as a model transfer |
| ROM reload, model transfer | `rom-reload`, `state-transfer` | `OnModelTransfer`: a new session linked to its parent (D26). The parent stays openable |
| Host speed multiplier change | `speed-multiplier-change` | An event, if Phase 3's configuration fingerprint shows the speed affects replay; otherwise nothing at all (open question 5) |
| Reset | Stops the recording | Ends the session; recording again (a setting) starts a new session linked to it (D39) |

"Invalidate" stays: it is the explicit discard of the session (FR-24 allows explicit deletion), still refused while capturing.

#### 4.3.5 "Start" and "frame N" (D12, D13)

- `Status` reports `earliest: {branch, frame, tinframe}` (the earliest kept position, D11) next to `session_start_frame`, which keeps its name and now means the same thing. Qt "Jump to start" (`ttdwidget.cpp:653-659`) and the slider minimum use it. A seek before it answers `OutOfRange` with the earliest position in the message.
- A seek to `{frame: N}` **without** `tinframe` lands at the end of frame N: machine state and picture agree. The WebAPI stops turning a missing `tinframe` into 0 (`ttd_api.cpp:731`); CLI `ttd seek N`, Lua / Python `ttd_seek(N)` and the Qt slider follow. `arrived_at` reports `{frame: N, tinframe: <length of frame N>}`, which names the same machine time as `{frame: N+1, tinframe: 0}`; the engine accepts both.
- `{frame: N, tinframe: T}` gives the state at T and the picture up to the beam, as today.
- This is a visible change for scripts that seek `{frame: N}` and read memory: they now see one frame later. The recipes that do so are updated (§4.6).

#### 4.3.6 What is still refused while recording

| `TTDGuardedAction` | After Phase 5 |
|---|---|
| `LoadSnapshot`, `LoadTape`, `LoadDisk`, `CreateDisk` | Allowed: events (§4.3.4) |
| `LoadRom` | Allowed: new linked session |
| `Invalidate` | Still refused while capturing (allowed while paused or stopped) |
| `DisableTimeTravel`, `DisableDebugMode` | Allowed: clean stop (§4.2.7) |
| `ChangeWriteJournal` | Removed: journal segments (D40) |
| `SwitchGsCard` | Still refused while recording: the device set is fixed for a session (D38) |
| `CdFrontPanel` | Allowed only if Phase 3 journals the front-panel action as an event; otherwise still refused |

The acceleration lock (host speed 1x, turbo and fast loaders off while recording or detached, [command-interface.md](../../emulator/design/control-interfaces/command-interface.md#ttd-session-rules)) is not changed by this step; its interaction with the black box is open question 1.

### 4.4 Step 3 — Black box and session file location (D29, D30, D11)

**Settings** (unreal-qt only, in the existing store `QSettings(IniFormat, UserScope, "Unreal", "Unreal-NG")`):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `TimeTravel/BlackBox` | bool | open question 2 | Record every instance this window creates |
| `TimeTravel/BlackBoxMinutes` | int | open question 3 | Retention "last N minutes" (D11) |
| `TimeTravel/BlackBoxJournal` | bool | false | Write journal on for the black box (it is the largest stream on active content, E6) |
| `TimeTravel/SessionFolder` | string | `scratch/ttd` | Where session files go (D30) |

- **Where they are set.** Preferences is not implemented (`menumanager.cpp:236`), so the controls go into the TTD panel's menu and the Run menu: "Always record (black box)", "Keep last … minutes", "Session folder…". When Preferences is built, they move there with the same keys.
- **Where they apply.** In `MainWindow::adoptEmulator` when `origin == EmulatorOrigin::CreatedByGui` (startup, model switch, new instance from the UI), after the instance is created: `TTDCommands::Start({retention: Minutes(n), journal, file: auto, blackBox: true})`. Toggling the setting on starts the black box on the current GUI-created instance; toggling it off stops it cleanly.
- **Off for automation.** Instances with `origin == Adopted` (WebAPI, MCP, CLI) and every test are never started by the setting. The core has no global auto-start. Automation can ask for the same behavior per call, additively: `POST /ttd/start {"retention": {"minutes": 10}, "file": "<path>"}` and the equivalent CLI / Lua / Python / MCP arguments.
- **Session file name** (D30): `<SessionFolder>/<YYYY-MM-DD-HHMMSS>-<name>.ttd`, where `<name>` is the model short name plus the stem of the last loaded medium, lower case, every character outside `[a-z0-9-]` replaced by `-` (for example `2026-10-02-141503-pentagon-elite.ttd`). The folder is made absolute at start through `FileHelper` (UTF-8 paths, Windows-safe), created if missing, and shown as an absolute path in the UI. If it is not writable, the UI says so and falls back to the platform's application-data folder plus `/ttd`.
- **Save and discard** (D28): "Save session…" renames the file to the chosen path (copy and delete across volumes). On a clean exit, an unsaved black-box file is deleted; after a crash it stays, and the next start offers to open it (the point of a file written as it records, FR-13).
- **Retention needs the file to drop old parts.** "Last N minutes" in a file that is append-only means the file must release its oldest self-contained parts (D6). `phase-4-session-file-tdd.md` must provide it (segment files or a ring of parts); this is a dependency of Step 3 (risk table).

### 4.5 Step 4 — v1 leaves the emulator (D32)

v1 is moved, not deleted; Phase 6 deletes it.

| Code | Moves to | Linked by |
|---|---|---|
| `timetravelmanager.{h,cpp}`, `ttdclipexport.cpp` (v1 part), `timetravelframecache.h`, `ttdcodecpagestore.{h,cpp}`, and any other file only v1 includes (the engine's phases decide which shared parts it reuses: compression, registry, serializers, journals stay in `core/src/debugger/ttd/`, [engine-approach-and-naming.md](engine-approach-and-naming.md)) | `core/src/debugger/ttd/verification/reference/` | `ttd-verification` library |
| v1's `ITimeTravelHooks` implementation (a thin adapter over `TimeTravelManager`) | same folder | same |
| The benchmark harness `core/src/debugger/ttd/bench/` (today inside the application, §3.5) and the v1 file feeder from Phase 1 | `core/src/debugger/ttd/verification/bench/` | same |
| The oracle helpers (`core/tests/_helpers/ttddivergenceharness.*`) | stay in `core/tests/_helpers/` | `core-tests` |

- **CMake**: `core/src/CMakeLists.txt` excludes `/debugger/ttd/verification/` from the `core` glob, like `/recording/` (line 109), and adds a static library target `ttd-verification` that links `core`. `core-tests`, `core-benchmarks` and the zxdlss verification tool link it. The application targets do not.
- **v1 in the verification tools**: a test or benchmark that wants v1 creates it and sets `pTimeTravelHooks` and the sinks to v1, exactly as the application sets them to the engine. With v1 as the recorder, Phase 1's shadow mode (v1 hands each frame to the engine, one check per frame) keeps working, so the D33 oracle still runs on live recordings after the switch.
- **Check that v1 is gone from the application**: a test of the build (a CMake check or a symbol check on the `unreal-qt` binary) fails if `ttd::TimeTravelManager` is linked into it.
- **Requirements BR-1**: "v1 stays buildable as a baseline engine until v2 is accepted; after that, v1 results are kept as frozen reference data". Step 4 keeps v1 buildable; Phase 6 freezes the results.

### 4.6 Recipes and documents that change

| File | Change |
|---|---|
| [.recipe/analysis/ttd-recording.md](../../../.recipe/analysis/ttd-recording.md) | Resume starts a branch (lines 35, 122-124, 163); scrub while recording pauses instead of 409 (128, 222); "Loads wipe the session" becomes "Loads are events" (231-235); reset keeps recording (236-238); SD card activity stops cleanly instead of dropping history (239); seek `{frame}` lands at the frame end; black box and session file sections |
| [.recipe/analysis/ttd-reverse-debugging.md](../../../.recipe/analysis/ttd-reverse-debugging.md) | 409 while recording (5) becomes "pauses"; "what wipes history" (7); resume no longer discards (169) |
| [.recipe/analysis/ttd-visual-inspection.md](../../../.recipe/analysis/ttd-visual-inspection.md) | 409 rules (94, 104-105), load wipes (107), resume (116) |
| [.recipe/articles/bug-hunt-ttd.md](../../../.recipe/articles/bug-hunt-ttd.md) | "truncates the future" (47, 151-153), checklist (184-186) |
| [.recipe/README.md](../../../.recipe/README.md) | Rule 2 "Scrubbing during TTD recording is a 409" (190) |
| [.recipe/_common/transports.md](../../../.recipe/_common/transports.md) | 409 example (60); `time_travel` action list (96): `branches`, `streams`, `stream_set` |
| [.recipe/media/load-snapshot.md](../../../.recipe/media/load-snapshot.md) | "TTD invalidate" (157-160) becomes "an event on the timeline" |
| [.recipe/media/use-media-slots.md](../../../.recipe/media/use-media-slots.md), [.recipe/media/cd-audio.md](../../../.recipe/media/cd-audio.md) | `recording` 409 for media changes (93) and the CD front panel (94), per §4.3.6 |
| [.recipe/machines/sprinter.md](../../../.recipe/machines/sprinter.md) | "A reset stops a TTD recording and a new image invalidates its session" (63) |
| [.recipe/analysis/sprinter-ttd.md](../../../.recipe/analysis/sprinter-ttd.md), [.recipe/articles/physical-protection-forensics.md](../../../.recipe/articles/physical-protection-forensics.md), [.recipe/analysis/nonstandard-loader.md](../../../.recipe/analysis/nonstandard-loader.md) | Re-run end to end; fix any step that depends on truncation, 409 or `{frame}` meaning T 0 |
| [command-interface.md → TTD Session Rules](../../emulator/design/control-interfaces/command-interface.md#ttd-session-rules) | States (`recording_paused`), "Recording blocks browsing", "A recording protects itself", "What wipes a stopped session", reset, positions with `branch`, frame-end seeks, black box |
| `webapi-interface.md`, `cli-interface.md`, `lua-interface.md`, `python-interface.md`, `gdb-protocol.md` (same folder), `docs/features/automation.md`, `docs/features/mcp/README.md` | New fields and routes; behavior changes above |
| `openapi_ttd.inc`, MCP tool text and server instructions | Same, in code |
| [current-state.md](current-state.md) | B9 note (§4.2.7) |
| [toolbar design](../2026-09-23-ttd-qt-toolbar-widget/ttd-qt-toolbar-widget-design.md), `docs/emulator/design/debugger/time-travel-debug/time-travel-ux.md` | "Jump to start (frame 0)" and "truncate future" |
| [DeZog reverse debugging](../2026-08-27-dezog-integration/reverse-debugging.md) | `DebuggerLive` and the edit wipe (§6) are gone |

Every recipe in the table is re-run against the switched build before the gate (requirements §6 item 9, QR-8).

### 4.7 File format

Phase 5 adds nothing to the file. It needs from earlier phases: branch records and parent links (Phase 1, Step 3; FR-23 in Phase 4, Step 2), event kinds for loads, configuration changes, tool edits, resets and stops (Phase 3, Step 1), linked sessions for model transfers (D26), and the release of the oldest parts for retention (Phase 4). Any of these missing blocks the step that needs it.

## 5. Performance

| Work | When | Cost rule |
|---|---|---|
| Per-event sinks (memory write, port write, coverage) | per event, only while recording | one pointer check, the same as v1's check today (§3.1); no new per-event work |
| Hooks (`OnFrameBoundary`, `ServiceInput`) | per frame; per instruction only when the step-work bit is set | one virtual call per frame |
| Optional streams | per frame | one bitmask check per frame when off (D19) |
| Black box | per frame, always while on | the engine's capture cost; PR-1 ≤ 15%, PR-2 ≤ 10% |
| `TTDCommands` | per command | not on a hot path |
| Clean stop | once | pauses the emulator, flushes the file writer |

- **Zero cost when off.** Without a recording, no sink is set, no stream is enabled, and the only TTD work per frame is the `OnFrameBoundary` call that returns at once. Measured as BM-1 "off" against the pre-switch baseline: no worse than noise on every matrix configuration.
- **PR-1 / PR-2 with v1 out of the frame** (requirements §7, Phase 5, Step 1): today v1 costs 7–14% extra frame time on 128K-class machines, 20–30% on ZX-Evo and 42–45% with GS512 (BM-1, journal and coverage on, [v0b §4](v0b-benchmark-results.md#4-other-findings-for-phase-1-and-later)). With the engine alone, BM-1 must be ≤ 15% in every mode and configuration, and ≤ 10% without the journal; a miss on PR-2 needs a written reason.
- **Black box.** This is the first time a recording runs during ordinary use, so PR-1 decides whether the black box can be offered at all on heavy configurations (open question 2). Measured: BM-1 and BM-2 (p50 / p99 capture) with the black box's settings (journal off, retention on).
- **Hooks behind a virtual call**: an A/B benchmark (`core-benchmarks`, frame overhead and matrix, interleaved runs, idle host, [performance guidelines](../../guidelines/performance-guidelines.md)) compares the switched build with a build that calls the engine directly. Expected within noise, since the calls are per frame.
- **Counted work** (`bm2_work_*`) and sizes run in the CI gate as before; the engine is now the default engine of the harness, v1 stays selectable there.

## 6. Tests and the go/no-go gate

Every new test is checked by mutation: it must fail when the mechanism it guards is removed.

| Step | Test | Proves |
|---|---|---|
| 1 | Surface contract test: every `TTDCommands` call on a recorded session, through WebAPI, CLI, Lua, Python, GDB (in-process), compared field by field | QR-8: same routes and commands, same answers on every surface. Mutation: change one surface's mapping → fails |
| 1 | The v1 contract test (`ttdautomationcontract_test.cpp`) run on the engine | Every result shape v1's surfaces relied on still holds |
| 1 | Degraded restore on every surface: a session with a corrupted device state, a damaged part | FR-7, I-4 reach WebAPI, MCP (through WebAPI), CLI, Lua, Python, DeZog, GDB, Qt |
| 1 | `timetravel` off mid-frame while recording, then reopen the file and seek to every frame and the stop position | FR-17: history valid up to the stop. Mutation: skip the pause before the flag change → a write is missed and the comparison fails |
| 1 | Same with `debugmode` off; write journal toggled twice during a recording | FR-17 for all three switches |
| 1 | Optional stream switched on and off at run time on every surface | D19 controls exist everywhere; the off state costs one check per frame (BM-1) |
| 1 | Build check: `unreal-qt` does not link `ttd::TimeTravelManager` (Step 4) | D32 |
| 2 | Resume in the past: trunk length unchanged, new branch's first checkpoint equals the restored state byte for byte, every trunk frame still restores | D7, FR-24, FR-9. Mutation: drop the later checkpoints (v1 behavior) → fails |
| 2 | Seek while recording, resume at the end: one continuous history, no gap, no duplicate frame | D8 |
| 2 | Tool edit at the present: replay across it reproduces the edited bytes; edit while detached starts a branch | D9 |
| 2 | Snapshot, tape, disk loads and a GS card switch during a recording: seek before and after each restores exactly | D10, D25, D26, FR-4 |
| 2 | ROM reload during a recording: a new session linked to the parent; the parent still opens | D26 |
| 2 | Seek before the earliest kept position answers `OutOfRange` naming it; "Jump to start" lands there | D12 |
| 2 | `{frame: N}` and `{frame: N+1, tinframe: 0}` restore identical state; the picture equals the frame's final picture | D13 |
| 3 | GUI-created instance with the black box setting on records; a WebAPI-created instance does not; a test emulator does not | D29 |
| 3 | File name and folder: date-time, sanitized name, created folder, unwritable folder falls back and reports | D30 |
| 3 | Retention: after N+2 minutes of machine time (run fast in the test), the earliest kept position is N minutes back and the file's size is bounded | D11 with the file |
| 3 | Kill during a black-box recording, restart: the file opens up to the last complete part | FR-13 through the UI path |
| 4 | `core-tests` runs v1 as the recorder with the engine in shadow mode; the oracle compares every frame | v1 still verifies the engine after it left the application |

Existing tests that change: `timetravelmanager_recordingguard_test.cpp` (refusals → events / clean stop), `timetravelmanager_resumesave_test.cpp`, `ttdresume_test.cpp` (truncation → branch), `ttdsessionlifecycle_test.cpp`, `ttdstatusendpoint_test.cpp`, `ttdexternaleventshooks_test.cpp` (invalidation → events), `dezogdebugadapter_test.cpp` (no `DebuggerLive`, no wipe). Tests of v1 itself stay, linked through `ttd-verification`.

### 6.1 The go/no-go gate

Users get the engine (Step 1 lands on master) only when all of these hold on one commit:

1. **D33 on the whole matrix**, engine against v1: every frame and every point inside a frame restores byte for byte as in v1; file size, memory and counted capture work not larger than v1's in any case for the same history kept (fixed ratios excepted as listed in D33); seek p99 ≤ 5 ms everywhere and within 25% of E4's estimates; capture p99 ≤ 3× median.
2. **Acceptance criteria** of [requirements §6](requirements.md#6-acceptance-criteria), items 1–8, through the surfaces, not only in core tests. Item 9 (documentation) holds for this phase's changes (§4.6); the parent TDD truth pass is Phase 6.
3. **PR-1** on the engine alone (§5).
4. **The surface contract test** passes on every surface, and every recipe in §4.6 has been re-run.
5. **Full build with zero warnings** on all compilers (QR-1), the full `core-tests` suite, the Linux gcc build (`docker/linux/build.sh --test`), ASan / UBSan on the TTD tests.
6. **Real-use sessions** (`common/record-real-sessions.sh`, as in Phase 1's oracle) recorded live by the engine and by v1 in shadow mode restore identically.

### 6.2 Rollback

- Phase 5 lands as one series of commits (README §3, "all or nothing"). If a problem shows up after the switch, the series is reverted as a whole; v1 is back in the application as it was.
- The revert is cheap only while v1 is still built (Steps 1–3). Step 4 (v1 out of the application) therefore lands last, after the gate has held on master for the matrix and the recipes again (open question 4 on how long).
- **What a revert cannot undo**: session files written by the engine are in the engine's format, which v1 cannot read. They stay readable by any build with the engine. The release note of the switch says so, and the Qt "Save session…" dialog does not offer a v1 format.
- Automation clients see only additive changes, except the behaviors D7–D13 change on purpose (§4.3); a revert brings back v1's behavior, which the recipes before Phase 5 describe (they are in git history).

## 7. Order of work

Each item lands as its own commits and passes the full gate of README §3; the go/no-go gate (§6.1) is checked before item 4.

1. **Layers without behavior change**: `ITimeTravelHooks`, the per-event sinks and `TTDCommands`, implemented over **v1**. Every surface moves to `TTDCommands`; v1 still runs the emulator. The surface contract test is written here and passes on v1. Nothing users see changes.
2. **Engine features users rely on** (§4.2.6) that earlier phases did not cover: clip export, frame visiting, the frame cache for instruction steps, port-event search in a file.
3. **Clean stop** (§4.2.7) on v1 first: it is independent of the engine and replaces B9's refusal.
4. **The switch (Step 1)**: `Emulator::Init` creates the engine and points the hooks, sinks and commands at it; the surface contract test runs on the engine; degraded-restore reporting; optional stream controls.
5. **Step 2** in this order, each with its recipe and doc updates: seek while recording pauses → resume starts a branch → tool edits → loads as events → configuration changes and linked sessions → earliest position → frame-end seeks.
6. **Step 3**: black-box settings and session-file location in unreal-qt, automation arguments, retention with the file.
7. **Step 4**: v1 to `ttd-verification`; build check that the application no longer links it.
8. Full matrix run stored as the Phase 5 baseline; results document; README and TODO updated.

Items 1 and 3 move code around v1 before the switch, so the switch itself (item 4) changes one object and a few pointers.

Item 1 lands in three parts:

| Part | Content | State |
|---|---|---|
| 1a | `ITimeTravelHooks` (`core/src/debugger/ttd/timetravelhooks.h`), implemented by v1's `TimeTravelManager` (`final`); `EmulatorContext::pTimeTravelHooks`; the core files call it instead of the manager; `InvalidateSession(reason)` calls become `OnLoad` / `OnConfigurationChange` / `OnModelTransfer` with the same reason strings | Done 2026-10-04 (`TimeTravelHooks_Test`) |
| 1b | `TTDControl` over v1 (`core/src/debugger/ttd/ttdcontrol.{h,cpp}`); the WebAPI, CLI, Lua and Python on it, verb group by verb group; the surface contract check | Group 1 done 2026-10-04: `status`, `start`, `stop`, `invalidate`, `history-limit`, `journal`, `journal-build`, `journal-build-cancel`. Group 2 done 2026-10-04: `position`, `seek` (frame or bookmark), `step-back`, `step-forward`, `resume`, `step-instruction`, `reverse-step`. Group 3 done 2026-10-04: `markers`, `bookmarks`, `bookmark-add`, `bookmark-delete`. Group 4a done 2026-10-04: `find-last`, `reverse-continue`, `port-events`. Group 4b done 2026-10-04: `coverage-probe`, `coverage-scan`, `coverage-summary`. Group 5 done 2026-10-04: `file-info`, `dump`, `load`, `export-clip`. **1b done**: 33 verbs, `TTDControl_Test` 22 tests, `tools/verification/ttd-surface-contract/` 83 checks |
| 1c | GDB, Qt, ZX-Poly's group control on `TTDControl`; the Sprinter port search through the hooks; DeZog in Step 2 | Done 2026-10-04 (see below) |

Decided in 1a:

- **Per-event sinks move at the switch (item 4), not in item 1.** Until then the engine is fed by v1 (shadow mode), so the per-event calls (memory and port writes, coverage) stay on the concrete manager; a sink type would only be a second name for it. The sink takes the engine's event stream as its shape when the engine records.
- **The interface is what the core calls today** (24 methods), not the sketch in §4.2.2: `OnMachineParking`, `StopRecording` (reset and autostart), `NoteRzxFrameEnd` / `NoteReplaySource` (RZX), `BeginToolEdit` / `EndToolEdit`, `UpdatePeripheral` (GS card swap), `HasHistory`, `GetState`, `IsReplayActive` are in it; `StopForFeatureChange` comes with the clean stop (item 3).
- `TTDSessionState`, `TTDGuardedAction` and `TTDMachineTaskResult` moved from the manager's header to the hooks header, so a core file needs only the interface.

Decided in 1b:

- **The command layer follows `MediaControl`** (`core/src/emulator/media/mediacontrol.h`), not the typed methods sketched in §4.2.4: a verb and options by name as strings in, an error code, a message and a `StateNode` body out. Option names and value parsing then live in one place, so `frames=-1` or a misspelled option fails with the same text on every surface; each surface only turns its input into strings and the reply into its own form. The class is `TTDControl`.
- **The surfaces doing all four at once.** Moving a verb group on the WebAPI, CLI, Lua and Python together lets one contract check compare all four on that group; 1c is then left with the debugger protocols and the Qt panel.
- **Each surface keeps its return shape** (QR-8): Lua's `ttd_invalidate` still answers `ok, reason`, Python still raises `RuntimeError`, the CLI keeps its text. What became the same is the behavior behind them. Group 1 removed these differences: Lua's and Python's `ttd_build_journal` and the CLI's `ttd journal build` now refuse while recording and pause the machine, as the WebAPI did; Lua's `ttd_start` now honors a ZX-Poly member's "not available"; the CLI's `ttd journal on/off` now switches the journal the way the WebAPI does (a segment starts or ends, the machine is paused for it); a failed journal switch is reported on every surface.
- **Defaults that differ by surface stay with the surface.** Lua and Python `ttd_start()` without an argument keep the earlier journal choice; the WebAPI and the CLI turn it off unless asked (D40). The verb changes the setting only when the option is given, and the WebAPI and the CLI pass their `false`.
- **Group 2: one rule for moving in the timeline.** Seek, frame steps, instruction steps and reverse steps are refused while recording on every surface with the same sentence (the CLI and Lua used to try and report a vague failure). They park the machine, and the machine stays paused at the target; `resume` records again and runs it. Lua and Python used to leave a running machine running through a seek, the WebAPI paused it; the WebAPI's rule won because a script that reads memory after a seek needs it to hold still. A call from the machine's own thread (a breakpoint callback) never waits for itself. Values are checked strictly everywhere (`tinframe` above 32 bits, `dir` other than back / forward, `count` and `tstates` together are 400s); the CLI's `ttd reverse-step` without arguments still means one instruction (its default, passed as `count=1`).
- **Group 3: a label is checked before the session is asked.** A label over 63 characters was answered 409 Conflict on the WebAPI whenever the position check failed first (the status came from matching words in the error text); the verb checks the length itself, so it is a 400 everywhere. An unknown label on delete is "Unknown bookmark label: X" on every surface (the CLI said "Unknown bookmark 'X'").
- **Group 4a: reverse queries behave as on the WebAPI everywhere.** Lua, Python and the CLI ran `find-last` and `reverse-continue` while recording and without parking the machine; now they are refused while recording and pause like a seek. Addresses take decimal, `0x..`, `#..` and `$..` on every surface (the CLI threw on a typo); an unknown `access` is a 400 instead of silently meaning `write`. Python keeps `None` for "no match", raises `ValueError` for a bad argument as it did for `phys_page`, and answers a refusal with `found: False` plus the reason. Lists (`pcs`) travel as one comma list; `port-events` takes any option name and the port query checks it.
- **Group 4b: coverage queries answer without time travel, as they did.** Status and the three coverage verbs are the only ones that answer when no time travel is constructed (`index_available: false`). Lua's `ttd_coverage_*` used to take an unknown `kind` as `executed`, skip the address-range check and run a probe without a frame; they now answer like the WebAPI (Lua: `ok = false` and the reason; Python: `ValueError`). GET routes take only the parameters the verb knows, so an unrelated query parameter is still ignored.
- **Group 5: files through FileHelper, failures as failures.** Dump and load opened files with `std::ofstream(path)` on every surface, which breaks non-ASCII paths on Windows; the verbs open them through `FileHelper::ToFsPath`. A dump that failed answered HTTP 200 (or 500 when the file could not be opened); it is a 409 with `ok: false` and the reason. `file-info` needs no instance (`TTDControl(nullptr)`). Export of a clip was WebAPI-only; it is now on every surface (CLI `ttd export-clip`, Lua / Python `ttd_export_clip`, MCP `time_travel` action `export_clip`), through the same verb.
- **1c: the remaining TTD callers.** The Qt TTD panel's actions (record, save, load, discard, slider seeks, frame steps, resume, journal switch and build, history limit) run the verbs; its display reads (published status, state, position) stay direct, they change nothing. Saving from the panel used `fileName.toStdString()` (not UTF-8 on Windows); it goes through the `dump` verb. GDB's `monitor ttd start|stop|seek|findlast` and reverse step / continue (`bs` / `bc`) run the verbs; GDB keeps its own rule of stopping a recording before reverse execution. ZX-Poly's group start, stop, seek and resume run the verbs on each member; the members' input journaling, the live-input interceptor and the unavailable reason are core calls and went to `ITimeTravelHooks` (with `RecordInputEvent`, `RecordMouse*` and `CurrentFrame`). The Sprinter PLD journal's `source=ttd` reads the port journals through `ITimeTravelHooks::SearchPortEvents`: it is a device report, not a surface, and takes a structured query. The verbs' pause and resume now act only on a machine whose own loop runs (a ZX-Poly member is stepped by its master), which is what every other surface had in practice.
- **DeZog moves in Step 2, not in 1c.** Its TTD use is v1's `DebuggerLive` mode, the restart after a debugger edit and the frame cache: exactly what Step 2 replaces (D8 seek while recording, D9 edits as events) and item 2 moves into the engine. Putting them behind verbs now would add verbs only to remove them.
- **Item 3, the clean stop (FR-17), on v1.** `FeatureManager::setFeature` calls `ITimeTravelHooks::StopForFeatureChange` before switching `timetravel` or `debugmode` off, outside its lock (the stop parks the machine, whose thread may need that lock). v1's `StopRecording` parks the machine first and keeps the stop position, so the history stays valid up to that instant; status gains `last_stop_reason` (`feature-off:<feature>`, cleared by the next start). `TTDGuardedAction::DisableTimeTravel` / `DisableDebugMode` are gone. A test written for the pre-B9 behavior (debug mode off mid-recording, writes missing from the journal) now checks the new one: the history ends at the switch with a complete journal. With `timetravel` off the write journal's memory is still released (TDD §15.2's rule, kept): the history stays valid, reverse queries replay a frame instead of reading the journal.
- **Item 2 becomes a playback controller of the engine (owner decision 2026-10-04).** The engine (`TimeTravelEngine`) is a store: checkpoints, pieces, events, segments, the session file. Seek, replay, input playback, the port journals' replay, the frame cache, the queries, the journal build, clip export and dump / load run inside v1's `TimeTravelManager`. Pointing v1 at the engine's data (`SetShadowEngine` + `SetReplaySource`, tried and dropped the same day) would leave all of that in the class Step 4 removes, so v1 could never be deleted. Instead the playback is cloned into a separate class in the engine module, `ttd::TimeTravelController`: it implements `ITimeTravelHooks`, serves `TTDControl`, and owns a `TimeTravelEngine` as its store (the store stays small and free of execution). What is already a class of its own - the input journal, the port journals, the coverage index, the frame cache, external events, bookmarks - is reused, not copied; the orchestration inside v1 is copied and adapted to the engine's store. v1 stays untouched as the byte-exact reference. Order, each with an A/B against v1:

  | Step | Content |
  |---|---|
  | C1 | The controller's skeleton: recording into the engine only, the core hooks |
  | C2 | Seek, steps, replay (input playback, port journals): every frame and points inside frames match v1 |
  | C3 | Queries: find-last, reverse step / continue, coverage, port events, journal build |
  | C4 | Files (the engine's session file), clip export, bookmarks and markers, status |
  | C5 | `TTDControl` on the controller; the surface contract on both backends |

  Then the switch (item 4: `Emulator::Init` creates the controller), the go/no-go gate, Steps 2 and 3, and Step 4 (v1 to the verification library).
- **C1d: the hot path reaches whichever recorder runs.** Memory writes (debug write path) and port OUTs go to `EmulatorContext::ttdWriteSink`, an `ITTDWriteSink` (v1's manager or the controller; a virtual call where v1 had a direct, non-inline one). Coverage stays inline: `EmulatorContext::ttdCoverage` points at the recording session's `TTDCoverageIndex` only while it records with coverage on (`SyncCoverageSink` at every state change), so the per-instruction record needs neither a call nor a state check. A/B (TTDMatrix PENTAGON/game and ATM3/idle, interleaved, two rounds, load 20-60): the before / after spread is inside the run-to-run spread (e.g. journal + coverage overhead 19.9 / 19.4 % before, 23.5 / 17.4 % after on PENTAGON/game); a quiet rerun belongs to the go/no-go gate.
- **C2: the engine decides which checkpoints exist.** `_timeline` stays as the controller's side record of each checkpoint until the session file moves to the engine (C4); `_timeline[i]` is the engine's checkpoint `FirstCheckpoint() + i`, and every lookup by time asks the engine (`CheckpointAtOrBefore`). The history limit is the engine's segments: the engine keeps all of them (Growable), the controller drops the oldest while the next one alone covers the frame window or while the piece store is over the byte budget, and `_timeline` with the input, marker, bookmark and port journals follows the new front. A frame limit makes a segment an eighth of the window, so up to 1/8 more than the limit is held (v1 held exactly the limit). A resume from the past truncates the engine at the cut (`TruncateAfter`) - the later checkpoints and their versions, the events and the bus, vector and media records after the cut - and rebuilds its capture state (each piece's version and content, the pieces changed since the last full table) from the kept checkpoint, so recording continues with differences against the right base. A file being written as the session records ends there; Step 2's branches keep the old future instead of dropping it.
- **The contract check runs against the live application** (`tools/verification/ttd-surface-contract/`): `core-tests` cannot host the WebAPI, Lua or Python. `TTDControl_Test` covers the verbs; the script covers each surface's mapping. Python automation is off in the default build (`ENABLE_PYTHON_AUTOMATION`); its bindings are compiled in a separate build directory with it on.

## 8. Risks and open questions

| # | Risk / question | Plan | User decision? |
|---|---|---|---|
| 1 | **Black box against the acceleration lock.** While recording, host speed is held at 1x and turbo, fast tape, turbo tape and fast disk are off. A black box always on would lock them for every GUI session | Recommendation: the black box pauses itself while an acceleration feature is on, with an event marking the gap, and resumes when it goes off; history before the gap is kept. Alternatives: lock acceleration (today's rule), or make each acceleration a replayable event (more work in Phase 3) | **Yes** |
| 2 | Black box default in unreal-qt: on or off. Its cost is the engine's frame overhead (v1 today: up to 42–45% with GS512) | Recommendation: off until BM-1 on the engine meets PR-2 (≤ 10%) on the default configurations; then on | **Yes** |
| 3 | Black box retention default N minutes; with D28 rates a 10-minute window is about 30–370 MB of file | Recommendation: 10 minutes, journal off | **Yes** |
| 4 | Soak time between the switch (item 4) and removing v1 from the application (item 7) | Recommendation: until the gate has run twice on master on different days and the recipes passed; no calendar minimum | **Yes** |
| 5 | Host speed multiplier change: does it affect replay? If not, it should not even be an event | Check against Phase 3's configuration fingerprint; if pacing only, drop the invalidation entirely | No |
| 6 | Minimal branch API (`branches`, `branch` in positions and seek) overlaps PLAN #76 | Phase 5 ships only listing and seeking, so the trunk's later history stays reachable after a resume; naming, switching UI, compare and delete stay in PLAN #76 | Confirm scope |
| 7 | Retention needs the append-only file to release its oldest parts; Phase 4 may not provide it | Dependency on `phase-4-session-file-tdd.md`; without it the black box keeps the whole session and only memory is bounded | No |
| 8 | Hooks interface adds a virtual call per frame and per serviced input | A/B benchmark (§5); if it shows, the application build calls the engine directly and only the verification tools use the interface | No |
| 9 | Scripts relying on `{frame: N}` meaning T 0 change behavior silently (D13) | Documented in the session rules and recipes; `arrived_at` always states where the seek landed | No |
| 10 | B9's refusal and FR-17's clean stop disagree in today's docs | Resolved by §4.2.7; current-state.md note | No |
| 11 | DeZog's `DebuggerLive` mode removed: DeZog may rely on its continuous-history behavior in ways not covered by tests | DeZog adapter tests plus a manual DeZog session in the recipe list before the gate | No |
| 12 | Session files after a revert are unreadable by v1 | §6.2: stated in the release note; files stay readable by engine builds | No |

## 9. Sources

- Code: master `8ddaf708e` (references above). Call-site counts from `grep -E 'pTimeTravelManager|TimeTravelManager'` over `core/`, `unreal-qt/`, `tools/`.
- Decisions and requirements: [engine-decisions.md](engine-decisions.md) (D7–D13, D17, D19, D26, D28–D33), [requirements.md](requirements.md) (FR-7, FR-8, FR-9, FR-17, FR-24, PR-1, PR-2, QR-8, BR-1, §6, §7), [engine-approach-and-naming.md](engine-approach-and-naming.md).
- Measurements: [v0b-benchmark-results.md](v0b-benchmark-results.md) (BM-1 frame overhead of v1), [E6](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e6-v1-v2-model/README.md) (file rates per minute, quoted through D28).
- Behavior today: [command-interface.md → TTD Session Rules](../../emulator/design/control-interfaces/command-interface.md#ttd-session-rules), [current-state.md](current-state.md) (B9), [model what-if design](../2026-09-29-model-what-if/design.md) (WI-1, WI-6), [toolbar design](../2026-09-23-ttd-qt-toolbar-widget/ttd-qt-toolbar-widget-design.md), [DeZog reverse debugging](../2026-08-27-dezog-integration/reverse-debugging.md).
- Benchmark harness: [tools/verification/ttd-bench](../../../tools/verification/ttd-bench/README.md); fixtures: [testdata/ttd/README.md](../../../testdata/ttd/README.md).
- Other phases' designs, by file name: `phase-1-memory-regions-tdd.md` (shadow mode, v1 file feeder, oracle), `phase-2-device-state-tdd.md` (restore report), `phase-3-replay-inputs-tdd.md` (event kinds, configuration fingerprint), `phase-4-session-file-tdd.md` (file written as it records, retention).
