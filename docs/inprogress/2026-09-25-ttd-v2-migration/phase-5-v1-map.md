# v1 map for the playback controller (Phase 5, item 2)

A map of `TimeTravelManager` (v1) made on 2026-10-04 at commit 77388c35f, the source the controller (`TimeTravelController`) was copied from. Line numbers refer to that commit. Status flags say what each part needs in the controller: copy, a mechanical index swap, a rewrite for the engine, or drop.

Source: `core/src/debugger/ttd/timetravelmanager.h` (2469 lines), `timetravelmanager.cpp` (7735 lines),
`ttdclipexport.cpp` (258 lines), worktree `scratch/wt-engine`, branch `ttd-engine`. All line numbers below are
`.cpp` lines unless marked `h:`. Ranges run from the signature line to the closing brace.

## Legend: storage flag (column "St")

| Flag | Meaning | What the controller does with it |
|---|---|---|
| **V1** | Reads or writes v1's OWN store: `_timeline` contents beyond time/index, `_pageStore`, `TTDCheckpoint::ramPages` / `peripheralBlobs` / `portReadCursor`, `ReleaseCheckpointRefs`, `RestoreCheckpoint`, `RestoreRamPages`, `CaptureNow` | **Rewrite** against `TimeTravelEngine` |
| **IDX** | Uses `_timeline` only as a checkpoint index (empty?, front/back frame, `upper_bound` by time, size, `cp.time`, `cp.chipset` for `CheckpointStartT`) and restores through `RestoreCheckpointForReplay` / `SeekToInternal` | **Mechanical port**: swap the index for `engine.CheckpointIndexOf / Checkpoint(i)->position / FirstCheckpoint / CheckpointCount`; logic copies verbatim |
| **SA** | Only live machine + standalone classes (journals, coverage, registry, probe, frame cache) | **Copy nearly verbatim** |
| **SH** | Shadow / engine glue (`_shadow*`, `_replayEngine`, `FeedShadow`) | **Do not clone** (but read it: it is the v1->engine adapter, see sections 3-4) |
| **E** | Already has a `_replayEngine` branch (engine-backed variant exists) | Keep the engine branch, drop the v1 branch |

---

## 1. Areas: every method

### 1.1 Recording lifecycle (start / stop / invalidate / resume / feature stewardship / recording lock)

| Method | Lines | St | Purpose |
|---|---|---|---|
| `TimeTravelManager(EmulatorContext*)` | 116-128 | SA | Wires `_context`, `_memory`, `_logger`, `_dirtyTracker = Memory::GetTTDDirtyTracker()` |
| `~TimeTravelManager` | 130-143 | V1 | Unhooks `context->ttdPortReads/Writes`, `ReleaseCheckpointRefs` on every checkpoint |
| `SetUnavailableReason` | 149-155 | SA | Hook; invalidates any session, stores reason (ZX-Poly members) |
| `GetUnavailableReason` | h:458 | SA | Inline getter |
| `StartRecording` | 157-368 | V1+SH | Idle->Recording: park machine, RTC time base (`ttdSessionWall/EmulatedMicros`), `EngageCaptureFeatures`, `RegisterModelPeripherals` (refuse on failure), wipe old history (timeline, page store, journals, coverage), write-journal alloc, `ResolveModelRamPages`, `EngageRecordingLock`, port journals `StartRecording`, baseline `CaptureNow`, `SetState(Recording)`, provenance reset, `_liveRomSignature`, `SyncMediaReadJournal`, publish, resume |
| `StopRecording` | 370-453 | SA (+SH 410-424) | Hook; park, flush coverage, `_recordingStoppedAtT`, `SetState(Idle)`, stop port journals, shadow flush, `FinishShadowFiles`, switch `debugmode` back if we turned it on, publish, resume |
| `BeginDebuggerLiveHistory` | 455-487 | IDX | DeZog live history: adopt Recording / `ResumeRecordingLive` / `StartRecording`; `_recordMode = DebuggerLive` |
| `EndDebuggerLiveHistory` | 489-501 | SA | Back to Session mode + `StopRecording` (history kept) |
| `InvalidateSession` | 503-567 | V1+SH | Drop everything: refs, `_pageStore.Reset`, peripherals, all journals, coverage, port journals, provenance, codec state, `_prevPageCache`, dirty tracker session |
| `OnLoad / OnConfigurationChange / OnModelTransfer` | h:471-473 | SA | Hooks -> `InvalidateSession(reason)` |
| `HasHistory` | h:474 | IDX | Hook; `!_timeline.empty()` |
| `EngageCaptureFeatures` | 569-597 | SA | Switch on `timetravel` + `debugmode` features, remember which (`_toggled*`) |
| `SetState` | 599-607 | SA | The only `_state` writer: recording lock engage/release + `SyncJournalSegment` |
| `EngageRecordingLock` | 672-699 | SA | Host speed -> 1x, `FeatureManager::onTtdRecordingStarted`, `_peripherals.NotifyRecording(true)` |
| `ReleaseRecordingLock` | 701-720 | SA | Reverse of the above |
| `StopForFeatureChange` | 722-731 | SA | Hook; FR-17 clean stop, `_lastStopReason` |
| `UpdateFeatureCache` | 733-764 | SA | Hook; journal segment sync, pre-allocate / free the write journal |
| `RecordingGuard` | 1128-1163 | SA | Hook; refusal text per `TTDGuardedAction` (user recording only) |
| `RequestInvalidation` / `IsInvalidationPending` | 1165-1174, h:491 | SA | Deferred invalidation from a device, applied in `OnFrameBoundary` |
| `IsRecording / GetState / GetRecordMode / IsDebuggerLive` | h:494-498 | SA | Atomic `_state` reads |
| `ResumeRecordingFrom` | 3418-3561 | IDX + V1 (via `TruncateTimelineAfter`, `_prevPageCacheValid`) | `SeekToInternal(from)`, truncate future of timeline + input/markers/tool-edits/bookmarks/coverage/port/write journals/segments, re-engage features, `SetState(Recording)`, port journals record again |
| `ResumeRecordingLive` | 3563-3657 | IDX | Idle->Recording appending (same-frame gap guard; `DropPortJournal` if the machine ran) |
| `TruncateTimelineAfter` | 4186-4225 | V1+SH | Drops checkpoints `> from`, releases refs, `ResetShadow()` (engine has no truncate/branch API yet) |
| `SetEnableCoverageIndex` / `IsCoverageIndexEnabled` | 5881-5892, h:1389 | SA | Coverage on/off + hot flag `ttdCoverageActive` |

### 1.2 Threading / publication (copy as is; see section 5)

| Method | Lines | St | Purpose |
|---|---|---|---|
| `OnMachineThread` | 915-918 | SA | `pMainLoop->IsRunThread()` |
| `SessionOperation::SessionOperation` | 920-939 | SA | Control lock + park a running machine with an active session |
| `SessionOperation::~SessionOperation` | 941-965 | SA | Outermost Change publishes; resume if parked; unlock |
| `OnMachineParking` | 991-995 | SA | Hook; publish summary as the machine parks |
| `GetPublishedSessionInfo` | 997-1004 | SA | Snapshot copy under `_publishedMutex`, sets `_publishRequested` |
| `PublishSessionInfo(info)` / `PublishSessionInfo()` | 1006-1010, h:2052 | SA | Store snapshot / compute + store |
| `MaybePublishAtFrameBoundary` | 1012-1024 | SA | Throttled publication (100 ms, only when asked, never in replay) |

### 1.3 Capture (frame boundary, checkpoint capture, dirty tracking, peripherals, model pages, history limit)

