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

#include <gtest/gtest.h>

#include <cstdint>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_probe.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

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
    EXPECT_GT(_ttd->GetWriteJournal().Size(), 0u);

    ttd::TTDSearchQuery q;
    q.addrFrom = 0x2000;
    q.addrTo   = 0x2000;
    q.access   = ttd::TTDAccessType::Write;

    auto result = _ttd->FindLastAccess(q);
    // No write to 0x2000 in the journal; ring hasn't wrapped (oldestInRing <= 1).
    EXPECT_FALSE(result.has_value());
}
