/// @file ttd_step_instruction_test.cpp
/// @brief Tests for StepBackInstruction / StepForwardInstruction.
///
/// Per parent TDD §10.2 + §16. Tests the single-instruction navigation
/// API: state guards, boundary conditions (session start/end), and the
/// recording-state rejection.
///
/// StepBackInstruction delegates to FindLastAccess(Execute) which uses the
/// replay fallback. StepForwardInstruction uses RunTStates(1) in replay
/// mode.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <unistd.h>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

class TTD_StepInstruction_Test : public ::testing::Test
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
// State guards
// ===========================================================================

TEST_F(TTD_StepInstruction_Test, StepBack_WhileRecording_ReturnsFalse)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);

    // Should be rejected: still recording
    EXPECT_FALSE(_ttd->StepBackInstruction());

    _ttd->StopRecording();
}

TEST_F(TTD_StepInstruction_Test, StepForward_WhileRecording_ReturnsFalse)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);

    EXPECT_FALSE(_ttd->StepForwardInstruction());

    _ttd->StopRecording();
}

TEST_F(TTD_StepInstruction_Test, StepBack_NoHistory_ReturnsFalse)
{
    EXPECT_FALSE(_ttd->StepBackInstruction());
}

TEST_F(TTD_StepInstruction_Test, StepForward_NoHistory_ReturnsFalse)
{
    EXPECT_FALSE(_ttd->StepForwardInstruction());
}

// ===========================================================================
// Boundary conditions
// ===========================================================================

TEST_F(TTD_StepInstruction_Test, StepBack_AtSessionStart_ReturnsFalse)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);
    _ttd->StopRecording();

    // Seek to the first frame
    ttd::TTDTimePoint first{_ttd->GetSessionInfo().sessionStartFrame, 0};
    ASSERT_TRUE(_ttd->SeekTo(first));

    // Step back at the very start should fail
    EXPECT_FALSE(_ttd->StepBackInstruction());
}

TEST_F(TTD_StepInstruction_Test, StepForward_AtSessionEnd_ReturnsFalse)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);
    _ttd->StopRecording();

    // Seek to session end
    ttd::TTDTimePoint end = _ttd->SessionEndPosition();
    ASSERT_TRUE(_ttd->SeekTo(end));

    // Step forward past session end should fail
    EXPECT_FALSE(_ttd->StepForwardInstruction());
}

// ===========================================================================
// SerializeSession round-trip with write journal
// ===========================================================================

TEST_F(TTD_StepInstruction_Test, SerializeSession_RoundTrip_PreservesJournal)
{
    ASSERT_TRUE(_ttd->StartRecording());

    // Populate the journal with known records
    _ttd->RecordMemoryWrite(0x1000, 0, 0x42, 0x2000, 1);
    _ttd->RecordMemoryWrite(0x2000, 0, 0x55, 0x2100, 2);
    _ttd->RecordIoWrite(0xFE, 0x08, 0x2200);

    RunFrames(1);
    _ttd->StopRecording();

    const size_t journalSizeBefore = _ttd->GetWriteJournal().Size();
    ASSERT_GT(journalSizeBefore, 0u);

    // Serialize
    char tmpfile[] = "/tmp/ttd_step_instruction_test_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT_GE(fd, 0);
    close(fd);

    {
        std::ofstream out(tmpfile, std::ios::binary);
        std::string err;
        ASSERT_TRUE(_ttd->SerializeSession(out, err)) << "SerializeSession failed: " << err;
    }

    // Deserialize into a fresh emulator
    Emulator* emu2 = new Emulator(LoggerLevel::LogError);
    ASSERT_TRUE(emu2->Init());
    EmulatorContext* ctx2 = emu2->GetContext();
    ttd::TimeTravelManager* ttd2 = ctx2->pTimeTravelManager;

    {
        std::ifstream in(tmpfile, std::ios::binary);
        std::string err;
        ASSERT_TRUE(ttd2->DeserializeSession(in, err)) << "DeserializeSession failed: " << err;
    }

    // Verify journal was preserved
    EXPECT_EQ(ttd2->GetWriteJournal().Size(), journalSizeBefore);

    // Verify checkpoint count
    EXPECT_GT(ttd2->GetCheckpointCount(), 0u);

    // Verify we can query the journal after load
    ttd::TTDSearchQuery q;
    q.addrFrom = 0x1000;
    q.addrTo   = 0x1000;
    q.access   = ttd::TTDAccessType::Write;
    auto result = ttd2->FindLastAccess(q);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, 0x42u);

    emu2->Stop();
    emu2->Release();
    delete emu2;
    remove(tmpfile);
}