| Method | Lines | St | Purpose |
|---|---|---|---|
| `OnFrameBoundary` | 1176-1267 | V1 (push) + SA | Hook. Pending invalidation; Recording: `ClearFrameCache`, `_coverageIndex.SealFrame(frame-1)`, `CaptureNow`, push, `EnforceHistoryLimit`, `_perf.lastCaptureNs`, publish. Detached: auto-pause past session end |
| `CaptureNow` | 1278-1373 | V1 | Builds `TTDCheckpoint`: time, CPU, chipset, device blobs, port cursors, key/delta decision, RAM refs, `UpdatePrevPageCache`, `FeedShadow`, work counters |
| `CaptureBaselineRamPages` | 1375-1405 | V1 | Every model RAM page -> 4x `_pageStore.InternFull` |
| `IsPageAllZero` (static) | 1410-1423 | SA | 16 KB zero check (key frames) |
| `UpdateRamPages` | 1425-1572 | V1 | AddRef clean / InternXor(Cached) dirty / InternFull on key frames |
| `ReleaseCheckpointRefs` | 1574-1590 | V1 | Release 4 slots per page |
| `UpdatePrevPageCache` | 1592-1620 | V1 | Copy RAM into `_prevPageCache` (XOR delta base) - engine keeps its own delta base |
| `ResolveModelRamPages` | 1622-1654 | SA | Exclusive page bound (48K = 6, else `ramsize/16`) |
| `GetCheckpoint` | 1656-1661 | V1 | `const TTDCheckpoint*` (bench/feeder/tests) |
| `GetCheckpointCount` / `GetEarliestRecordedFrame` | h:1681, h:1689 | IDX | Inline |
| `ComputeRomSignature` | 1692-1706 | SA | `HashBytes` over the ROM region |
| `RegisterModelPeripherals` | 1708-1713 | SA | Wrapper on free `RegisterMachinePeripherals` (`ttdmachineperipherals.cpp:40`) |
| `ReleaseModelPeripherals` | 1715-1722 | SA | `_peripherals.Clear()`, `_ownedPeripherals.clear()` |
| `UpdatePeripheral` | h:1743-1752 | SA | Hook; re-point a registry slot (GS personality switch) |
| `GetPeripheralRegistry` / `GetModelRamPages` / `NotRecordedMask` | h:1707, h:1756, h:1760 | SA | Inline getters |
| `SetHistoryLimit` | 3659-3669 | SA | Sets atomics, enforces while recording |
| `BlobBytes` (static) | 3671-3677 | V1 | Sum of a checkpoint's blob sizes |
| `HistoryBytes` | 3679-3682 | V1 | `_pageStore.GetUsedBytes() + _blobBytes` |
| `EnforceHistoryLimit` | 3684-3698 | V1 | Frame / byte limit -> `EvictOldest` |
| `EvictOldest` | 3700-3730 | V1 | Release oldest refs, erase, cut input/markers/bookmarks/port journals to new front |

### 1.4 Journals (input, port, write + segments, coverage, external events, tool edits, bookmarks)

| Method | Lines | St | Purpose |
|---|---|---|---|
| `JournalLive` | 609-615 | SA | Writes reach the write journal now? |
| `SyncJournalSegment` | 617-637 | SA | Open / close a D40 segment at the current `GlobalT` |
| `ClipJournalSegments` | 639-650 | SA | Cut segments at a resume point |
| `JournalSegments` | 652-670 | SA | Effective segments (open one ends now; ring eviction) |
| `SetWriteJournalCapacity` / `GetWriteJournalCapacity` | 880-892, h:635 | IDX | Only without a session |
| `SwitchWriteJournal` | 894-907 | SA | Pause, `SetEnableWriteJournal`, resume |
| `SetEnableWriteJournal` / `GetEnableWriteJournal` | 1026-1046, h:627 | IDX | D40 switch at any moment |
| `CheckpointStartT` | 1048-1053 | IDX | `GlobalT(cp.time) + cpu_t_in_frame(cp.chipset)` - engine checkpoint has `chipset` too |
| `JournalCoversSession` | 1055-1060 | IDX | One segment over the whole session |
| `RecordInputEvent` | 2016-2039 | SA | Hook; keyboard -> `_inputJournal` |
| `InputEventTimeNow` (file-static) | 2042-2050 | SA | (frame, `TtdTInFrame(z80.t)`) |
| `RecordMouseMove / Buttons / Wheel` | 2052-2084 | SA | Hooks; mouse -> `_inputJournal` |
| `RecordKeyboardReset` / `RecordMouseCounters` | 2086-2106 | SA | Not hooks; `_inputJournal` |
| `GetInputJournal / GetPortReadJournal / GetPortWriteJournal` | h:885-889 | SA | Getters (network devices read `GetInputJournal().NetAt()` on restore!) |
| `PortJournalUnsupportedReason` | 2436-2479 | SA | Machine gate for v1 port replay (Next, NeoGS DMA, IM2 vector source, step hook) |
| `DropPortJournal` | 2481-2497 | V1 (light) | Clears journals and zeroes every `cp.portRead/WriteCursor` |
| `SyncPortJournalHook` | 2499-2505 | SA | `context->ttdPortReads/Writes` = journals when not Off |
| `PutEditRecord` (anon) | 2544-2555 | SA | Tool-edit payload record encoder |
| `BeginToolEdit` | 2557-2566 | SA | Hook; save device states before an edit |
| `EndToolEdit` | 2568-2618 | SA | Hook; payload of dirty RAM pages, dirty region pieces, changed device states + DebuggerEdit marker |
| `ApplyToolEdit` | 2620-2654 | SA | Apply payload during replay (used by engine branch of `ServiceInput`) |
| `ToolEditPayloads` | h:1084 | SA | Getter |
| `RecordExternalEvent` | 2656-2699 | SA | Hook; marker into `_externalEvents` (Read op) |
| `GetExternalEvents` | h:1088 | SA | Getter |
| `RecordMemoryWrite` | 5894-5937 | SA | Hot path from `Memory`: frame-cache capture, coverage Written, write journal record |
| `RecordIoWrite` | 5939-5950 | SA | Hot path from `PortDecoder`: frame-cache capture only |
| `RecordReadCoverage` / `RecordExecutedCoverage` | h:1359-1383 | SA | Inline hot path (Memory / Z80 M1) |
| `GetCoverageIndex` / `GetWriteJournal` | h:1386, h:1635 | SA | Getters |
| `AddBookmark` | 2814-2847 | IDX | Validates against session end, `_bookmarks.Add` |
| `GetBookmarks / FindBookmark / RemoveBookmark` | 2849-2865 | SA | `TTDBookmarkJournal` wrappers |

### 1.5 Seek / replay (restore, replay mode, input playback, port replay, display, auto pause, Detached, resume input)

| Method | Lines | St | Purpose |
|---|---|---|---|
| `RestoreCheckpointForTesting` | 1667-1690 | V1 | Test entry: `RestoreCheckpointForReplay(_timeline[idx])` |
| `RestoreCheckpoint` | 1724-1844 | V1 + **E** (1790-1806) | CPU, chipset, `cpu->t`, frame timing, devices, banks, RAM, screen resync, perf |
| `ResyncScreenState` | 1846-1882 | SA | `InitRaster`, active screen, border, `ResetPrevTstate`, `InitFrame` (no pixels) |
| `RestoreRamPages` | 1884-1926 | V1 | `_pageStore.GetPage` per sub-page into `RAMPageAddress(p)` |
| `EnterReplayMode` | 1932-1971 | SA | `ttdReplayActive`, media `HoldHostWrites`, `SyncMediaReadJournal`, audio `HostOutputHold`, debug memory path on |
| `ExitReplayMode` | 1973-2005 | SA + E (2001-2004 stop engine playback) | Reverse of the above |
| `ReplayModeScope` | h:823-841 | SA | RAII enter/exit |
| `IsReplayActive` | 2007-2010 | SA | Hook |
| `OwnsInput` | 2112-2126 | IDX | Hook; replay active, or Detached and `frame <= last checkpoint` |
| `SetLiveInputInterceptor` | 2128-2132 | SA | Hook; ZX-Poly lockstep |
| `SubmitLiveInput` (x2) | 2134-2143 | SA | Hooks -> `SubmitLiveInputImpl` |
| `SubmitLiveInputImpl` | 2145-2198 | SA | Refuse while `OwnsInput`; interceptor; `RunWhileParked` / queue / apply now |
| `SubmitMachineTask` | 2200-2220 | SA | Hook; queue or run on machine thread |
| `ApplyLiveInput` | 2222-2247 | SA | Journal (when Recording) then `ApplyInputEvent` |
| `ServiceInput` | 2249-2320 | SA + **E** (2255-2286 engine event log; 2287-2299 v1 journal) | Hook; per-instruction playback, drain live input, run tasks |
| `DrainPendingLiveInput` | 2322-2335 | SA | Apply queued live input unless journal owns input |
| `UpdateInputWorkFlag` | 2337-2348 | SA | `kStepWorkTtdInput` step gate |
| `ArmInputPlayback` | 2350-2369 | SA + **E** (2360-2366) | Cursor at restored time (v1 journal or engine `Events().CursorAt`) |
| `DisarmInputPlayback` | 2371-2376 | SA | |
| `OnMachineReset` | 2378-2395 | SA | Hook; stop playback, port journals live, Detached->Idle |
| `RestoreCheckpointForReplay` | 2397-2434 | V1 + **E** (2403-2417) | `RestoreCheckpoint` + `ArmInputPlayback` + port playback (v1 cursors or engine `PlayBus` + media) |
| `CurrentPosition` / `CurrentFrame` | 2705-2720, h:1003 | SA | (frame_counter, `TInFrameNow`) |
| `TInFrameNow` / `FrameSpan` / `GlobalT` | 2722-2736, h:1020 | SA | TTD time units (top clock) |
| `RunToTInFrame` | 2738-2752 | SA | `RunTStates` loop to a TTD-time target inside the frame |
| `SessionEndPosition` | 2754-2760 | IDX | `_timeline.back().time` |
| `SeekTo(target, result)` / `SeekTo(target)` | 2762-2808, h:1140 | SA | Refuse while Recording; `SeekToInternal`; `PresentPosition` |
| `SeekToBookmark` | 2867-2892 | SA | `FindBookmark` + `SeekTo` |
| `PublishSeekedFrame` | 2894-2934 | SA | `FlushAndPresentFramebuffer` + `NC_VIDEO_FRAME_REFRESH` |
| `SeekToInternal` | 2938-3142 | IDX + **E** (3066-3094) | `upper_bound` checkpoint, `RestoreCheckpointForReplay`, barrier check, `ReplayWithinFrame`, `SetState(Detached)` |
| `ReplayWithinFrame` | 3144-3198 | SA | Clamp, `ReplayModeScope`, `RunToTInFrame`, `_perf.lastReplayNs` |
| `RunToFrameEnd` | 3200-3211 | SA | Run the rest of the frame (frame-end processing paints it) |
| `ComposeDisplay` | 3213-3336 | IDX | Sandbox (`SaveLiveState` local) -> restore neighbor checkpoint(s), replay, copy framebuffer + plane B -> `RestoreLiveState` -> write pixels |
| `PresentPosition` | 3338-3349 | SA | `ComposeDisplay` + `PublishSeekedFrame`, keeps `_lastEngineCheck` |
| `StepBackFrame` / `StepForwardFrame` | 3351-3412 | IDX | `SeekTo({frame-1/+1, 0})` |
| `ConsumeAutoPauseRequest` | 1269-1272 | SA | Test flag set by `OnFrameBoundary` Detached branch |
| `TimePointOf` (anon) | 91-95 | SA | globalT -> (frame, t) |

