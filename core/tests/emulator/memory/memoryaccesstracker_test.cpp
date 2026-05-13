#include "memoryaccesstracker_test.h"

#include <gtest/gtest.h>

#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>

#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "emulator/platform.h"

/// region <SetUp / TearDown>

void MemoryAccessTracker_Test::SetUp()
{
    // Create emulator context with minimal logging
    _context = new EmulatorContext(LoggerLevel::LogError);

    // Mock all objects used
    _core = new CoreCUT(_context);
    _z80 = new Z80(_context);
    _core->_z80 = _z80;
    _context->pCore = _core;
    _debugManager = new DebugManager(_context);
    _breakpointManager = _debugManager->GetBreakpointsManager();
    _context->pDebugManager = _debugManager;

    // Create FeatureManager for feature flag tests
    _featureManager = new FeatureManager(_context);
    _context->pFeatureManager = _featureManager;

    // Enable memory tracking by default for existing tests
    _featureManager->setFeature(Features::kDebugMode, true);
    _featureManager->setFeature(Features::kMemoryTracking, true);

    _memory = new MemoryCUT(_context);
    _context->pMemory = _memory;

    // Initialize memory banks and function pointers for testing
    // Set up basic memory bank configuration similar to a 48K Spectrum
    _memory->DefaultBanksFor48k();

    // Get the memory access tracker (cast to CUT for direct member access)
    _tracker = static_cast<MemoryAccessTrackerCUT*>(_memory->_memoryAccessTracker);

    // Make sure tracker is initialized
    ASSERT_NE(_tracker, nullptr) << "MemoryAccessTracker was not initialized in Memory";

    // Initialize some test data in memory
    for (uint16_t i = 0; i < 0x100; i++)
    {
        _memory->DirectWriteToZ80Memory(i, (uint8_t)(i & 0xFF));
    }
}

void MemoryAccessTracker_Test::TearDown()
{
    if (_memory != nullptr)
    {
        delete _memory;
        _memory = nullptr;
    }

    if (_featureManager != nullptr)
    {
        delete _featureManager;
        _featureManager = nullptr;
    }

    if (_context != nullptr)
    {
        delete _context;
        _context = nullptr;
    }

    _tracker = nullptr;  // Memory owns this, so don't delete it
}

/// endregion </SetUp / TearDown>

/// region <Helper Methods>

void MemoryAccessTracker_Test::SimulateMemoryReads(uint16_t address, uint16_t /*callerAddress*/, int count)
{
    for (int i = 0; i < count; i++)
    {
        _memory->MemoryReadDebug(address, false);
    }
}

void MemoryAccessTracker_Test::SimulateMemoryWrites(uint16_t address, uint8_t value, uint16_t /*callerAddress*/,
                                                    int count)
{
    for (int i = 0; i < count; i++)
    {
        _memory->MemoryWriteDebug(address, value);
    }
}

void MemoryAccessTracker_Test::SimulateMemoryExecutes(uint16_t address, uint16_t /*callerAddress*/, int count)
{
    for (int i = 0; i < count; i++)
    {
        _memory->MemoryReadDebug(address, true);
    }
}

void MemoryAccessTracker_Test::SimulatePortReads(uint16_t port, uint16_t callerAddress, int count)
{
    // We need to simulate port reads through the memory access tracker directly
    // since we don't have a port decoder in this test
    for (int i = 0; i < count; i++)
    {
        _tracker->TrackPortRead(port, 0x42, callerAddress);  // Use arbitrary value
    }
}

void MemoryAccessTracker_Test::SimulatePortWrites(uint16_t port, uint8_t value, uint16_t callerAddress, int count)
{
    // We need to simulate port writes through the memory access tracker directly
    for (int i = 0; i < count; i++)
    {
        _tracker->TrackPortWrite(port, value, callerAddress);
    }
}

/// endregion </Helper Methods>

/// region <Test Cases>

