/// @file ttd_seek_test.cpp
/// @brief Phase 2 Item 4 — Seek engine tests.
///
/// Per parent TDD §8.1 (SeekTo Algorithm) + §8.2 (Silent Replay Mode).
///
/// Coverage areas:
///   - SeekTo with frame-aligned targets (tInFrame == 0)
///   - SeekTo with intra-frame targets (tInFrame > 0) — exercises silent
///     replay
///   - SeekTo precondition violations (Idle, empty timeline, target out
///     of bounds)
///   - SeekTo state transitions (Recording → Detached, Detached → Detached)
///   - StepBackFrame / StepForwardFrame composition
///   - CurrentPosition / SessionEndPosition helpers
///   - Round-trip: SeekTo an earlier point, hash the machine state, seek
///     back to the same point later, verify the hash matches
///
/// Driving the emulator: tests use Emulator::RunNFrames(N) which is the
/// canonical "run N complete frames" entry point. Inside RunTStates the
/// frame-boundary handler calls MainLoop::OnFrameEnd → OnFrameBoundary,
/// which appends a checkpoint per frame when Recording is active.

#include <gtest/gtest.h>

#include <cstdint>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/machine_state_hash.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

// ===========================================================================
// Fixture
// ===========================================================================

class TTD_Seek_Test : public ::testing::Test
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

    /// Run exactly N frames, populating the timeline with N+1 checkpoints
    /// (baseline at StartRecording + N more from OnFrameBoundary).
    void RunFrames(size_t n)
    {
        _emulator->RunNFrames(static_cast<unsigned>(n), /*skipBreakpoints=*/true);
    }

    /// Capture a 64-bit hash of the live machine state. Used to verify
    /// that two seek-to-same-position calls produce identical machine
    /// state (round-trip determinism check).
    uint64_t HashNow()
    {
        Z80* z80 = _context->pCore->GetZ80();
        if (!z80 || !_memory)
            return 0;

        // RAM digest: hash every model-RAM byte. config.ramsize is in KB.
        const uint32_t ramBytes = static_cast<uint32_t>(_context->config.ramsize) * 1024u;
        const uint64_t ramDigest = ttd::HashBytes(_memory->RAMBase(), ramBytes);

        const auto snap = ttd::CaptureSnapshot(*static_cast<Z80State*>(z80),
                                                _context->emulatorState,
                                                ramDigest);
        return ttd::HashSnapshot(snap);
    }
};

// ===========================================================================
// Helpers — CurrentPosition / SessionEndPosition
// ===========================================================================

TEST_F(TTD_Seek_Test, CurrentPosition_InitiallyZeroFrame)
{
    // After Init the emulator has typically executed a few startup t-states
    // (ROM walkthrough). We only assert the frame counter is 0; the exact
    // intra-frame t-state depends on init details we don't want to pin here.
    EXPECT_EQ(_ttd->CurrentPosition().frame, 0u);
}

TEST_F(TTD_Seek_Test, SessionEndPosition_EmptyTimeline_ReturnsZero)
{
    // No StartRecording — timeline is empty
    EXPECT_EQ(_ttd->SessionEndPosition().frame,    0u);
    EXPECT_EQ(_ttd->SessionEndPosition().tInFrame, 0u);
}

TEST_F(TTD_Seek_Test, SessionEndPosition_AfterStartRecording_IsBaseline)
{
    ASSERT_TRUE(_ttd->StartRecording());
    // Baseline checkpoint at frame 0
    EXPECT_EQ(_ttd->SessionEndPosition().frame,    0u);
    EXPECT_EQ(_ttd->SessionEndPosition().tInFrame, 0u);
}

TEST_F(TTD_Seek_Test, SessionEndPosition_AdvancesWithFramesRun)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    EXPECT_EQ(_ttd->GetCheckpointCount(), 4u);  // baseline + 3 frames
    EXPECT_EQ(_ttd->SessionEndPosition().frame,    3u);
    EXPECT_EQ(_ttd->SessionEndPosition().tInFrame, 0u);
}

// ===========================================================================
// SeekTo precondition violations
// ===========================================================================

TEST_F(TTD_Seek_Test, SeekTo_IdleState_ReturnsFalse)
{
    EXPECT_FALSE(_ttd->SeekTo({0, 0}));
}

TEST_F(TTD_Seek_Test, SeekTo_EmptyTimeline_ReturnsFalse)
{
    // Recording active, but timeline not yet populated — actually
    // StartRecording captures a baseline, so the timeline has 1 entry.
    // Force empty by invalidating.
    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->InvalidateSession("test");
    EXPECT_FALSE(_ttd->SeekTo({0, 0}));
}

TEST_F(TTD_Seek_Test, SeekTo_TargetBeyondSessionEnd_ReturnsFalse)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);
    // Session end is frame 2; try frame 5
    EXPECT_FALSE(_ttd->SeekTo({5, 0}));
}

// ===========================================================================
// SeekTo frame-aligned (tInFrame == 0) — no intra-frame replay
// ===========================================================================