### 1.6 Instruction level (frame cache, step instruction, reverse step, reverse continue)

| Method | Lines | St | Purpose |
|---|---|---|---|
| `StepBackInstruction` | 6446-6488 | IDX | `FindLastAccess(Execute, now-1)` (seeks on hit) |
| `StepForwardInstruction` | 6490-6530 | IDX | `RunTStates(1)` in replay mode + `PresentPosition(false)` |
| `EnumerateM1InRange` | 6545-6717 | IDX | Walk checkpoint intervals backward; restore, arm Execute probe, replay, collect M1 records; v1 marker barrier |
| `ReverseStepInstructions` | 6719-6827 | IDX | n <= 4: repeated step-back; else enumerate + `SeekTo` |
| `ReverseStepTStates` | 6829-6903 | IDX | Enumerate, last M1 <= target, `SeekTo` |
| `ReverseContinue` | 6904-7168 | IDX | Coverage-pruned per-frame enumerate for PC set, fallback prefix / full enumerate, `SeekTo` |
| `ClearFrameCache` | 7174-7179 | SA | Free `_frameCache` |
| `CaptureM1` | 7181-7229 | SA | `m1TraceHook` body: registers, opcodes, SP content, bank slots |
| `BuildFrameCache` | 7231-7283 | IDX | `SeekToInternal({frame,0})` + hook + run frame |
| `SaveLiveState` | 7285-7353 | SA | Exact live snapshot (CPU, chipset, z80.t, devices, input cursor, port positions, keyboard, framebuffer, plane B, RAM) - store independent |
| `RestoreLiveState` | 7355-7426 | SA | Reverse of the above |
| `GetFrameCache` | 7428-7496 | IDX | Save live -> replay-scope build -> restore live; DebuggerLive paused exception |
| `GetFrameCacheBytes` / `GetCachedFrame` | h:1665-1674 | SA | |

### 1.7 Queries (find-last, port search, journal build, coverage queries)

| Method | Lines | St | Purpose |
|---|---|---|---|
| `SearchPortEvents` | 2507-2543 | SA | Hook; free `ttd::SearchPortEvents(_portReads, _portWrites, q)` (`ttdportsearch.cpp:349`) |
| `FindLastAccess` | 6160-6444 | IDX | Io: port-write journal; Write: journal segments `FindLastInRange`; else per-interval coverage prune + restore + probe replay; v1 marker barrier; `SeekTo(answer)` |
| `RegenerateFrameWrites` | 5952-5978 | IDX | Restore frame, arm Write probe, replay whole frame |
| `BuildWriteJournal` | 5980-6138 | IDX + SH (6127 `ResetShadow`) | Replay frames outside segments into the write journal (progress / cancel atomics) |
| `BuildWriteJournalFrames` | 6140-6159 | IDX | Frame numbers -> machine times |
| `GetJournalBuildState` / `CancelJournalBuild` | h:1495-1501 | SA | Atomics |
| `CanPruneByCoverage` | h:1420-1434 | SA | Inline gate |
| `QueryCoverageProbe` | 7498-7537 | SA | `_coverageIndex` |
| `QueryCoverageScan` | 7539-7612 | SA | `_coverageIndex` |
| `QueryCoverageSummary` | 7614-7735 | IDX (+`cp.frameKind`) | Buckets; `hasKeyframe` reads v1 `TTDCheckpoint::frameKind` (engine: `HasFullTable` / segment baseline) |

### 1.8 Files (.ttd v1, file info, clip export, source path, self test)

| Method | Lines | St | Purpose |
|---|---|---|---|
| `WritePod / ReadPod / WriteBlob / ReadBlob` (anon) | 4257-4334 | V1-format | Stream helpers |
| `Write/ReadInputJournalSection`, `Write/ReadNetInputSection`, `Write/ReadExternalEventSection` (anon) | 4340-4612 | V1-format | Section codecs |
| `SerializeSession` | 4614-5033 | V1 | Header + page store slots + checkpoints + journals + coverage + bookmarks |
| `TurboSoundSessionKindMatches` (static) | 5035-5058 | SA | Pure check on a blob map |
| `DeserializeSession` | 5060-5073 | V1+SH | `ResetShadow` + `Impl` |
| `SearchPortEventsInFile` | 5075-5092 | V1-format | `Impl(journalsOnly)` + search |
| `DeserializeSessionImpl` | 5094-5815 | V1 | Parse/stage/check (ROM, TSFM kind, ...), commit at 5716-5800, `SetState(Idle)` |
| `SetSessionSourcePath` | 909-913 | SA | Provenance |
| `CaptureRestoreSelfTest` | 5817-5875 | V1 | `CaptureNow` -> `RestoreCheckpoint` -> `ReleaseCheckpointRefs` + hash compare |
| `WriteFile` (anon, clip) | ttdclipexport 36-45 | SA | |
| `VisitComposedFrames` | ttdclipexport 47-93 | IDX | Per frame `SeekToInternal` + `ComposeDisplay(true)` + visitor |
| `ExportClip` | ttdclipexport 95-258 | IDX | Visitor writing zstd RGBA / plane B chunks + meta |

### 1.9 Status

| Method | Lines | St | Purpose |
|---|---|---|---|
| `GetSessionInfo` | 766-878 | V1 (page store stats, `frameKind` count, `front().peripheralBlobs` mask) + SA | Full `TTDSessionInfo`, publishes it |
| `ReadSessionInfo` | 967-989 | SA | Live or published depending on thread / lock / machine state |
| `EstimateSessionHeapBytes` | 1062-1065 | V1 | `GetHeapBreakdown().Total()` |
| `GetHeapBreakdown` | 1067-1122 | V1 + SA | Page store, checkpoints, blobs, refs, journals, coverage, port, frame cache |
| `GetPerfCounters` / `GetPageStore` | h:1702, h:1699 | V1 (page store) | Bench / tests |
| `TTDSessionStateToString` (free) | 101-110 | SA | |

`TTDSessionInfo` fields (h:199-336) and their sources: `state`; `sessionStartFrame/currentEndFrame/checkpointCount` (timeline); `pageStoreBytes/UsedBytes/baselineFramesCaptured/compressionRatio/livePayloadBytes` (page store, **V1**); `keyFrameCount/deltaFrameCount` (`frameKind`, **V1**); `sessionHeapBytes` (heap breakdown); `historyLimitFrames/Bytes/evictedCheckpoints/historyBytes`; `writeJournal*` (enabled, segments, spans, complete, records, bytes); provenance (`loadedFromFile, sourcePath, capturedAtUnixMs, modelId, modelRamPages, machine, recordedBy`); `coverageIndexFrames/Bytes`; `bookmarkCount, inputEventCount, externalEventCount, inputHistoryComplete`; port journal (`portJournalActive, OffReason, portRead/WriteCount, portJournalBytes, portReplayValueMismatches, portReplayDivergences`); `lastDropReason, lastStopReason, unavailableReason`. `TTDControl::StatusBody` (ttdcontrol.cpp:347) renders them.

### 1.10 Shadow / engine glue (NOT to be cloned)

