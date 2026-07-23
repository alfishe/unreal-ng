#include "porttracker_test.h"

#include <gtest/gtest.h>

#include "base/featuremanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/porttracker.h"
#include "pch.h"

/// region <SetUp / TearDown>

void PortTracker_Test::SetUp()
{
    _context = new EmulatorContext(LoggerLevel::LogError);

    // Create FeatureManager for feature flag tests
    _featureManager = new FeatureManager(_context);
    _context->pFeatureManager = _featureManager;

    // Create PortTracker under test
    _tracker = new PortTracker(_context);
    _context->pPortTracker = _tracker;
}

void PortTracker_Test::TearDown()
{
    if (_tracker)
    {
        delete _tracker;
        _tracker = nullptr;
    }

    if (_featureManager)
    {
        delete _featureManager;
        _featureManager = nullptr;
    }

    if (_context)
    {
        _context->pPortTracker = nullptr;
        _context->pFeatureManager = nullptr;
        delete _context;
        _context = nullptr;
    }
}

/// endregion </SetUp / TearDown>

// =============================================================================
// Session Lifecycle Tests
// =============================================================================

TEST_F(PortTracker_Test, InitiallyInactive)
{
    EXPECT_FALSE(_tracker->IsActive());
    EXPECT_EQ(_tracker->GetSessionState(), ProfilerSessionState::Stopped);
}

TEST_F(PortTracker_Test, StartSession)
{
    _tracker->StartSession();
    EXPECT_TRUE(_tracker->IsActive());
    EXPECT_EQ(_tracker->GetSessionState(), ProfilerSessionState::Capturing);
}

TEST_F(PortTracker_Test, StopSession)
{
    _tracker->StartSession();
    _tracker->StopSession();
    EXPECT_FALSE(_tracker->IsActive());
    EXPECT_EQ(_tracker->GetSessionState(), ProfilerSessionState::Stopped);
}

TEST_F(PortTracker_Test, PauseResumeSession)
{
    _tracker->StartSession();
    EXPECT_TRUE(_tracker->IsActive());

    _tracker->PauseSession();
    EXPECT_FALSE(_tracker->IsActive());
    EXPECT_EQ(_tracker->GetSessionState(), ProfilerSessionState::Paused);

    _tracker->ResumeSession();
    EXPECT_TRUE(_tracker->IsActive());
    EXPECT_EQ(_tracker->GetSessionState(), ProfilerSessionState::Capturing);
}

TEST_F(PortTracker_Test, DoubleStartResetsData)
{
    _tracker->StartSession();
    _tracker->TrackWrite(0xFE, 0x07, 0x1234);
    EXPECT_EQ(_tracker->GetWriteCount(0xFE), 1u);

    // Second start resets data (per design: StartSession always calls Reset)
    _tracker->StartSession();
    EXPECT_TRUE(_tracker->IsActive());
    EXPECT_EQ(_tracker->GetWriteCount(0xFE), 0u);
}

TEST_F(PortTracker_Test, StopRetainsData)
{
    _tracker->StartSession();
    _tracker->TrackWrite(0xFE, 0x07, 0x1234);
    EXPECT_EQ(_tracker->GetWriteCount(0xFE), 1u);

    _tracker->StopSession();
    // StopSession retains data (only Reset() clears it)
    EXPECT_EQ(_tracker->GetWriteCount(0xFE), 1u);
    EXPECT_FALSE(_tracker->IsActive());
}

// =============================================================================
// Basic Tracking Tests
// =============================================================================

TEST_F(PortTracker_Test, TrackWriteCountsAccumulate)
{
    _tracker->StartSession();

    _tracker->TrackWrite(0xFE, 0x00, 0x1000);
    _tracker->TrackWrite(0xFE, 0x01, 0x1000);
    _tracker->TrackWrite(0xFE, 0x02, 0x1003);

    EXPECT_EQ(_tracker->GetWriteCount(0xFE), 3u);
    EXPECT_EQ(_tracker->GetReadCount(0xFE), 0u);
}

TEST_F(PortTracker_Test, TrackReadCountsAccumulate)
{
    _tracker->StartSession();

    _tracker->TrackRead(0xFE, 0xFF, 0x2000);
    _tracker->TrackRead(0xFE, 0xFE, 0x2000);

    EXPECT_EQ(_tracker->GetReadCount(0xFE), 2u);
    EXPECT_EQ(_tracker->GetWriteCount(0xFE), 0u);
}

