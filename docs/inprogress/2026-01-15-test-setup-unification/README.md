# Test Setup Unification Analysis & Proposal

**Date**: 2026-01-15  
**Status**: Analysis Complete - Awaiting Review  
**Scope**: `core/tests/` directory

---

## Executive Summary

This document analyzes the repetitive test setup patterns across the `core/tests` codebase and proposes a consolidation strategy. The analysis identifies **6 distinct setup patterns** that are duplicated across **42 test files**, with the most complex pattern (Core+Z80+TimingHelper) appearing in **8+ files** with nearly identical code.

**Key Finding**: Approximately **400+ lines of setup/teardown code** can be consolidated into **~150 lines** of reusable test helpers, reducing boilerplate by ~60% and ensuring consistent initialization across all tests.

---

## Table of Contents

1. [Recent Changes Summary](#recent-changes-summary)
2. [Current State Analysis](#current-state-analysis)
3. [Identified Setup Patterns](#identified-setup-patterns)
4. [Pattern Frequency Analysis](#pattern-frequency-analysis)
5. [Consolidation Proposal](#consolidation-proposal)
6. [Prioritized Implementation Plan](#prioritized-implementation-plan)
7. [Risk Assessment](#risk-assessment)
8. [Migration Strategy](#migration-strategy)

---

## Recent Changes Summary

### Commits Affecting Tests (Last 10)

| Commit | Description | Files Changed |
|--------|-------------|---------------|
| `278dc0d7` | **Unit tests stabilization** - eliminated crashes, fixed z80 timing tests | 13 files, +2092/-726 lines |
| `c203ae13` | WD1793 improvements | `wd1793_test.cpp` |
| `88137357` | Added test development guidelines | `core/tests/README.md` (+1307 lines) |
| `21537a8c` | Unit tests stabilization - eliminating crashes | Multiple test files |
| `62a864c4` | SharedMemory feature tests | `sharedmemory_test.cpp` |

### Notable New Patterns Introduced

#### 1. Fixture-Specific Helper Methods (`Z80_Test::ResetCPUAndMemory`)

The recent stabilization commit introduced a **fixture-local helper method** pattern in `Z80_Test`:

```cpp
// z80_test.h - New helper method declaration
class Z80_Test : public ::testing::Test
{
protected:
    // ... existing members ...
    void DumpFirst256ROMBytes();
    void ResetCPUAndMemory();  // NEW - added for test isolation
};

// z80_test.cpp - Implementation
void Z80_Test::ResetCPUAndMemory()
{
    Z80& z80 = *_cpu->GetZ80();
    z80.Reset();

    // Reset all other registers to 0 for a predictable state
    z80.bc = 0;
    z80.de = 0;
    z80.hl = 0;
    z80.ix = 0;
    z80.iy = 0;
    z80.alt.af = 0;
    z80.alt.bc = 0;
    z80.alt.de = 0;
    z80.alt.hl = 0;

    // Reset memory banking to default 48k layout
    _cpu->GetMemory()->DefaultBanksFor48k();

    // Clear emulator state flags (like CF_TRDOS)
    _context->emulatorState.flags = 0;
}
```

**Usage**: Called at the start of each opcode timing test iteration to ensure clean CPU/memory state:
```cpp
TEST_F(Z80_Test, Z80OpcodeTimings)
{
    for (uint16_t i = 0; i <= 0xFF; i++)
    {
        ResetCPUAndMemory();  // Clean state for each instruction
        // ... test instruction timing ...
    }
}
```

**Analysis**: This pattern is valuable but currently **fixture-specific**. It should be extracted to a shared helper for reuse in `Z80_Logic_Jump_Test` and other Z80-related tests.

#### 2. Singleton Manager Pattern (`EmulatorManager_Test`)

New test file uses a **singleton manager pattern** with pre/post cleanup:

```cpp
class EmulatorManager_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;

protected:
    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);

        // Clean up any existing emulators before each test
        auto emulatorIds = _manager->GetEmulatorIds();
        for (const auto& id : emulatorIds)
        {
            _manager->RemoveEmulator(id);
        }
    }

    void TearDown() override
    {
        // Clean up after each test
        auto emulatorIds = _manager->GetEmulatorIds();
        for (const auto& id : emulatorIds)
        {
            _manager->RemoveEmulator(id);
        }
    }
};
```

**Note**: This pattern is unique and doesn't fit the existing patterns. It's a new **Pattern 7: Singleton Manager**.

#### 3. Inline Emulator Creation in Tests (`Breakpoints_test`)

Multiple tests in `breakpoints_test.cpp` create **inline Emulator instances** within individual tests rather than in fixture SetUp:

```cpp
TEST_F(BreakpointManager_test, executionBreakpoint)
{
    // Inline emulator creation
    Emulator* emulator = new Emulator(LoggerLevel::LogError);
    bool init = emulator->Init();
    EXPECT_EQ(init, true) << "Unable to initialize emulator instance";
    emulator->DebugOn();

    // ... test logic ...

    // Inline cleanup
    emulator->Stop();
    emulator->Release();
    delete emulator;
}
```

**Analysis**: This pattern appears in 5 tests within `breakpoints_test.cpp`. It indicates a need for **per-test emulator lifecycle** (not shared across tests), which could be simplified with a helper.

---

## Current State Analysis

### Existing Test Helpers

The project already has two test helpers in `core/tests/_helpers/`:

| Helper | Purpose | Current Usage |
|--------|---------|---------------|
| `TestPathHelper` | Resolves test data paths portably | Used in ~15 test files |
| `TestTimingHelper` | Manages T-state clock for timing tests | Used in ~5 test files (FDC, WD1793) |

### Problem Areas

1. **Repeated EmulatorContext Creation**: 29+ files create `EmulatorContext` with nearly identical code
2. **Core+Z80 Mock Pattern**: 8+ files duplicate the exact same Core+Z80 mocking pattern (20+ lines each)
3. **Inconsistent Cleanup Order**: Some tests use reverse cleanup order, others don't
4. **Logger Configuration Duplication**: Multiple files configure module logging identically
5. **Memory Initialization**: Repeated `DefaultBanksFor48k()` calls across loader and CPU tests
6. **Full Emulator Initialization**: 4+ tests create full `Emulator` instances with duplicate init/cleanup

---

## Identified Setup Patterns

### Pattern 1: Simple Context Only

**Files Using Pattern**: 11 files  
**Complexity**: Low  
**Lines per instance**: ~10 lines

```cpp
// Example from common/modulelogger_test.cpp
void ModuleLogger_Test::SetUp()
{
    _context = new EmulatorContext();
}

void ModuleLogger_Test::TearDown()
{
    if (_context != nullptr)
    {
        delete _context;
        _context = nullptr;
    }
}
```

**Used by**:
- `BitHelper_Test`
- `DumpHelper_Test`
- `FileHelper_Test`
- `StringHelper_Test`
- `AudioHelper_Test`
- `AudioFileHelper_Test`
- `ModuleLogger_Test`
- `Disassembler_Test`
- `Disassembler_Opcode_Test`
- `PortDecoder_Spectrum128_Test` (and other PortDecoder tests)
- `SoundChip_AY8910_Test`

---

### Pattern 2: Context + Single Component

**Files Using Pattern**: 7 files  
**Complexity**: Low-Medium  
**Lines per instance**: ~15 lines

```cpp
// Example from emulator/memory/memory_test.cpp
void Memory_Test::SetUp()
{
    _context = new EmulatorContext(LoggerLevel::LogError);
    _memory = new MemoryCUT(_context);
}

void Memory_Test::TearDown()
{
    if (_memory != nullptr)
    {
        delete _memory;
        _memory = nullptr;
    }

    if (_context != nullptr)
    {
        delete _context;
        _context = nullptr;
    }
}
```

**Used by**:
- `Memory_Test`
- `Keyboard_Test`
- `ScreenZX_Test`
- `LabelManager_test`

---

### Pattern 3: Context + Core + Memory Init

**Files Using Pattern**: 5 files  
**Complexity**: Medium  
**Lines per instance**: ~25 lines

```cpp
// Example from loaders/loader_sna_test.cpp
void LoaderSNA_Test::SetUp()
{
    _context = new EmulatorContext(LoggerLevel::LogError);

    _cpu = new Core(_context);
    if (_cpu->Init())
    {
        _cpu->GetMemory()->DefaultBanksFor48k();
    }
    else
    {
        throw std::runtime_error("Unable to SetUp LoaderSNA test(s)");
    }
}

void LoaderSNA_Test::TearDown()
{
    if (_cpu != nullptr)
    {
        delete _cpu;
        _cpu = nullptr;
    }

    if (_context != nullptr)
    {
        delete _context;
        _context = nullptr;
    }
}
```

**Used by**:
- `LoaderSNA_Test`
- `LoaderTAP_Test`
- `LoaderTZX_Test`
- `LoaderZ80_Test`
- `Z80_Logic_Jump_Test`

---

### Pattern 4: Context + Core Mock + Z80 (FDC/Disk Tests)

**Files Using Pattern**: 8 files  
**Complexity**: High  
**Lines per instance**: ~35 lines

```cpp
// Example from loaders/loader_trd_test.cpp and loader_scl_test.cpp
void LoaderTRD_Test::SetUp()
{
    _context = new EmulatorContext(LoggerLevel::LogError);

    // Set-up module logger only for FDC messages
    _context->pModuleLogger->TurnOffLoggingForAll();
    _context->pModuleLogger->TurnOnLoggingForModule(PlatformModulesEnum::MODULE_DISK,
                                                    PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC);

    // Mock Core and Z80 to make timings work
    _core = new CoreCUT(_context);
    _z80 = new Z80(_context);
    _core->_z80 = _z80;
    _context->pCore = _core;
}

void LoaderTRD_Test::TearDown()
{
    if (_context)
    {
        if (_context->pCore)
        {
            if (_core->_z80)
            {
                _core->_z80 = nullptr;
                delete _z80;
            }

            _context->pCore = nullptr;
            delete _core;
        }

        delete _context;
    }
}
```

**Used by**:
- `LoaderTRD_Test`
- `LoaderSCL_Test`
- `WD1793_Test`
- `MemoryAccessTracker_Test`
- `VG93_Test` (commented out)
- `DiskImage_Test`

---

### Pattern 5: Context + Core Mock + Z80 + TimingHelper

**Files Using Pattern**: 2 files  
**Complexity**: High  
**Lines per instance**: ~45 lines

```cpp
// Example from emulator/io/fdc/wd1793_test.cpp
void WD1793_Test::SetUp()
{
    _context = new EmulatorContext(LoggerLevel::LogDebug);
    _logger = _context->pModuleLogger;

    // Enable logging for WD1793 module
    _logger->TurnOffLoggingForAll();
    _logger->TurnOnLoggingForModule(PlatformModulesEnum::MODULE_DISK,
                                    PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC);

    // Set log level to warning by default
    _logger->SetLoggingLevel(LoggerLevel::LogWarning);

    // Mock Core and Z80 to make timings work
    _core = new CoreCUT(_context);
    _z80 = new Z80(_context);
    _core->_z80 = _z80;
    _context->pCore = _core;

    // Timing helper
    _timingHelper = new TestTimingHelper(_context);
    _timingHelper->resetClock();
}

void WD1793_Test::TearDown()
{
    if (_timingHelper)
    {
        delete _timingHelper;
    }

    if (_context)
    {
        if (_context->pCore)
        {
            if (_core->_z80)
            {
                _core->_z80 = nullptr;
                delete _z80;
            }

            _context->pCore = nullptr;
            delete _core;
        }

        delete _context;
    }
}
```

**Used by**:
- `WD1793_Test`
- `MemoryAccessTracker_Test` (partial - no timing helper, but has FeatureManager)

---

### Pattern 6: Full Emulator Instance

**Files Using Pattern**: 4 files  
**Complexity**: Very High  
**Lines per instance**: ~20 lines

```cpp
// Example from emulator/io/tape/tape_test.cpp
void Tape_Test::SetUp()
{
    _emulator = new Emulator(LoggerLevel::LogError);
    if (!_emulator->Init())
    {
        throw std::runtime_error("Failed to initialize emulator for Tape_Test");
    }

    _context = _emulator->GetContext();
    _tape = new TapeCUT(_context);
}

void Tape_Test::TearDown()
{
    if (_tape != nullptr)
    {
        delete _tape;
        _tape = nullptr;
    }

    if (_emulator != nullptr)
    {
        _emulator->Stop();
        _emulator->Release();
        delete _emulator;
        _emulator = nullptr;
    }

    _context = nullptr;  // Owned by _emulator, don't delete
}
```

**Used by**:
- `Tape_Test`
- `SharedMemory_Test`
- `Breakpoints_test` (partial - some tests create inline Emulator instances)
- `Emulator_Test`

---

### Pattern 7: Singleton Manager (NEW)

**Files Using Pattern**: 1 file  
**Complexity**: Medium  
**Lines per instance**: ~20 lines

```cpp
// Example from emulator/emulatormanager_test.cpp
class EmulatorManager_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;

protected:
    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);

        // Clean up any existing emulators before each test
        auto emulatorIds = _manager->GetEmulatorIds();
        for (const auto& id : emulatorIds)
        {
            _manager->RemoveEmulator(id);
        }
    }

    void TearDown() override
    {
        // Clean up after each test
        auto emulatorIds = _manager->GetEmulatorIds();
        for (const auto& id : emulatorIds)
        {
            _manager->RemoveEmulator(id);
        }
    }
};
```

**Used by**:
- `EmulatorManager_Test`

**Note**: This is a unique pattern for testing singleton managers. Does not require shared helper extraction at this time.

---

## Pattern Frequency Analysis

| Pattern | Files | Lines Duplicated | Priority |
|---------|-------|------------------|----------|
| Pattern 4: Core+Z80 Mock | 8 | ~280 lines | **HIGH** |
| Pattern 3: Core+Memory Init | 5 | ~125 lines | **HIGH** |
| Pattern 1: Simple Context | 11 | ~110 lines | MEDIUM |
| Pattern 6: Full Emulator | 4 | ~80 lines | MEDIUM |
| Pattern 2: Context+Component | 7 | ~105 lines | LOW |
| Pattern 5: Timing Helper | 2 | ~90 lines | LOW |
| Pattern 7: Singleton Manager | 1 | ~20 lines | LOW |

**Total Estimated Duplication**: ~810 lines across 38 files

### Additional Helpers to Extract

| Helper | Source | Potential Reuse |
|--------|--------|-----------------|
| `ResetCPUAndMemory()` | `Z80_Test` | `Z80_Logic_Jump_Test`, other Z80 tests |
| `DumpFirst256ROMBytes()` | `Z80_Test` | Debugging helper, general reuse |

---

## Consolidation Proposal

### Proposed New Helpers

All new helpers should be placed in `core/tests/_helpers/` following existing conventions.

#### 1. `TestContextHelper` - Base Context Factory

```cpp
// _helpers/test_context_helper.h
#pragma once

#include "emulator/emulatorcontext.h"
#include "common/modulelogger.h"

class TestContextHelper
{
public:
    /// Create a basic EmulatorContext with specified log level
    static EmulatorContext* CreateContext(LoggerLevel level = LoggerLevel::LogError);
    
    /// Create context with module-specific logging enabled
    static EmulatorContext* CreateContextWithModuleLogging(
        PlatformModulesEnum module,
        uint16_t submodule,
        LoggerLevel level = LoggerLevel::LogError);
    
    /// Safe cleanup of context (handles nullptr, sets to nullptr after delete)
    static void DestroyContext(EmulatorContext*& context);
};
```

#### 2. `TestCoreHelper` - Core/Z80 Mock Factory

```cpp
// _helpers/test_core_helper.h
#pragma once

#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"

/// Holds mocked Core and Z80 instances for testing
struct MockedCore
{
    CoreCUT* core = nullptr;
    Z80* z80 = nullptr;
};

class TestCoreHelper
{
public:
    /// Create mocked Core+Z80 and attach to context
    /// Returns MockedCore struct for cleanup
    static MockedCore CreateMockedCore(EmulatorContext* context);
    
    /// Create full Core (not mocked) with memory initialized for 48k
    static Core* CreateCoreWith48kMemory(EmulatorContext* context);
    
    /// Safe cleanup of mocked core (handles nullptr, detaches from context)
    static void DestroyMockedCore(EmulatorContext* context, MockedCore& mocked);
    
    /// Safe cleanup of full Core
    static void DestroyCore(Core*& core);
};
```

#### 3. `TestEmulatorHelper` - Full Emulator Factory

```cpp
// _helpers/test_emulator_helper.h
#pragma once

#include "emulator/emulator.h"

class TestEmulatorHelper
{
public:
    /// Create and initialize a full Emulator instance
    /// Throws std::runtime_error on init failure
    static Emulator* CreateEmulator(LoggerLevel level = LoggerLevel::LogError);
    
    /// Safe cleanup of Emulator (Stop, Release, delete)
    static void DestroyEmulator(Emulator*& emulator);
};
```

#### 4. Enhanced `TestTimingHelper` 

The existing `TestTimingHelper` already works well. Minor enhancement to add factory method:

```cpp
// Add to existing _helpers/testtiminghelper.h

/// Factory method that creates context, mocked core, and timing helper together
static TestTimingHelper* CreateWithMockedContext(
    EmulatorContext*& outContext,
    MockedCore& outMocked,
    LoggerLevel level = LoggerLevel::LogError);
```

#### 5. `TestZ80Helper` - Z80 State Reset Helper (NEW)

Based on the `ResetCPUAndMemory()` method recently added to `Z80_Test`, extract this as a reusable helper:

```cpp
// _helpers/test_z80_helper.h
#pragma once

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"

class TestZ80Helper
{
public:
    /// Reset Z80 CPU and memory to clean state for test isolation
    /// Resets: PC, SP, all registers (main + alt), memory banking, emulator flags
    static void ResetCPUAndMemory(Core* core, EmulatorContext* context);
    
    /// Dump first N bytes of ROM for debugging
    static void DumpROMBytes(Core* core, size_t count = 256);
    
    /// Prepare instruction bytes in memory at specified address
    static void WriteInstruction(Core* core, uint16_t address, const uint8_t* bytes, size_t length);
};
```

**Implementation** (extracted from `Z80_Test::ResetCPUAndMemory()`):

```cpp
// _helpers/test_z80_helper.cpp
void TestZ80Helper::ResetCPUAndMemory(Core* core, EmulatorContext* context)
{
    Z80& z80 = *core->GetZ80();
    z80.Reset();

    // Reset all registers to 0 for predictable state
    z80.bc = 0;
    z80.de = 0;
    z80.hl = 0;
    z80.ix = 0;
    z80.iy = 0;
    z80.alt.af = 0;
    z80.alt.bc = 0;
    z80.alt.de = 0;
    z80.alt.hl = 0;

    // Reset memory banking to default 48k layout
    core->GetMemory()->DefaultBanksFor48k();

    // Clear emulator state flags (like CF_TRDOS)
    context->emulatorState.flags = 0;
}
```

**Reuse potential**:
- `Z80_Test` - replace fixture method with helper call
- `Z80_Logic_Jump_Test` - can use for test isolation between iterations
- Future Z80 instruction tests

---

### Proposed Base Test Fixture Classes

For tests that need common setup, provide optional base classes:

```cpp
// _helpers/test_fixtures.h
#pragma once

#include <gtest/gtest.h>
#include "test_context_helper.h"
#include "test_core_helper.h"
#include "test_emulator_helper.h"

/// Base fixture with just EmulatorContext
class ContextTestFixture : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _context = TestContextHelper::CreateContext(LoggerLevel::LogError);
    }

    void TearDown() override
    {
        TestContextHelper::DestroyContext(_context);
    }
};

/// Base fixture with mocked Core+Z80 (for FDC, disk loader tests)
class MockedCoreTestFixture : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    MockedCore _mocked;

    void SetUp() override
    {
        _context = TestContextHelper::CreateContext(LoggerLevel::LogError);
        _mocked = TestCoreHelper::CreateMockedCore(_context);
    }

    void TearDown() override
    {
        TestCoreHelper::DestroyMockedCore(_context, _mocked);
        TestContextHelper::DestroyContext(_context);
    }
};

/// Base fixture with full Core and 48k memory (for loader tests)
class CoreTestFixture : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Core* _core = nullptr;

    void SetUp() override
    {
        _context = TestContextHelper::CreateContext(LoggerLevel::LogError);
        _core = TestCoreHelper::CreateCoreWith48kMemory(_context);
    }

    void TearDown() override
    {
        TestCoreHelper::DestroyCore(_core);
        TestContextHelper::DestroyContext(_context);
    }
};

/// Base fixture with full Emulator (for integration tests)
class EmulatorTestFixture : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;  // Owned by _emulator

    void SetUp() override
    {
        _emulator = TestEmulatorHelper::CreateEmulator(LoggerLevel::LogError);
        _context = _emulator->GetContext();
    }

    void TearDown() override
    {
        _context = nullptr;
        TestEmulatorHelper::DestroyEmulator(_emulator);
    }
};
```

---

## Prioritized Implementation Plan

### Phase 1: High-Impact Helpers (Priority: HIGH)

**Effort**: 2-3 hours  
**Impact**: Consolidates ~400 lines of duplicated code

| Task | Description | Files Affected |
|------|-------------|----------------|
| 1.1 | Create `test_context_helper.h/cpp` | New file |
| 1.2 | Create `test_core_helper.h/cpp` | New file |
| 1.3 | Create `test_z80_helper.h/cpp` (extract from `Z80_Test`) | New file |
| 1.4 | Update CMakeLists.txt to include new helpers | `core/tests/CMakeLists.txt` |
| 1.5 | Add unit tests for new helpers | New `_helpers/*_test.cpp` |

### Phase 2: Migrate Pattern 4 Tests (Priority: HIGH)

**Effort**: 1-2 hours  
**Impact**: Refactors 8 test files, removes ~280 lines

| Task | Description | Files Affected |
|------|-------------|----------------|
| 2.1 | Migrate `LoaderTRD_Test` | `loaders/loader_trd_test.cpp` |
| 2.2 | Migrate `LoaderSCL_Test` | `loaders/loader_scl_test.cpp` |
| 2.3 | Migrate `WD1793_Test` | `emulator/io/fdc/wd1793_test.cpp` |
| 2.4 | Migrate `MemoryAccessTracker_Test` | `emulator/memory/memoryaccesstracker_test.cpp` |
| 2.5 | Migrate `DiskImage_Test` | `emulator/io/fdc/diskimage_test.cpp` |

### Phase 3: Migrate Pattern 3 Tests (Priority: HIGH)

**Effort**: 1 hour  
**Impact**: Refactors 5 test files, removes ~125 lines

| Task | Description | Files Affected |
|------|-------------|----------------|
| 3.1 | Migrate `LoaderSNA_Test` | `loaders/loader_sna_test.cpp` |
| 3.2 | Migrate `LoaderTAP_Test` | `loaders/loader_tap_test.cpp` |
| 3.3 | Migrate `LoaderTZX_Test` | `loaders/loader_tzx_test.cpp` |
| 3.4 | Migrate `LoaderZ80_Test` | `loaders/loader_z80_test.cpp` |
| 3.5 | Migrate `Z80_Logic_Jump_Test` | `z80/z80_logic_jump_test.cpp` |

### Phase 4: Emulator Helper & Pattern 6 (Priority: MEDIUM)

**Effort**: 1 hour  
**Impact**: Refactors 4 test files, removes ~80 lines

| Task | Description | Files Affected |
|------|-------------|----------------|
| 4.1 | Create `test_emulator_helper.h/cpp` | New file |
| 4.2 | Migrate `Tape_Test` | `emulator/io/tape/tape_test.cpp` |
| 4.3 | Migrate `SharedMemory_Test` | `emulator/memory/sharedmemory_test.cpp` |
| 4.4 | Migrate `Emulator_Test` | `emulator/emulator_test.cpp` |

### Phase 5: Base Fixtures & Pattern 1/2 (Priority: LOW)

**Effort**: 2 hours  
**Impact**: Optional refactoring for simpler tests

| Task | Description | Files Affected |
|------|-------------|----------------|
| 5.1 | Create `test_fixtures.h` with base classes | New file |
| 5.2 | Optionally migrate simple context tests | 11 files (optional) |
| 5.3 | Update test README with new patterns | `core/tests/README.md` |

---

## Risk Assessment

### Low Risk
- **Helper implementation**: Pure additions, no existing code changes initially
- **Gradual migration**: Tests can be migrated one at a time
- **Rollback**: Easy to revert individual test migrations

### Medium Risk
- **Behavioral differences**: Helpers must exactly match current behavior
- **Order of initialization**: Some tests may depend on specific init order
- **Memory management**: Cleanup order must be preserved

### Mitigations
1. **Unit test the helpers first** before migrating any production tests
2. **Run full test suite** after each migration batch
3. **Keep original code in comments** during initial migration for easy comparison
4. **Migrate one pattern at a time**, validate, then continue

---

## Migration Strategy

### Step-by-Step Migration for Each Test File

1. **Create helper** (if not exists)
2. **Add `#include` for helper** in test file
3. **Replace SetUp() body** with helper calls
4. **Replace TearDown() body** with helper cleanup calls
5. **Run test** to verify behavior unchanged
6. **Remove redundant member variables** if now managed by helper
7. **Run full test suite** to catch any regressions

### Example Migration: `LoaderTRD_Test`

**Before (35 lines)**:
```cpp
class LoaderTRD_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    CoreCUT* _core = nullptr;
    Z80* _z80 = nullptr;

protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _context->pModuleLogger->TurnOffLoggingForAll();
        _context->pModuleLogger->TurnOnLoggingForModule(
            PlatformModulesEnum::MODULE_DISK,
            PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC);
        _core = new CoreCUT(_context);
        _z80 = new Z80(_context);
        _core->_z80 = _z80;
        _context->pCore = _core;
    }

    void TearDown() override
    {
        if (_context)
        {
            if (_context->pCore)
            {
                if (_core->_z80)
                {
                    _core->_z80 = nullptr;
                    delete _z80;
                }
                _context->pCore = nullptr;
                delete _core;
            }
            delete _context;
        }
    }
};
```

**After (15 lines)**:
```cpp
class LoaderTRD_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    MockedCore _mocked;

protected:
    void SetUp() override
    {
        _context = TestContextHelper::CreateContextWithModuleLogging(
            PlatformModulesEnum::MODULE_DISK,
            PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC);
        _mocked = TestCoreHelper::CreateMockedCore(_context);
    }

    void TearDown() override
    {
        TestCoreHelper::DestroyMockedCore(_context, _mocked);
        TestContextHelper::DestroyContext(_context);
    }
};
```

**Reduction**: 20 lines saved per file, 57% reduction

---

## Summary

| Metric | Current | After Consolidation |
|--------|---------|---------------------|
| Total setup/teardown lines | ~810 | ~340 |
| Unique setup implementations | 38 | 4 base patterns |
| Helper files | 2 | 6 |
| Identified patterns | 7 | 7 (consolidated) |
| Maintenance burden | High | Low |

### Recommended Next Steps

1. **Review this proposal** and provide feedback
2. **Prioritize phases** based on upcoming development needs
3. **Start with Phase 1** to create helper infrastructure
4. **Extract `ResetCPUAndMemory()`** from `Z80_Test` to shared helper (quick win)
5. **Migrate highest-impact tests first** (Pattern 4 - FDC/disk tests)
6. **Update README.md** with new helper usage guidelines

---

## Appendix: File-to-Pattern Mapping

| Test File | Pattern | Priority | Notes |
|-----------|---------|----------|-------|
| `loader_trd_test.cpp` | Pattern 4 (Core+Z80 Mock) | HIGH | |
| `loader_scl_test.cpp` | Pattern 4 (Core+Z80 Mock) | HIGH | |
| `wd1793_test.cpp` | Pattern 5 (Core+Z80+Timing) | HIGH | |
| `memoryaccesstracker_test.cpp` | Pattern 4+ (Core+Z80+Feature) | HIGH | |
| `loader_sna_test.cpp` | Pattern 3 (Core+Memory) | HIGH | |
| `loader_tap_test.cpp` | Pattern 3 (Core+Memory) | HIGH | |
| `loader_tzx_test.cpp` | Pattern 3 (Core+Memory) | HIGH | |
| `loader_z80_test.cpp` | Pattern 3 (Core+Memory) | HIGH | |
| `z80_logic_jump_test.cpp` | Pattern 3 (Core+Memory) | HIGH | |
| `z80_test.cpp` | Pattern 3 (Core+Memory) | HIGH | Has `ResetCPUAndMemory()` helper |
| `tape_test.cpp` | Pattern 6 (Full Emulator) | MEDIUM | |
| `sharedmemory_test.cpp` | Pattern 6 (Full Emulator) | MEDIUM | |
| `emulator_test.cpp` | Pattern 6 (Full Emulator) | MEDIUM | |
| `emulatormanager_test.cpp` | Pattern 7 (Singleton Manager) | MEDIUM | NEW - unique pattern |
| `breakpoints_test.cpp` | Pattern 4 + inline Emulator | MEDIUM | 5 inline Emulator instances |
| `memory_test.cpp` | Pattern 2 (Context+Component) | LOW | |
| `screenzx_test.cpp` | Pattern 2 (Context+Component) | LOW | |
| `keyboard_test.cpp` | Pattern 2 (Context+Component) | LOW | |
| `soundchip_ay8910_test.cpp` | Pattern 2 (Context+Component) | LOW | |
| `portdecoder_*_test.cpp` (5 files) | Pattern 2 (Context+Component) | LOW | |
| `disassembler_test.cpp` | Pattern 1 (Simple Context) | LOW | |
| `disassembler_opcode_test.cpp` | Pattern 1 (Simple Context) | LOW | |
| `modulelogger_test.cpp` | Pattern 1 (Simple Context) | LOW | |
| `*helper_test.cpp` (6 files) | Pattern 1 (Simple Context) | LOW | |
