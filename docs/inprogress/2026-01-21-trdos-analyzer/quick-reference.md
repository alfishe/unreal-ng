# TR-DOS Analyzer Testing - Quick Reference

## Test Execution

### Run All Tests
```bash
cd core/tests/cmake-build-release
./bin/core-tests --gtest_filter="TRDOSIntegration_test.*-*DISABLED*"
```

### Run Specific Test Category
```bash
# E2E tests only
./bin/core-tests --gtest_filter="TRDOSIntegration_test.RealExecution*"

# Unit tests only  
./bin/core-tests --gtest_filter="TRDOSIntegration_test.*-*RealExecution*"
```

### Run Single Test
```bash
./bin/core-tests --gtest_filter="TRDOSIntegration_test.RealExecution_MinimalProof_JumpToTRDOSEntry"
```

## Expected Results

**All 20 tests should pass:**
```
[==========] 20 tests from 1 test suite ran.
[  PASSED  ] 20 tests.
```

## Common Issues & Solutions

### Issue: Tests fail with "TR-DOS ROM not available"
**Solution**: Ensure Pentagon model is selected (has TR-DOS ROM)

### Issue: Events not collected (count = 0)
**Solution**: Verify TR-DOS ROM is activated with `_memory->SetROMDOS(true)`

### Issue: Breakpoints not registered
**Solution**: Check that `AnalyzerManager` is initialized and analyzer is activated

### Issue: FDC observer not working
**Solution**: Verify `_fdc` is not null and `addObserver()` was called

## Test File Locations

```
core/tests/debugger/analyzers/trdos/
├── trdos_integration_test.cpp    # Main integration tests (20 tests)
└── trdos_unit_test.cpp           # Unit tests (if exists)

core/tests/_helpers/
├── trdostesthelper.h/cpp         # TR-DOS test utilities
└── emulatortesthelper.h/cpp      # Emulator setup utilities
```

## Key Test Patterns

### Pattern 1: Manual Breakpoint Triggering
```cpp
activateAnalyzer();
_memory->SetROMDOS(true);

_z80->pc = 0x3D00;
_z80->tt = 1000;
_analyzer->onBreakpointHit(0x3D00, _z80);

EXPECT_GT(_analyzer->getEventCount(), 0);
```

### Pattern 2: FDC Command Simulation
```cpp
activateAnalyzer();
_memory->SetROMDOS(true);

// Trigger TR-DOS entry first
_analyzer->onBreakpointHit(0x3D00, _z80);

// Simulate FDC command
_analyzer->onFDCCommand(0x88, *_fdc);  // Read Sector
_analyzer->onFDCCommandComplete(0x00, *_fdc);

// Verify events
auto events = _analyzer->getEvents();
```

### Pattern 3: Real Z80 Execution
```cpp
activateAnalyzer();
_memory->SetROMDOS(true);

// Write code to RAM
_memory->DirectWriteToZ80Memory(0x8000, 0xC3);  // JP
_memory->DirectWriteToZ80Memory(0x8001, 0x00);  // $3D00 low
_memory->DirectWriteToZ80Memory(0x8002, 0x3D);  // $3D00 high

_z80->pc = 0x8000;
_emulator->RunNCPUCycles(100, false);

EXPECT_GT(_analyzer->getEventCount(), 0);
```

## Event Verification

### Check Event Count
```cpp
EXPECT_EQ(_analyzer->getEventCount(), expectedCount);
```

### Find Specific Event Type
```cpp
auto events = _analyzer->getEvents();
bool found = false;
for (const auto& e : events)
{
    if (e.type == TRDOSEventType::TRDOS_ENTRY)
    {
        found = true;
        break;
    }
}
EXPECT_TRUE(found);
```

### Verify Event Sequence
```cpp
std::vector<TRDOSEventType> expected = {
    TRDOSEventType::TRDOS_ENTRY,
    TRDOSEventType::FDC_CMD_READ,
    TRDOSEventType::SECTOR_TRANSFER
};

auto events = _analyzer->getEvents();
for (size_t i = 0; i < expected.size() && i < events.size(); i++)
{
    EXPECT_EQ(events[i].type, expected[i]);
}
```

## Debugging Failed Tests

### Enable Verbose Output
```bash
./bin/core-tests --gtest_filter="TRDOSIntegration_test.*" --gtest_print_time=1
```

### Add Debug Output in Test
```cpp
std::cout << "[DEBUG] State: " << (int)_analyzer->getState() << "\n";
std::cout << "[DEBUG] Events: " << _analyzer->getEventCount() << "\n";

auto events = _analyzer->getEvents();
for (const auto& e : events)
{
    std::cout << "[DEBUG] " << e.format() << "\n";
}
```

### Check Analyzer State
```cpp
ASSERT_EQ(_analyzer->getState(), TRDOSAnalyzerState::IN_TRDOS);
```

## Build Commands

### Clean Build
```bash
cd core/tests/cmake-build-release
rm -rf *
cmake ..
cmake --build . --target core-tests -- -j8
```

### Incremental Build
```bash
cd core/tests/cmake-build-release
cmake --build . --target core-tests -- -j8
```

### Force Rebuild Single File
```bash
cd core/tests/cmake-build-release
rm CMakeFiles/core-tests.dir/debugger/analyzers/trdos/trdos_integration_test.cpp.o
cmake --build . --target core-tests
```

## WebAPI Testing

See `docs/analysis/capture/high-level-disk-operations.md` Section 21 for complete WebAPI testing guide.

### Quick Test
```bash
# Activate analyzer
curl -X POST http://localhost:8080/api/emulators/{uuid}/analyzers/trdos/activate

# Execute command
curl -X POST http://localhost:8080/api/emulators/{uuid}/basic/execute \
  -d '{"command": "CAT"}'

# Get events
curl http://localhost:8080/api/emulators/{uuid}/analyzers/trdos/events
```

## Success Criteria

✅ All 20 tests pass  
✅ E2E tests capture events (count > 0)  
✅ Event types match expected sequence  
✅ State transitions work correctly  
✅ No crashes or memory leaks  
✅ Build completes without warnings