| Method | Lines | Purpose |
|---|---|---|
| `SetShadowEngine` / `GetShadowEngine` | 3732-3739, h:585 | Attach the engine fed in parallel |
| `SetShadowRecordingRoot` / `ShadowRecordingFolder` | h:582, 3799-3802 | Segment files of the shadow session |
| `RegisterScreenshotStream` | 3741-3764 | Engine frame stream 0 (RGBA + w/h/mode) - **reusable idea** for the controller |
| `NoteFact` | 3766-3780 | Pending engine fact at the current instant |
| `NoteRzxFrameEnd` / `NoteReplaySource` | 3782-3797 | Hooks -> facts (controller: `engine.AppendEvent` directly) |
| `FinishShadowFiles` / `DiscardShadowFiles` / `ResetShadow` | 3804-3827 | Writer finish / folder discard / `EndSession` |
| `ArmShadowRegions` | 3829-3836 | `ITTDRegionSource::TTDArmRegions` (device memory dirty marks) |
| `MediaReadAdapter::Playing / Play / Record` | 3838-3856 | `IMediaReadJournal` over `engine.MediaReads()` |
| `SyncMediaReadJournal` | 3858-3872 | Media manager read journal + `context->ttdVectors` |
| `LiveRegions` | 3874-3890 | RAM + device regions as `TTDRegionDesc` |
| `CheckEngineCheckpoint` | 3892-3928 | `CheckConfiguration` + media version check / `SetHead` -> `_lastEngineCheck` |
| `SetReplaySource` / `GetReplaySource` / `LastEngineCheck` | 3930-3941, h:592, h:599 | Seek from engine; `BindLive` |
| `FeedShadow` | 3943-4184 | v1 capture -> `TTDFrameInput` -> `engine.CaptureFrame` (section 4) |

Note: although "not cloned", `MediaReadAdapter`, `SyncMediaReadJournal`, `LiveRegions`, `CheckEngineCheckpoint`, `RegisterScreenshotStream` and the body of `FeedShadow` are exactly what the controller needs as its OWN capture/replay plumbing (with `_shadowEngine` / `_replayEngine` collapsed into the one owned engine).

### 1.11 Hooks (`ITimeTravelHooks` overrides, timetravelhooks.h:98-167)

`OnFrameBoundary` 1176, `OnMachineParking` 991, `ServiceInput` 2249, `OnMachineReset` 2378, `StopRecording` 370, `NoteRzxFrameEnd` 3782, `NoteReplaySource` 3791, `OnLoad` / `OnConfigurationChange` / `OnModelTransfer` h:471-473, `RecordExternalEvent` 2656, `BeginToolEdit` 2557, `EndToolEdit` 2568, `UpdatePeripheral` h:1743, `SubmitLiveInput` x2 2134/2139, `OwnsInput` 2112, `SetLiveInputInterceptor` 2128, `SubmitMachineTask` 2200, `RecordInputEvent` 2016, `RecordMouseMove/Buttons/Wheel` 2052-2084, `GetState` / `IsRecording` h:494-496, `IsReplayActive` 2007, `CurrentFrame` h:1003, `SearchPortEvents` 2507, `HasHistory` h:474, `RecordingGuard` 1128, `SetUnavailableReason` 149, `UpdateFeatureCache` 733, `StopForFeatureChange` 722.

NOT in the hooks interface but called on `pTimeTravelManager` from the emulator hot paths: `RecordMemoryWrite` (memory.cpp:443), `RecordReadCoverage` (memory.cpp:339), `RecordExecutedCoverage` (z80.cpp:1141), `RecordIoWrite` (portdecoder.cpp:425). The controller needs these wired too (new hooks or a second context pointer); they are hot paths (performance-guidelines: A/B).

---

## 2. Dependencies per area

### 2.1 Standalone classes / free functions (reusable as is)

| Class / function | File | Used by areas |
|---|---|---|
| `TTDInputJournal` (`_inputJournal`) | ttdinputjournal.h:155 | Lifecycle (clear/DropAfter), Capture (`EvictOldest` DropBefore), Journals (record), Input (playback cursor, `FirstIndexAtOrAfter`, `NetOf/PayloadOf`), Status, Files, Shadow (`FeedV1Events`); network devices (`ttdzifi.cpp:70`, `ttdzxnetusb.cpp:53`, `ttdserialport.cpp:56`, `ttdmachineserialpeer.cpp:40`) read `GetInputJournal().NetAt()` |
| `TTDPortJournal` x2 (`_portReads/_portWrites`) | ttdportjournal.h:62 | Lifecycle (StartRecording/Stop/TruncateTo), Capture (cursors in checkpoint, DropBefore), Seek (StartPlayback, `RestorePosition`), Queries (`SearchPortEvents`, FindLastAccess Io), Status |
| `TTDWriteJournal` (`_writeJournal`) + `_journalSegments` | ttdwritejournal.h:76 | Journals, Lifecycle, Queries (FindLastAccess Write, BuildWriteJournal), Status |
| `TTDCoverageIndex` (`_coverageIndex`), `MakeCoverageKey` | ttdcoverageindex.h:102 / :89 | Capture (`SealFrame`), Journals (Record), Lifecycle (DropFramesFrom, Clear), Queries, Instruction (ReverseContinue), Status. Engine has NO coverage |
| `TTDExternalEventJournal` (`_externalEvents`) | ttdexternalevents.h:104 | Journals, Seek (barrier), Queries/Instruction (barrier `FirstMarkerInInterval`), Lifecycle, Files |
| `TTDBookmarkJournal` (`_bookmarks`) | ttdbookmarks.h:78 | Journals, Lifecycle, Capture (evict), Files. Engine has NO bookmarks |
| `TTDFrameCache` (`_frameCache`) | timetravelframecache.h:76 | Instruction; `RecordMemoryWrite/RecordIoWrite` append during build |
| `TTDAccessProbe` (`context->ttdProbe`) | ttdprobe.h:145 | Queries, Instruction (`Arm/ExtractHits/Disarm`) |
| `TTDPeripheralRegistry` (`_peripherals`, `_ownedPeripherals`) | ttdperipheralregistry.h:68 | Capture (`CaptureAll`, `LastCaptureState*`), Seek (`RestoreAll`), Lifecycle (`NotifyRecording`, Register/Clear), Journals (tool edits, `RegionSources`), Instruction (`SaveLiveState`), Shadow (`DeviceEntries`, `RegionSources`) |
| `TTDDirtyTracker` (`_dirtyTracker`, owned by Memory) | ttddirtytracker.h:48 | Capture (`CollectAndClear`), Lifecycle (`ResetSession`), Journals (`IsDirty` in `EndToolEdit`) |
| `CaptureCpuState / RestoreCpuState / CaptureChipsetState / RestoreChipsetState / GetChipsetCpuTInFrame` | ttdcheckpoint.h:341-361, :243 | Capture, Seek, Instruction (live snapshot), Journals (`CheckpointStartT`) |
| `RegisterMachinePeripherals` | ttdmachineperipherals.cpp:40 | Capture |
| `ApplyInputEvent / InputDevicesOf` | ttdinputapply.h:46-51 | Input |
| `SearchPortEvents` (free) | ttdportsearch.cpp:349 | Queries |
| `FeedV1Events`, `TTDV1EventCursor`, `TTDPendingFact` | ttdv1events.h:26-49 | Shadow only (controller writes events directly) |
| `CaptureConfigFingerprint` | ttdconfigcapture.cpp:13 | Shadow (`SetConfiguration`, `CheckEngineCheckpoint`) |
| `HashBytes`, `CaptureSnapshot/HashSnapshot` | machinestatehash.h:48/130 | Capture (ROM signature), self test |
| `DescribeRecordedMachine` | ttdfileinfo.cpp:191 | Status |
| `SoundManager::HostOutputHold`, `MediaManager::HoldHostWrites / SetReadJournal`, `Core::SetSpeedMultiplier`, `FeatureManager::onTtdRecordingStarted/Stopped` | emulator | Seek (replay mode), Lifecycle (lock) |

### 2.2 v1-own storage (must be rewritten for the engine)

