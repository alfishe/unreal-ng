#include "pch.h"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "common/stringhelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/memory/memory.h"

/// @brief Unit tests for Emulator::StepOver() non-blocking behavior
class StepOver_test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;

    void SetUp() override
    {
        // Ensure complete isolation - dispose any existing MessageCenter
        MessageCenter::DisposeDefaultMessageCenter();

        // Create 48K emulator with debug features enabled
        _emulator = EmulatorTestHelper::CreateDebugEmulator(
            {"debugmode", "breakpoints"},
            "48K",
            LoggerLevel::LogError);

        ASSERT_NE(_emulator, nullptr) << "Failed to create 48K debug emulator";
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }

        MessageCenter::DisposeDefaultMessageCenter();
    }
};

/// @brief StepOver on simple instruction (NOP) should execute single step
TEST_F(StepOver_test, SimpleInstruction_ExecutesSingleStep)
{
    // Write NOP at $0000
    Memory* memory = _emulator->GetMemory();
    memory->DirectWriteToZ80Memory(0x0000, 0x00);  // NOP
    memory->DirectWriteToZ80Memory(0x0001, 0x76);  // HALT

    // Get initial PC
    Z80State* z80 = _emulator->GetZ80State();
    z80->pc = 0x0000;
    uint16_t initialPC = z80->pc;

    // StepOver should return immediately (non-blocking)
    auto start = std::chrono::steady_clock::now();
    _emulator->StepOver();
    auto duration = std::chrono::steady_clock::now() - start;

    // Should return quickly (< 100ms)
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count(), 100)
        << "StepOver on simple instruction should return quickly";

    // PC should have advanced past NOP
    EXPECT_EQ(z80->pc, 0x0001) << "PC should advance to next instruction after NOP";
}

/// @brief StepOver on CALL should return immediately (non-blocking)
TEST_F(StepOver_test, CallInstruction_ReturnsImmediately)
{
    // Write CALL $0010 at $0000, then a RET at $0010
    Memory* memory = _emulator->GetMemory();
    memory->DirectWriteToZ80Memory(0x0000, 0xCD);  // CALL
    memory->DirectWriteToZ80Memory(0x0001, 0x10);  // addr lo
    memory->DirectWriteToZ80Memory(0x0002, 0x00);  // addr hi = $0010
    memory->DirectWriteToZ80Memory(0x0003, 0x76);  // HALT (return address)

    memory->DirectWriteToZ80Memory(0x0010, 0xC9);  // RET

    // Set PC to CALL instruction
    Z80State* z80 = _emulator->GetZ80State();
    z80->pc = 0x0000;
    z80->sp = 0xFFFE;

    // StepOver should return immediately, NOT wait for CALL to complete
    auto start = std::chrono::steady_clock::now();
    _emulator->StepOver();
    auto duration = std::chrono::steady_clock::now() - start;

    // Should return in < 50ms since it's non-blocking
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count(), 50)
        << "StepOver on CALL should return immediately (non-blocking)";
}

/// @brief Verify orphan cleanup on Release() when breakpoint not hit
TEST_F(StepOver_test, OrphanCleanup_OnRelease)
{
    // Write CALL to infinite loop
    Memory* memory = _emulator->GetMemory();
    memory->DirectWriteToZ80Memory(0x0000, 0xCD);  // CALL
    memory->DirectWriteToZ80Memory(0x0001, 0x10);  // addr lo
    memory->DirectWriteToZ80Memory(0x0002, 0x00);  // addr hi = $0010
    memory->DirectWriteToZ80Memory(0x0003, 0x76);  // HALT (return address)

    // Infinite loop at $0010
    memory->DirectWriteToZ80Memory(0x0010, 0x18);  // JR $-2
    memory->DirectWriteToZ80Memory(0x0011, 0xFE);  // offset = -2

    Z80State* z80 = _emulator->GetZ80State();
    z80->pc = 0x0000;
    z80->sp = 0xFFFE;

    BreakpointManager* bpManager = _emulator->GetBreakpointManager();
    size_t bpCountBefore = bpManager->GetBreakpointsCount();

    // StepOver sets breakpoint at $0003
    _emulator->StepOver();

    // Verify breakpoint was added
    EXPECT_GT(bpManager->GetBreakpointsCount(), bpCountBefore)
        << "StepOver should add a breakpoint";

    // Release without hitting breakpoint - should clean up
    // (TearDown will call CleanupEmulator which calls Release)
    // No assertion needed - if orphan cleanup fails, we'd leak or crash
}

/// @brief StepOver on RST instruction returns immediately (non-blocking)
TEST_F(StepOver_test, RstInstruction_ReturnsImmediately)
{
    // RST $10 = print character (fast ROM routine)
    Memory* memory = _emulator->GetMemory();
    memory->DirectWriteToZ80Memory(0x8000, 0xD7);  // RST $10
    memory->DirectWriteToZ80Memory(0x8001, 0x76);  // HALT

    Z80State* z80 = _emulator->GetZ80State();
    z80->pc = 0x8000;
    z80->sp = 0xFFFE;

    auto start = std::chrono::steady_clock::now();
    _emulator->StepOver();
    auto duration = std::chrono::steady_clock::now() - start;

    // Should return in < 50ms since it's non-blocking
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count(), 50)
        << "StepOver on RST should return immediately (non-blocking)";
}

/// @brief StepOver on DJNZ loop returns immediately (non-blocking)
TEST_F(StepOver_test, DjnzInstruction_ReturnsImmediately)
{
    // DJNZ $-2 (tight loop)
    Memory* memory = _emulator->GetMemory();
    memory->DirectWriteToZ80Memory(0x8000, 0x06);  // LD B, $10
    memory->DirectWriteToZ80Memory(0x8001, 0x10);
    memory->DirectWriteToZ80Memory(0x8002, 0x10);  // DJNZ $8002 (self-loop)
    memory->DirectWriteToZ80Memory(0x8003, 0xFE);  // offset = -2
    memory->DirectWriteToZ80Memory(0x8004, 0x76);  // HALT

    Z80State* z80 = _emulator->GetZ80State();
    z80->pc = 0x8002;  // Start at DJNZ
    z80->b = 0x10;
    z80->sp = 0xFFFE;

    auto start = std::chrono::steady_clock::now();
    _emulator->StepOver();
    auto duration = std::chrono::steady_clock::now() - start;

    // Should return in < 50ms since it's non-blocking
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count(), 50)
        << "StepOver on DJNZ should return immediately (non-blocking)";
}
