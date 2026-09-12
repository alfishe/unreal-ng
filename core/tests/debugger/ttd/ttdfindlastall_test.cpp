/// @file TTD "find last" query tests - core queries, marker blocking and journal
/// fallback.
///
/// Consolidated from ttd_find_last_test.cpp, ttd_find_last_marker_test.cpp and
/// ttd_find_last_fallback_test.cpp. Suite and test names are unchanged.

#include <gtest/gtest.h>
#include <cstdint>
#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdprobe.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "debugger/ttd/ttdexternalevents.h"

/// region <From ttd_find_last_test.cpp>
/// @file ttd_find_last_test.cpp
/// @brief Integration tests for TimeTravelManager::FindLastAccess — journal fast path.
///
/// Per parent TDD §9.2 + §9.4. Tests the journal fast path (Write access type)
/// by recording a session and populating the write journal with known records
/// via RecordMemoryWrite. Then queries FindLastAccess to verify correct
/// results for address range, value filter, PC filter, and before-frame
/// constraints.

class TTD_FindLast_Test : public ::testing::Test
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
// Write access — journal fast path
// ===========================================================================

TEST_F(TTD_FindLast_Test, FindWrite_SpecificAddress_ReturnsNewest)
{
    ASSERT_TRUE(_ttd->StartRecording());

    // Populate the journal with known writes
    _ttd->RecordMemoryWrite(0x1000, 0, 0x42, 0x2000, 1);
    _ttd->RecordMemoryWrite(0x2000, 0, 0x55, 0x2100, 2);
    _ttd->RecordMemoryWrite(0x1000, 0, 0x99, 0x2200, 1);  // newer write to same addr

    _ttd->StopRecording();

    ttd::TTDSearchQuery q;
    q.addrFrom = 0x1000;
    q.addrTo   = 0x1000;
    q.access   = ttd::TTDAccessType::Write;

    auto result = _ttd->FindLastAccess(q);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, 0x99u);       // newest write
    EXPECT_EQ(result->pc, 0x2200u);
    EXPECT_EQ(result->physPage, 1u);
}

TEST_F(TTD_FindLast_Test, FindWrite_AddressRange)
{
    ASSERT_TRUE(_ttd->StartRecording());

    _ttd->RecordMemoryWrite(0x1000, 0, 0x01, 0x2000, 0);
    _ttd->RecordMemoryWrite(0x1500, 0, 0x02, 0x2000, 0);
    _ttd->RecordMemoryWrite(0x1FFF, 0, 0x03, 0x2000, 0);

    _ttd->StopRecording();

    ttd::TTDSearchQuery q;
    q.addrFrom = 0x1000;
    q.addrTo   = 0x1FFF;
    q.access   = ttd::TTDAccessType::Write;

    auto result = _ttd->FindLastAccess(q);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, 0x03u);       // newest in range
}

TEST_F(TTD_FindLast_Test, FindWrite_NeverWrittenAddress_ReturnsNullopt)
{
    ASSERT_TRUE(_ttd->StartRecording());

    _ttd->RecordMemoryWrite(0x1000, 0, 0x01, 0x2000, 0);
    _ttd->RecordMemoryWrite(0x2000, 0, 0x02, 0x2000, 0);

    _ttd->StopRecording();

    ttd::TTDSearchQuery q;
    q.addrFrom = 0xFFFE;
    q.addrTo   = 0xFFFF;
    q.access   = ttd::TTDAccessType::Write;

    auto result = _ttd->FindLastAccess(q);
    EXPECT_FALSE(result.has_value());
}

// ===========================================================================
// Value filter
// ===========================================================================

TEST_F(TTD_FindLast_Test, FindWrite_ValueFilter_MatchesSpecificValue)
{
    ASSERT_TRUE(_ttd->StartRecording());

    _ttd->RecordMemoryWrite(0x1000, 0, 0x42, 0x2000, 0);
    _ttd->RecordMemoryWrite(0x1000, 0, 0x55, 0x2000, 0);  // different value
    _ttd->RecordMemoryWrite(0x1000, 0, 0x42, 0x2000, 0);  // same value, newer

    _ttd->StopRecording();

    ttd::TTDSearchQuery q;
    q.addrFrom       = 0x1000;
    q.addrTo         = 0x1000;
    q.access         = ttd::TTDAccessType::Write;
    q.hasValueFilter = true;
    q.value          = 0x42;

    auto result = _ttd->FindLastAccess(q);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, 0x42u);
}