| Member | Declared | Touched by (methods) |
|---|---|---|
| `std::vector<TTDCheckpoint> _timeline` (content: `ramPages`, `peripheralBlobs`, `portRead/WriteCursor`, `frameKind`, `keyFrameAnchor`) | h:2096 | **V1**: dtor, StartRecording, InvalidateSession, OnFrameBoundary (push), CaptureNow (`back().ramPages`), UpdateRamPages, GetCheckpoint, RestoreCheckpointForTesting, EvictOldest, EnforceHistoryLimit, DropPortJournal, TruncateTimelineAfter, GetSessionInfo (`frameKind`, `front().peripheralBlobs`), GetHeapBreakdown, Serialize/Deserialize, QueryCoverageSummary (`frameKind`). **IDX only**: everything else listed IDX in section 1 |
| `TTDCodecPageStore _pageStore` | h:2099 | StartRecording, InvalidateSession, CaptureBaselineRamPages, UpdateRamPages, ReleaseCheckpointRefs, RestoreRamPages, HistoryBytes, GetSessionInfo, GetHeapBreakdown, GetPageStore, Serialize/Deserialize |
| `_prevPageCache`, `_prevPageCacheValid` | h:2264-2265 | UpdateRamPages, UpdatePrevPageCache, InvalidateSession, ResumeRecordingFrom, DeserializeSessionImpl |
| `_lastKeyFrameIdx`, `_forceNextKeyFrame`, `kKeyFrameInterval` | h:2199, 2212, 434 | CaptureNow, InvalidateSession, DeserializeSessionImpl (tests read `kKeyFrameInterval`) |
| `_blobBytes`, `_evictedCheckpoints`, `_historyLimit*` | h:2202-2205 | Capture / history limit, Status |
| `_dirtyScratch` | h:2268 | CaptureNow (also read by FeedShadow) |
| `_captureWork`, `_perf` | h:2104-2105 | CaptureNow, RestoreCheckpoint, ReplayWithinFrame, PresentPosition (bench) |

### 2.3 Other session fields (copy as is)

`_state` (atomic), `_recordMode`, `_modelRamPages`, `_dirtyPageOverflowReported`, provenance (`_loadedFromFile, _inputHistoryComplete, _sourcePath, _capturedAtUnixMs, _sessionModelId, _loaded*`, `_liveRomSignature`), `_seekLandedOnCheckpoint`, input (`_inputCursor, _inputPlaybackArmed, _pendingInput*, _pendingTasks, _liveInputInterceptor*`), port flags (`_portJournalValid, _portJournalRecorded, _portJournalOffReason`), write journal (`_enableWriteJournal, _writeJournalBytes, _journalSegments, _journalBuild*`), `_enableCoverageIndex`, reasons (`_lastDropReason, _lastStopReason, _unavailableReason, _recordingStoppedAtT`), replay mode (`_inReplayMode, _replayHostHold, _debugModeBeforeReplay`), `_autoPauseRequested`, `_pendingInvalidation`, feature stewardship (`_toggledDebugModeOn/TimeTravelOn, _recordingLockEngaged, _savedHostSpeedMultiplier`), frame cache (`_frameCache, _frameCaptureActive, _capturingCache, _liveSnapshot`), tool edits (`_toolEditBefore, _toolEditOpen, _toolEditPayloads`), publication (`_publishedMutex, _published, _publishRequested, _controlMutex, _operationDepth, _lastPublish`).

---

## 3. Restore path

### 3.1 v1: `RestoreCheckpoint(const TTDCheckpoint& cp)` (1724-1844)

| Step | Lines | What |
|---|---|---|
| 0 | 1727-1729 | `_shadowRescan = true`; `_shadowEngine->ForgetMemory()` (glue) |
| 1 | 1750-1756 | Engine lookup: `_replayEngine->CheckpointIndexOf({0, cp.time.frame, 0})` -> `engineCp` (null without replay source) |
| 2 | 1758-1762 | CPU: `RestoreCpuState(cpuState, Z80State*)` + `SetNmiPending` (from `engineCp->cpu` when present) |
| 3 | 1768 | Chipset: `RestoreChipsetState(chipsetState, &emulatorState)` (pure field copy; sets `frame_counter`, `t_states`) |
| 4 | 1773-1780 | `cpu->t = GetChipsetCpuTInFrame(chipset)` (frame-end overshoot), `RecomputeFrameTiming()` |
| 5E | 1790-1806 | **Engine branch**: `ForgetMemory`, `CheckEngineCheckpoint(i,false)`, `RestoreToMemory(i)` (all regions incl. device memory), `RestoreDevices(i, {frame,0,true})`, `UpdateZ80Banks`, `ResyncScreenState`, **return** (no perf counters) |
| 5 | 1807-1813 | Devices: `_peripherals.RestoreAll(cp.peripheralBlobs)` - BEFORE banks (model latches feed paging, e.g. Scorpion ProfROM) |
| 6 | 1819 | `_memory->UpdateZ80Banks()` |
| 7 | 1826 | `RestoreRamPages(cp.ramPages)` (1884-1926: `_pageStore.GetPage` per 4 KB, CRC fail -> zero fill, NEVER_TOUCHED skipped) |
| 8 | 1836 | `ResyncScreenState()` (1846-1882) |
| 9 | 1838-1841 | `_perf.lastRestore*Ns` |

Order difference to note: v1 restores devices BEFORE RAM (then banks, then RAM); the engine branch restores memory regions first, then devices, then banks (engine `RestoreDevices` doc: "Run after the memory regions are restored").

### 3.2 Callers

| Caller | Lines | Via |
|---|---|---|
| `RestoreCheckpointForReplay` | 2397-2434 | `RestoreCheckpoint` + `ArmInputPlayback` + port playback |
| `RestoreCheckpointForTesting` | 1667-1690 | `RestoreCheckpointForReplay(_timeline[idx])` |
| `CaptureRestoreSelfTest` | 5849-5851 | `RestoreCheckpoint` directly (no input/port arming) |
| `SeekToInternal` | 3036 | `RestoreCheckpointForReplay(cp)` |
| `ComposeDisplay` | 3267-3311 | `RestoreCheckpointForReplay(*cp/*prev/*cur)` |
| `RegenerateFrameWrites` | 5965 | `RestoreCheckpointForReplay(*it)` |
| `FindLastAccess` | 6399 | per interval |
| `EnumerateM1InRange` | 6653 | per interval |
| Indirect via `SeekToInternal` | | `SeekTo`, `ResumeRecordingFrom` (3466), `BuildFrameCache` (7260), `VisitComposedFrames` (clip 65), `BuildWriteJournal` (6134 via SeekTo) |

### 3.3 `RestoreCheckpointForReplay` (2397-2434)

1. `RestoreCheckpoint(cp)`.
2. `ArmInputPlayback()` (2350-2369): `_inputCursor = _inputJournal.FirstIndexAtOrAfter(now)`; `_inputPlaybackArmed`; **engine**: `Frames().Start(now.frame, start)` + `_engineEventCursor = Events().CursorAt(start + now.tInFrame)`; `UpdateInputWorkFlag()` sets `kStepWorkTtdInput`.
3. **Engine** (2403-2417): stop v1 port journals; `PlayBus(busRead/Write/VectorCursor)`, `MediaReads().StartPlayback(mediaReadCursor)`, `context->ttdPortReads/Writes = BusReads/WritesForPlayback()`. Else v1 (2421-2433): `_portReads.StartPlayback(cp.portReadCursor)` / `_portWrites...` when `_portJournalValid`, else Stop; `SyncPortJournalHook()`.

### 3.4 Seek inside a frame: `SeekToInternal` (2938-3142)

| Step | Lines | What |
|---|---|---|
| Defaults / checks | 2947-2990 | result = OutOfRange; empty timeline / `target.frame > last.frame` fail |
| Pick checkpoint | 3004-3020 | `upper_bound(_timeline, target)` - 1 (rightmost `cp.time <= target`) |
| Restore | 3036 | `RestoreCheckpointForReplay(cp)` |
| Landed? | 3046-3049 | `restoredTInFrame = z80.t`; `_seekLandedOnCheckpoint = !(target.tInFrame > restoredTInFrame)` |
| Engine replay | 3066-3094 | `Events().FirstBarrierIn(start, start+target.tInFrame)` -> replay to barrier, `CheckEngineCheckpoint(i, true)`, Detached, ExternalEvent halt; else `ReplayWithinFrame` + check |
| v1 replay | 3095-3124 | `_externalEvents.FirstMarkerInInterval(cp.time, target)` -> replay to marker, Detached, halt; else `ReplayWithinFrame(cp.time.frame, target.tInFrame)` |
| Finish | 3129-3141 | `SetState(Detached)`, result reached |

`ReplayWithinFrame` (3144-3198): clamp to `FrameSpan()`, `ReplayModeScope` (3186) = `EnterReplayMode` (1932-1971: `ttdReplayActive=true`, `HoldHostWrites(true)`, `SyncMediaReadJournal`, audio hold, `isDebugMode=true` + `SelectMemoryInterface`, `Memory::UpdateFeatureCache`), `RunToTInFrame` (2738-2752: `RunTStates(skipBreakpoints=true)` loop in TTD units). During the run, every instruction calls `ServiceInput` (2249) which plays the v1 journal or the engine event log (incl. `ApplyToolEdit` for DebuggerEdit input events); IN/OUT go through `context->ttdPortReads/Writes` in Play mode (recorded values fed to the CPU, mismatches counted); media sector reads through `MediaReadAdapter::Play` when the engine plays. `ExitReplayMode` (1973-2005) releases all, stops engine media/bus playback.