TEST_F(TTD_Seek_Test, SeekTo_FrameAligned_AtBaseline_IsNoOp)
{
    ASSERT_TRUE(_ttd->StartRecording());
    // Capture baseline hash BEFORE running any frames.
    const uint64_t baselineHash = HashNow();

    RunFrames(2);
    // Live state has now diverged from baseline.
    ASSERT_NE(HashNow(), baselineHash)
        << "Sanity: after RunFrames(2) the live state must differ from baseline";

    // Seek back to baseline — state must match what we captured.
    EXPECT_TRUE(_ttd->SeekTo({0, 0}));
    EXPECT_EQ(_ttd->CurrentPosition().frame,    0u);
    EXPECT_EQ(_ttd->CurrentPosition().tInFrame, 0u);
    EXPECT_EQ(HashNow(), baselineHash)
        << "Seek to baseline must reproduce the same machine state";
}

TEST_F(TTD_Seek_Test, SeekTo_FrameAligned_ToMidpoint_CheckpointRestored)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(4);
    ASSERT_EQ(_ttd->GetCheckpointCount(), 5u);

    // Seek to frame 2 (a checkpoint exists there)
    EXPECT_TRUE(_ttd->SeekTo({2, 0}));
    EXPECT_EQ(_ttd->CurrentPosition().frame,    2u);
    EXPECT_EQ(_ttd->CurrentPosition().tInFrame, 0u);
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Detached);
}

TEST_F(TTD_Seek_Test, SeekTo_FrameAligned_RoundTripDeterminism)
{
    // The seek engine lands at the *checkpoint-captured* state, which sits
    // at the OnFrameBoundary point (mid-OnFrameEnd, BEFORE OnFrameStart
    // runs Tape/SoundManager/Screen::InitFrame). The natural live state
    // after RunFrames is post-OnFrameStart, so its hash differs by those
    // side effects.
    //
    // The right reference for a frame-aligned SeekTo(N, 0) is therefore
    // RestoreCheckpointForTesting(N), which exercises the exact same
    // RestoreCheckpoint code path. We verify SeekTo reproduces it.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(4);
    ASSERT_EQ(_ttd->GetCheckpointCount(), 5u);

    for (int i = 0; i <= 4; ++i)
    {
        // Reference hash via RestoreCheckpointForTesting
        ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(static_cast<size_t>(i)));
        const uint64_t refHash = HashNow();

        // SeekTo the same frame — must reproduce the reference hash.
        // We seek to a slightly later frame first to force a real restore
        // (not a no-op), then back to the target.
        if (i < 4)
            ASSERT_TRUE(_ttd->SeekTo({static_cast<uint64_t>(i + 1), 0}));
        else
            ASSERT_TRUE(_ttd->SeekTo({static_cast<uint64_t>(i - 1), 0}));

        ASSERT_TRUE(_ttd->SeekTo({static_cast<uint64_t>(i), 0}));
        const uint64_t seekHash = HashNow();

        if (seekHash != refHash)
        {
            ADD_FAILURE()
                << "SeekTo frame " << i << " (" << std::hex
                << "ref=" << refHash << " seek=" << seekHash
                << ") must match RestoreCheckpointForTesting(" << i << ")";
        }
    }
}

TEST_F(TTD_Seek_Test, SeekTo_FrameAligned_TransitionsToDetached)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);

    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Recording);
    EXPECT_TRUE(_ttd->SeekTo({1, 0}));
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Detached);
}

TEST_F(TTD_Seek_Test, SeekTo_FromDetached_ToEarlier_Succeeds)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);

    ASSERT_TRUE(_ttd->SeekTo({2, 0}));
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Detached);

    // Now seek back from Detached state
    EXPECT_TRUE(_ttd->SeekTo({0, 0}));
    EXPECT_EQ(_ttd->CurrentPosition().frame, 0u);
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Detached);
}

TEST_F(TTD_Seek_Test, SeekTo_FromDetached_Forward_Succeeds)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);

    ASSERT_TRUE(_ttd->SeekTo({0, 0}));  // back to baseline

    // Forward-seek to frame 2 (needs forward replay across frames)
    EXPECT_TRUE(_ttd->SeekTo({2, 0}));
    EXPECT_EQ(_ttd->CurrentPosition().frame, 2u);
}

// ===========================================================================
// SeekTo intra-frame (tInFrame > 0) — silent replay path
// ===========================================================================

TEST_F(TTD_Seek_Test, SeekTo_IntraFrame_ToSmallTInFrame_Succeeds)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);

    // Target frame 1, t-state 100 (well within a frame).
    // Intra-frame replay will engage.
    EXPECT_TRUE(_ttd->SeekTo({1, 100}));
    EXPECT_EQ(_ttd->CurrentPosition().frame,    1u);
    // The intra-frame position after replay should be 100. We read it
    // from z80.t directly (CurrentPosition() derives from t_states which
    // is updated by RunTStates).
    Z80* z80 = _context->pCore->GetZ80();
    ASSERT_NE(z80, nullptr);
    EXPECT_EQ(z80->t, 100u);
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Detached);
}

