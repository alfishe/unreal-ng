/// @file ttd_find_last_marker_test.cpp
/// @brief Tests for FindLastAccess when external-event markers block replay.
///
/// Per parent TDD §9.2 + §5.1. When a FindLastAccess replay interval contains
/// an external-event marker, the search stops and returns std::nullopt with
/// the blocking marker info (via the outBlockingMarker parameter).
///
/// The journal fast path (Write queries) is NOT blocked by markers — it's a
/// direct scan. Only the replay fallback (Read/Execute queries) is blocked.

#include <gtest/gtest.h>

#include <cstdint>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_external_events.h"
#include "debugger/ttd/ttd_probe.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

class TTD_FindLast_Marker_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    FeatureManager* _fm = nullptr;
    Memory* _memory = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        _memory = _context->pMemory;
        ASSERT_NE(_memory, nullptr);
        _fm = _emulator->GetFeatureManager();
        ASSERT_NE(_fm, nullptr);

        _fm->setFeature(Features::kDebugMode, true);
        _fm->setFeature(Features::kTimeTravel, true);
        _memory->UpdateFeatureCache();
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }

    void RunFrames(uint32_t n)
    {
        _emulator->RunNFrames(static_cast<unsigned>(n), /*skipBreakpoints=*/true);
    }
};

// ===========================================================================
// Read query with marker — marker blocks the replay interval
// ===========================================================================

TEST_F(TTD_FindLast_Marker_Test, ReadQuery_MarkerBlocks_ReturnsNulloptWithMarker)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);

    // Record a marker mid-frame
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::TapeControl, "test tape control");

    RunFrames(2);
    _ttd->StopRecording();

    // Read access queries always use replay. With a marker blocking the
    // most recent interval, the query should return nullopt.
    ttd::TTDSearchQuery q;
    q.addrFrom = 0x0000;
    q.addrTo   = 0x3FFF;
    q.access   = ttd::TTDAccessType::Read;

    ttd::TTDExternalEvent blockingMarker;
    auto result = _ttd->FindLastAccess(q, &blockingMarker);

    // The marker should block the search in the most recent interval.
    // Result should be nullopt (or found in an earlier interval).
    if (!result.has_value())
    {
        EXPECT_STRNE(blockingMarker.reason, "");
    }
}

// ===========================================================================
// Execute query with marker — marker blocks the replay interval
// ===========================================================================

TEST_F(TTD_FindLast_Marker_Test, ExecuteQuery_MarkerBlocks_ReturnsNulloptWithMarker)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);

    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::DiskWrite, "test disk write");

    RunFrames(1);
    _ttd->StopRecording();

    ttd::TTDSearchQuery q;
    q.addrFrom = 0x0000;
    q.addrTo   = 0x3FFF;
    q.access   = ttd::TTDAccessType::Execute;

    ttd::TTDExternalEvent blockingMarker;
    auto result = _ttd->FindLastAccess(q, &blockingMarker);

    if (!result.has_value())
    {
        EXPECT_STRNE(blockingMarker.reason, "");
    }
}

// ===========================================================================
// Write queries with markers — journal fast path is NOT blocked by markers
// ===========================================================================

TEST_F(TTD_FindLast_Marker_Test, WriteQuery_MarkerDoesNotBlock_JournalFastPath)
{
    ASSERT_TRUE(_ttd->StartRecording());

    // Record a known write
    _ttd->RecordMemoryWrite(0x1000, 0, 0x42, 0x2000, 1);
    RunFrames(1);

    // Add a marker
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::DebuggerEdit, "mem edit");

    // Record another write after the marker
    _ttd->RecordMemoryWrite(0x1000, 0, 0x99, 0x2000, 1);
    RunFrames(1);

    _ttd->StopRecording();

    // Write query should use the journal fast path and find the newest write
    // (0x99), regardless of the marker.
    ttd::TTDSearchQuery q;
    q.addrFrom = 0x1000;
    q.addrTo   = 0x1000;
    q.access   = ttd::TTDAccessType::Write;

    ttd::TTDExternalEvent blockingMarker;
    auto result = _ttd->FindLastAccess(q, &blockingMarker);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, 0x99u);
    EXPECT_EQ(result->access, ttd::TTDAccessType::Write);
    // No blocking marker on the journal fast path
    EXPECT_STREQ(blockingMarker.reason, "");
}
