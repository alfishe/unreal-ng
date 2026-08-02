/// @file ttd_lifecycle_stress_test.cpp
/// @brief Stress test: rapid session lifecycle transitions.
///
/// Verifies that repeated start/stop/seek/invalidate cycles don't leak
/// memory, corrupt internal state, or accumulate stale data. The TDD §6
/// page store uses COW refcounting; rapid create/destroy is the most
/// likely path for refcount bugs.

#include <gtest/gtest.h>

#include <cstdint>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

class TTD_Lifecycle_Stress_Test : public ::testing::Test
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

    void RunFrames(size_t n)
    {
        _emulator->RunNFrames(static_cast<unsigned>(n), /*skipBreakpoints=*/true);
    }
};

// ===========================================================================
// Rapid start/stop cycles — no state leakage between sessions
// ===========================================================================

TEST_F(TTD_Lifecycle_Stress_Test, RapidStartStop_10Cycles_NoStateLeak)
{
    for (int i = 0; i < 10; ++i)
    {
        ASSERT_TRUE(_ttd->StartRecording());
        RunFrames(3);

        EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Recording);
        EXPECT_GT(_ttd->GetCheckpointCount(), 0u);

        _ttd->StopRecording();
        EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Idle);

        // StartRecording again — must clear and start fresh.
        ASSERT_TRUE(_ttd->StartRecording());
        EXPECT_EQ(_ttd->GetCheckpointCount(), 1u);  // Baseline only

        _ttd->StopRecording();
    }
}

// ===========================================================================
// Start → Stop → Seek → Start — interleaving browse and record
// ===========================================================================

TEST_F(TTD_Lifecycle_Stress_Test, StartStopSeekResume_5Cycles_Consistent)
{
    for (int cycle = 0; cycle < 5; ++cycle)
    {
        ASSERT_TRUE(_ttd->StartRecording());
        RunFrames(5);
        _ttd->StopRecording();

        // Compute valid seek targets relative to this session's start.
        ttd::TTDSessionInfo info = _ttd->GetSessionInfo();
        uint64_t midFrame = info.sessionStartFrame + 2;

        // Browse history.
        ASSERT_TRUE(_ttd->SeekTo({midFrame, 0}));
        EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Detached);

        ttd::TTDTimePoint pos = _ttd->CurrentPosition();
        EXPECT_EQ(pos.frame, midFrame);

        // Resume from past.
        ASSERT_TRUE(_ttd->ResumeRecordingFrom({midFrame, 0}));
        EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Recording);

        RunFrames(2);
        _ttd->StopRecording();

        // Invalidate to clear for next cycle.
        _ttd->InvalidateSession("cycle reset");
    }
}

// ===========================================================================
// Invalidate clears all state
// ===========================================================================

TEST_F(TTD_Lifecycle_Stress_Test, InvalidateClearsAllState)
{
    for (int i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(_ttd->StartRecording());
        RunFrames(3);

        // Populate journal — must be during Recording state.
        _ttd->RecordMemoryWrite(0x1000 + i, 0, 0x42, 0x2000, 1);
        _ttd->RecordIoWrite(0xFE, 0x08, 0x2200);

        _ttd->StopRecording();

        EXPECT_GT(_ttd->GetCheckpointCount(), 0u);
        EXPECT_GT(_ttd->GetWriteJournal().Size(), 0u);

        ttd::TTDSessionInfo infoBefore = _ttd->GetSessionInfo();
        EXPECT_GT(infoBefore.sessionHeapBytes, 0u);

        _ttd->InvalidateSession("stress test");

        EXPECT_EQ(_ttd->GetCheckpointCount(), 0u);
        EXPECT_EQ(_ttd->GetWriteJournal().Size(), 0u);
        EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Idle);

        ttd::TTDSessionInfo infoAfter = _ttd->GetSessionInfo();
        // sessionHeapBytes may have small residual from vector capacities,
        // but must be drastically smaller than the pre-invalidate value.
        EXPECT_LT(infoAfter.sessionHeapBytes, infoBefore.sessionHeapBytes / 2);
    }
}

// ===========================================================================
// StartRecording after StopRecording preserves old data until next Start
// ===========================================================================

TEST_F(TTD_Lifecycle_Stress_Test, StopRecording_PreservesHistory_ForSeeking)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(10);
    _ttd->StopRecording();

    const size_t cpCount = _ttd->GetCheckpointCount();
    ASSERT_GT(cpCount, 5u);

    // Seek around the stopped session — history must be intact.
    ASSERT_TRUE(_ttd->SeekTo({5, 0}));
    EXPECT_EQ(_ttd->CurrentPosition().frame, 5u);

    ASSERT_TRUE(_ttd->SeekTo({0, 0}));
    EXPECT_EQ(_ttd->CurrentPosition().frame, 0u);

    // Still the same checkpoint count.
    EXPECT_EQ(_ttd->GetCheckpointCount(), cpCount);
}

// ===========================================================================
// Page store capacity stays bounded across sessions
// ===========================================================================

TEST_F(TTD_Lifecycle_Stress_Test, PageStoreCapacity_BoundedAcrossSessions)
{
    size_t maxCapacity = 0;

    for (int i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(_ttd->StartRecording());
        RunFrames(10);
        _ttd->StopRecording();

        ttd::TTDSessionInfo info = _ttd->GetSessionInfo();
        maxCapacity = std::max(maxCapacity, info.pageStoreBytes);

        _ttd->InvalidateSession("capacity check");
    }

    // Capacity should not grow unbounded across sessions.
    // The page store reuses slots after InvalidateSession.
    ttd::TTDSessionInfo info = _ttd->GetSessionInfo();
    EXPECT_EQ(info.pageStoreBytes, 0u);
}

// ===========================================================================
// Double-start / double-stop idempotency under rapid cycling
// ===========================================================================

TEST_F(TTD_Lifecycle_Stress_Test, DoubleStartDoubleStop_Idempotent)
{
    for (int i = 0; i < 5; ++i)
    {
        // Double start.
        ASSERT_TRUE(_ttd->StartRecording());
        EXPECT_TRUE(_ttd->StartRecording());  // Idempotent.
        EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Recording);

        RunFrames(2);

        // Double stop.
        _ttd->StopRecording();
        _ttd->StopRecording();  // Idempotent.
        EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Idle);
    }
}
