/// @file ttd_session_lifecycle_test.cpp
/// @brief Integration tests for TTD session invalidation hooks (P1.6).
///
/// Per parent TDD §4.2: "Invalidated (history cleared, new session started)
/// by any event that breaks determinism or teleports state":
///   - Snapshot/tape/disk load
///   - `Emulator::Reset()`
///   - ROM reload
///   - Speed multiplier change
///   - (Disk media write-back — staged handling per TDD §12.2)
///
/// These tests verify the wiring: each listed entry point calls
/// TTDManager::InvalidateSession, which transitions Recording → Idle and
/// clears the timeline. When no session is active the calls are no-ops.

#include <gtest/gtest.h>

#include <string>

#include "_helpers/test_path_helper.h"
#include "debugger/ttd/ttd_checkpoint.h"
#include "debugger/ttd/ttd_manager.h"
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
void ExpectSessionState(ttd::TTDManager* mgr,
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

/// @test Reset() invalidates the session.
TEST(TTD_SessionLifecycle_Test, Reset_InvalidatesActiveSession)
{
    Emulator emulator(LoggerLevel::LogError);
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pTTDManager, nullptr);

    ASSERT_TRUE(context->pTTDManager->StartRecording());
    ExpectSessionState(context->pTTDManager, ttd::TTDSessionState::Recording, 1,
                       "after StartRecording");

    emulator.Reset();

    ExpectSessionState(context->pTTDManager, ttd::TTDSessionState::Idle, 0,
                       "after Reset");

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
    ExpectSessionState(context->pTTDManager, ttd::TTDSessionState::Idle, 0,
                       "before Reset on idle session");

    emulator.Reset();

    ExpectSessionState(context->pTTDManager, ttd::TTDSessionState::Idle, 0,
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

    ASSERT_TRUE(context->pTTDManager->StartRecording());
    ExpectSessionState(context->pTTDManager, ttd::TTDSessionState::Recording, 1,
                       "after StartRecording");

    emulator.SetSpeedMultiplier(2);  // Queue 2x — invalidates per TDD §4.2

    ExpectSessionState(context->pTTDManager, ttd::TTDSessionState::Idle, 0,
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

    ASSERT_TRUE(context->pTTDManager->StartRecording());
    ExpectSessionState(context->pTTDManager, ttd::TTDSessionState::Recording, 1,
                       "after StartRecording");

    bool ok = emulator.LoadTape(TestPathHelper::GetTestDataPath(kTestTapeRelPath));
    ASSERT_TRUE(ok) << "Test precondition: LoadTape('" << kTestTapeRelPath << "') must succeed";

    ExpectSessionState(context->pTTDManager, ttd::TTDSessionState::Idle, 0,
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

    ASSERT_TRUE(context->pTTDManager->StartRecording());
    ExpectSessionState(context->pTTDManager, ttd::TTDSessionState::Recording, 1,
                       "after StartRecording");

    bool ok = emulator.LoadTape("nonexistent/path/to/file.tap");
    EXPECT_FALSE(ok) << "LoadTape on missing file should return false";

    // Session must still be active — the hook fires only after path validation.
    ExpectSessionState(context->pTTDManager, ttd::TTDSessionState::Recording, 1,
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

    ASSERT_TRUE(context->pTTDManager->StartRecording());
    ExpectSessionState(context->pTTDManager, ttd::TTDSessionState::Recording, 1,
                       "after StartRecording");

    bool ok = emulator.LoadDisk(TestPathHelper::GetTestDataPath(kTestDiskRelPath));
    ASSERT_TRUE(ok) << "Test precondition: LoadDisk('" << kTestDiskRelPath << "') must succeed";

    ExpectSessionState(context->pTTDManager, ttd::TTDSessionState::Idle, 0,
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

    ASSERT_TRUE(context->pTTDManager->StartRecording());
    ExpectSessionState(context->pTTDManager, ttd::TTDSessionState::Recording, 1,
                       "after StartRecording");

    context->pTTDManager->StopRecording();

    // State is Idle but history (1 baseline checkpoint) is retained.
    ttd::TTDSessionInfo info = context->pTTDManager->GetSessionInfo();
    EXPECT_EQ(info.state, ttd::TTDSessionState::Idle);
    EXPECT_GE(info.checkpointCount, 1u)
        << "StopRecording must retain history for browse/seek (TDD §4.2)";

    emulator.Stop();
    emulator.Release();
}

/// @test Re-StartRecording after invalidation works cleanly.
TEST(TTD_SessionLifecycle_Test, ReStartRecording_AfterInvalidation)
{
    Emulator emulator(LoggerLevel::LogError);
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);

    ASSERT_TRUE(context->pTTDManager->StartRecording());
    EXPECT_EQ(context->pTTDManager->GetSessionInfo().state,
              ttd::TTDSessionState::Recording);

    emulator.Reset();
    EXPECT_EQ(context->pTTDManager->GetSessionInfo().state,
              ttd::TTDSessionState::Idle);

    // Restart — should succeed and capture a fresh baseline.
    ASSERT_TRUE(context->pTTDManager->StartRecording());
    ExpectSessionState(context->pTTDManager, ttd::TTDSessionState::Recording, 1,
                       "after second StartRecording");

    emulator.Stop();
    emulator.Release();
}

/// endregion </Invalidation hook tests>