Public `SeekTo` (2762-2808) adds: clear `_autoPauseRequested`, refuse while Recording, `PresentPosition(target.tInFrame == 0)` (2805) on success or marker halt -> `ComposeDisplay` sandbox + `PublishSeekedFrame`.

State after a seek: `_state = Detached` (recording lock held), input playback armed (journal owns input: `OwnsInput` true until `frame_counter > last checkpoint`), port journals in Play, `OnFrameBoundary` Detached branch auto-pauses past session end (1245-1266). `OnMachineReset` (2378) or `StartRecording`/`ResumeRecordingFrom` leave Detached.

### 3.5 Engine restore API vs v1

| v1 | Engine | Already wired in v1? |
|---|---|---|
| `_timeline` `upper_bound` by `TTDTimePoint` | `CheckpointIndexOf(TTDPosition)` (exact frame only, -1 otherwise), `Checkpoint(i)->position`, `FirstCheckpoint()`, `CheckpointCount()`, `PositionOf(machineTime)`, `Frames().Start(frame)` | Only exact-frame lookup (1750, 2405, 3070); v1 still picks the checkpoint from `_timeline` |
| `cp.cpu`, `cp.chipset` | `TTDEngineCheckpoint::cpu/chipset` | Yes (1756-1757) |
| `RestoreRamPages(cp.ramPages)` + `UpdateZ80Banks` | `RestoreToMemory(i, written, stats)` (all regions with live memory; skips unchanged pieces) | Yes (1795) |
| `_peripherals.RestoreAll(blobs)` | `RestoreDevices(i, TTDRestoreContext)` | Yes (1796) |
| `cp.portReadCursor/WriteCursor` + `TTDPortJournal::StartPlayback` | `busRead/Write/VectorCursor` + `PlayBus`, `BusReadsForPlayback()` | Yes (2409-2413) |
| v1 input journal cursor | `Events().CursorAt`, `ToInput`, `UnpackNet`, `Payloads()` | Yes (2255-2286, 2360-2366) |
| `_externalEvents.FirstMarkerInInterval` | `Events().FirstBarrierIn(from, to)` | Only in `SeekToInternal` (3077); **not** in `FindLastAccess` (6358), `EnumerateM1InRange` (6630), `RegenerateFrameWrites` (5962) |
| (none) media reads | `MediaReads().StartPlayback/PlayNext` | Yes (2412, adapter 3838-3856) |
| (none) config / media check | `CheckConfiguration`, `MediaVersionAt` | Yes (`CheckEngineCheckpoint` 3892) |
| `_writeJournal->FindLastInRange` + segments | `Writes()` (`TTDWriteIndex::FindLastInRange`, `Segments()`) | **No** (queries still use v1 journal) |
| `_coverageIndex` | none | n/a |
| `_bookmarks` | none | n/a |
| `TruncateTimelineAfter` / branches | none (`TTDEngineCheckpoint::parent`, `TTDPosition::branch` exist, no API) | v1 ends the engine session (`ResetShadow`, 4204) |
| `EvictOldest` / history limit | `SetHistoryPolicy` (Ring: window/segment frames), `Segments()` | No |
| `SetReplaySource` binding | `BindLive(LiveRegions(), DeviceEntries())` (3939) | Yes |

So: the CPU/chipset/memory/device restore, bus/media/input playback and the in-seek barrier already have engine-backed code (blocks marked **E**). What is still v1-only on the restore side: checkpoint selection (`_timeline`), perf counters on the engine path, barriers in the query/enumerate paths, write-journal queries, coverage, bookmarks, truncate/resume, history limit.

---

## 4. Capture path

`MainLoop::CompleteFrame` (mainloop.cpp:455-480: `MediaManager::ApplyPending`, `Core::OnNetworkFrameDevices`, then `pTimeTravelHooks->OnFrameBoundary()`) -> `OnFrameBoundary` (1176) -> `SealFrame(frame-1)` (1219) -> `CaptureNow` (1278) -> push -> `EnforceHistoryLimit` (1225) -> `MaybePublishAtFrameBoundary` (1228).

### 4.1 What `CaptureNow` collects

| Item | Lines | Source |
|---|---|---|
| Time | 1285-1288 | `frame_counter`, `tInFrame = 0`, `globalT = frame` |
| CPU | 1292-1297 | `CaptureCpuState(Z80State)` (ttdcheckpoint.cpp:22) + NMI pending |
| Chipset | 1301 | `CaptureChipsetState(emulatorState, z80.t)` (ttdcheckpoint.cpp:132) - holds the overshoot |
| Devices | 1308-1311 | `_peripherals.CaptureAll(out.peripheralBlobs)` (compressed blobs); raw states kept in the registry (`LastCaptureState(id)`, `LastCaptureStateBytes`) |
| Port cursors | 1315-1316 | `_portReads.Size()`, `_portWrites.Size()` |
| Key/delta | 1323-1326 | `kKeyFrameInterval = 50`, `_forceNextKeyFrame` |
| RAM | 1328-1357 | baseline: `CaptureBaselineRamPages`; else `_dirtyTracker->CollectAndClear(_dirtyScratch)` (1354) + `UpdateRamPages` |
| Delta base | 1362 | `UpdatePrevPageCache` |
| Shadow | 1364-1365 | `FeedShadow(out, baseline)` |
| Work | 1367-1372 | page store work counters -> `_perf.lastCaptureWork` |

### 4.2 What `FeedShadow` (3943-4184) turns into `TTDFrameInput` (engine/ttdframeinput.h)

