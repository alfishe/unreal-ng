# AnalyzerManager Main Loop Integration Plan

## Overview
Integrate AnalyzerManager dispatch hooks into the emulator's main loop to enable real-time analysis during emulation.

## Integration Points

### 1. Frame Events (Cold Path - Virtual Dispatch)
**Location**: [mainloop.cpp](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/mainloop.cpp)
- [OnFrameStart()](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/mainloop.cpp#217-227) - Called at the beginning of each frame
- [OnFrameEnd()](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/mainloop.cpp#240-347) - Called at the end of each frame

**Changes**:
```cpp
void MainLoop::OnFrameStart() {
    // Existing code...
    
    // Dispatch to AnalyzerManager
    if (_context->pDebugManager && _context->pDebugManager->GetAnalyzerManager()) {
        _context->pDebugManager->GetAnalyzerManager()->dispatchFrameStart();
    }
}

void MainLoop::OnFrameEnd() {
    // Existing code...
    
    // Dispatch to AnalyzerManager
    if (_context->pDebugManager && _context->pDebugManager->GetAnalyzerManager()) {
        _context->pDebugManager->GetAnalyzerManager()->dispatchFrameEnd();
    }
}
```

### 2. CPU Step Events (Hot Path - Raw Function Pointers)
**Location**: `mainloop.cpp::OnCPUStep()` or CPU execution loop

**Research Needed**:
- Determine exact location where CPU step dispatch should occur
- Check if there's already a hook point in the Z80 execution
- Verify performance impact of hot path dispatch

**Proposed Change**:
```cpp
// In CPU step handler
if (_context->pDebugManager && _context->pDebugManager->GetAnalyzerManager()) {
    _context->pDebugManager->GetAnalyzerManager()->dispatchCPUStep(cpu, pc);
}
```

### 3. Breakpoint Hit Events (Cold Path - Virtual Dispatch)
**Location**: Breakpoint handling in CPU or BreakpointManager

**Research Needed**:
- Find where breakpoints are currently triggered
- Determine if BreakpointManager already has a notification mechanism
- Check MessageCenter integration for breakpoint events

**Proposed Change**:
```cpp
// In breakpoint hit handler
if (_context->pDebugManager && _context->pDebugManager->GetAnalyzerManager()) {
    _context->pDebugManager->GetAnalyzerManager()->dispatchBreakpointHit(address, cpu);
}
```

### 4. Video Line Events (Warm Path - std::function)
**Location**: Video rendering code

**Research Needed**:
- Locate video line rendering in ULA/Screen implementation
- Determine current line tracking mechanism
- Assess performance impact

### 5. Memory Access Events (Future - Not in Phase 4)
**Note**: Memory read/write dispatch will be added later if needed
- Would require hooks in Memory class
- High performance impact - needs careful consideration

## Feature Toggle Integration

**Location**: AnalyzerManager initialization

**Change**:
```cpp
void AnalyzerManager::init(DebugManager* debugManager) {
    // Existing code...
    
    // Check if debug mode is enabled
    if (_context && _context->pFeatureManager) {
        bool debugEnabled = _context->pFeatureManager->isEnabled(Features::kDebugMode);
        setEnabled(debugEnabled);
    }
}
```

## Verification Plan

### Automated Tests
1. **Existing Unit Tests** ✅
   - Already passing: 14/14 tests in [AnalyzerManager_test](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/tests/debugger/analyzermanager/analyzermanager_test.h#11-21)
   - Command: `./bin/core-tests --gtest_filter="AnalyzerManager*"`
   - These verify the dispatch methods work correctly

2. **Integration Test** (To be created)
   - Create simple test analyzer that counts frame events
   - Run emulator for N frames
   - Verify analyzer received N frame start/end events
   - Location: `core/tests/debugger/analyzermanager/integration_test.cpp`

### Manual Verification
1. **Frame Event Dispatch**
   - Create a test analyzer that logs frame events
   - Run emulator and verify log output shows frame events

2. **Performance Impact**
   - Run emulator with AnalyzerManager disabled
   - Run emulator with AnalyzerManager enabled but no analyzers active
   - Run emulator with active analyzer
   - Compare frame rates to ensure minimal overhead

## Implementation Order

1. ✅ **Phase 1-3**: Core infrastructure and tests (COMPLETE)
2. **Phase 4a**: Add frame event dispatch hooks (OnFrameStart/OnFrameEnd)
3. **Phase 4b**: Research and add breakpoint hit dispatch
4. **Phase 4c**: Add CPU step dispatch (if performance acceptable)
5. **Phase 4d**: Add video line dispatch (if needed)
6. **Phase 4e**: Feature toggle integration
7. **Phase 4f**: Integration testing

## Risks & Mitigations

**Risk**: Performance impact on hot path (CPU step)
- **Mitigation**: Use raw function pointers, add feature toggle check
- **Mitigation**: Benchmark before/after integration

**Risk**: Null pointer access if DebugManager not initialized
- **Mitigation**: Add null checks before all dispatch calls
- **Mitigation**: Ensure DebugManager is always created in debug builds

**Risk**: Breaking existing emulation behavior
- **Mitigation**: All dispatch calls are additions, no existing code modified
- **Mitigation**: Comprehensive testing with existing test suite

## Success Criteria

- [ ] Frame events dispatched correctly
- [ ] Breakpoint events dispatched correctly  
- [ ] No performance regression when AnalyzerManager disabled
- [ ] < 1% performance impact when enabled with no active analyzers
- [ ] All existing tests still pass
- [ ] Integration test demonstrates end-to-end functionality