TEST_F(PortTracker_Test, TrackMultiplePorts)
{
    _tracker->StartSession();

    _tracker->TrackWrite(0xFE, 0x00, 0x1000);
    _tracker->TrackWrite(0xFD, 0x01, 0x1000);
    _tracker->TrackRead(0xFE, 0xFF, 0x2000);
    _tracker->TrackRead(0x1F, 0x00, 0x2000);

    EXPECT_EQ(_tracker->GetWriteCount(0xFE), 1u);
    EXPECT_EQ(_tracker->GetWriteCount(0xFD), 1u);
    EXPECT_EQ(_tracker->GetReadCount(0xFE), 1u);
    EXPECT_EQ(_tracker->GetReadCount(0x1F), 1u);
}

TEST_F(PortTracker_Test, UnusedPortReturnsZero)
{
    _tracker->StartSession();

    EXPECT_EQ(_tracker->GetWriteCount(0xAA), 0u);
    EXPECT_EQ(_tracker->GetReadCount(0xAA), 0u);
}

// =============================================================================
// Caller Tracking Tests
// =============================================================================

TEST_F(PortTracker_Test, WriteCallerTracking)
{
    _tracker->StartSession();

    _tracker->TrackWrite(0xFE, 0x00, 0x1000);
    _tracker->TrackWrite(0xFE, 0x01, 0x1000);
    _tracker->TrackWrite(0xFE, 0x02, 0x2000);

    const auto& callers = _tracker->GetWriteCallers(0xFE);
    EXPECT_EQ(callers.size(), 2u);

    // Caller 0x1000 should have count 2
    auto it1 = callers.find(0x1000);
    ASSERT_NE(it1, callers.end());
    EXPECT_EQ(it1->second, 2u);

    auto it2 = callers.find(0x2000);
    ASSERT_NE(it2, callers.end());
    EXPECT_EQ(it2->second, 1u);
}

TEST_F(PortTracker_Test, ReadCallerTracking)
{
    _tracker->StartSession();

    _tracker->TrackRead(0xFE, 0xFF, 0x3000);
    _tracker->TrackRead(0xFE, 0xFE, 0x3000);
    _tracker->TrackRead(0xFE, 0xFD, 0x4000);

    const auto& callers = _tracker->GetReadCallers(0xFE);
    EXPECT_EQ(callers.size(), 2u);
}

TEST_F(PortTracker_Test, CallerTrackingEmptyForUnusedPort)
{
    _tracker->StartSession();

    const auto& callers = _tracker->GetWriteCallers(0xBB);
    EXPECT_TRUE(callers.empty());
}

// =============================================================================
// Value Distribution Tests
// =============================================================================

TEST_F(PortTracker_Test, WriteValueDistribution)
{
    _tracker->StartSession();

    // Write 3 different values to port 0xFE
    _tracker->TrackWrite(0xFE, 0x00, 0x1000);
    _tracker->TrackWrite(0xFE, 0x00, 0x1000);
    _tracker->TrackWrite(0xFE, 0x01, 0x1000);
    _tracker->TrackWrite(0xFE, 0x07, 0x1000);

    const auto& dist = _tracker->GetWriteValues(0xFE);
    EXPECT_EQ(dist.size(), 3u);

    // Value 0x00 should have count 2
    auto it = dist.find(0x00);
    ASSERT_NE(it, dist.end());
    EXPECT_EQ(it->second, 2u);
}

TEST_F(PortTracker_Test, ReadValueDistribution)
{
    _tracker->StartSession();

    _tracker->TrackRead(0xFE, 0xFF, 0x1000);
    _tracker->TrackRead(0xFE, 0xFF, 0x1000);
    _tracker->TrackRead(0xFE, 0xFE, 0x1000);

    const auto& dist = _tracker->GetReadValues(0xFE);
    EXPECT_EQ(dist.size(), 2u);
}

// =============================================================================
// Feature Gate Tests
// =============================================================================

TEST_F(PortTracker_Test, TrackingSkippedWhenInactive)
{
    // Don't start session — tracker is inactive
    _tracker->TrackWrite(0xFE, 0x00, 0x1000);
    _tracker->TrackRead(0xFE, 0xFF, 0x2000);

    // Values should not be tracked
    EXPECT_EQ(_tracker->GetWriteCount(0xFE), 0u);
    EXPECT_EQ(_tracker->GetReadCount(0xFE), 0u);
}