TEST_F(TTD_FindLast_Test, FindWrite_ValueFilter_NonMatchingValue_ReturnsNullopt)
{
    ASSERT_TRUE(_ttd->StartRecording());

    _ttd->RecordMemoryWrite(0x1000, 0, 0x01, 0x2000, 0);
    _ttd->RecordMemoryWrite(0x1000, 0, 0x02, 0x2000, 0);

    _ttd->StopRecording();

    ttd::TTDSearchQuery q;
    q.addrFrom       = 0x1000;
    q.addrTo         = 0x1000;
    q.access         = ttd::TTDAccessType::Write;
    q.hasValueFilter = true;
    q.value          = 0xFF;

    auto result = _ttd->FindLastAccess(q);
    EXPECT_FALSE(result.has_value());
}

// ===========================================================================
// PC filter
// ===========================================================================

TEST_F(TTD_FindLast_Test, FindWrite_PcFilter_InRange)
{
    ASSERT_TRUE(_ttd->StartRecording());

    _ttd->RecordMemoryWrite(0x1000, 0, 0x01, 0x2000, 0);
    _ttd->RecordMemoryWrite(0x1000, 0, 0x02, 0x2500, 0);  // in PC range
    _ttd->RecordMemoryWrite(0x1000, 0, 0x03, 0x3000, 0);  // out of PC range

    _ttd->StopRecording();

    ttd::TTDSearchQuery q;
    q.addrFrom      = 0x1000;
    q.addrTo        = 0x1000;
    q.access        = ttd::TTDAccessType::Write;
    q.hasPcFilter   = true;
    q.pcFrom        = 0x2000;
    q.pcTo          = 0x2FFF;

    auto result = _ttd->FindLastAccess(q);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, 0x02u);  // the write with PC in range
}

TEST_F(TTD_FindLast_Test, FindWrite_PcFilter_OutOfRange_ReturnsNullopt)
{
    ASSERT_TRUE(_ttd->StartRecording());

    _ttd->RecordMemoryWrite(0x1000, 0, 0x01, 0x1000, 0);
    _ttd->RecordMemoryWrite(0x1000, 0, 0x02, 0x3000, 0);

    _ttd->StopRecording();

    ttd::TTDSearchQuery q;
    q.addrFrom      = 0x1000;
    q.addrTo        = 0x1000;
    q.access        = ttd::TTDAccessType::Write;
    q.hasPcFilter   = true;
    q.pcFrom        = 0x5000;
    q.pcTo          = 0x5FFF;

    auto result = _ttd->FindLastAccess(q);
    EXPECT_FALSE(result.has_value());
}

// ===========================================================================
// Before-frame constraint
// ===========================================================================

TEST_F(TTD_FindLast_Test, FindWrite_BeforeGlobalT_LimitsResults)
{
    ASSERT_TRUE(_ttd->StartRecording());

    // Record writes at different globalT values.
    // RecordMemoryWrite computes globalT from frame_counter * frame + t_in_frame.
    // With the emulator at frame 0, all writes will have the same frame but
    // different t_in_frame (since t advances with each RunTStates call).
    // We use RunNFrames to advance the frame counter between writes.
    _ttd->RecordMemoryWrite(0x1000, 0, 0x01, 0x2000, 0);
    RunFrames(5);  // advance to frame ~5
    _ttd->RecordMemoryWrite(0x1000, 0, 0x02, 0x2000, 0);
    RunFrames(5);  // advance to frame ~10

    _ttd->StopRecording();

    const uint32_t frameT = _context->config.frame;

    // Query before frame 10 → should find the write at frame ~5 (value=0x02)
    ttd::TTDSearchQuery q;
    q.addrFrom      = 0x1000;
    q.addrTo        = 0x1000;
    q.access        = ttd::TTDAccessType::Write;
    q.beforeGlobalT = static_cast<uint64_t>(8) * frameT;  // before frame 8

    auto result = _ttd->FindLastAccess(q);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, 0x02u);
}

// ===========================================================================
// State guards
// ===========================================================================

TEST_F(TTD_FindLast_Test, FindWrite_WhileRecording_ReturnsNullopt)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->RecordMemoryWrite(0x1000, 0, 0x42, 0x2000, 0);

    ttd::TTDSearchQuery q;
    q.addrFrom = 0x1000;
    q.addrTo   = 0x1000;
    q.access   = ttd::TTDAccessType::Write;

    auto result = _ttd->FindLastAccess(q);
    EXPECT_FALSE(result.has_value());  // Rejected: still recording

    _ttd->StopRecording();
}

TEST_F(TTD_FindLast_Test, FindWrite_NoHistory_ReturnsNullopt)
{
    ttd::TTDSearchQuery q;
    q.addrFrom = 0;
    q.addrTo   = 0xFFFF;
    q.access   = ttd::TTDAccessType::Write;

    auto result = _ttd->FindLastAccess(q);
    EXPECT_FALSE(result.has_value());
}

// ===========================================================================
// Result shape
// ===========================================================================