| Step | Lines | Into the engine |
|---|---|---|
| New session when needed | 3947-4001 | Region 0 = machine RAM (`pieces = modelRamPages*4`, contiguous `RAMPageAddress(0)`); regions 1.. = `ITTDRegionSource::TTDRegions` device memory; `engine.BeginSession(regions, _peripherals.DeviceEntries())`; `ArmShadowRegions(true)`; event cursors; `RegisterScreenshotStream`; RZX source fact; bus cursors; write-journal seq; ROM signature; `SyncMediaReadJournal` |
| Media versions | 4006-4019 | `IMediaHistory::VersionStamp` -> `engine.NoteMediaVersion` |
| `in.position.frame`, `in.start` | 4021-4033 | machine time from the frame table: `_shadowLastStart + (t_states - _shadowLastBase) * ttd_clock_units` (NOT v1's `frame * FrameSpan`) |
| `in.cpu`, `in.chipset` | 4034-4035 | copies of the v1 checkpoint's |
| `in.deviceStates` | 4036-4057 | raw states from the registry; `TTDStateWithoutRegions` strips region memory (GS) |
| `in.changed` RAM | 4058-4064, 4093-4100 | 4 pieces per dirty page from `_dirtyScratch` (all pages on `_shadowRescan`) |
| `in.changed` devices | 4065-4091, 4101-4104 | `r.tracker->CollectAndClear` per region (all on rescan / `compareEachCapture`), partial piece padded |
| Bus data | 4107-4115 | `AppendBusRead/Write` from v1 port journals since last cursor |
| Capture | 4117-4122 | `engine.CaptureFrame(in, error)` |
| Write journal | 4124-4130 | `engine.Writes().Append(...)`, `SetSegments(JournalSegments())` |
| Events | 4132-4135 | `FeedV1Events` (input, net, markers, tool edits, facts) |
| Frame length change | 4139-4148 | `FrameLengthChange` event |
| Configuration | 4150 | `engine.SetConfiguration(frame, CaptureConfigFingerprint)` |
| Files | 4155-4183 | `TTDRecordingFolder` + `TTDRecordingWriter::Begin/Collect` |

The controller's capture = `FeedShadow` without the v1 checkpoint: build `in.cpu/chipset/deviceStates` directly (same free functions), take dirty pages from `_dirtyTracker->CollectAndClear`, record events straight into `engine.AppendEvent` instead of v1 journals + `FeedV1Events` (or keep the v1 journals as the recording buffers - a design choice), keep the coverage index and bookmarks as standalone members.

### 4.3 Reusable vs TTM-private capture helpers

| Reusable (free / other class) | TTM-private (rewrite or drop) |
|---|---|
| `CaptureCpuState`, `CaptureChipsetState`, `GetChipsetCpuTInFrame` (ttdcheckpoint) | `CaptureNow`, `CaptureBaselineRamPages`, `UpdateRamPages`, `IsPageAllZero`, `UpdatePrevPageCache`, `ReleaseCheckpointRefs` (page-store specific - dropped) |
| `TTDPeripheralRegistry::CaptureAll / LastCaptureState / DeviceEntries / RegionSources / NotifyRecording` | `ResolveModelRamPages`, `ComputeRomSignature` (TTM-private but store-independent: copy) |
| `RegisterMachinePeripherals` (ttdmachineperipherals) | `EnforceHistoryLimit`, `EvictOldest`, `HistoryBytes`, `BlobBytes` (rewrite on engine history policy) |
| `TTDDirtyTracker::CollectAndClear / ResetSession / IsDirty` | `FeedShadow`, `LiveRegions`, `ArmShadowRegions`, `RegisterScreenshotStream` (glue; their bodies become the controller's capture) |
| `ITTDRegionSource` (`TTDRegions`, `TTDArmRegions`, `TTDBeforeCapture`, `TTDStateWithoutRegions`) | |
| `CaptureConfigFingerprint`, `TTDRecordingFolder`, `TTDRecordingWriter` | |
| `TTDCoverageIndex::SealFrame` | |

---

## 5. Threading model (copy verbatim)

| Element | Lines | Rule |
|---|---|---|
| `_state` atomic | h:2045 | Written only by the session-driving thread (via `SetState` 599); observers read lock-free |
| `SessionOperation` | h:2069-2083, 920-965 | Every public op that reads/changes the session holds one. On the machine thread (`OnMachineThread` 915: `MainLoop::IsRunThread`) it does nothing but publish after a Change. Elsewhere: lock `_controlMutex` (recursive, h:2085), `++_operationDepth`, park a running machine when `_state != Idle` (`Pause(false)` + `WaitForPauseConfirmation(1000)`), resume in dtor only if it parked. Outermost Change publishes unless a recording machine runs |
| Ops with `Kind::Change` | | SetUnavailableReason, Start/Stop, Begin/EndDebuggerLive, Invalidate, SetSessionSourcePath, SetEnableWriteJournal, OnMachineReset, SeekTo, Add/RemoveBookmark, SeekToBookmark, StepBack/ForwardFrame, ResumeRecordingFrom/Live, SetHistoryLimit, DeserializeSession, Step*Instruction, ReverseStep*, ReverseContinue, ExportClip |
| Ops with `Kind::Read` | | RecordExternalEvent, SearchPortEvents, SessionEndPosition, GetBookmarks, FindBookmark, FindLastAccess, QueryCoverage* (no op: BuildWriteJournal*, GetFrameCache, RegenerateFrameWrites, VisitComposedFrames - caller holds the pause) |
| Extra explicit park | 189-196, 386-393, 896-902, 3613-3620 | `StartRecording`, `StopRecording`, `SwitchWriteJournal`, `ResumeRecordingLive` pause even for Idle sessions (feature flip swaps `Z80::MemIf`) and resume after publishing |
| Publication | 997-1024, h:2051-2057 | `_published` under `_publishedMutex`; `GetPublishedSessionInfo` sets `_publishRequested`; machine thread republishes at frame boundaries at most every `kPublishIntervalMs = 100` and never in replay mode |
| `ReadSessionInfo` | 967-989 | Machine thread: live. Else `try_lock` control mutex (busy -> published); machine driven (`IsDirectStepping` or running and not `IsEmulationParked`) -> published; parked but Recording -> published; else live |
| `OnMachineParking` | 991-995; mainloop.cpp:148-150 | Machine thread publishes when it parks while Recording (not in replay) |
| Live input | 2145-2198 | Any thread; on a running loop from another thread: `Emulator::RunWhileParked` applies now if parked, else queue under `_pendingInputMutex` + `kStepWorkTtdInput`; machine thread drains in `ServiceInput` |
| Deferred invalidation | 1165-1174, 1182-1189 | `_pendingInvalidation` atomic CAS, applied at the frame boundary |
| History limit atomics | h:2202-2203 | Control thread writes, machine thread enforces |
| Journal build atomics | h:2372-2375 | Polled from any thread, `CancelJournalBuild` from any thread |
| Auto pause | 1245-1266, h:2424 | Machine thread sets `_autoPauseRequested` + `Emulator::Pause()` |

---

## 6. Public API used outside the class

### 6.1 `TTDControl` (ttdcontrol.cpp, `_manager` / `manager`)

`ReadSessionInfo`(9) `GetState`(8) `CurrentPosition`(6) `IsRecording`(3) `SetHistoryLimit`(2) `GetUnavailableReason`(2) `GetJournalBuildState`(2) `GetEnableWriteJournal`(2) `SwitchWriteJournal` `StopRecording` `StartRecording` `StepForwardInstruction` `StepBackInstruction` `StepForwardFrame` `StepBackFrame` `SetSessionSourcePath` `SetEnableWriteJournal` `SessionEndPosition` `SerializeSession` `DeserializeSession` `SeekTo` `SearchPortEventsInFile` `SearchPortEvents` `ReverseStepTStates` `ReverseStepInstructions` `ReverseContinue` `ResumeRecordingFrom` `RemoveBookmark` `AddBookmark` `GetBookmarks` `FindBookmark` `RecordingGuard` `QueryCoverageSummary` `QueryCoverageScan` `QueryCoverageProbe` `InvalidateSession` `GlobalT` `GetSessionInfo` `GetExternalEvents` `FindLastAccess` `ExportClip` `CancelJournalBuild` `BuildWriteJournalFrames` - plus types `TTDSeekResult`, `TTDSeekHaltReason`, `TTDClipExportOptions`, `JournalBuildState` (ttdcontrol.cpp:438, 664-677, 1411). Verbs: Status 446, Start 453, Stop 498, Invalidate 508, HistoryLimit 522, Journal 548, JournalBuild 566, Position 621, Seek 630, StepFrame 690, Resume 703, StepInstruction 732, ReverseStep 754, Markers 779, Bookmarks 798-852, PortEvents 869, FindLast 921, ReverseContinue 1052, Coverage* 1156-1228, FileInfo 1314, Dump 1339, Load 1369, ExportClip 1404.

### 6.2 Called directly on `pTimeTravelManager` outside TTDControl and the hooks (production code; core/tests, core/benchmarks, tools/poc excluded)

| Method | Callers |
|---|---|
| `GetPublishedSessionInfo` | unreal-qt `ttdwidget.cpp` (3), `statusbarmanager.cpp`, `emulator/ttdsessionobserver.cpp` |
| `CurrentPosition` | dezog (2), gdbserver, cli-processor-ttd, Qt statusbar, Qt ttdwidget, zxdlss `ttd_source.cpp` |
| `ReadSessionInfo` | gdbserver (6), cli-processor-ttd (3) |
| `GetSessionInfo` / `SessionEndPosition` | cli-processor-ttd; bench `ttdbench.cpp`, `ttdv1feeder.cpp` |
| `SetSessionSourcePath` | Qt ttdwidget, zxdlss tool |
| `StartRecording` / `StopRecording` | dezog, python_emulator.h, lua_emulator.h, bench (StopRecording also emulator.cpp via hooks) |
| `BeginDebuggerLiveHistory` / `EndDebuggerLiveHistory` / `IsDebuggerLive` / `GetFrameCache` (2) / `ClearFrameCache` / `GetEarliestRecordedFrame` / `GetCheckpointCount` / `GetUnavailableReason` | dezog `dezogdebugadapter.cpp` (frame cache = `TTDFrameCache` type) |
| `GetInputJournal` | network TTD serializers: `ttdzifi.cpp:70`, `ttdzxnetusb.cpp:53`, `ttdserialport.cpp:56`, `ttdmachineserialpeer.cpp:40` (`NetAt()` payloads on restore); bench |
| `RecordMemoryWrite`, `RecordReadCoverage` | `memory.cpp:443`, `:339` (hot) |
| `RecordExecutedCoverage` | `z80.cpp:1141` (hot) |
| `RecordIoWrite` | `portdecoder.cpp:425` (hot) |
| `RecordExternalEvent` (direct, not via hooks) | webapi `state_memory_api.cpp:700`, `cli-processor-memory.cpp:76`, dezog |
| `IsRecording` (direct) | python/lua emulator, webapi, Qt toolbar/menu/ttdwidget, dezog |
| `SeekTo`, `DeserializeSession`, `VisitComposedFrames` | `tools/verification/zxdlss/tool/ttd_source.cpp` |
| `GetCheckpoint`(11), `GetPortRead/WriteJournal`, `GetPerfCounters`, `GetWriteJournal`, `GetPageStore`, `GetHeapBreakdown`, `GetCoverageIndex`, `FrameSpan`, `GlobalT`, `SetShadowEngine`, `SetWriteJournalCapacity`, `SetEnableWriteJournal`, `SetEnableCoverageIndex`, `SerializeSession`, `QueryCoverageScan`, `RegenerateFrameWrites` | `core/src/debugger/ttd/bench/ttdbench.cpp`, `ttdv1feeder.cpp` (benchmark harness + v1->engine feeder) |
| Construction | `emulator.cpp:288-292` (`new TimeTravelManager`, sets both `pTimeTravelManager` and `pTimeTravelHooks`), reset at 499 |

Hook-interface callers (via `pTimeTravelHooks`): emulator.cpp, mainloop.cpp, z80.cpp (`ServiceInput`, `NoteRzxFrameEnd`), soundmanager.cpp, zxpolygroup.cpp, rzxsession.cpp, featuremanager.cpp, tape.cpp (`RecordExternalEvent` x9), debug keyboard/mouse/joystick managers (`SubmitLiveInput`, `OwnsInput`), sprinterdevicestate.cpp.

### 6.3 Most used by tests (core/tests + unreal-qt/tests, 114 files; call counts / files)

| # | Method | Calls | Files |
|---|---|---|---|
| 1 | StartRecording | 550 | 102 |
| 2 | StopRecording | 425 | 93 |
| 3 | SeekTo | 319 | 60 |
| 4 | GetCheckpointCount | 238 | 48 |
| 5 | GetCheckpoint | 145 | 44 |
| 6 | GetSessionInfo | 135 | 28 |
| 7 | GetState | 111 | 29 |
| 8 | OnFrameBoundary | 103 | 14 |
| 9 | UpdateFeatureCache (*) | 99 | 69 |
| 10 | GetExternalEvents | 94 | 15 |
| 11 | CurrentPosition | 94 | 17 |
| 12 | DeserializeSession | 72 | 33 |
| 13 | SessionEndPosition | 67 | 16 |
| 14 | SetEnableWriteJournal | 66 | 16 |
| 15 | SerializeSession | 61 | 27 |
| 16 | GetInputJournal | 61 | 16 |
| 17 | SetShadowEngine | 59 | 8 |
| 18 | GetWriteJournal | 57 | 14 |
| 19 | FindLastAccess | 54 | 11 |
| 20 | GetPeripheralRegistry | 53 | 21 |
| 21 | RecordExternalEvent | 46 | 11 |
| 22 | RecordMemoryWrite | 43 | 5 |
| 23 | IsRecording | 40 | 18 |
| 24 | ResumeRecordingFrom | 38 | 12 |
| 25 | RestoreCheckpointForTesting | 38 | 9 |
| 26 | GetPageStore | 36 | 9 |
| 27 | InvalidateSession | 33 | 18 |
| 28 | ReverseContinue | 22 | 4 |
| 29 | SetReplaySource | 21 | 2 |
| 30 | GetModelRamPages | 20 | 9 |

Next: SetEnableCoverageIndex 20, ReverseStepInstructions 17, AddBookmark 16, GlobalT 16, EnterReplayMode 16, ExitReplayMode 15, GetCoverageIndex 15, GetPortReadJournal 14, GetFrameCache 14, QueryCoverageProbe 13, SubmitLiveInput 13, StepBackFrame 11. (*) `UpdateFeatureCache` / `GetState` counts include same-named calls on other classes (Memory, FeatureManager), so they overcount. Header types used by tests: `TTDSeekResult` (12), `TTDSeekHaltReason` (10), `TurboSoundSessionKindMatches` (8), `kKeyFrameInterval` (5), `kScreenshotStream` (3), `TTDComposedFrame`, `TTDClipExportOptions`.

Tests drive v1 directly (only 1 test file uses `TTDControl`, for `file-info`). For an A/B oracle the controller needs the same public method names, or the tests must be templated over the class. v1-store-specific test APIs (`GetCheckpoint` -> `TTDCheckpoint`, `GetPageStore`, `RestoreCheckpointForTesting`, `kKeyFrameInterval`, `SerializeSession/DeserializeSession` of .ttd v1) have no 1:1 controller equivalent.

---

## 7. Size estimate (lines per area, method bodies only; total 7569 of 7993)

| Area | Lines | Reusable verbatim | Mechanical (index swap) | Rewrite for engine | Drop |
|---|---|---|---|---|---|
| Lifecycle | 906 | ~560 (feature/lock/guard/stop/live mode) | ~95 (ResumeRecordingLive, BeginDebuggerLive) | ~250: StartRecording wipe+baseline, InvalidateSession, ResumeRecordingFrom truncate (needs engine truncate/branch API), TruncateTimelineAfter, dtor | - |
| Threading | 80 | 80 | - | - | - |
| Capture | 563 | ~120 (ResolveModelRamPages, ComputeRomSignature, peripherals, SetHistoryLimit) | - | OnFrameBoundary recording branch + history limit on `SetHistoryPolicy` (~90) | ~340 page-store capture (CaptureNow, Baseline, UpdateRamPages, Release, PrevPageCache), replaced by a FeedShadow-like builder (~200 from 3943-4184) |
| Journals | 528 | ~490 | ~40 (CheckpointStartT, JournalCoversSession, AddBookmark, SetEnable/Capacity) | DropPortJournal (17, engine bus journal has no cursor rewrite) | - |
| Input (playback / live) | 272 | ~230 | OwnsInput (15) | - (engine branches of ServiceInput / ArmInputPlayback already exist) | v1 journal-playback branches (~25) |
| Seek / replay | 984 | ~500 (replay mode, RunTo*, Present/Publish, SeekTo, ResyncScreenState) | ~400 (SeekToInternal, ComposeDisplay, StepFrame, SessionEndPosition) | RestoreCheckpoint/ForReplay keep only the **E** blocks (~60 of 160); RestoreRamPages, RestoreCheckpointForTesting dropped | ~110 |
| Instruction level | 1022 | ~300 (CaptureM1, Save/RestoreLiveState, ClearFrameCache) | ~720 (Enumerate, ReverseStep*, ReverseContinue, Step*Instruction, BuildFrameCache, GetFrameCache) + barrier -> `Events().FirstBarrierIn` | - | - |
| Queries | 762 | ~150 (coverage probe/scan, port search) | ~610 (FindLastAccess, Regenerate, BuildWriteJournal*, coverage summary) + write journal -> `engine.Writes()` + barriers | `hasKeyframe` in summary | - |
| Files | 1810 | ~220 (clip export, TurboSound check, source path) | - | self test (~60) on engine capture/restore | ~1530 .ttd v1 (serialize/deserialize/section codecs, `SearchPortEventsInFile`) -> engine session file (`TTDSessionFile`, `TTDRecordingWriter`); loading v1 files = `ttdv1feeder` |
| Status | 206 | ~100 (ReadSessionInfo, provenance/journal fields) | - | ~100 (page store / key frame / heap fields -> `engine.HeapBreakdown()`, checkpoints, segments) | - |
| Shadow / glue | 436 | ~180 bodies become the controller's plumbing (MediaReadAdapter, SyncMediaReadJournal, LiveRegions, CheckEngineCheckpoint, ScreenshotStream, NoteFact -> AppendEvent) | - | - | ~250 (shadow/replay dual pointers, FeedV1Events cursors, ResetShadow) |

Rough totals: ~2900 lines copy verbatim, ~1900 lines mechanical (checkpoint index + barrier + write-journal source swap), ~650 lines real rewrite (capture builder, restore wiring, lifecycle wipe/truncate, history limit, status), ~2100 lines dropped (v1 page store capture/restore, .ttd v1 file format, shadow cursors).

### Engine gaps the controller must solve (found while mapping)

1. **Checkpoint selection by time**: engine `CheckpointIndexOf` matches an exact frame only; v1 uses `upper_bound` / `lower_bound` over `_timeline` in 8 places (3004, 3227, 4193, 5957, 6146, 6273, 6597, 7676). Needs an "at or before" lookup (binary search over `Checkpoint(i)->position` from `FirstCheckpoint()`).
2. **Time base**: v1 `GlobalT = frame * FrameSpan()`; the engine's machine time comes from its frame table (`Frames().Start`), which diverges when a frame length changes (Sprinter 320/312 lines). Write journal records, journal segments, `beforeGlobalT` in queries and `CheckpointStartT` are in v1 GlobalT; the engine is fed those records unchanged (4127).
3. **Truncate / resume-from-past**: no engine API (only `parent` / `branch` fields); v1 ends the engine session on truncate (4204, `ResetShadow`).
4. **History limit**: v1 frame/byte limits vs engine `TTDHistoryPolicy` (Ring window/segment frames).
5. **Coverage index and bookmarks**: not in the engine; keep `TTDCoverageIndex` / `TTDBookmarkJournal` as controller members (and in the session file if they must persist).
6. **Barriers in queries**: `FindLastAccess`, `EnumerateM1InRange`, `RegenerateFrameWrites`, `ReverseContinue` use v1 `_externalEvents` even with a replay engine.
7. **Network device restore** reads `pTimeTravelManager->GetInputJournal().NetAt(index)` (4 serializers) - must point at the engine's payloads when the controller drives replay.
8. **Hot-path calls** (`RecordMemoryWrite`, `RecordReadCoverage`, `RecordExecutedCoverage`, `RecordIoWrite`) go through `pTimeTravelManager`, not the hooks interface.
9. **Restore order**: v1 devices -> banks -> RAM; engine branch RAM/regions -> devices -> banks. The engine branch skips `_perf.lastRestore*Ns`.
