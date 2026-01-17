# Unit Test Development Guidelines

This document outlines the testing practices, conventions, and guidelines used in the `core/tests` directory for the Unreal-NG emulator project.

## Table of Contents

1. [Testing Framework](#testing-framework)
2. [Directory Structure](#directory-structure)
3. [File Naming Conventions](#file-naming-conventions)
4. [Test Class Structure](#test-class-structure)
5. [Test Fixtures](#test-fixtures)
6. [CUT (Class Under Test) Pattern](#cut-class-under-test-pattern)
7. [Test Helpers](#test-helpers)
8. [Code Organization with Regions](#code-organization-with-regions)
9. [Assertion Guidelines](#assertion-guidelines)
10. [Test Data Management](#test-data-management)
11. [Logging in Tests](#logging-in-tests)
12. [Test Categories](#test-categories)
13. [Writing New Tests](#writing-new-tests)
14. [Building and Running Tests](#building-and-running-tests)

---

## Best Practices Summary

> **TL;DR** — The essential rules for writing tests in this project.

1. **Structure mirrors source**: Test file location must reflect source file location
2. **Use CUT pattern**: Expose internals through CUT classes, not by making everything public
3. **Clean fixtures**: Always clean up in TearDown in reverse order of creation
4. **Use regions**: Organize code with `/// region` comments
5. **Prefer EXPECT over ASSERT**: Use ASSERT only for preconditions that would make later assertions meaningless
6. **Descriptive messages**: Include context in failure messages using `StringHelper::Format()`
7. **Control logging**: Set appropriate log levels to reduce noise
8. **Test data in testdata/**: Use `TestPathHelper::GetTestDataPath()` for portable paths
9. **Mark incomplete tests**: Use `FAIL() << "Not Implemented yet"` for placeholder tests
10. **Timing tests**: Use `TestTimingHelper` for cycle-accurate emulator tests

---

## Testing Framework

This project uses **Google Test (gtest)** as the primary testing framework.

- **Header**: `#include <gtest/gtest.h>` or `#include "pch.h"` (precompiled header includes gtest)
- **Submodule Location**: `googletest/` (linked via CMake)
- **Test Runner**: `main.cpp` with standard gtest initialization

```cpp
#include "gtest/gtest.h"

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
```

---

## Directory Structure

**CRITICAL**: Test directory structure MUST mirror the `core/src` directory structure.

```
core/
├── src/
│   ├── common/
│   │   ├── stringhelper.cpp
│   │   └── filehelper.cpp
│   ├── emulator/
│   │   ├── emulator.cpp
│   │   ├── io/
│   │   │   ├── fdc/
│   │   │   │   └── wd1793.cpp
│   │   │   └── keyboard/
│   │   │       └── keyboard.cpp
│   │   └── memory/
│   │       └── memory.cpp
│   ├── loaders/
│   │   ├── snapshot/
│   │   │   └── loader_sna.cpp
│   │   └── disk/
│   │       └── loader_trd.cpp
│   └── debugger/
│       └── breakpoints/
│           └── breakpointmanager.cpp
│
└── tests/
    ├── _helpers/                          # Test helper utilities (note underscore prefix)
    │   ├── test_path_helper.h
    │   ├── testtiminghelper.h
    │   └── testtiminghelper.cpp
    ├── common/                            # Mirrors core/src/common/
    │   ├── stringhelper_test.cpp
    │   ├── stringhelper_test.h
    │   ├── filehelper_test.cpp
    │   └── bithelper_test.cpp
    ├── emulator/                          # Mirrors core/src/emulator/
    │   ├── emulator_test.cpp
    │   ├── emulator_test.h
    │   ├── io/
    │   │   ├── fdc/
    │   │   │   ├── wd1793_test.cpp        # Tests for wd1793.cpp
    │   │   │   └── diskimage_test.cpp
    │   │   └── keyboard/
    │   │       └── keyboard_test.cpp
    │   └── memory/
    │       ├── memory_test.cpp
    │       └── memory_test.h
    ├── loaders/                           # Mirrors core/src/loaders/
    │   ├── loader_sna_test.cpp
    │   ├── loader_sna_test.h
    │   ├── loader_trd_test.cpp
    │   └── loader_tzx_test.cpp
    ├── debugger/                          # Mirrors core/src/debugger/
    │   ├── breakpoints_test.cpp
    │   └── breakpoints_test.h
    ├── 3rdparty/                          # Tests for third-party libraries
    │   ├── liblzma_test.cpp
    │   └── rapidyaml_test.cpp
    ├── pch.h                              # Precompiled header
    ├── pch.cpp
    ├── main.cpp                           # Test runner entry point
    └── README.md                          # This file
```

### Key Rules:

1. **Mirror the source structure**: If source is at `core/src/emulator/io/fdc/wd1793.cpp`, test goes to `core/tests/emulator/io/fdc/wd1793_test.cpp`
2. **Special directories**: `_helpers/` (prefixed with underscore) contains shared test utilities
3. **Third-party tests**: `3rdparty/` for testing external library integrations

---

## File Naming Conventions

| File Type | Pattern | Example |
|-----------|---------|---------|
| Test Implementation | `<component>_test.cpp` | `memory_test.cpp` |
| Test Header | `<component>_test.h` | `memory_test.h` |
| Test Helper Implementation | `<helper>.cpp` | `testtiminghelper.cpp` |
| Test Helper Header | `<helper>.h` | `test_path_helper.h` |

---

## Test Class Structure

Test classes inherit from `::testing::Test` and use protected members for fixture data.

### Header File (`<component>_test.h`)

```cpp
#pragma once
#include "pch.h"

#include "emulator/memory/memory.h"

class Memory_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    MemoryCUT* _memory = nullptr;  // CUT = Class Under Test

protected:
    void SetUp() override;
    void TearDown() override;
};
```

### Implementation File (`<component>_test.cpp`)

```cpp
#include "memory_test.h"

#include "common/stringhelper.h"

/// region <SetUp / TearDown>

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

/// endregion </SetUp / TearDown>

TEST_F(Memory_Test, RAMBase)
{
    uint8_t* referenceRAMBase = _memory->_memory;
    uint8_t* value = _memory->RAMBase();
    EXPECT_EQ(value, referenceRAMBase);
}
```

### Class Naming Convention

Use the pattern `<Component>_Test` for test class names.

Examples: `Memory_Test`, `Z80_Test`, `LoaderSNA_Test`, `BreakpointManager_Test`, `WD1793_Test`

---

## Test Fixtures

### Standard Pattern

```cpp
void ClassName_Test::SetUp()
{
    // 1. Create emulator context (controls logging level)
    _context = new EmulatorContext(LoggerLevel::LogError);

    // 2. Create component under test
    _component = new ComponentCUT(_context);

    // 3. Optional: Initialize dependencies
    if (_component->Init())
    {
        // Setup specific configurations
    }
    else
    {
        throw std::runtime_error("Unable to SetUp test");
    }
}

void ClassName_Test::TearDown()
{
    // Clean up in REVERSE order of creation
    if (_component != nullptr)
    {
        delete _component;
        _component = nullptr;
    }

    if (_context != nullptr)
    {
        delete _context;
        _context = nullptr;
    }
}
```

### Inline Fixtures (for simple tests)

```cpp
class LzmaTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        rng.seed(42);  // Initialize random generator
    }

    void TearDown() override
    {
        // Cleanup if needed
    }

    std::mt19937 rng;
};
```

---

## CUT (Class Under Test) Pattern

The CUT (Class Under Test) pattern exposes protected/private members for testing.

> **IMPORTANT**: If your test needs access to ANY non-public method or field, you MUST ensure a CUT wrapper class exists. The CUT wrapper MUST be defined in the **same header file** where the original class is declared, wrapped in `#ifdef _CODE_UNDER_TEST`. Do NOT create CUT wrappers in test files.

> **NOTE**: The `_CODE_UNDER_TEST` macro is automatically defined when building tests. This is configured in `core/tests/CMakeLists.txt` via:
> ```cmake
> target_compile_definitions(${PROJECT_NAME} PRIVATE _CODE_UNDER_TEST)
> ```
> You do NOT need to define this manually — it's handled by the build system.

### Definition in Source Header

```cpp
// In emulator/memory/memory.h

class Memory
{
protected:
    uint8_t* _memory;
    // ... other protected members

public:
    // Public interface
};

// CUT definition - expose internals for testing
#ifdef _CODE_UNDER_TEST
class MemoryCUT : public Memory
{
public:
    using Memory::Memory;          // Inherit constructors
    using Memory::_memory;         // Expose protected member
    // Expose other protected members as needed
};
#endif
```

### Usage in Tests

```cpp
#include "emulator/memory/memory.h"

class Memory_Test : public ::testing::Test
{
protected:
    MemoryCUT* _memory = nullptr;  // Use CUT version
};

TEST_F(Memory_Test, InternalAccess)
{
    // Direct access to protected members
    uint8_t* internalMemory = _memory->_memory;
    EXPECT_NE(internalMemory, nullptr);
}
```

### Available CUT Classes

| CUT Class | Description | Source Header |
|-----------|-------------|---------------|
| `MemoryCUT` | Memory system internals | [emulator/memory/memory.h](../src/emulator/memory/memory.h) |
| `Z80DisassemblerCUT` | Disassembler internals | [debugger/disassembler/z80disasm.h](../src/debugger/disassembler/z80disasm.h) |
| `BreakpointManagerCUT` | Breakpoint manager internals | [debugger/breakpoints/breakpointmanager.h](../src/debugger/breakpoints/breakpointmanager.h) |
| `LoaderSNACUT` | SNA loader internals | [loaders/snapshot/loader_sna.h](../src/loaders/snapshot/loader_sna.h) |
| `WD1793CUT` | Floppy disk controller internals | [emulator/io/fdc/wd1793.h](../src/emulator/io/fdc/wd1793.h) |

### Creating a New CUT Class

When you need to test non-public members and no CUT class exists:

1. **Open the source header file** (e.g., `core/src/emulator/video/screen.h`)

2. **Add the CUT class at the END of the file**, wrapped in `#ifdef _CODE_UNDER_TEST`:

   ```cpp
   // At the end of screen.h, after the Screen class definition
   
   #ifdef _CODE_UNDER_TEST
   class ScreenCUT : public Screen
   {
   public:
       using Screen::Screen;              // Inherit all constructors
       using Screen::_frameBuffer;        // Expose protected member
       using Screen::_borderColor;        // Expose another member
       using Screen::renderScanline;      // Expose protected method
   };
   #endif
   ```

3. **Naming convention**: `<OriginalClassName>CUT`

4. **Use `using` declarations** to expose each protected/private member you need:
   - `using ClassName::_memberVariable;` — for fields
   - `using ClassName::methodName;` — for methods
   - `using ClassName::ClassName;` — to inherit constructors

5. **In your test**, use the CUT version:
   ```cpp
   ScreenCUT* _screen = new ScreenCUT(_context);
   _screen->_frameBuffer;     // Now accessible
   _screen->renderScanline(); // Now accessible
   ```

> **TIP**: Only expose members you actually need for testing. Don't expose everything "just in case".

---

## Test Helpers

### TestPathHelper

Provides reliable test data path resolution across different execution environments.

```cpp
#include "_helpers/test_path_helper.h"

TEST_F(LoaderTest, LoadFile)
{
    // Get path to test data file
    std::string path = TestPathHelper::GetTestDataPath("loaders/sna/test.sna");
    
    // Use the path
    LoaderSNA loader(_context, path);
}
```

### TestTimingHelper

Provides timing simulation utilities for cycle-accurate tests.

```cpp
#include "_helpers/testtiminghelper.h"

class FDC_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    TestTimingHelper* _timingHelper = nullptr;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _timingHelper = new TestTimingHelper(_context);
        _timingHelper->resetClock();
    }

    void TearDown() override
    {
        if (_timingHelper != nullptr)
        {
            delete _timingHelper;
            _timingHelper = nullptr;
        }

        if (_context != nullptr)
        {
            delete _context;
            _context = nullptr;
        }
    }
};

TEST_F(FDC_Test, Timing)
{
    // Advance time
    _timingHelper->forward(1000);  // Forward 1000 T-states
    
    // Convert T-states to milliseconds
    size_t ms = TestTimingHelper::convertTStatesToMs(clk);
}
```

### Custom Assertion Macros

Defined in `_helpers/testtiminghelper.h`:

```cpp
// Range checking
#define EXPECT_IN_RANGE(VAL, MIN, MAX) \
    EXPECT_GE((VAL), (MIN));           \
    EXPECT_LE((VAL), (MAX))

#define ASSERT_IN_RANGE(VAL, MIN, MAX) \
    ASSERT_GE((VAL), (MIN));           \
    ASSERT_LE((VAL), (MAX))

// Array comparison
#define EXPECT_ARRAYS_EQ(arr1, arr2, size) \
    ASSERT_TRUE(areUint8ArraysEqual(arr1, arr2, size))

#define ASSERT_ARRAYS_EQ(arr1, arr2, size) \
    ASSERT_TRUE(areUint8ArraysEqual(arr1, arr2, size))
```

---

## Code Organization with Regions

Use region comments to organize test code into logical sections.

```cpp
/// region <SetUp / TearDown>

void ClassName_Test::SetUp() { /* ... */ }
void ClassName_Test::TearDown() { /* ... */ }

/// endregion </SetUp / TearDown>

/// region <Helper methods>

void ClassName_Test::DumpState() { /* ... */ }

/// endregion </Helper methods>

/// region <Positive cases>

TEST_F(ClassName_Test, ValidInput) { /* ... */ }

/// endregion </Positive cases>

/// region <Negative cases>

TEST_F(ClassName_Test, InvalidInput) { /* ... */ }

/// endregion </Negative cases>
```

### Common Regions

- `<SetUp / TearDown>` - Fixture setup and cleanup
- `<Helper methods>` - Test helper functions
- `<Pre-checks>` - Precondition validation
- `<Main loop>` / `<Perform simulation loop>` - Test execution
- `<Check results>` - Result verification
- `<Positive cases>` / `<Negative cases>` - Test case categories
- `<Test types>` - Type definitions for tests
- `<Constants>` - Test constants
- `<Fields>` - Member variables

---

## Assertion Guidelines

### Choosing the Right Assertion

| Situation | Use | Why |
|-----------|-----|-----|
| Non-fatal check | `EXPECT_*` | Test continues on failure |
| Fatal precondition | `ASSERT_*` | Test stops on failure |
| Immediate failure | `FAIL()` | Unconditional failure |

### Common Assertion Patterns

```cpp
// Value comparison
EXPECT_EQ(result, expected);          // result == expected
EXPECT_NE(result, unexpected);        // result != unexpected
EXPECT_LT(result, maxValue);          // result < maxValue
EXPECT_LE(result, maxValue);          // result <= maxValue
EXPECT_GT(result, minValue);          // result > minValue
EXPECT_GE(result, minValue);          // result >= minValue

// Boolean checks
EXPECT_TRUE(condition);
EXPECT_FALSE(condition);

// Pointer checks
EXPECT_EQ(pointer, nullptr);
EXPECT_NE(pointer, nullptr);

// Floating point comparison
EXPECT_NEAR(value, expected, tolerance);

// Container/collection comparison
EXPECT_EQ(decompressed, input);       // Works for std::vector

// With custom message
EXPECT_EQ(result, expected) << "Custom error message";
EXPECT_EQ(result, expected) << StringHelper::Format("Value was %d", result);
```

### Error Reporting Patterns

```cpp
// Formatted error messages
TEST_F(Test, Example)
{
    std::string message = StringHelper::Format("Opcode: 0x%02X", opcode);
    EXPECT_EQ(result, expected) << message;
}

// Multi-condition failure with FAIL()
TEST_F(Test, MultiCheck)
{
    if (result != true)
    {
        std::string message = StringHelper::Format("Validation FAILED for file '%s'", path.c_str());
        FAIL() << message << std::endl;
    }
}
```

---

## Test Data Management

### Location

Test data files are stored in the project's `testdata/` directory (at project root level).

### Accessing Test Data

```cpp
#include "_helpers/test_path_helper.h"

// Get absolute path to test data
std::string path = TestPathHelper::GetTestDataPath("loaders/sna/test.sna");

// Use with file helpers
std::string absolutePath = FileHelper::AbsolutePath(path);
FILE* file = FileHelper::OpenExistingFile(absolutePath);
```

### Test Data Organization

```
testdata/
├── loaders/
│   ├── sna/
│   │   ├── 48k.sna
│   │   └── 128k.sna
│   ├── trd/
│   │   └── test.trd
│   └── tzx/
│       └── test.tzx
└── roms/
    └── 48.rom
```

---

## Logging in Tests

### Controlling Log Levels

```cpp
void ClassName_Test::SetUp()
{
    // Set log level for test output control
    _context = new EmulatorContext(LoggerLevel::LogError);  // Only errors
    
    // Or adjust per-test
    _context->pModuleLogger->SetLoggingLevel(LoggerLevel::LogDebug);
}

TEST_F(ClassName_Test, VerboseTest)
{
    // Enable verbose logging for debugging
    _context->pModuleLogger->SetLoggingLevel(LoggerLevel::LogDebug);
    
    // ... test code ...
}
```

### Module-Specific Logging

```cpp
ModuleLogger* logger = _context->pModuleLogger;

// Disable all logging
logger->TurnOffLoggingForAll();

// Enable specific module
logger->TurnOnLoggingForModule(PlatformModulesEnum::MODULE_DISK,
                               PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC);
```

### Google Test Logging

```cpp
// Info-level output (visible with --gtest_print_time=1)
GTEST_LOG_(INFO) << "Test state: " << state;

// Standard output (always visible)
std::cout << "Debug info: " << value << std::endl;
```

---

## Test Categories

### Functional Tests

Test individual functions/methods in isolation:

```cpp
TEST_F(StringHelper_Test, LTrim)
{
    std::string original = " \t    test string \t  ";
    std::string reference = "test string \t  ";
    
    std::string_view result = StringHelper::LTrim(original);
    EXPECT_EQ(std::string(result), reference);
}
```

### Integration Tests

Test component interactions:

```cpp
TEST_F(Emulator_Test, MultiInstanceRun)
{
    for (int i = 0; i < 5; ++i)
    {
        auto emulator = std::make_unique<Emulator>(LoggerLevel::LogError);
        ASSERT_TRUE(emulator->Init());
        emulator->StartAsync();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        emulator->Stop();
    }
}
```

### Timing/Cycle-Accurate Tests

Test precise timing behavior:

```cpp
TEST_F(WD1793_Test, FDD_Motor_StartStop)
{
    static constexpr size_t TEST_DURATION_TSTATES = Z80_FREQUENCY * 4;
    
    for (size_t clk = 0; clk < TEST_DURATION_TSTATES; clk += 1000)
    {
        fdc._time = clk;
        fdc.process();
        
        // Check state at each time step
    }
    
    EXPECT_IN_RANGE(motorWasOnForTSTates, expected - tolerance, expected + tolerance);
}
```

### Data-Driven Tests

Test multiple inputs systematically:

```cpp
TEST_F(Disassembler_Test, commandType)
{
    struct TestCase
    {
        std::vector<uint8_t> bytes;
        bool hasJump;
        bool hasReturn;
        // ... other expected flags
    };

    std::vector<TestCase> testCases = {
        { {0x00}, false, false },  // NOP
        { {0xC3, 0x34, 0x12}, true, false },  // JP nn
        { {0xC9}, false, true },  // RET
    };

    for (const auto& test : testCases)
    {
        DecodedInstruction decoded = _disasm->decodeInstruction(test.bytes, 0);
        EXPECT_EQ(decoded.hasJump, test.hasJump);
        EXPECT_EQ(decoded.hasReturn, test.hasReturn);
    }
}
```

### Placeholder Tests (Not Yet Implemented)

```cpp
TEST_F(WD1793_Test, Beta128_Status_INTRQ)
{
    FAIL() << "Not Implemented yet";
}
```

---

## Writing New Tests

### Step-by-Step Guide

1. **Determine the test location**
   - Mirror the source file path in `core/tests/`
   - Example: `core/src/emulator/video/screen.cpp` → `core/tests/emulator/video/screen_test.cpp`

2. **Create the header file** (`<component>_test.h`)
   ```cpp
   #pragma once
   #include "pch.h"
   #include "path/to/component.h"
   
   class Component_Test : public ::testing::Test
   {
   protected:
       EmulatorContext* _context = nullptr;
       ComponentCUT* _component = nullptr;
   
   protected:
       void SetUp() override;
       void TearDown() override;
   };
   ```

3. **Create the implementation file** (`<component>_test.cpp`)
   ```cpp
   #include "component_test.h"
   #include "common/stringhelper.h"
   
   /// region <SetUp / TearDown>
   
   void Component_Test::SetUp()
   {
       _context = new EmulatorContext(LoggerLevel::LogError);
       _component = new ComponentCUT(_context);
   }
   
   void Component_Test::TearDown()
   {
       if (_component) { delete _component; _component = nullptr; }
       if (_context) { delete _context; _context = nullptr; }
   }
   
   /// endregion </SetUp / TearDown>
   
   TEST_F(Component_Test, BasicFunctionality)
   {
       // Arrange
       // ... setup test conditions
       
       // Act
       auto result = _component->SomeMethod();
       
       // Assert
       EXPECT_EQ(result, expectedValue);
   }
   ```

4. **CMake auto-discovery**: Test files are automatically discovered via glob patterns in `CMakeLists.txt`. You typically do NOT need to manually add new test files — just ensure they follow the naming convention (`*_test.cpp`) and are placed in the correct mirrored directory.

### Test Naming Guidelines

Use descriptive test names that indicate:
- **What** is being tested
- **Under what conditions** (optional)
- **Expected outcome** (optional)

```cpp
// Good names
TEST_F(Memory_Test, RAMPageAddress)
TEST_F(WD1793_Test, FDD_Motor_StartStop)
TEST_F(StringHelper_Test, ReplaceAll)
TEST_F(Disassembler_Test, disassembleSingleCommand)

// Pattern: ComponentUnderTest_MethodOrFeature_Condition
TEST_F(WD1793_Test, FSM_CMD_Restore_OnReset)
TEST_F(WD1793_Test, FSM_CMD_Restore_Verify)
```

---

## Building and Running Tests

### Building

```bash
# From project root
mkdir build && cd build
cmake ..
cmake --build . --target core-tests

# Or on Windows with specific config
cmake --build . --config Release --target core-tests
```

### Running Tests

```bash
# Run all tests
./core-tests

# Run specific test
./core-tests --gtest_filter="Memory_Test.*"

# Run with verbose output
./core-tests --gtest_print_time=1

# List all tests
./core-tests --gtest_list_tests
```

### Filtering Tests

```bash
# Single test
./core-tests --gtest_filter="Memory_Test.RAMBase"

# All tests in a fixture
./core-tests --gtest_filter="Memory_Test.*"

# Pattern matching
./core-tests --gtest_filter="*FDD*"

# Exclude tests
./core-tests --gtest_filter="-*NotImplemented*"
```

---

## Test Examples: Good vs Bad

This section illustrates what makes a test good or bad with concrete examples.

### Good Test Examples

#### Example 1: Clear Arrange-Act-Assert Structure

```cpp
TEST_F(StringHelper_Test, TrimRemovesLeadingAndTrailingWhitespace)
{
    // Arrange
    std::string input = "  \t hello world \t  ";
    std::string expected = "hello world";
    
    // Act
    std::string result = StringHelper::Trim(input);
    
    // Assert
    EXPECT_EQ(result, expected);
}
```

**Why it's good:**
- Descriptive test name explains what's being tested
- Clear separation of setup, execution, and verification
- Single responsibility — tests one thing
- Uses meaningful variable names (`input`, `expected`, `result`)

---

#### Example 2: Proper Fixture with Complete Cleanup

```cpp
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

TEST_F(Memory_Test, RAMPageAddress)
{
    // Arrange
    uint8_t expectedPage = 5;
    
    // Act
    uint8_t* pageAddress = _memory->RAMPageAddress(expectedPage);
    uint8_t* baseAddress = _memory->_memory;
    
    // Assert
    size_t offset = pageAddress - baseAddress;
    EXPECT_EQ(offset, expectedPage * 0x4000);
}
```

**Why it's good:**
- TearDown cleans up in reverse order of creation
- Null checks prevent double-delete issues
- Uses CUT to access internals cleanly
- Test verifies behavior, not just that code runs

---

#### Example 3: Data-Driven Test with Good Error Messages

```cpp
TEST_F(Disassembler_Test, DecodeInstructionLength)
{
    struct TestCase
    {
        std::vector<uint8_t> bytes;
        size_t expectedLength;
        const char* mnemonic;
    };

    std::vector<TestCase> testCases = {
        { {0x00},             1, "NOP" },
        { {0xC3, 0x00, 0x00}, 3, "JP nn" },
        { {0xCB, 0x00},       2, "RLC B" },
        { {0xDD, 0x21, 0x00, 0x00}, 4, "LD IX,nn" },
    };

    for (const auto& test : testCases)
    {
        DecodedInstruction decoded = _disasm->decodeInstruction(test.bytes, 0);
        
        EXPECT_EQ(decoded.length, test.expectedLength) 
            << "Failed for instruction: " << test.mnemonic
            << " (opcode: 0x" << std::hex << (int)test.bytes[0] << ")";
    }
}
```

**Why it's good:**
- Tests multiple cases efficiently without code duplication
- Custom error message identifies which case failed
- Test cases are self-documenting with mnemonic names
- Easy to add new test cases

---

#### Example 4: Integration Test with Proper Isolation

```cpp
TEST_F(Emulator_Test, MultiInstancesDoNotInterfere)
{
    // Arrange
    auto emulator1 = std::make_unique<Emulator>(LoggerLevel::LogError);
    auto emulator2 = std::make_unique<Emulator>(LoggerLevel::LogError);
    
    ASSERT_TRUE(emulator1->Init()) << "Failed to init emulator1";
    ASSERT_TRUE(emulator2->Init()) << "Failed to init emulator2";
    
    // Act - modify state in emulator1
    emulator1->GetMemory()->WriteByte(0x4000, 0xAB);
    
    // Assert - emulator2 should not be affected
    uint8_t value = emulator2->GetMemory()->ReadByte(0x4000);
    EXPECT_NE(value, 0xAB) << "Emulator instances are sharing memory!";
}
```

**Why it's good:**
- Uses ASSERT for preconditions (Init must succeed)
- Tests a specific invariant (instance isolation)
- Clear failure message explains the problem
- Uses smart pointers for automatic cleanup

---

### Bad Test Examples (Anti-Patterns)

#### Anti-Pattern 1: No Assertions

```cpp
// BAD: Test with no assertions
TEST_F(Memory_Test, Initialize)
{
    _memory->Init();
    // Test passes if no crash... but verifies nothing!
}
```

**Why it's bad:**
- Doesn't verify any expected behavior
- Passes even if Init() does nothing
- False sense of security

**Fix:**
```cpp
TEST_F(Memory_Test, InitializeAllocatesMemory)
{
    bool result = _memory->Init();
    
    EXPECT_TRUE(result);
    EXPECT_NE(_memory->_memory, nullptr);
    EXPECT_EQ(_memory->GetSize(), 128 * 1024);  // Expected memory size
}
```

---

#### Anti-Pattern 2: Testing Multiple Things

```cpp
// BAD: Tests too many things at once
TEST_F(Z80_Test, Everything)
{
    _cpu->Reset();
    EXPECT_EQ(_cpu->GetPC(), 0);
    EXPECT_EQ(_cpu->GetSP(), 0xFFFF);
    
    _cpu->ExecuteInstruction(0x3E, 0x42);  // LD A, 0x42
    EXPECT_EQ(_cpu->GetA(), 0x42);
    
    _cpu->ExecuteInstruction(0xC3, 0x00, 0x10);  // JP 0x1000
    EXPECT_EQ(_cpu->GetPC(), 0x1000);
    
    // ... 50 more instructions ...
}
```

**Why it's bad:**
- If it fails, hard to know which part broke
- Single test doing too many things
- Difficult to maintain
- Takes long to run

**Fix:** Split into focused tests:
```cpp
TEST_F(Z80_Test, ResetSetsPC_ToZero) { /* ... */ }
TEST_F(Z80_Test, ResetSetsSP_ToFFFF) { /* ... */ }
TEST_F(Z80_Test, LD_A_ImmediateLoadsValue) { /* ... */ }
TEST_F(Z80_Test, JP_SetsPC_ToAddress) { /* ... */ }
```

---

#### Anti-Pattern 3: Magic Numbers Without Context

```cpp
// BAD: Magic numbers everywhere
TEST_F(Memory_Test, PageMapping)
{
    _memory->SetPage(3, 7);
    uint8_t* addr = _memory->GetPageAddress(3);
    EXPECT_EQ(addr - _memory->_memory, 0x1C000);
}
```

**Why it's bad:**
- What do 3, 7, and 0x1C000 mean?
- Hard to verify correctness
- Maintenance nightmare

**Fix:**
```cpp
TEST_F(Memory_Test, PageMappingCalculatesCorrectOffset)
{
    // Arrange
    constexpr uint8_t SLOT = 3;           // Memory slot (0-3)
    constexpr uint8_t RAM_PAGE = 7;       // RAM page to map
    constexpr size_t PAGE_SIZE = 0x4000;  // 16KB per page
    constexpr size_t EXPECTED_OFFSET = RAM_PAGE * PAGE_SIZE;  // 0x1C000
    
    // Act
    _memory->SetPage(SLOT, RAM_PAGE);
    uint8_t* addr = _memory->GetPageAddress(SLOT);
    
    // Assert
    size_t actualOffset = addr - _memory->_memory;
    EXPECT_EQ(actualOffset, EXPECTED_OFFSET) 
        << "RAM page " << (int)RAM_PAGE << " should be at offset 0x" 
        << std::hex << EXPECTED_OFFSET;
}
```

---

#### Anti-Pattern 4: Missing Cleanup (Memory Leak)

```cpp
// BAD: Memory leak in test
TEST_F(LoaderTest, LoadFile)
{
    EmulatorContext* ctx = new EmulatorContext();
    LoaderSNA* loader = new LoaderSNA(ctx);
    
    loader->Load("test.sna");
    EXPECT_TRUE(loader->IsLoaded());
    
    // Forgot to delete loader and ctx!
}
```

**Why it's bad:**
- Memory leak accumulates over test runs
- Can cause spurious failures in later tests
- Masks real memory issues in code under test

**Fix:** Use fixture or smart pointers:
```cpp
TEST_F(LoaderTest, LoadFileSucceeds)
{
    // Use fixture's _context, or:
    auto ctx = std::make_unique<EmulatorContext>(LoggerLevel::LogError);
    auto loader = std::make_unique<LoaderSNA>(ctx.get());
    
    std::string path = TestPathHelper::GetTestDataPath("loaders/sna/test.sna");
    loader->Load(path);
    
    EXPECT_TRUE(loader->IsLoaded());
    // Automatic cleanup via unique_ptr
}
```

---

#### Anti-Pattern 5: Test Depends on Other Tests

```cpp
// BAD: Test depends on state from previous test
static int sharedCounter = 0;

TEST_F(Counter_Test, Increment)
{
    sharedCounter++;
    EXPECT_EQ(sharedCounter, 1);
}

TEST_F(Counter_Test, IncrementAgain)
{
    sharedCounter++;
    EXPECT_EQ(sharedCounter, 2);  // Fails if run alone or in different order!
}
```

**Why it's bad:**
- Tests are not independent
- Order-dependent (gtest doesn't guarantee order)
- Parallel test execution breaks it
- Hard to run single test in isolation

**Fix:** Each test sets up its own state:
```cpp
TEST_F(Counter_Test, IncrementFromZero)
{
    int counter = 0;
    counter++;
    EXPECT_EQ(counter, 1);
}

TEST_F(Counter_Test, IncrementFromFive)
{
    int counter = 5;
    counter++;
    EXPECT_EQ(counter, 6);
}
```

---

#### Anti-Pattern 6: Catching Exceptions Without Verification

```cpp
// BAD: Swallowing exceptions
TEST_F(FileHelper_Test, OpenFile)
{
    try
    {
        FileHelper::OpenFile("nonexistent.txt");
        // Should have thrown!
    }
    catch (...)
    {
        // Test passes... but did it throw the RIGHT exception?
    }
}
```

**Why it's bad:**
- Catches ANY exception, not the expected one
- Doesn't verify exception type or message
- Test passes even if wrong exception thrown

**Fix:** Use gtest exception assertions:
```cpp
TEST_F(FileHelper_Test, OpenNonexistentFileThrowsFileNotFound)
{
    EXPECT_THROW(
        FileHelper::OpenFile("nonexistent.txt"),
        FileNotFoundException
    );
}

// Or for more detailed checking:
TEST_F(FileHelper_Test, OpenNonexistentFileThrowsWithPath)
{
    try
    {
        FileHelper::OpenFile("nonexistent.txt");
        FAIL() << "Expected FileNotFoundException";
    }
    catch (const FileNotFoundException& e)
    {
        EXPECT_THAT(e.what(), ::testing::HasSubstr("nonexistent.txt"));
    }
}
```

---

### Quick Reference: Good Test Checklist

| Criterion | Check |
|-----------|-------|
| **Single purpose** | Tests exactly one behavior |
| **Independent** | Can run alone, in any order |
| **Deterministic** | Same result every time |
| **Fast** | Completes in milliseconds |
| **Readable** | Another dev can understand intent |
| **Meaningful assertions** | Verifies expected behavior, not just "no crash" |
| **Good failure messages** | Clear what went wrong when it fails |
| **Clean setup/teardown** | No leaked resources |

---

## References

- [Google Test Documentation](https://google.github.io/googletest/)
- [Google Test Primer](https://google.github.io/googletest/primer.html)
- [Advanced Google Test Topics](https://google.github.io/googletest/advanced.html)
