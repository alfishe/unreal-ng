# TR-DOS Analyzer and Automated Execution Fixes

## Tasks

- [x] **Refactor BasicEncoder API**
    - [x] Implement physical ENTER key injection for TR-DOS ([injectEnterPhysical](core/src/debugger/analyzers/basic-lang/basicencoder.cpp#1233-1245))
    - [x] Move [injectEnterPhysical](core/src/debugger/analyzers/basic-lang/basicencoder.cpp#1233-1245) to a dedicated method in [BasicEncoder](core/src/debugger/analyzers/basic-lang/basicencoder.h#58-59)
    - [x] Update [runCommand](core/src/debugger/analyzers/basic-lang/basicencoder.cpp#1187-1232) to call [injectEnterPhysical](core/src/debugger/analyzers/basic-lang/basicencoder.cpp#1233-1245)
- [x] **Fix TR-DOS Analyzer Event Collection**
    - [x] Handle active TR-DOS state in [onActivate](core/src/debugger/analyzers/trdos/trdosanalyzer.cpp#93-185)
    - [x] Implement [identifyCommand](core/src/debugger/analyzers/trdos/trdosanalyzer.cpp#523-562) logic to detect TR-DOS commands (LIST, LOAD, etc.)
    - [x] Implement [readFilenameFromMemory](core/src/debugger/analyzers/trdos/trdosanalyzer.cpp#563-589) to capture file operations
- [x] **Verification**
    - [x] Re-run [verify_trdos_analyzer.py](tools/verification/analyzers/trdos/verify_trdos_analyzer.py)
    - [x] Verify events are correctly captured with timestamps and filenames
    - [x] Ensure event log output is human-readable and clean
    - [x] Verify PC context capture (no longer $0000)
    - [x] Ensure timestamps are sorted/monotonic (handled frame resets)
    - [x] Improve timestamp precision (millisecond resolution)
- [ ] **Enhance Non-Standard Loader Support**
    - [ ] Add breakpoints for `#3D13` and `#3D2F`
    - [ ] Relax FDC monitoring to capture IO outside standard ROM states
    - [ ] Implement `IN_CUSTOM` state detection for direct ROM calls
    - [ ] **Implement Interrupt State Capture**
        - [ ] Add `iff1` and [im](core/src/debugger/analyzers/basic-lang/basicencoder.cpp#395-403) fields to [TRDOSEvent](core/src/debugger/analyzers/trdos/trdosevent.h#74-108) struct
        - [ ] Populate these fields in [TRDOSAnalyzer](core/src/debugger/analyzers/trdos/trdosanalyzer.cpp#76-82) from Z80 context
        - [ ] Update event formatting to display `DI/EI` and `IM` status
    - [ ] Verify with custom loader logic (mock or simulation)
