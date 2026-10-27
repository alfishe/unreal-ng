# Appendix: unleased context reads in unreal-qt

Generated 2026-10-10 from master `cca7d28c0`: `GetContext()` and the accessors that dereference the context
(`GetMemory`, `GetDebugManager`, `GetBreakpointManager`, `GetZ80State`, `GetFeatureManager`, `GetLabelManager`,
`GetMainLoop`, `GetDisassembler`) in `unreal-qt/src/*.cpp`, grouped by the enclosing function. Paths are relative to
`unreal-qt/src/`. Functions that already take a `ContextLease` for another read are still listed when they also read
without one.

| File | Reads | Functions | Functions (×reads when more than one) |
|------|-------|-----------|----------------------------------------|
| `mainwindow.cpp` | 52 | 36 | `adoptEmulator` ×9, `applyBlackBox`, `applyDisplayRefreshRate`, `confirmUnsavedMedia`, `dropFiles`, `handleAudioSettingsRequested`, `handleContentionToggled`, `handleFastDiskToggled`, `handleFt812DebugToggled`, `handleGpuAccelerationToggled`, `handleHudOverlayToggled` ×2, `handleHudSettingsRequested`, `handleOverscanModeToggled`, `handleQuickRecord`, `handleSaveScreenshotRequested`, `handleSystemReset` ×2, `handleTapeTrapsToggled`, `handleTemporalBlendingToggled`, `handleTurboModeToggled`, `handleTurboTapeToggled`, `handleVideoModeChanged`, `insertMediumDialog`, `loadFile` ×3, `MainWindow` ×2, `onBindingStateChanged`, `openAdvancedRecordingDialog`, `placeMedium` ×3, `playRzxFile`, `saveDiskAsSCLDialog`, `saveDiskAsTRDDialog`, `saveDiskAsUDIDialog`, `saveDiskDialog`, `showDropVerdict`, `toggleEmulatorStartStop` ×2, `updateMenuStates`, `updateUnsavedMediaMark` |
| `debugger/debuggerwindow.cpp` | 17 | 10 | `changeMemoryViewBank`, `changeMemoryViewZ80Address`, `clearInterruptBreakpoints`, `handleMessageBreakpointTriggered` ×2, `loadState` ×2, `onBindingReady`, `showLabelManager`, `stepOut` ×3, `updateState` ×2, `waitInterrupt` ×3 |
| `widgets/ttdwidget.cpp` | 15 | 15 | `applyHistoryLimit`, `applyJournalPreference`, `buildJournal`, `onClearSession`, `onExportSession`, `onJournalToggled`, `onJumpEnd`, `onJumpStart`, `onLoadSession`, `onRecordToggled`, `onResumeFromHere`, `onStepBack`, `onStepForward`, `performSeekToFrame`, `updateTelemetry` |
| `debugger/disassemblerwidget.cpp` | 9 | 7 | `getBreakpointManager`, `getDisassembler` ×2, `getEmulatorContext`, `getMemory`, `getZ80Registers`, `setDisassemblerAddress` ×2, `updateBankIndicator` |
| `emulator/emulatorbinding.cpp` | 8 | 8 | `cacheEmulatorState`, `produceTapeSnapshot`, `tapeGetBlockData`, `tapePause`, `tapePlay`, `tapeRewind`, `tapeSeekToBlock`, `tapeStop` |
| `debugger/debugvisualizationwindow.cpp` | 8 | 6 | `onCallTraceToggled` ×2, `onDumpVizDataClicked`, `onMemoryTrackingToggled` ×2, `onOpcodeProfilerToggled`, `onResetCountersClicked`, `syncFeatureCheckboxes` |
| `debugger/widgets/memory16kbwidget.cpp` | 7 | 4 | `event`, `eventFilter`, `updateCounterLabels`, `updateMemoryImage` ×4 |
| `debugger/diskfilesdialog.cpp` | 6 | 5 | `convertTo` ×2, `exportText`, `importLabels`, `openSource`, `refresh` |
| `menumanager.cpp` | 5 | 2 | `updateMachineModelSelection`, `updateMenuStates` ×4 |
| `media/mediapanelwindow.cpp` | 5 | 4 | `insertFolderAsync`, `onFilesDropped`, `refresh`, `run` ×2 |
| `debugger/devicememorydialog.cpp` | 5 | 4 | `DeviceMemoryDialog`, `documentChanged`, `regionSelected`, `reload` ×2 |
| `ui/intparametersdialog.cpp` | 4 | 4 | `applyIntLen`, `applyIntPos`, `loadValues`, `restoreOriginalValues` |
| `emulator/remotecontrolguard.cpp` | 4 | 4 | `allowAgain`, `InputBlocked`, `refresh`, `takeBack` |
| `debugger/livesourcewindow.cpp` | 4 | 3 | `convert`, `extract` ×2, `service` |
| `debugger/breakpointgroupdialog.cpp` | 4 | 4 | `addGroup`, `deleteGroup`, `populateGroupList`, `renameGroup` |
| `debugger/breakpointdialog.cpp` | 4 | 4 | `deleteBreakpoint`, `populateBreakpointTable`, `populateGroupComboBox`, `toggleBreakpoint` |
| `statusbarmanager.cpp` | 3 | 3 | `setActiveEmulator`, `toggleSound`, `updateFpsToolTip` |
| `network/networkwindow.cpp` | 3 | 2 | `onApply` ×2, `refresh` |
| `debugger/widgets/registerswidget.cpp` | 3 | 3 | `getEmulatorContext`, `getMemory`, `getRegisters` |
| `debugger/widgets/bordertimingwidget.cpp` | 3 | 2 | `refresh` ×2, `reset` |
| `debugger/breakpointeditor.cpp` | 3 | 3 | `loadBreakpointData`, `onAccept`, `populateGroupComboBox` |
| `cardslots/slotswindow.cpp` | 3 | 3 | `onConfigureSlot`, `previewText`, `refresh` |
| `cardslots/slotchangecontroller.cpp` | 3 | 3 | `ApplyChanges`, `ApplyGeneralSound`, `ApplyNetworkCards` |
| `debugger/widgets/stackwidget.cpp` | 2 | 2 | `getEmulatorContext`, `getMemory` |
| `debugger/widgets/speedcontrolwidget.cpp` | 2 | 2 | `getTStatesForPreset`, `updateLabel` |
| `debugger/widgets/memorypageswidget.cpp` | 2 | 2 | `getEmulatorContext`, `getMemory` |
| `debugger/widgets/memorypagesviswidget.cpp` | 2 | 2 | `reset`, `updatePageDisplay` |
| `debugger/disksectordialog.cpp` | 2 | 1 | `load` ×2 |
| `cardslots/midiactivitywindow.cpp` | 2 | 2 | `panic`, `refresh` |
| `widgets/recordingwidget.cpp` | 1 | 1 | `getActiveContext` |
| `widgets/devicescreenwrapper.cpp` | 1 | 1 | `setupMouseCapture` |
| `ui/temporaleffectsdialog.cpp` | 1 | 1 | `(file scope)` |
| `toolbarmanager.cpp` | 1 | 1 | `isVideoRecording` |
| `network/trafficwindow.cpp` | 1 | 1 | `context` |
| `debugger/widgets/videorecordingwidget.cpp` | 1 | 1 | `recordingContext` |
| `debugger/widgets/ulabeamwidget.cpp` | 1 | 1 | `refresh` |
| `debugger/widgets/floppydiskwidget.cpp` | 1 | 1 | `updateDiskInfo` |
| `debugger/vdac2/ft812debugwindow.cpp` | 1 | 1 | `context` |