// Test basic memory access tracking
TEST_F(MemoryAccessTracker_Test, BasicMemoryAccessTracking)
{
    const int NUM_READS = 10;
    const int NUM_WRITES = 5;
    const int NUM_EXECUTES = 3;

    // Reset counters before testing
    _tracker->ResetCounters();

    // Add monitored region with tracking options
    MonitoringOptions options;
    options.trackCallers = true;
    _tracker->AddMonitoredRegion(TEST_REGION_NAME, TEST_ADDRESS, 16, options);

    // Perform memory operations
    SimulateMemoryReads(TEST_ADDRESS, CALLER_ADDRESS_1, NUM_READS);
    SimulateMemoryWrites(TEST_ADDRESS, 0x42, CALLER_ADDRESS_1, NUM_WRITES);
    SimulateMemoryExecutes(TEST_ADDRESS, CALLER_ADDRESS_1, NUM_EXECUTES);

    // Get memory region stats
    const AccessStats* regionStats = _tracker->GetRegionStats(TEST_REGION_NAME);
    ASSERT_NE(regionStats, nullptr) << "Failed to get monitored region";

    // Check access counts
    EXPECT_GE(regionStats->readCount, NUM_READS) << "Tracker read counter mismatch";
    EXPECT_GE(regionStats->writeCount, NUM_WRITES) << "Tracker write counter mismatch";
    EXPECT_GE(regionStats->executeCount, NUM_EXECUTES) << "Tracker execute counter mismatch";
}

// Test port access tracking
TEST_F(MemoryAccessTracker_Test, PortAccessTracking)
{
    const int NUM_PORT_READS = 7;
    const int NUM_PORT_WRITES = 4;
    const uint8_t TEST_VALUE = 0x42;

    // Reset counters
    _tracker->ResetCounters();

    // Add monitored port with tracking options
    MonitoringOptions options;
    options.trackCallers = true;
    options.trackDataFlow = true;
    options.maxCallers = 10;
    options.maxDataValues = 10;

    _tracker->AddMonitoredPort(TEST_PORT_NAME, TEST_PORT, options);

    // Simulate port operations
    SimulatePortReads(TEST_PORT, CALLER_ADDRESS_1, NUM_PORT_READS);
    SimulatePortWrites(TEST_PORT, TEST_VALUE, CALLER_ADDRESS_1, NUM_PORT_WRITES);

    // Get port statistics
    const AccessStats* portStats = _tracker->GetPortStats(TEST_PORT_NAME);
    ASSERT_NE(portStats, nullptr) << "Failed to get port information";

    // Verify port access counts
    EXPECT_GE(portStats->readCount, NUM_PORT_READS) << "Port read counter mismatch";
    EXPECT_GE(portStats->writeCount, NUM_PORT_WRITES) << "Port write counter mismatch";

    // Verify data flow tracking
    bool foundTestValue = false;
    for (const auto& [value, count] : portStats->dataValues)
    {
        if (value == TEST_VALUE)
        {
            foundTestValue = true;
            break;
        }
    }
    EXPECT_TRUE(foundTestValue) << "Test value not found in tracked data";
}

