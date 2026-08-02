/// @file ttd_find_last_test.cpp
/// @brief Integration tests for TimeTravelManager::FindLastAccess — journal fast path.
///
/// Per parent TDD §9.2 + §9.4. Tests the journal fast path (Write access type)
/// by recording a session and populating the write journal with known records
/// via RecordMemoryWrite. Then queries FindLastAccess to verify correct
/// results for address range, value filter, PC filter, and before-frame
/// constraints.

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