TEST_F(PortTracker_Test, TrackingSkippedWhenPaused)
{
    _tracker->StartSession();
    _tracker->TrackWrite(0xFE, 0x00, 0x1000);
    EXPECT_EQ(_tracker->GetWriteCount(0xFE), 1u);

    _tracker->PauseSession();

    // These should be ignored
    _tracker->TrackWrite(0xFE, 0x01, 0x1000);
    _tracker->TrackWrite(0xFE, 0x02, 0x1000);

    // Count should not increase
    EXPECT_EQ(_tracker->GetWriteCount(0xFE), 1u);

    _tracker->ResumeSession();

    // Now tracking should work again
    _tracker->TrackWrite(0xFE, 0x03, 0x1000);
    EXPECT_EQ(_tracker->GetWriteCount(0xFE), 2u);
}

// =============================================================================
// Lazy Allocation Tests
// =============================================================================

TEST_F(PortTracker_Test, LazyAllocationOnFirstAccess)
{
    _tracker->StartSession();

    // Port 0xAA has no data yet — reads should return 0
    EXPECT_EQ(_tracker->GetWriteCount(0xAA), 0u);

    // First write to port allocates data
    _tracker->TrackWrite(0xAA, 0x55, 0x1000);
    EXPECT_EQ(_tracker->GetWriteCount(0xAA), 1u);
}

// =============================================================================
// Summary and Report Tests
// =============================================================================

TEST_F(PortTracker_Test, PortSummary)
{
    _tracker->StartSession();

    _tracker->TrackWrite(0xFE, 0x00, 0x1000);
    _tracker->TrackWrite(0xFE, 0x01, 0x2000);
    _tracker->TrackRead(0xFE, 0xFF, 0x3000);

    auto summary = _tracker->GetPortSummary(0xFE);
    EXPECT_EQ(summary.port, 0xFE);
    EXPECT_EQ(summary.writeCount, 2u);
    EXPECT_EQ(summary.readCount, 1u);
    EXPECT_EQ(summary.uniqueWriteCallers, 2u);
    EXPECT_EQ(summary.uniqueReadCallers, 1u);
}

TEST_F(PortTracker_Test, ActivePortCount)
{
    _tracker->StartSession();

    EXPECT_EQ(_tracker->GetActivePortCount(), 0u);

    _tracker->TrackWrite(0xFE, 0x00, 0x1000);
    _tracker->TrackWrite(0x7FFD, 0x10, 0x2000);
    _tracker->TrackRead(0x1F, 0x00, 0x3000);

    EXPECT_EQ(_tracker->GetActivePortCount(), 3u);
}

TEST_F(PortTracker_Test, ReportIsNonEmpty)
{
    _tracker->StartSession();

    _tracker->TrackWrite(0xFE, 0x00, 0x1000);
    _tracker->TrackRead(0xFE, 0xFF, 0x2000);
    _tracker->TrackWrite(0x7FFD, 0x10, 0x3000);

    std::string report = _tracker->GenerateReport();
    EXPECT_FALSE(report.empty());

    // Report should mention the ports we used
    EXPECT_NE(report.find("FE"), std::string::npos);
    EXPECT_NE(report.find("7FFD"), std::string::npos);
}

TEST_F(PortTracker_Test, ResetClearsAllData)
{
    _tracker->StartSession();

    _tracker->TrackWrite(0xFE, 0x00, 0x1000);
    _tracker->TrackRead(0x7FFD, 0x10, 0x2000);
    EXPECT_EQ(_tracker->GetActivePortCount(), 2u);

    _tracker->Reset();
    EXPECT_EQ(_tracker->GetActivePortCount(), 0u);
    EXPECT_EQ(_tracker->GetWriteCount(0xFE), 0u);
    EXPECT_EQ(_tracker->GetReadCount(0x7FFD), 0u);
}

// =============================================================================
// Edge Case Tests
// =============================================================================

TEST_F(PortTracker_Test, AllPortAddresses)
{
    _tracker->StartSession();

    // Track port 0x0000 (minimum)
    _tracker->TrackWrite(0x0000, 0x42, 0x1000);
    EXPECT_EQ(_tracker->GetWriteCount(0x0000), 1u);

    // Track port 0xFFFF (maximum)
    _tracker->TrackWrite(0xFFFF, 0x42, 0x1000);
    EXPECT_EQ(_tracker->GetWriteCount(0xFFFF), 1u);
}

TEST_F(PortTracker_Test, HighVolumeTracking)
{
    _tracker->StartSession();

    // Simulate heavy port activity (e.g., beeper or AY)
    for (int i = 0; i < 1000; i++)
    {
        _tracker->TrackWrite(0xFE, static_cast<uint8_t>(i & 0xFF), 0x1000);
    }

    EXPECT_EQ(_tracker->GetWriteCount(0xFE), 1000u);
}