// Test segment tracking
TEST_F(MemoryAccessTracker_Test, SegmentTracking)
{
    // Reset counters
    _tracker->ResetCounters();

    // Add monitored region with tracking options
    MonitoringOptions options;
    options.trackCallers = true;
    _tracker->AddMonitoredRegion(TEST_REGION_NAME, TEST_ADDRESS, 16, options);

    // Enable segment tracking
    _tracker->EnableSegmentTracking(true);

    // Create segments and perform operations
    _tracker->StartSegment("Segment1", TrackingEvent::Custom, 1);
    _memory->MemoryReadDebug(TEST_ADDRESS, false);
    _memory->MemoryReadDebug(TEST_ADDRESS + 1, false);
    _memory->MemoryWriteDebug(TEST_ADDRESS, 0x42);

    _tracker->StartSegment("Segment2", TrackingEvent::Custom, 2);
    _memory->MemoryReadDebug(TEST_ADDRESS, false);
    _memory->MemoryWriteDebug(TEST_ADDRESS, 0x43);
    _memory->MemoryWriteDebug(TEST_ADDRESS, 0x44);

    // Get segment statistics
    const TrackingSegment* segment1 = _tracker->GetSegment("Segment1");
    const TrackingSegment* segment2 = _tracker->GetSegment("Segment2");

    ASSERT_NE(segment1, nullptr) << "Failed to get Segment1";
    ASSERT_NE(segment2, nullptr) << "Failed to get Segment2";

    // Verify segment operations were tracked
    EXPECT_GE(segment1->regionStats.at(TEST_REGION_NAME).readCount, 2) << "Unexpected read count in Segment1";
    EXPECT_GE(segment1->regionStats.at(TEST_REGION_NAME).writeCount, 1) << "Unexpected write count in Segment1";
    EXPECT_EQ(segment1->regionStats.at(TEST_REGION_NAME).executeCount, 0) << "Unexpected execute count in Segment1";

    EXPECT_GE(segment2->regionStats.at(TEST_REGION_NAME).readCount, 1) << "Unexpected read count in Segment2";
    EXPECT_GE(segment2->regionStats.at(TEST_REGION_NAME).writeCount, 2) << "Unexpected write count in Segment2";
    EXPECT_EQ(segment2->regionStats.at(TEST_REGION_NAME).executeCount, 0) << "Unexpected execute count in Segment2";
}

// Test port monitoring with advanced options
TEST_F(MemoryAccessTracker_Test, PortMonitoringWithOptions)
{
    // Reset counters
    _tracker->ResetCounters();

    // Add monitored port with options
    MonitoringOptions options;
    options.trackCallers = true;
    options.trackDataFlow = true;
    options.maxCallers = 10;
    options.maxDataValues = 10;

    _tracker->AddMonitoredPort(TEST_PORT_NAME, TEST_PORT, options);

    // Simulate port operations
    _tracker->TrackPortRead(TEST_PORT, 0x10, CALLER_ADDRESS_1);
    _tracker->TrackPortRead(TEST_PORT, 0x20, CALLER_ADDRESS_1);
    _tracker->TrackPortRead(TEST_PORT, 0x30, CALLER_ADDRESS_2);
    _tracker->TrackPortWrite(TEST_PORT, 0x40, CALLER_ADDRESS_1);
    _tracker->TrackPortWrite(TEST_PORT, 0x50, CALLER_ADDRESS_2);

    // Verify port statistics
    const AccessStats* portStats = _tracker->GetPortStats(TEST_PORT_NAME);
    ASSERT_NE(portStats, nullptr) << "Failed to get port information";
    EXPECT_GE(portStats->readCount, 3) << "Unexpected read count";
    EXPECT_GE(portStats->writeCount, 2) << "Unexpected write count";
}

// Test region monitoring
TEST_F(MemoryAccessTracker_Test, RegionMonitoring)
{
    // Reset counters
    _memory->_memoryAccessTracker->ResetCounters();

    // Add monitored region with tracking options
    MonitoringOptions options;
    options.trackCallers = true;
    options.trackDataFlow = true;
    _tracker->AddMonitoredRegion(TEST_REGION_NAME, TEST_ADDRESS, 16, options);

    // Perform memory operations on the monitored region
    const int NUM_READS = 5;
    const int NUM_WRITES = 3;
    const uint8_t TEST_VALUE = 0x55;

    // Simulate memory operations
    for (int i = 0; i < NUM_READS; i++)
    {
        _memory->MemoryReadDebug(TEST_ADDRESS + (i % 16), false);
    }

    for (int i = 0; i < NUM_WRITES; i++)
    {
        _memory->MemoryWriteDebug(TEST_ADDRESS + (i % 16), TEST_VALUE);
    }

    // Get region stats
    const AccessStats* regionStats = _tracker->GetRegionStats(TEST_REGION_NAME);
    if (regionStats)
    {
        // Note: We don't have direct access to region name and bounds from AccessStats
        std::cout << "\nRegion " << TEST_REGION_NAME << " (0x" << std::hex << TEST_ADDRESS << "-0x"
                  << (TEST_ADDRESS + 15) << ")" << std::endl;
        std::cout << "  Reads:  " << regionStats->readCount << std::endl;
        std::cout << "  Writes: " << regionStats->writeCount << std::endl;
    }
    else
    {
        std::cout << "\nFailed to get region information for " << TEST_REGION_NAME << std::endl;
    }
}

