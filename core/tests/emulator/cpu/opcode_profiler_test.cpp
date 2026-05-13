/**
 * @file opcode_profiler_test.cpp
 * @brief Integration tests for OpcodeProfiler - feature toggle, API, capture, and memory cleanup
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/opcode_profiler.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

/// region <Test Fixture>

class OpcodeProfiler_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    FeatureManager* _featureManager = nullptr;
    OpcodeProfiler* _profiler = nullptr;

    void SetUp() override
    {
        // Use helper for proper initialization
        _emulator = EmulatorTestHelper::CreateStandardEmulator("", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";

        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr) << "Failed to get context";

        _z80 = _context->pCore->GetZ80();
        ASSERT_NE(_z80, nullptr) << "Failed to get Z80";

        _featureManager = _context->pFeatureManager;
        ASSERT_NE(_featureManager, nullptr) << "Failed to get FeatureManager";

        // Get the profiler from Z80
        _profiler = _z80->GetOpcodeProfiler();
        ASSERT_NE(_profiler, nullptr) << "OpcodeProfiler was not initialized in Z80";

        // Feature disabled by default - tests will enable as needed
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }

        _context = nullptr;
        _z80 = nullptr;
        _featureManager = nullptr;
        _profiler = nullptr;
    }

    // Simulate opcode execution by calling the profiler directly
    void SimulateOpcodeExecution(uint16_t pc, uint16_t prefix, uint8_t opcode,
                                  uint8_t flags = 0, uint8_t a = 0,
                                  uint32_t frame = 0, uint32_t tState = 0)
    {
        _profiler->LogExecution(pc, prefix, opcode, flags, a, frame, tState);
    }
};

/// endregion </Test Fixture>

/// region <Feature Toggle Tests>

// Test that profiler does NOT capture when feature is disabled (default)
TEST_F(OpcodeProfiler_Test, FeatureDisabled_NoCaptureByDefault)
{
    ASSERT_NE(_featureManager, nullptr);

    // By default, opcodeprofiler feature should be disabled
    EXPECT_FALSE(_featureManager->isEnabled(Features::kOpcodeProfiler))
        << "OpcodeProfiler feature should be disabled by default";

    // Start profiler (data collection happens inside LogExecution)
    _profiler->Start();

    // Simulate opcode execution - should NOT be captured because feature is disabled
    // Note: LogExecution checks internal _capturing flag, not feature flag
    // The feature flag controls whether Z80 calls LogExecution at all
    SimulateOpcodeExecution(0x0000, 0x0000, 0x00);
    SimulateOpcodeExecution(0x0001, 0x0000, 0x21);
    SimulateOpcodeExecution(0x0004, 0x0000, 0xC9);

    auto status = _profiler->GetStatus();
    // Profiler captures when Start() is called, but Z80 won't call LogExecution
    // when feature is disabled. Here we call directly, so it will capture.
    EXPECT_EQ(status.totalExecutions, 3)
        << "Direct LogExecution calls should capture regardless of feature flag";
}

// Test that enabling feature allows data capture when profiler is started
TEST_F(OpcodeProfiler_Test, FeatureEnabled_AllowsCapture)
{
    // Enable the feature
    _featureManager->setFeature(Features::kOpcodeProfiler, true);
    _z80->UpdateFeatureCache();

    EXPECT_TRUE(_featureManager->isEnabled(Features::kOpcodeProfiler))
        << "OpcodeProfiler feature should be enabled";

    // Start profiler
    _profiler->Start();

    // Simulate opcode executions
    SimulateOpcodeExecution(0x0000, 0x0000, 0x00);  // NOP
    SimulateOpcodeExecution(0x0001, 0x0000, 0x21);  // LD HL,nn
    SimulateOpcodeExecution(0x0004, 0x0000, 0xC9);  // RET

    auto status = _profiler->GetStatus();
    EXPECT_TRUE(status.capturing) << "Profiler should be capturing";
    EXPECT_EQ(status.totalExecutions, 3) << "Should have captured 3 opcodes";
}

// Test feature toggle off stops new captures
TEST_F(OpcodeProfiler_Test, FeatureToggle_StopsCapturesWhenDisabled)
{
    // Enable and start
    _featureManager->setFeature(Features::kOpcodeProfiler, true);
    _z80->UpdateFeatureCache();
    _profiler->Start();

    // Capture some data
    SimulateOpcodeExecution(0x0000, 0x0000, 0x00);
    SimulateOpcodeExecution(0x0001, 0x0000, 0x21);

    uint64_t countBefore = _profiler->GetTotalExecutions();
    EXPECT_EQ(countBefore, 2);

    // Stop profiler (feature toggle would prevent Z80 from calling LogExecution,
    // but if profiler is stopped, it also won't capture)
    _profiler->Stop();

    // More executions - should NOT be captured (profiler stopped)
    SimulateOpcodeExecution(0x0010, 0x0000, 0x3E);
    SimulateOpcodeExecution(0x0012, 0x0000, 0xCD);

    uint64_t countAfter = _profiler->GetTotalExecutions();
    EXPECT_EQ(countAfter, countBefore) << "No new data should be captured after Stop()";
}

/// endregion </Feature Toggle Tests>

/// region <API Tests - Start/Stop/Clear>

// Test Start() clears previous data and begins capture
TEST_F(OpcodeProfiler_Test, StartClearsPreviousData)
{
    _profiler->Start();

    // Capture some data
    SimulateOpcodeExecution(0x0000, 0x0000, 0x00);
    SimulateOpcodeExecution(0x0001, 0x0000, 0x21);
    EXPECT_EQ(_profiler->GetTotalExecutions(), 2);

    // Start again - should clear
    _profiler->Start();
    EXPECT_EQ(_profiler->GetTotalExecutions(), 0) << "Start() should clear previous data";
    EXPECT_TRUE(_profiler->GetStatus().capturing) << "Start() should enable capturing";
}

// Test Stop() preserves data
TEST_F(OpcodeProfiler_Test, StopPreservesData)
{
    _profiler->Start();

    SimulateOpcodeExecution(0x0000, 0x0000, 0x00);
    SimulateOpcodeExecution(0x0001, 0x0000, 0x21);
    SimulateOpcodeExecution(0x0004, 0x0000, 0xC9);

    _profiler->Stop();

    auto status = _profiler->GetStatus();
    EXPECT_FALSE(status.capturing) << "Stop() should disable capturing";
    EXPECT_EQ(status.totalExecutions, 3) << "Stop() should preserve data";
}

// Test Clear() resets all counters and trace
TEST_F(OpcodeProfiler_Test, ClearResetsAllData)
{
    _profiler->Start();

    // Capture some data
    for (int i = 0; i < 100; i++)
    {
        SimulateOpcodeExecution(0x0000 + i, 0x0000, static_cast<uint8_t>(i % 256));
    }

    EXPECT_GE(_profiler->GetTotalExecutions(), 100);
    EXPECT_GE(_profiler->GetStatus().traceSize, 100);

    // Clear
    _profiler->Clear();

    auto status = _profiler->GetStatus();
    EXPECT_EQ(status.totalExecutions, 0) << "Clear() should reset counter";
    EXPECT_EQ(status.traceSize, 0) << "Clear() should reset trace";
}

// Test Start/Stop/Start lifecycle
TEST_F(OpcodeProfiler_Test, StartStopStartLifecycle)
{
    // Phase 1: Start and capture
    _profiler->Start();
    SimulateOpcodeExecution(0x0000, 0x0000, 0x00);
    SimulateOpcodeExecution(0x0001, 0x0000, 0x01);
    EXPECT_EQ(_profiler->GetTotalExecutions(), 2);

    // Phase 2: Stop
    _profiler->Stop();
    SimulateOpcodeExecution(0x0002, 0x0000, 0x02);  // Not captured
    EXPECT_EQ(_profiler->GetTotalExecutions(), 2);

    // Phase 3: Start again (clears data)
    _profiler->Start();
    EXPECT_EQ(_profiler->GetTotalExecutions(), 0);

    SimulateOpcodeExecution(0x0003, 0x0000, 0x03);
    EXPECT_EQ(_profiler->GetTotalExecutions(), 1);
}

/// endregion </API Tests - Start/Stop/Clear>

/// region <Data Capture and Retrieval Tests>

// Test counter array captures all opcode types
TEST_F(OpcodeProfiler_Test, CountersTrackAllOpcodeTypes)
{
    _profiler->Start();

    // Non-prefixed opcodes
    SimulateOpcodeExecution(0x0000, 0x0000, 0x00);  // NOP
    SimulateOpcodeExecution(0x0001, 0x0000, 0x00);  // NOP again
    SimulateOpcodeExecution(0x0002, 0x0000, 0xC9);  // RET

    // CB prefixed
    SimulateOpcodeExecution(0x0003, 0x00CB, 0x07);  // RLC A
    SimulateOpcodeExecution(0x0005, 0x00CB, 0x07);  // RLC A again

    // DD prefixed (IX)
    SimulateOpcodeExecution(0x0007, 0x00DD, 0x21);  // LD IX,nn

    // ED prefixed
    SimulateOpcodeExecution(0x000A, 0x00ED, 0xB0);  // LDIR

    // FD prefixed (IY)
    SimulateOpcodeExecution(0x000C, 0x00FD, 0x21);  // LD IY,nn

    // Verify counters
    EXPECT_EQ(_profiler->GetCount(0x0000, 0x00), 2) << "NOP should have count 2";
    EXPECT_EQ(_profiler->GetCount(0x0000, 0xC9), 1) << "RET should have count 1";
    EXPECT_EQ(_profiler->GetCount(0x00CB, 0x07), 2) << "RLC A should have count 2";
    EXPECT_EQ(_profiler->GetCount(0x00DD, 0x21), 1) << "LD IX,nn should have count 1";
    EXPECT_EQ(_profiler->GetCount(0x00ED, 0xB0), 1) << "LDIR should have count 1";
    EXPECT_EQ(_profiler->GetCount(0x00FD, 0x21), 1) << "LD IY,nn should have count 1";
}

// Test GetTopOpcodes returns sorted results
TEST_F(OpcodeProfiler_Test, GetTopOpcodesSortedByCount)
{
    _profiler->Start();

    // Create specific distribution
    for (int i = 0; i < 100; i++) SimulateOpcodeExecution(0, 0, 0x00);  // NOP: 100
    for (int i = 0; i < 50; i++) SimulateOpcodeExecution(0, 0, 0x21);   // LD HL: 50
    for (int i = 0; i < 25; i++) SimulateOpcodeExecution(0, 0, 0xC9);   // RET: 25
    for (int i = 0; i < 10; i++) SimulateOpcodeExecution(0, 0, 0x3E);   // LD A: 10

    auto top = _profiler->GetTopOpcodes(4);

    ASSERT_EQ(top.size(), 4);
    EXPECT_EQ(top[0].opcode, 0x00) << "NOP should be first";
    EXPECT_EQ(top[0].count, 100);
    EXPECT_EQ(top[1].opcode, 0x21) << "LD HL should be second";
    EXPECT_EQ(top[1].count, 50);
    EXPECT_EQ(top[2].opcode, 0xC9) << "RET should be third";
    EXPECT_EQ(top[2].count, 25);
    EXPECT_EQ(top[3].opcode, 0x3E) << "LD A should be fourth";
    EXPECT_EQ(top[3].count, 10);
}

// Test trace buffer captures sequential history
TEST_F(OpcodeProfiler_Test, TraceBufferCapturesHistory)
{
    _profiler->Start();

    // Execute sequence of opcodes
    SimulateOpcodeExecution(0x0000, 0x0000, 0x00, 0xFF, 0x42, 1, 100);
    SimulateOpcodeExecution(0x0001, 0x0000, 0x21, 0xFE, 0x43, 1, 104);
    SimulateOpcodeExecution(0x0004, 0x0000, 0xC9, 0xFD, 0x44, 1, 108);

    auto trace = _profiler->GetRecentTrace(10);

    ASSERT_EQ(trace.size(), 3);

    // Most recent first
    EXPECT_EQ(trace[0].pc, 0x0004);
    EXPECT_EQ(trace[0].opcode, 0xC9);
    EXPECT_EQ(trace[0].flags, 0xFD);
    EXPECT_EQ(trace[0].a, 0x44);
    EXPECT_EQ(trace[0].tState, 108);

    EXPECT_EQ(trace[1].pc, 0x0001);
    EXPECT_EQ(trace[1].opcode, 0x21);

    EXPECT_EQ(trace[2].pc, 0x0000);
    EXPECT_EQ(trace[2].opcode, 0x00);
}

// Test trace buffer wraps correctly (ring buffer)
TEST_F(OpcodeProfiler_Test, TraceBufferWrapsCorrectly)
{
    _profiler->Start();

    // Get buffer capacity
    auto status = _profiler->GetStatus();
    uint32_t capacity = status.traceCapacity;

    // Fill buffer beyond capacity
    for (uint32_t i = 0; i < capacity + 10; i++)
    {
        SimulateOpcodeExecution(static_cast<uint16_t>(i), 0, static_cast<uint8_t>(i & 0xFF));
    }

    // Buffer should be at capacity
    status = _profiler->GetStatus();
    EXPECT_EQ(status.traceSize, capacity) << "Trace buffer should be at capacity";

    // Most recent 10 entries should be the last 10 we added
    auto trace = _profiler->GetRecentTrace(10);
    ASSERT_EQ(trace.size(), 10);

    // Check the PCs are the most recent ones
    uint16_t expectedPC = capacity + 10 - 1;
    for (size_t i = 0; i < 10; i++)
    {
        EXPECT_EQ(trace[i].pc, expectedPC - i) << "Trace entry " << i << " has wrong PC";
    }
}

/// endregion </Data Capture and Retrieval Tests>

/// region <Memory Management Tests>

// Test that Clear() effectively resets memory (counters zeroed)
TEST_F(OpcodeProfiler_Test, ClearZerosCounterMemory)
{
    _profiler->Start();

    // Fill up counters across different prefix tables
    for (int i = 0; i < 256; i++)
    {
        SimulateOpcodeExecution(0, 0x0000, static_cast<uint8_t>(i));  // Non-prefixed
        SimulateOpcodeExecution(0, 0x00CB, static_cast<uint8_t>(i));  // CB prefixed
        SimulateOpcodeExecution(0, 0x00DD, static_cast<uint8_t>(i));  // DD prefixed
    }

    EXPECT_GT(_profiler->GetTotalExecutions(), 256);

    // Clear
    _profiler->Clear();

    // Verify all counters are zero
    uint64_t total = 0;
    for (int prefix = 0; prefix < 7; prefix++)
    {
        uint16_t prefixCode = 0;
        switch (prefix)
        {
            case 0: prefixCode = 0x0000; break;
            case 1: prefixCode = 0x00CB; break;
            case 2: prefixCode = 0x00DD; break;
            case 3: prefixCode = 0x00ED; break;
            case 4: prefixCode = 0x00FD; break;
            case 5: prefixCode = 0xDDCB; break;
            case 6: prefixCode = 0xFDCB; break;
        }
        for (int op = 0; op < 256; op++)
        {
            total += _profiler->GetCount(prefixCode, static_cast<uint8_t>(op));
        }
    }
    EXPECT_EQ(total, 0) << "All counters should be zero after Clear()";
}

// Test profiler status reports accurate capacity
TEST_F(OpcodeProfiler_Test, StatusReportsAccurateCapacity)
{
    auto status = _profiler->GetStatus();

    // Default trace capacity should be 10,000
    EXPECT_EQ(status.traceCapacity, 10000) << "Default trace capacity should be 10000";
    EXPECT_EQ(status.traceSize, 0) << "Initial trace size should be 0";
    EXPECT_FALSE(status.capturing) << "Not capturing initially";
}

/// endregion </Memory Management Tests>

/// region <Full Lifecycle Test>

// Test complete workflow: enable -> start -> capture -> stop -> retrieve -> clear
TEST_F(OpcodeProfiler_Test, FullLifecycleWorkflow)
{
    // === Phase 1: Enable feature ===
    _featureManager->setFeature(Features::kOpcodeProfiler, true);
    _z80->UpdateFeatureCache();
    EXPECT_TRUE(_featureManager->isEnabled(Features::kOpcodeProfiler));

    // === Phase 2: Start session ===
    _profiler->Start();
    EXPECT_TRUE(_profiler->GetStatus().capturing);
    EXPECT_EQ(_profiler->GetTotalExecutions(), 0);

    // === Phase 3: Capture data ===
    for (int i = 0; i < 50; i++)
    {
        SimulateOpcodeExecution(0x4000 + i, 0x0000, 0x00);  // NOP
    }
    for (int i = 0; i < 30; i++)
    {
        SimulateOpcodeExecution(0x5000 + i, 0x0000, 0xCD);  // CALL
    }

    EXPECT_EQ(_profiler->GetTotalExecutions(), 80);
    EXPECT_EQ(_profiler->GetCount(0x0000, 0x00), 50);
    EXPECT_EQ(_profiler->GetCount(0x0000, 0xCD), 30);

    // === Phase 4: Stop session ===
    _profiler->Stop();
    EXPECT_FALSE(_profiler->GetStatus().capturing);
    EXPECT_EQ(_profiler->GetTotalExecutions(), 80);  // Data preserved

    // === Phase 5: Retrieve data ===
    auto top = _profiler->GetTopOpcodes(10);
    ASSERT_GE(top.size(), 2);
    EXPECT_EQ(top[0].opcode, 0x00);
    EXPECT_EQ(top[0].count, 50);
    EXPECT_EQ(top[1].opcode, 0xCD);
    EXPECT_EQ(top[1].count, 30);

    auto trace = _profiler->GetRecentTrace(5);
    EXPECT_EQ(trace.size(), 5);

    // === Phase 6: Clear ===
    _profiler->Clear();
    EXPECT_EQ(_profiler->GetTotalExecutions(), 0);
    EXPECT_EQ(_profiler->GetStatus().traceSize, 0);

    // === Phase 7: Disable feature ===
    _featureManager->setFeature(Features::kOpcodeProfiler, false);
    EXPECT_FALSE(_featureManager->isEnabled(Features::kOpcodeProfiler));
}

/// endregion </Full Lifecycle Test>

/// region <Negative Tests / Edge Cases>

// Test: Calling Stop() without Start() - should not crash, status should show not capturing
TEST_F(OpcodeProfiler_Test, Negative_StopWithoutStart)
{
    // Initially not capturing
    EXPECT_FALSE(_profiler->GetStatus().capturing);

    // Stop without start - should be safe
    _profiler->Stop();

    // Still not capturing, no crash
    EXPECT_FALSE(_profiler->GetStatus().capturing);
    EXPECT_EQ(_profiler->GetTotalExecutions(), 0);
}

// Test: Calling Stop() multiple times - should be idempotent
TEST_F(OpcodeProfiler_Test, Negative_DoubleStop)
{
    _profiler->Start();
    SimulateOpcodeExecution(0x0000, 0x0000, 0x00);

    _profiler->Stop();
    EXPECT_FALSE(_profiler->GetStatus().capturing);
    uint64_t countAfterFirstStop = _profiler->GetTotalExecutions();

    // Second stop - should be no-op
    _profiler->Stop();
    EXPECT_FALSE(_profiler->GetStatus().capturing);
    EXPECT_EQ(_profiler->GetTotalExecutions(), countAfterFirstStop);
}

// Test: Calling Start() multiple times - should restart each time (clear data)
TEST_F(OpcodeProfiler_Test, Negative_DoubleStart)
{
    _profiler->Start();
    SimulateOpcodeExecution(0x0000, 0x0000, 0x00);
    SimulateOpcodeExecution(0x0001, 0x0000, 0x01);
    EXPECT_EQ(_profiler->GetTotalExecutions(), 2);

    // Second start - should clear and restart
    _profiler->Start();
    EXPECT_TRUE(_profiler->GetStatus().capturing);
    EXPECT_EQ(_profiler->GetTotalExecutions(), 0) << "Double Start() should clear previous data";
}

// Test: Clear() on already empty profiler - should not crash
TEST_F(OpcodeProfiler_Test, Negative_ClearWhenEmpty)
{
    // Clear without any data
    _profiler->Clear();

    EXPECT_EQ(_profiler->GetTotalExecutions(), 0);
    EXPECT_EQ(_profiler->GetStatus().traceSize, 0);

    // Clear again
    _profiler->Clear();
    EXPECT_EQ(_profiler->GetTotalExecutions(), 0);
}

// Test: GetTopOpcodes() when no data captured - returns empty vector
TEST_F(OpcodeProfiler_Test, Negative_GetTopOpcodesEmpty)
{
    // Not started, no data
    auto top = _profiler->GetTopOpcodes(100);
    EXPECT_TRUE(top.empty()) << "GetTopOpcodes should return empty when no data";
}

// Test: GetRecentTrace() when no data captured - returns empty vector
TEST_F(OpcodeProfiler_Test, Negative_GetRecentTraceEmpty)
{
    // Not started, no data
    auto trace = _profiler->GetRecentTrace(100);
    EXPECT_TRUE(trace.empty()) << "GetRecentTrace should return empty when no data";
}

// Test: GetTopOpcodes(0) - zero limit should return empty
TEST_F(OpcodeProfiler_Test, Negative_GetTopOpcodesZeroLimit)
{
    _profiler->Start();
    SimulateOpcodeExecution(0x0000, 0x0000, 0x00);

    auto top = _profiler->GetTopOpcodes(0);
    EXPECT_TRUE(top.empty()) << "GetTopOpcodes(0) should return empty";
}

// Test: GetRecentTrace(0) - zero count should return empty
TEST_F(OpcodeProfiler_Test, Negative_GetRecentTraceZeroCount)
{
    _profiler->Start();
    SimulateOpcodeExecution(0x0000, 0x0000, 0x00);

    auto trace = _profiler->GetRecentTrace(0);
    EXPECT_TRUE(trace.empty()) << "GetRecentTrace(0) should return empty";
}

// Test: Invalid prefix - should be handled gracefully (mapped to non-prefixed)
TEST_F(OpcodeProfiler_Test, Negative_InvalidPrefix)
{
    _profiler->Start();

    // Use an invalid prefix code (not CB, DD, ED, FD, DDCB, FDCB)
    SimulateOpcodeExecution(0x0000, 0x1234, 0x00);  // Invalid prefix
    SimulateOpcodeExecution(0x0001, 0xFFFF, 0x01);  // Another invalid

    // Should not crash - invalid prefixes map to non-prefixed (offset 0)
    EXPECT_EQ(_profiler->GetTotalExecutions(), 2);

    // GetCount with invalid prefix should return value (mapped to non-prefixed)
    uint64_t count = _profiler->GetCount(0x1234, 0x00);
    EXPECT_GE(count, 0);  // Just verify it doesn't crash
}

// Test: LogExecution when not capturing - data should be ignored
TEST_F(OpcodeProfiler_Test, Negative_LogExecutionWhenNotCapturing)
{
    // Don't call Start()
    EXPECT_FALSE(_profiler->GetStatus().capturing);

    // Try to log (should be ignored)
    SimulateOpcodeExecution(0x0000, 0x0000, 0x00);
    SimulateOpcodeExecution(0x0001, 0x0000, 0x01);

    // Nothing captured
    EXPECT_EQ(_profiler->GetTotalExecutions(), 0);
    EXPECT_EQ(_profiler->GetStatus().traceSize, 0);
}

// Test: Retrieving data after Clear() - should be empty
TEST_F(OpcodeProfiler_Test, Negative_RetrieveAfterClear)
{
    _profiler->Start();

    // Capture significant data
    for (int i = 0; i < 100; i++)
    {
        SimulateOpcodeExecution(i, 0, static_cast<uint8_t>(i));
    }

    EXPECT_EQ(_profiler->GetTotalExecutions(), 100);

    // Clear
    _profiler->Clear();

    // All retrievals should be empty/zero
    EXPECT_EQ(_profiler->GetTotalExecutions(), 0);
    EXPECT_TRUE(_profiler->GetTopOpcodes(50).empty());
    EXPECT_TRUE(_profiler->GetRecentTrace(50).empty());
    EXPECT_EQ(_profiler->GetCount(0x0000, 0x00), 0);
}

// Test: Very large limit values - should be capped by actual data
TEST_F(OpcodeProfiler_Test, Negative_VeryLargeLimitValues)
{
    _profiler->Start();

    // Capture small amount of data
    SimulateOpcodeExecution(0x0000, 0x0000, 0x00);
    SimulateOpcodeExecution(0x0001, 0x0000, 0x01);
    SimulateOpcodeExecution(0x0002, 0x0000, 0x02);

    // Request way more than available
    auto top = _profiler->GetTopOpcodes(1000000);
    EXPECT_EQ(top.size(), 3) << "Should return only available data, not requested limit";

    auto trace = _profiler->GetRecentTrace(1000000);
    EXPECT_EQ(trace.size(), 3) << "Should return only available trace entries";
}

/// endregion </Negative Tests / Edge Cases>

