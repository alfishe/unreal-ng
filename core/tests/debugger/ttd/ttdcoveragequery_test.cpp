/// @file ttdcoveragequery_test.cpp
/// @brief Unit tests for TD-7 TTD Coverage Index Query methods (probe, scan, summary).

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcoverageindex.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{

class TTDCoverageQuery_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    Memory* _memory = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);

        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        _ttd = _context->pTimeTravelManager;
        _memory = _context->pMemory;
        ASSERT_NE(_ttd, nullptr);
        ASSERT_NE(_memory, nullptr);

        FeatureManager* fm = _emulator->GetFeatureManager();
        ASSERT_NE(fm, nullptr);
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        _memory->UpdateFeatureCache();
        _emulator->EnableTurboMode();
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }
};

TEST_F(TTDCoverageQuery_Test, ProbeQuery_LiveRecording)
{
    _ttd->StartRecording();

    _emulator->RunNFrames(5, true);
    _ttd->StopRecording();

    // Covered window of the session (recording started before any frame ran)
    auto window = _ttd->QueryCoverageScan(0, 1000, ttd::TTDCoverageKind::Executed, 0x0000, 0x00FF);
    ASSERT_TRUE(window.indexAvailable);
    const uint64_t coveredFrom = window.coveredFrom;
    const uint64_t coveredTo = window.coveredTo;
    ASSERT_LE(coveredFrom, coveredTo);
    ASSERT_LE(0u, coveredTo);

    // Probe ROM 0x0000 (ROM boot execution) on a covered frame
    auto probeRes = _ttd->QueryCoverageProbe(coveredFrom, ttd::TTDCoverageKind::Executed, 0x0000, 0x0010);
    EXPECT_TRUE(probeRes.indexAvailable);
    EXPECT_TRUE(probeRes.touched);

    // Probe an address range never touched, e.g., high address in RAM that wasn't touched
    auto probeUnused = _ttd->QueryCoverageProbe(coveredFrom, ttd::TTDCoverageKind::Executed, 0xBF00, 0xBFFF, 127);
    EXPECT_TRUE(probeUnused.indexAvailable);
    EXPECT_FALSE(probeUnused.touched);
}

TEST_F(TTDCoverageQuery_Test, ProbeQuery_FrameOutsideCoveredRange_ReportsIndexUnavailable)
{
    _ttd->StartRecording();

    _emulator->RunNFrames(5, true);
    _ttd->StopRecording();

    auto window = _ttd->QueryCoverageScan(0, 1000, ttd::TTDCoverageKind::Executed, 0x0000, 0x00FF);
    ASSERT_TRUE(window.indexAvailable);

    // Regression (TD-7 defect A): FrameMayContain answers conservatively outside the
    // covered range; the exact probe must not leak that as a false-positive "touched".
    auto probeAfter = _ttd->QueryCoverageProbe(window.coveredTo + 10, ttd::TTDCoverageKind::Executed, 0x0000, 0xFFFF);
    EXPECT_FALSE(probeAfter.indexAvailable);
    EXPECT_FALSE(probeAfter.touched);

    if (window.coveredFrom > 0)
    {
        auto probeBefore = _ttd->QueryCoverageProbe(window.coveredFrom - 1, ttd::TTDCoverageKind::Executed, 0x0000, 0xFFFF);
        EXPECT_FALSE(probeBefore.indexAvailable);
        EXPECT_FALSE(probeBefore.touched);
    }

    // Sanity: a frame inside the covered window is still reported available.
    auto probeInside = _ttd->QueryCoverageProbe(window.coveredTo, ttd::TTDCoverageKind::Executed, 0x0000, 0xFFFF);
    EXPECT_TRUE(probeInside.indexAvailable);
}

TEST_F(TTDCoverageQuery_Test, ScanQuery_LiveRecording)
{
    _ttd->StartRecording();

    _emulator->RunNFrames(10, true);
    _ttd->StopRecording();

    // Scan for executed ROM region 0x0000..0x00FF across frames 0..10
    auto scanRes = _ttd->QueryCoverageScan(0, 10, ttd::TTDCoverageKind::Executed, 0x0000, 0x00FF);
    EXPECT_TRUE(scanRes.indexAvailable);
    EXPECT_GT(scanRes.scannedFrames, 0u);
    EXPECT_GT(scanRes.matchingFrames, 0u);
    EXPECT_FALSE(scanRes.frames.empty());
    EXPECT_EQ(scanRes.firstMatch, scanRes.frames.front());

    // Test scan limit
    auto scanLimit = _ttd->QueryCoverageScan(0, 10, ttd::TTDCoverageKind::Executed, 0x0000, 0x00FF, std::nullopt, 2);
    EXPECT_LE(scanLimit.frames.size(), 2u);
    if (scanLimit.matchingFrames > 2)
    {
        EXPECT_TRUE(scanLimit.truncated);
    }
}