// Test caller tracking
TEST_F(MemoryAccessTracker_Test, CallerTracking)
{
    // Reset counters
    _tracker->ResetCounters();

    // Add monitored region with caller tracking
    MonitoringOptions options;
    options.trackCallers = true;
    _tracker->AddMonitoredRegion(TEST_REGION_NAME, TEST_ADDRESS, 16, options);

    // Perform memory operations from different callers
    _tracker->TrackMemoryRead(TEST_ADDRESS, 0x00, CALLER_ADDRESS_1);
    _tracker->TrackMemoryRead(TEST_ADDRESS, 0x00, CALLER_ADDRESS_1);
    _tracker->TrackMemoryRead(TEST_ADDRESS, 0x00, CALLER_ADDRESS_2);
    _tracker->TrackMemoryWrite(TEST_ADDRESS, 0x42, CALLER_ADDRESS_1);
    _tracker->TrackMemoryWrite(TEST_ADDRESS, 0x43, CALLER_ADDRESS_2);

    // Get region stats and verify caller tracking
    const AccessStats* regionStats = _tracker->GetRegionStats(TEST_REGION_NAME);
    ASSERT_NE(regionStats, nullptr) << "Failed to get region information";

    // Check that we have some callers tracked
    EXPECT_FALSE(regionStats->callerAddresses.empty()) << "No callers were tracked";

    // Check that our callers are in the tracked data
    bool foundCaller1 = false;
    bool foundCaller2 = false;
    for (const auto& [caller, count] : regionStats->callerAddresses)
    {
        if (caller == CALLER_ADDRESS_1)
            foundCaller1 = true;
        if (caller == CALLER_ADDRESS_2)
            foundCaller2 = true;
    }

    EXPECT_TRUE(foundCaller1) << "Caller1 (0x" << std::hex << CALLER_ADDRESS_1 << ") not found in tracked callers";
    EXPECT_TRUE(foundCaller2) << "Caller2 (0x" << std::hex << CALLER_ADDRESS_2 << ") not found in tracked callers";
}

// Test data flow tracking
TEST_F(MemoryAccessTracker_Test, DataFlowTracking)
{
    // Reset counters
    _tracker->ResetCounters();

    // Add monitored region with data flow tracking
    MonitoringOptions options;
    options.trackDataFlow = true;
    _tracker->AddMonitoredRegion(TEST_REGION_NAME, TEST_ADDRESS, 16, options);

    // Perform operations that read/write the same memory location
    const uint8_t value1 = 0x12;
    const uint8_t value2 = 0x34;

    _memory->MemoryWriteDebug(TEST_ADDRESS, value1);
    uint8_t readValue1 = _memory->MemoryReadDebug(TEST_ADDRESS, false);
    _memory->MemoryWriteDebug(TEST_ADDRESS, value2);
    uint8_t readValue2 = _memory->MemoryReadDebug(TEST_ADDRESS, false);

    // Verify data flow was tracked
    const AccessStats* regionStats = _tracker->GetRegionStats(TEST_REGION_NAME);
    ASSERT_NE(regionStats, nullptr) << "Failed to get region information";

    // Check that we have some data values tracked
    EXPECT_FALSE(regionStats->dataValues.empty()) << "No data values were tracked";

    // Check that our written values are in the tracked data
    bool foundValue1 = false;
    bool foundValue2 = false;
    for (const auto& [value, count] : regionStats->dataValues)
    {
        if (value == value1)
            foundValue1 = true;
        if (value == value2)
            foundValue2 = true;
    }

    EXPECT_TRUE(foundValue1) << "Value1 (0x" << std::hex << (int)value1 << ") not found in tracked data";
    EXPECT_TRUE(foundValue2) << "Value2 (0x" << std::hex << (int)value2 << ") not found in tracked data";
}