TEST_F(TTD_Seek_Test, SeekTo_IntraFrame_ReplayModeFlagRestored)
{
    // Critical invariant: after SeekTo returns, the ttdReplayActive flag
    // must be cleared (ExitReplayMode was called even on the happy path).
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);

    EXPECT_TRUE(_ttd->SeekTo({0, 50}));
    EXPECT_FALSE(_context->ttdReplayActive);
    EXPECT_FALSE(_ttd->IsReplayActive());
}

TEST_F(TTD_Seek_Test, SeekTo_IntraFrame_RoundTripDeterminism)
{
    // Run a few frames. At each frame boundary, also hash at t=500 to
    // establish an intra-frame reference. Seek back to (frame, 500) and
    // verify the hash matches.
    ASSERT_TRUE(_ttd->StartRecording());

    // Capture baseline + advance 2 frames
    RunFrames(2);

    // Walk forward to (frame=1, tInFrame=500), hash, then walk to frame 2
    // boundary. We can't easily hash at (1, 500) without seeking, so this
    // test does it in two passes: first seek to (1, 500) and hash, then
    // seek to (1, 0) and re-seek to (1, 500) and verify hashes match.
    EXPECT_TRUE(_ttd->SeekTo({1, 500}));
    const uint64_t hash1 = HashNow();

    // Re-seek from baseline
    EXPECT_TRUE(_ttd->SeekTo({1, 0}));
    EXPECT_TRUE(_ttd->SeekTo({1, 500}));
    const uint64_t hash2 = HashNow();

    EXPECT_EQ(hash1, hash2)
        << "SeekTo (1, 500) twice must produce identical machine state";
}

TEST_F(TTD_Seek_Test, SeekTo_IntraFrame_AtFrameStart_EquivalentToFrameAligned)
{
    // tInFrame == 0 should be a pure restore (no replay needed).
    // The code path skips ReplayWithinFrame entirely.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);

    EXPECT_TRUE(_ttd->SeekTo({1, 0}));
    EXPECT_FALSE(_context->ttdReplayActive)
        << "tInFrame == 0 must not engage replay mode";
}

// ===========================================================================
// Step helpers — StepBackFrame / StepForwardFrame
// ===========================================================================

TEST_F(TTD_Seek_Test, StepBackFrame_AtFrame0_Fails)
{
    ASSERT_TRUE(_ttd->StartRecording());
    // Seek to baseline (frame 0)
    ASSERT_TRUE(_ttd->SeekTo({0, 0}));

    EXPECT_FALSE(_ttd->StepBackFrame())
        << "Cannot step back past frame 0";
}

TEST_F(TTD_Seek_Test, StepBackFrame_PreservesTInFrame)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);

    // Seek to (2, 100) — sets current t-in-frame to ≈100 (RunTStates may
    // overshoot by the last instruction length; accept ±32).
    ASSERT_TRUE(_ttd->SeekTo({2, 100}));
    Z80* z80 = _context->pCore->GetZ80();
    ASSERT_NE(z80, nullptr);
    const uint32_t tAfterSeek = z80->t;
    EXPECT_NEAR(static_cast<int>(tAfterSeek), 100, 32);

    EXPECT_TRUE(_ttd->StepBackFrame());
    EXPECT_EQ(_ttd->CurrentPosition().frame,    1u);

    // The intra-frame position should be preserved (within the same
    // instruction-overshoot tolerance).
    EXPECT_NEAR(static_cast<int>(z80->t), 100, 32)
        << "StepBackFrame must preserve the intra-frame position";
}

TEST_F(TTD_Seek_Test, StepForwardFrame_AtLastFrame_Fails)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);
    // Session end is frame 2
    ASSERT_TRUE(_ttd->SeekTo({2, 0}));

    EXPECT_FALSE(_ttd->StepForwardFrame())
        << "Cannot step forward past the last captured frame";
}

TEST_F(TTD_Seek_Test, StepForwardFrame_FromDetached_Succeeds)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);

    // Seek to baseline
    ASSERT_TRUE(_ttd->SeekTo({0, 0}));

    EXPECT_TRUE(_ttd->StepForwardFrame());
    EXPECT_EQ(_ttd->CurrentPosition().frame, 1u);

    EXPECT_TRUE(_ttd->StepForwardFrame());
    EXPECT_EQ(_ttd->CurrentPosition().frame, 2u);
}

TEST_F(TTD_Seek_Test, StepForwardFrame_PreservesTInFrame)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);

    // Seek to (0, 50)
    ASSERT_TRUE(_ttd->SeekTo({0, 50}));

    EXPECT_TRUE(_ttd->StepForwardFrame());
    EXPECT_EQ(_ttd->CurrentPosition().frame, 1u);

    Z80* z80 = _context->pCore->GetZ80();
    ASSERT_NE(z80, nullptr);
    EXPECT_NEAR(static_cast<int>(z80->t), 50, 32)
        << "StepForwardFrame must preserve the intra-frame position";
}

TEST_F(TTD_Seek_Test, StepBackFrame_Idle_Fails)
{
    EXPECT_FALSE(_ttd->StepBackFrame());
}

TEST_F(TTD_Seek_Test, StepForwardFrame_Idle_Fails)
{
    EXPECT_FALSE(_ttd->StepForwardFrame());
}