TEST_F(TTDCoverageQuery_Test, ScanQuery_ReportsCoveredWindow)
{
    _ttd->StartRecording();

    _emulator->RunNFrames(10, true);
    _ttd->StopRecording();

    // Regression (TD-7 defect B): the clamped effective window must be echoed.
    auto scanRes = _ttd->QueryCoverageScan(0, 100000, ttd::TTDCoverageKind::Executed, 0x0000, 0x00FF);
    ASSERT_TRUE(scanRes.indexAvailable);
    EXPECT_LE(scanRes.coveredFrom, scanRes.coveredTo);
    // The scan never looks past the covered window even for a huge request range.
    EXPECT_LE(scanRes.scannedFrames, scanRes.coveredTo - scanRes.coveredFrom + 1);
    if (!scanRes.frames.empty())
    {
        EXPECT_GE(scanRes.frames.front(), scanRes.coveredFrom);
        EXPECT_LE(scanRes.frames.back(), scanRes.coveredTo);
    }

    // A window fully outside the covered range scans nothing but still echoes the window.
    auto scanOutside = _ttd->QueryCoverageScan(scanRes.coveredTo + 100, scanRes.coveredTo + 200,
                                                ttd::TTDCoverageKind::Executed, 0x0000, 0x00FF);
    EXPECT_TRUE(scanOutside.indexAvailable);
    EXPECT_EQ(scanOutside.scannedFrames, 0u);
    EXPECT_EQ(scanOutside.matchingFrames, 0u);
    EXPECT_EQ(scanOutside.coveredFrom, scanRes.coveredFrom);
    EXPECT_EQ(scanOutside.coveredTo, scanRes.coveredTo);
}

TEST_F(TTDCoverageQuery_Test, SummaryQuery_LiveRecording)
{
    _ttd->StartRecording();

    _emulator->RunNFrames(15, true);
    _ttd->StopRecording();

    uint64_t endFrame = _ttd->GetSessionInfo().currentEndFrame;
    ASSERT_GE(endFrame, 1u);

    // Summary across frames 1..endFrame with bucket_size 5
    auto summaryRes = _ttd->QueryCoverageSummary(1, endFrame, std::nullopt, 5, 10);
    EXPECT_TRUE(summaryRes.indexAvailable);
    EXPECT_GT(summaryRes.bucketCount, 0u);
    EXPECT_FALSE(summaryRes.buckets.empty());

    for (const auto& bucket : summaryRes.buckets)
    {
        EXPECT_LE(bucket.frameStart, bucket.frameEnd);
        EXPECT_GT(bucket.executedDistinct, 0u);
    }
}

TEST_F(TTDCoverageQuery_Test, SummaryQuery_ReportsCoveredWindow)
{
    _ttd->StartRecording();

    _emulator->RunNFrames(15, true);
    _ttd->StopRecording();

    auto summaryRes = _ttd->QueryCoverageSummary(0, 100000, std::nullopt, 5, 10);
    EXPECT_TRUE(summaryRes.indexAvailable);
    // Regression (TD-7 defect B): the covered window is echoed (union across kinds).
    EXPECT_LE(summaryRes.coveredFrom, summaryRes.coveredTo);

    // Cross-check against the per-kind scan window for executed coverage.
    auto window = _ttd->QueryCoverageScan(0, 100000, ttd::TTDCoverageKind::Executed, 0x0000, 0x00FF);
    ASSERT_TRUE(window.indexAvailable);
    EXPECT_LE(summaryRes.coveredFrom, window.coveredFrom);
    EXPECT_GE(summaryRes.coveredTo, window.coveredTo);
}

TEST_F(TTDCoverageQuery_Test, NoIndex_UnrecordedSession)
{
    // Idle session without recording
    auto probeRes = _ttd->QueryCoverageProbe(0, ttd::TTDCoverageKind::Executed, 0x0000, 0x0010);
    EXPECT_FALSE(probeRes.indexAvailable);
    EXPECT_FALSE(probeRes.touched);

    auto scanRes = _ttd->QueryCoverageScan(0, 10, ttd::TTDCoverageKind::Executed, 0x0000, 0x00FF);
    EXPECT_FALSE(scanRes.indexAvailable);
    EXPECT_EQ(scanRes.scannedFrames, 0u);

    auto summaryRes = _ttd->QueryCoverageSummary(0, 10);
    EXPECT_FALSE(summaryRes.indexAvailable);
    EXPECT_TRUE(summaryRes.buckets.empty());
}

} // namespace