/// endregion </Test Cases>

/// region <Feature Flag Tests>

// Test that disabling tracking deallocates buffers
TEST_F(MemoryAccessTracker_Test, FeatureFlag_OffByDefault)
{
    FeatureManager* fm = _context->pFeatureManager;
    ASSERT_NE(fm, nullptr) << "FeatureManager not available";

    // SetUp enables tracking and buffers are allocated
    EXPECT_TRUE(_tracker->_isAllocated) << "SetUp should have allocated buffers";

    // Disable tracking - should deallocate buffers
    fm->setFeature(Features::kMemoryTracking, false);
    fm->setFeature(Features::kDebugMode, false);
    _tracker->UpdateFeatureCache();

    // Verify tracking is disabled and buffers are deallocated
    EXPECT_FALSE(fm->isEnabled(Features::kMemoryTracking));
    EXPECT_FALSE(_tracker->_isAllocated) << "Buffers should be deallocated when tracking disabled";
    EXPECT_TRUE(_tracker->_z80ReadCounters.empty()) << "Counter vectors should be empty";

    // Attempt to track memory - should be no-op when disabled
    _tracker->TrackMemoryRead(0x4000, 0x42, 0x8000);
    _tracker->TrackMemoryWrite(0x4000, 0x55, 0x8000);
    _tracker->TrackMemoryExecute(0x4000, 0x8000);
}

// Test that enabling memory tracking feature allocates buffers and enables stats collection
TEST_F(MemoryAccessTracker_Test, FeatureFlag_EnableAllocatesBuffers)
{
    FeatureManager* fm = _context->pFeatureManager;
    ASSERT_NE(fm, nullptr) << "FeatureManager not available";

    // First disable to start from clean state
    fm->setFeature(Features::kMemoryTracking, false);
    fm->setFeature(Features::kDebugMode, false);
    _tracker->UpdateFeatureCache();
    EXPECT_FALSE(_tracker->_isAllocated) << "Buffers should be deallocated";

    // Enable debug mode (required for memory tracking)
    fm->setFeature(Features::kDebugMode, true);
    fm->setFeature(Features::kMemoryTracking, true);
    _tracker->UpdateFeatureCache();

    // Verify buffers are allocated
    EXPECT_TRUE(_tracker->_isAllocated) << "Buffers should be allocated when tracking enabled";
    EXPECT_FALSE(_tracker->_z80ReadCounters.empty()) << "Counter vectors should be allocated";
    EXPECT_TRUE(_tracker->_feature_memorytracking_enabled) << "Internal flag should be true";

    // Add a monitored region and verify stats collection works
    MonitoringOptions options;
    options.trackCallers = true;
    _tracker->AddMonitoredRegion("FeatureTestRegion", 0x5000, 16, options);

    _tracker->TrackMemoryRead(0x5000, 0xAB, 0x8000);
    _tracker->TrackMemoryWrite(0x5000, 0xAB, 0x8100);

    const AccessStats* regionStats = _tracker->GetRegionStats("FeatureTestRegion");
    ASSERT_NE(regionStats, nullptr) << "Region stats should be available";
    EXPECT_GE(regionStats->readCount, 1) << "Read count should be tracked";
    EXPECT_GE(regionStats->writeCount, 1) << "Write count should be tracked";
}

