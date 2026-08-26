/// @file ttd_session_lifecycle_test.cpp
/// @brief Integration tests for TTD session invalidation hooks (P1.6).
///
/// Per parent TDD §4.2: "Invalidated (history cleared, new session started)
/// by any event that breaks determinism or teleports state":
///   - Snapshot/tape/disk load
///   - ROM reload
///   - Speed multiplier change
///   - (Disk media write-back — staged handling per TDD §12.2)
///
/// Reset() is NOT in this list — it must NEVER modify the recorded timeline.
/// If recording is active, Reset() stops it first (transitioning Recording
/// → Idle with history retained) so the timeline isn't corrupted by the
/// frame_counter teleport. If recording is already stopped, Reset() is a
/// pure no-op for TTD. See Reset_StopsRecordingAndPreservesHistory below.
///
/// These tests verify the wiring: each listed entry point (EXCEPT Reset)
/// calls TimeTravelManager::InvalidateSession, which transitions Recording
/// → Idle and clears the timeline. When no session is active the calls are
/// no-ops.

#include <gtest/gtest.h>

#include <string>

#include "_helpers/testpathhelper.h"
#include "debugger/ttd/ttd_checkpoint.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"

/// region <Helpers>

namespace
{
/// Relative paths (under testdata/) resolved via TestPathHelper — works
/// regardless of the test binary's current working directory.
const char* const kTestTapeRelPath = "loaders/tap/traffic_lights.tap";
const char* const kTestDiskRelPath = "loaders/trd/EyeAche.trd";

/// Assert that the manager is in the given state with the given checkpoint count.
void ExpectSessionState(ttd::TimeTravelManager* mgr,
                        ttd::TTDSessionState expectedState,
                        size_t expectedMinCheckpoints,
                        const char* msg)
{
    ASSERT_NE(mgr, nullptr);
    ttd::TTDSessionInfo info = mgr->GetSessionInfo();
    EXPECT_EQ(info.state, expectedState) << msg;
    if (expectedState == ttd::TTDSessionState::Recording)
    {
        EXPECT_GE(info.checkpointCount, expectedMinCheckpoints)
            << msg << " (expected >= " << expectedMinCheckpoints << " checkpoints)";
    }
    else
    {
        EXPECT_EQ(info.checkpointCount, 0u)
            << msg << " (expected cleared timeline)";
    }
}
}  // anonymous namespace

/// endregion </Helpers>

/// region <Invalidation hook tests>

/// @test Reset() on an active recording STOPS the recording and preserves
///       the timeline — it does NOT invalidate and does NOT add markers.
///
///       The recorded history is the user's property; Reset() must never
///       touch it. Stopping recording before _core->Reset() runs prevents
///       frame_counter teleport from corrupting the timeline (a checkpoint
///       captured at frame 0 after reset would break the sorted invariant
///       when appended after checkpoints at higher frame numbers).
///
///       After Reset():
///         - State is Idle (Recording was stopped, not invalidated)
///         - Timeline is intact (checkpoint count unchanged)
///         - No new markers (the HardwareReset event is not recorded —
///           markers are for nondeterministic INPUT events, not for
///           state teleports that happen AFTER recording stops)
///         - User can seek and replay within the captured history
TEST(TTD_SessionLifecycle_Test, Reset_StopsRecordingAndPreservesHistory)
{
    Emulator emulator(LoggerLevel::LogError);
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pTimeTravelManager, nullptr);

    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());
    const size_t checkpointsBefore =
        context->pTimeTravelManager->GetSessionInfo().checkpointCount;
    EXPECT_GE(checkpointsBefore, 1u);

    const size_t markersBefore = context->pTimeTravelManager->GetExternalEvents().Size();

    emulator.Reset();

    // State transitioned Recording → Idle (stopped, not invalidated).
    EXPECT_EQ(context->pTimeTravelManager->GetState(),
              ttd::TTDSessionState::Idle)
        << "Reset must stop recording (Recording → Idle)";

    // Timeline is UNTOUCHED — same checkpoint count as before Reset.
    const size_t checkpointsAfter =
        context->pTimeTravelManager->GetSessionInfo().checkpointCount;
    EXPECT_EQ(checkpointsAfter, checkpointsBefore)
        << "Reset must not modify the timeline (checkpoints changed)";

    // No markers were added — Reset is not a recorded event.
    const size_t markersAfter = context->pTimeTravelManager->GetExternalEvents().Size();
    EXPECT_EQ(markersAfter, markersBefore)
        << "Reset must not add markers to the timeline";

    emulator.Stop();
    emulator.Release();
}