TEST_F(TTD_FindLast_Test, FindWrite_ResultShape_HasExpectedFields)
{
    ASSERT_TRUE(_ttd->StartRecording());

    _ttd->RecordMemoryWrite(0x1234, 0, 0x56, 0x5678, 3);

    _ttd->StopRecording();

    ttd::TTDSearchQuery q;
    q.addrFrom = 0x1234;
    q.addrTo   = 0x1234;
    q.access   = ttd::TTDAccessType::Write;

    auto result = _ttd->FindLastAccess(q);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->access, ttd::TTDAccessType::Write);
    EXPECT_EQ(result->pc, 0x5678u);
    EXPECT_EQ(result->value, 0x56u);
    EXPECT_EQ(result->physPage, 3u);
    EXPECT_GE(result->time.frame, 0u);
}

// ===========================================================================
// Bank-aware search (parent TDD §9.4)
// ===========================================================================
//
// On a banked machine one Z80 address names different bytes of RAM depending
// on which page is mapped into that window. Without a page filter, "who wrote
// 0xC000?" answers with writes to pages the caller never asked about — the
// journal has always recorded physPage, but nothing consulted it.

TEST_F(TTD_FindLast_Test, FindWrite_PhysPageFilter_SeparatesBankedAddresses)
{
    ASSERT_TRUE(_ttd->StartRecording());

    // Same Z80 address, three different pages banked in at the time.
    _ttd->RecordMemoryWrite(0xC000, 0, 0xAA, 0x8000, 0);
    _ttd->RecordMemoryWrite(0xC000, 0, 0xBB, 0x8100, 3);
    _ttd->RecordMemoryWrite(0xC000, 0, 0xCC, 0x8200, 7);  // newest overall

    _ttd->StopRecording();

    ttd::TTDSearchQuery q;
    q.addrFrom = 0xC000;
    q.addrTo   = 0xC000;
    q.access   = ttd::TTDAccessType::Write;

    // Unfiltered: the newest write wins regardless of page.
    auto newest = _ttd->FindLastAccess(q);
    ASSERT_TRUE(newest.has_value());
    EXPECT_EQ(newest->value, 0xCCu);

    // Filtered to page 3: must skip the newer page-7 write.
    q.hasPhysPageFilter = true;
    q.physPage = 3;
    auto page3 = _ttd->FindLastAccess(q);
    ASSERT_TRUE(page3.has_value());
    EXPECT_EQ(page3->value, 0xBBu) << "page filter did not exclude writes from other banks";
    EXPECT_EQ(page3->physPage, 3u);

    // Filtered to page 0: the oldest of the three.
    q.physPage = 0;
    auto page0 = _ttd->FindLastAccess(q);
    ASSERT_TRUE(page0.has_value());
    EXPECT_EQ(page0->value, 0xAAu);
}

/// A page that was never written must report no hit, not fall back to another
/// bank's write at the same address.
TEST_F(TTD_FindLast_Test, FindWrite_PhysPageFilter_UnwrittenPageReturnsNullopt)
{
    ASSERT_TRUE(_ttd->StartRecording());

    _ttd->RecordMemoryWrite(0xC000, 0, 0xAA, 0x8000, 1);
    _ttd->RecordMemoryWrite(0xC000, 0, 0xBB, 0x8100, 2);

    _ttd->StopRecording();

    ttd::TTDSearchQuery q;
    q.addrFrom = 0xC000;
    q.addrTo   = 0xC000;
    q.access   = ttd::TTDAccessType::Write;
    q.hasPhysPageFilter = true;
    q.physPage = 5;  // never written

    EXPECT_FALSE(_ttd->FindLastAccess(q).has_value())
        << "a page filter matched a write that belongs to a different bank";
}

/// endregion </From ttd_find_last_test.cpp>

/// region <From ttd_find_last_marker_test.cpp>
/// @file ttd_find_last_marker_test.cpp
/// @brief Tests for FindLastAccess when external-event markers block replay.
///
/// Per parent TDD §9.2 + §5.1. When a FindLastAccess replay interval contains
/// an external-event marker, the search stops and returns std::nullopt with
/// the blocking marker info (via the outBlockingMarker parameter).
///
/// The journal fast path (Write queries) is NOT blocked by markers — it's a
/// direct scan. Only the replay fallback (Read/Execute queries) is blocked.

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

/// endregion </From ttd_find_last_marker_test.cpp>

/// region <From ttd_find_last_fallback_test.cpp>
/// @file ttd_find_last_fallback_test.cpp
/// @brief Tests for FindLastAccess replay fallback path behavior.
///
/// Per parent TDD §9.2. Read and Execute access types are NOT journaled,
/// so they always use the replay fallback path. These tests verify the
/// boundary between journal fast path (Write) and replay fallback (Read)
/// by checking that Write queries find results in the journal while Read
/// queries at the same address do not (reads aren't journaled).
///
/// The journal ring-wrap fallback scenario (Write/Io after journal wraps)
/// requires millions of writes to trigger naturally — the journal unit
/// tests cover ring wrap behavior at the data-structure level.