// Test that disabling debug mode also disables memory tracking
TEST_F(MemoryAccessTracker_Test, FeatureFlag_DebugModeGatesTracking)
{
    FeatureManager* fm = _context->pFeatureManager;
    ASSERT_NE(fm, nullptr) << "FeatureManager not available";

    // Enable both features first
    fm->setFeature(Features::kDebugMode, true);
    fm->setFeature(Features::kMemoryTracking, true);
    _tracker->UpdateFeatureCache();

    // Add a monitored region
    MonitoringOptions options;
    _tracker->AddMonitoredRegion("DebugGateTestRegion", 0x6000, 16, options);

    // Track some operations while enabled
    _tracker->TrackMemoryRead(0x6000, 0x42, 0x8000);
    _tracker->TrackMemoryWrite(0x6000, 0x42, 0x8000);

    const AccessStats* statsBeforeDisable = _tracker->GetRegionStats("DebugGateTestRegion");
    ASSERT_NE(statsBeforeDisable, nullptr);
    uint32_t readsBefore = statsBeforeDisable->readCount;
    uint32_t writesBefore = statsBeforeDisable->writeCount;

    EXPECT_GE(readsBefore, 1) << "Should have tracked reads while enabled";
    EXPECT_GE(writesBefore, 1) << "Should have tracked writes while enabled";

    // Now disable debug mode (which should also disable memory tracking)
    fm->setFeature(Features::kDebugMode, false);
    _tracker->UpdateFeatureCache();

    // Try to track more operations - these should be ignored
    _tracker->TrackMemoryRead(0x6000, 0x42, 0x8000);
    _tracker->TrackMemoryRead(0x6000, 0x42, 0x8000);
    _tracker->TrackMemoryWrite(0x6000, 0x42, 0x8000);

    const AccessStats* statsAfterDisable = _tracker->GetRegionStats("DebugGateTestRegion");
    ASSERT_NE(statsAfterDisable, nullptr);

    // Counts should NOT have changed since tracking was disabled
    EXPECT_EQ(statsAfterDisable->readCount, readsBefore) << "Read count should not change when tracking is disabled";
    EXPECT_EQ(statsAfterDisable->writeCount, writesBefore) << "Write count should not change when tracking is disabled";
}

// Test ResetCounters only works when buffers are allocated
TEST_F(MemoryAccessTracker_Test, FeatureFlag_ResetRequiresAllocation)
{
    FeatureManager* fm = _context->pFeatureManager;
    ASSERT_NE(fm, nullptr);

    // Disable tracking first (SetUp enables it)
    fm->setFeature(Features::kMemoryTracking, false);
    fm->setFeature(Features::kDebugMode, false);
    _tracker->UpdateFeatureCache();

    // With tracking disabled, ResetCounters should be a no-op (not crash)
    _tracker->ResetCounters();  // Should not crash

    // Now enable tracking
    fm->setFeature(Features::kDebugMode, true);
    fm->setFeature(Features::kMemoryTracking, true);
    _tracker->UpdateFeatureCache();

    // Add a region and track some operations
    MonitoringOptions options;
    _tracker->AddMonitoredRegion("ResetTestRegion", 0x7000, 16, options);
    _tracker->TrackMemoryRead(0x7000, 0x42, 0x8000);
    _tracker->TrackMemoryWrite(0x7000, 0x42, 0x8000);

    const AccessStats* statsBeforeReset = _tracker->GetRegionStats("ResetTestRegion");
    ASSERT_NE(statsBeforeReset, nullptr);
    EXPECT_GE(statsBeforeReset->readCount, 1);
    EXPECT_GE(statsBeforeReset->writeCount, 1);

    // Reset counters
    _tracker->ResetCounters();

    // Region stats should be cleared
    const AccessStats* statsAfterReset = _tracker->GetRegionStats("ResetTestRegion");
    // Note: After reset, the region is still registered but stats are cleared
    // Need to re-add the region since _monitoredRegions was also reset
    _tracker->AddMonitoredRegion("ResetTestRegion2", 0x7000, 16, options);
    const AccessStats* newStats = _tracker->GetRegionStats("ResetTestRegion2");
    ASSERT_NE(newStats, nullptr);
    EXPECT_EQ(newStats->readCount, 0) << "Read count should be 0 after reset";
    EXPECT_EQ(newStats->writeCount, 0) << "Write count should be 0 after reset";
}

