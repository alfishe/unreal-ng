#pragma once

#include <string>
#include <cstdint>
#include <gtest/gtest.h>
#include "emulator/cpu/core.h"
#include "debugger/debugmanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "emulator/memory/memory.h"

// Forward declarations
class EmulatorContext;

class MemoryAccessTracker_Test : public ::testing::Test
{
protected:
    // Test constants
    static constexpr const char* TEST_REGION_NAME = "TestRegion";
    static constexpr const char* TEST_PORT_NAME = "TestPort";
    static constexpr uint16_t TEST_ADDRESS = 0x4000;
    static constexpr uint16_t TEST_PORT = 0xFE;
    static constexpr uint16_t CALLER_ADDRESS_1 = 0x8000;
    static constexpr uint16_t CALLER_ADDRESS_2 = 0x8100;

    // Test objects
    EmulatorContext* _context = nullptr;
    CoreCUT* _core = nullptr;
    Z80* _z80 = nullptr;
    DebugManager* _debugManager = nullptr;
    BreakpointManager* _breakpointManager = nullptr;
    MemoryCUT* _memory = nullptr;
    MemoryAccessTracker* _tracker = nullptr;

    // Setup and teardown
    void SetUp() override;
    void TearDown() override;

    // Helper methods
    void SimulateMemoryReads(uint16_t address, uint16_t callerAddress, int count);
    void SimulateMemoryWrites(uint16_t address, uint8_t value, uint16_t callerAddress, int count);
    void SimulateMemoryExecutes(uint16_t address, uint16_t callerAddress, int count);
    void SimulatePortReads(uint16_t port, uint16_t callerAddress, int count);
    void SimulatePortWrites(uint16_t port, uint8_t value, uint16_t callerAddress, int count);
};