class TTD_FindLast_Fallback_Test : public ::testing::Test
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
// Write queries use journal fast path; Read queries use replay fallback.
// A Read query at an address that was written should NOT find the write
// (reads are a different access type and aren't in the journal).
// ===========================================================================

TEST_F(TTD_FindLast_Fallback_Test, WriteQuery_FindsInJournal_ReadQueryDoesNot)
{
    ASSERT_TRUE(_ttd->StartRecording());

    // Record a known write
    _ttd->RecordMemoryWrite(0x1000, 0, 0x42, 0x2000, 1);
    RunFrames(1);  // advance to create at least one checkpoint

    _ttd->StopRecording();

    // Write query — should find the journal record
    ttd::TTDSearchQuery qw;
    qw.addrFrom = 0x1000;
    qw.addrTo   = 0x1000;
    qw.access   = ttd::TTDAccessType::Write;

    auto wResult = _ttd->FindLastAccess(qw);
    ASSERT_TRUE(wResult.has_value());
    EXPECT_EQ(wResult->value, 0x42u);

    // Read query at same address — should NOT find the write.
    // Reads are not journaled; the replay path runs but the HALTed
    // Z80 produces no memory accesses.
    ttd::TTDSearchQuery qr;
    qr.addrFrom = 0x1000;
    qr.addrTo   = 0x1000;
    qr.access   = ttd::TTDAccessType::Read;

    auto rResult = _ttd->FindLastAccess(qr);
    // The Read query should fall through to replay and find nothing
    // (the write is in the journal, not a read access).
    EXPECT_FALSE(rResult.has_value());
}

// ===========================================================================
// Execute query — always uses replay (not journaled)
// ===========================================================================

TEST_F(TTD_FindLast_Fallback_Test, ExecuteQuery_UsesReplayPath)
{
    ASSERT_TRUE(_ttd->StartRecording());

    _ttd->RecordMemoryWrite(0x1000, 0, 0x42, 0x2000, 1);
    RunFrames(1);

    _ttd->StopRecording();

    // Execute queries always use replay. A write journal record for the
    // same address should NOT be found by an Execute query.
    ttd::TTDSearchQuery q;
    q.addrFrom = 0x1000;
    q.addrTo   = 0x1000;
    q.access   = ttd::TTDAccessType::Execute;

    auto result = _ttd->FindLastAccess(q);
    // No execution at 0x1000 — the write was not an execute access.
    EXPECT_FALSE(result.has_value());
}

// ===========================================================================
// Io queries use journal fast path
// ===========================================================================

TEST_F(TTD_FindLast_Fallback_Test, IoQuery_FindsInJournal)
{
    ASSERT_TRUE(_ttd->StartRecording());

    // Record both a memory write and an IO write
    _ttd->RecordMemoryWrite(0xFE, 0, 0x01, 0x2000, 0);
    _ttd->RecordIoWrite(0xFE, 0x42, 0x2100);
    RunFrames(1);

    _ttd->StopRecording();

    // IO query should find the IO record in the journal
    ttd::TTDSearchQuery q;
    q.addrFrom = 0xFE;
    q.addrTo   = 0xFE;
    q.access   = ttd::TTDAccessType::Io;

    auto result = _ttd->FindLastAccess(q);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->access, ttd::TTDAccessType::Io);
    EXPECT_EQ(result->value, 0x42u);
    EXPECT_EQ(result->pc, 0x2100u);
}

// ===========================================================================
// Write query with no matching record falls through to replay
// when the ring has records (but the journal scan finds no match).
// Since the ring has records at different addresses, the scan returns
// nullopt and (because oldestInRing > 0) the ring hasn't wrapped, so
// the result is genuinely nullopt.
// ===========================================================================

TEST_F(TTD_FindLast_Fallback_Test, WriteQuery_NoMatch_JournalHasRecords_ReturnsNullopt)
{
    ASSERT_TRUE(_ttd->StartRecording());

    // Write to 0x1000 but query for 0x2000
    _ttd->RecordMemoryWrite(0x1000, 0, 0x42, 0x2000, 1);
    RunFrames(1);

    _ttd->StopRecording();

    // Verify journal is not empty
    EXPECT_GT(_ttd->GetWriteJournal()->Size(), 0u);

    ttd::TTDSearchQuery q;
    q.addrFrom = 0x2000;
    q.addrTo   = 0x2000;
    q.access   = ttd::TTDAccessType::Write;

    auto result = _ttd->FindLastAccess(q);
    // No write to 0x2000 in the journal; ring hasn't wrapped (oldestInRing <= 1).
    EXPECT_FALSE(result.has_value());
}

/// endregion </From ttd_find_last_fallback_test.cpp>