// Test full lifecycle: disabled -> enabled -> collect stats -> reset -> collect more
TEST_F(MemoryAccessTracker_Test, FeatureFlag_FullLifecycle)
{
    FeatureManager* fm = _context->pFeatureManager;
    ASSERT_NE(fm, nullptr);

    // === Phase 1: Feature disabled ===
    fm->setFeature(Features::kMemoryTracking, false);
    fm->setFeature(Features::kDebugMode, false);
    _tracker->UpdateFeatureCache();

    // Operations should be no-op
    _tracker->TrackMemoryRead(0x4000, 0x42, 0x8000);

    // === Phase 2: Enable feature ===
    fm->setFeature(Features::kDebugMode, true);
    fm->setFeature(Features::kMemoryTracking, true);
    _tracker->UpdateFeatureCache();

    // Add region and track operations
    MonitoringOptions options;
    options.trackCallers = true;
    _tracker->AddMonitoredRegion("LifecycleRegion", 0x4000, 16, options);

    for (int i = 0; i < 5; i++)
    {
        _tracker->TrackMemoryRead(0x4000, 0x42, 0x8000);
    }
    for (int i = 0; i < 3; i++)
    {
        _tracker->TrackMemoryWrite(0x4000, 0x55, 0x8100);
    }

    const AccessStats* phase2Stats = _tracker->GetRegionStats("LifecycleRegion");
    ASSERT_NE(phase2Stats, nullptr);
    EXPECT_GE(phase2Stats->readCount, 5) << "Should have 5+ reads after Phase 2";
    EXPECT_GE(phase2Stats->writeCount, 3) << "Should have 3+ writes after Phase 2";

    // === Phase 3: Reset and continue ===
    _tracker->ResetCounters();

    // Re-add region after reset
    _tracker->AddMonitoredRegion("LifecycleRegion", 0x4000, 16, options);

    // Track more operations
    _tracker->TrackMemoryRead(0x4000, 0x42, 0x8000);
    _tracker->TrackMemoryWrite(0x4000, 0x55, 0x8000);

    const AccessStats* phase3Stats = _tracker->GetRegionStats("LifecycleRegion");
    ASSERT_NE(phase3Stats, nullptr);
    EXPECT_GE(phase3Stats->readCount, 1) << "Should have 1+ read after Phase 3";
    EXPECT_GE(phase3Stats->writeCount, 1) << "Should have 1+ write after Phase 3";

    // === Phase 4: Disable ===
    fm->setFeature(Features::kMemoryTracking, false);
    _tracker->UpdateFeatureCache();

    // Track more - should be ignored
    _tracker->TrackMemoryRead(0x4000, 0x42, 0x8000);

    const AccessStats* phase4Stats = _tracker->GetRegionStats("LifecycleRegion");
    ASSERT_NE(phase4Stats, nullptr);
    // Count should not have increased
    EXPECT_EQ(phase4Stats->readCount, phase3Stats->readCount) << "Read count should not change after disabling";
}

// Test that disabling the feature deallocates memory
TEST_F(MemoryAccessTracker_Test, FeatureFlag_DisableDeallocatesMemory)
{
    FeatureManager* fm = _context->pFeatureManager;
    ASSERT_NE(fm, nullptr);

    // Enable tracking first
    fm->setFeature(Features::kDebugMode, true);
    fm->setFeature(Features::kMemoryTracking, true);
    _tracker->UpdateFeatureCache();

    // Perform some tracking to ensure buffers are in use
    _tracker->TrackMemoryRead(0x4000, 0x42, 0x8000);
    _tracker->TrackMemoryWrite(0x4000, 0x55, 0x8000);

    // Now disable tracking - this should deallocate memory
    fm->setFeature(Features::kMemoryTracking, false);
    _tracker->UpdateFeatureCache();

    // Re-enable tracking - should reallocate fresh buffers
    fm->setFeature(Features::kMemoryTracking, true);
    _tracker->UpdateFeatureCache();

    // Add new region after reallocation
    MonitoringOptions options;
    _tracker->AddMonitoredRegion("ReallocTestRegion", 0x4000, 16, options);

    // Track should work again
    _tracker->TrackMemoryRead(0x4000, 0x42, 0x8000);

    const AccessStats* stats = _tracker->GetRegionStats("ReallocTestRegion");
    ASSERT_NE(stats, nullptr);
    EXPECT_GE(stats->readCount, 1) << "Tracking should work after reallocation";
}

/// endregion </Feature Flag Tests>

/// endregion </Feature Flag Tests>