/// @test Reset() on a stopped recording (Idle with history) is a pure
///       no-op for TTD — the timeline is preserved exactly so the user
///       can replay anytime.
TEST(TTD_SessionLifecycle_Test, Reset_OnStoppedRecording_PreservesHistory)
{
    Emulator emulator(LoggerLevel::LogError);
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pTimeTravelManager, nullptr);

    // Start then stop — leaves state=Idle with history retained.
    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());
    context->pTimeTravelManager->StopRecording();

    const size_t checkpointsBefore =
        context->pTimeTravelManager->GetSessionInfo().checkpointCount;
    const size_t markersBefore = context->pTimeTravelManager->GetExternalEvents().Size();
    ASSERT_GE(checkpointsBefore, 1u) << "Precondition: history must exist";

    emulator.Reset();

    // Everything is preserved — state stays Idle, timeline untouched.
    EXPECT_EQ(context->pTimeTravelManager->GetState(),
              ttd::TTDSessionState::Idle);
    EXPECT_EQ(context->pTimeTravelManager->GetSessionInfo().checkpointCount,
              checkpointsBefore)
        << "Reset on stopped recording must not touch the timeline";
    EXPECT_EQ(context->pTimeTravelManager->GetExternalEvents().Size(),
              markersBefore)
        << "Reset on stopped recording must not add markers";

    emulator.Stop();
    emulator.Release();
}

/// @test Reset() on an idle session is a no-op (doesn't crash, stays idle).
TEST(TTD_SessionLifecycle_Test, Reset_OnIdleSession_IsNoOp)
{
    Emulator emulator(LoggerLevel::LogError);
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);

    // No StartRecording — session is already idle.
    ExpectSessionState(context->pTimeTravelManager, ttd::TTDSessionState::Idle, 0,
                       "before Reset on idle session");

    emulator.Reset();

    ExpectSessionState(context->pTimeTravelManager, ttd::TTDSessionState::Idle, 0,
                       "after Reset on idle session");

    emulator.Stop();
    emulator.Release();
}

/// @test SetSpeedMultiplier() invalidates the session.
TEST(TTD_SessionLifecycle_Test, SetSpeedMultiplier_InvalidatesActiveSession)
{
    Emulator emulator(LoggerLevel::LogError);
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);

    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());
    ExpectSessionState(context->pTimeTravelManager, ttd::TTDSessionState::Recording, 1,
                       "after StartRecording");

    emulator.SetSpeedMultiplier(2);  // Queue 2x — invalidates per TDD §4.2

    ExpectSessionState(context->pTimeTravelManager, ttd::TTDSessionState::Idle, 0,
                       "after SetSpeedMultiplier");

    emulator.Stop();
    emulator.Release();
}

/// @test LoadTape() invalidates the session (TDD §4.2 + §5 row 3).
TEST(TTD_SessionLifecycle_Test, LoadTape_InvalidatesActiveSession)
{
    Emulator emulator(LoggerLevel::LogError);
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);

    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());
    ExpectSessionState(context->pTimeTravelManager, ttd::TTDSessionState::Recording, 1,
                       "after StartRecording");

    bool ok = emulator.LoadTape(TestPathHelper::GetTestDataPath(kTestTapeRelPath));
    ASSERT_TRUE(ok) << "Test precondition: LoadTape('" << kTestTapeRelPath << "') must succeed";

    ExpectSessionState(context->pTimeTravelManager, ttd::TTDSessionState::Idle, 0,
                       "after LoadTape");

    emulator.Stop();
    emulator.Release();
}

