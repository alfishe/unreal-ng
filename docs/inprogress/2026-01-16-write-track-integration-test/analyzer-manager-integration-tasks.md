# AnalyzerManager Implementation

## Phase 1: Core Infrastructure ✅
- [x] Create IAnalyzer interface ([ianalyzer.h](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/debugger/ianalyzer.h))
- [x] Create AnalyzerManager class ([analyzermanager.h](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/debugger/analyzermanager.h))
- [x] Implement AnalyzerManager class ([analyzermanager.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/debugger/analyzers/analyzermanager.cpp))
- [x] Update DebugManager to include AnalyzerManager
- [x] Fix two-phase initialization (init() method)
- [x] Fix init() to accept DebugManager pointer directly
- [ ] Add message definitions to `messages.h`

## Phase 2: Callback & Dispatch System ✅
- [x] Implement hot path callbacks (raw function pointers)
- [x] Implement warm path callbacks (std::function)
- [x] Implement cold path callbacks (virtual dispatch)
- [x] Add automatic cleanup on analyzer deactivation
- [x] Implement breakpoint ownership tracking

## Phase 3: Testing ✅ (14/14 passing! 🎉)
- [x] Create EmulatorTestHelper (uses EmulatorManager)
- [x] Create 14 comprehensive unit tests
- [x] All tests passing (registration, lifecycle, dispatch, callbacks, breakpoints)
- [x] Fixed test use-after-free issue in UnregisterDeactivates
- [ ] Integration test with TR-DOS FORMAT

## Phase 4: Main Loop Integration ✅
- [x] Add frame start/end dispatch hooks in [mainloop.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/mainloop.cpp)
- [x] Wire up breakpoint hit notifications in [z80.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/cpu/z80.cpp)
- [ ] Add CPU step dispatch (deferred - performance sensitive)
- [ ] Add video line dispatch (deferred - not needed yet)
- [ ] Add feature toggle integration (deferred - works via master enable/disable)

## Phase 5: First Analyzer - ROMPrintDetector
- [ ] Create ROMPrintDetector class
- [ ] Implement breakpoint-based character capture
- [ ] Add MessageCenter integration
- [ ] Port existing BasicEncoder for token decoding