/// @test LoadTape() with a non-existent file does NOT invalidate (validation
///       rejects the call before the invalidation hook fires).
TEST(TTD_SessionLifecycle_Test, LoadTape_MissingFile_DoesNotInvalidate)
{
    Emulator emulator(LoggerLevel::LogError);
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);

    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());
    ExpectSessionState(context->pTimeTravelManager, ttd::TTDSessionState::Recording, 1,
                       "after StartRecording");

    bool ok = emulator.LoadTape("nonexistent/path/to/file.tap");
    EXPECT_FALSE(ok) << "LoadTape on missing file should return false";

    // Session must still be active — the hook fires only after path validation.
    ExpectSessionState(context->pTimeTravelManager, ttd::TTDSessionState::Recording, 1,
                       "after failed LoadTape");

    emulator.Stop();
    emulator.Release();
}

/// @test LoadDisk() invalidates the session (TDD §4.2 + §12.2).
TEST(TTD_SessionLifecycle_Test, LoadDisk_InvalidatesActiveSession)
{
    Emulator emulator(LoggerLevel::LogError);
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);

    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());
    ExpectSessionState(context->pTimeTravelManager, ttd::TTDSessionState::Recording, 1,
                       "after StartRecording");

    bool ok = emulator.LoadDisk(TestPathHelper::GetTestDataPath(kTestDiskRelPath));
    ASSERT_TRUE(ok) << "Test precondition: LoadDisk('" << kTestDiskRelPath << "') must succeed";

    ExpectSessionState(context->pTimeTravelManager, ttd::TTDSessionState::Idle, 0,
                       "after LoadDisk");

    emulator.Stop();
    emulator.Release();
}

/// @test StopRecording (explicit stop) keeps history but transitions state.
///       Distinct from InvalidateSession (which clears history).
TEST(TTD_SessionLifecycle_Test, StopRecording_RetainsHistory)
{
    Emulator emulator(LoggerLevel::LogError);
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);

    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());
    ExpectSessionState(context->pTimeTravelManager, ttd::TTDSessionState::Recording, 1,
                       "after StartRecording");

    context->pTimeTravelManager->StopRecording();

    // State is Idle but history (1 baseline checkpoint) is retained.
    ttd::TTDSessionInfo info = context->pTimeTravelManager->GetSessionInfo();
    EXPECT_EQ(info.state, ttd::TTDSessionState::Idle);
    EXPECT_GE(info.checkpointCount, 1u)
        << "StopRecording must retain history for browse/seek (TDD §4.2)";

    emulator.Stop();
    emulator.Release();
}

/// @test Re-StartRecording after invalidation works cleanly.
///       Uses LoadTape() to invalidate (Reset() no longer invalidates).
TEST(TTD_SessionLifecycle_Test, ReStartRecording_AfterInvalidation)
{
    Emulator emulator(LoggerLevel::LogError);
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);

    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());
    EXPECT_EQ(context->pTimeTravelManager->GetSessionInfo().state,
              ttd::TTDSessionState::Recording);

    bool ok = emulator.LoadTape(TestPathHelper::GetTestDataPath(kTestTapeRelPath));
    ASSERT_TRUE(ok) << "Test precondition: LoadTape must succeed";
    EXPECT_EQ(context->pTimeTravelManager->GetSessionInfo().state,
              ttd::TTDSessionState::Idle);

    // Restart — should succeed and capture a fresh baseline.
    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());
    ExpectSessionState(context->pTimeTravelManager, ttd::TTDSessionState::Recording, 1,
                       "after second StartRecording");

    emulator.Stop();
    emulator.Release();
}

/// endregion </Invalidation hook tests>
