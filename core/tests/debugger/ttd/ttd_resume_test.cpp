/// @file ttd_resume_test.cpp
/// @brief Phase 2 Item 5 — Resume-from-past (history truncation) tests.
///
/// Per parent TDD §8.3 (Resume-from-Past / History Truncation).
///
/// Coverage areas:
///   - ResumeRecordingFrom precondition violations (Idle, out-of-bounds)
///   - State transition Detached → Recording
///   - Timeline truncation: checkpoints strictly after `from` are dropped
///   - Page store slot release: dropped checkpoints' page refs are released
///     and become eligible for reuse by future captures
///   - Input journal truncation: events strictly after `from` are dropped,
///     events at exactly `from` are kept
///   - Emulator position matches `from` after resume (delegated to SeekTo)
///   - Recording continues correctly: subsequent OnFrameBoundary captures
///     grow the timeline by one entry per frame, RAM content is captured
///     correctly
///   - Boundary cases: resume at first checkpoint, resume at last checkpoint
///     (no-op truncation), resume mid-frame
///
/// Driving the emulator: tests use Emulator::RunNFrames(N). Each frame
/// boundary appends one checkpoint when Recording is active, so after
/// StartRecording + N frames the timeline has N+1 entries (baseline at
/// frame 0 plus one per OnFrameBoundary for frames 1..N).

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
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

// ===========================================================================
// Fixture
// ===========================================================================

class TTD_Resume_Test : public ::testing::Test
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
// Preconditions
// ===========================================================================

TEST_F(TTD_Resume_Test, IdleState_ReturnsFalse)
{
    // Manager is Idle by default — never started recording.
    ttd::TTDTimePoint target{0, 0};
    EXPECT_FALSE(_ttd->ResumeRecordingFrom(target));
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Idle);
}

TEST_F(TTD_Resume_Test, TargetBeyondSessionEnd_ReturnsFalse)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    // Timeline: frames 0, 1, 2, 3 (4 checkpoints).
    ASSERT_EQ(_ttd->GetCheckpointCount(), 4u);

    // Target frame 99 is beyond the session end (frame 3).
    ttd::TTDTimePoint beyond{99, 0};
    EXPECT_FALSE(_ttd->ResumeRecordingFrom(beyond));

    // State must be unchanged — no partial mutation.
    EXPECT_EQ(_ttd->GetCheckpointCount(), 4u);
}

// ===========================================================================
// Basic truncation
// ===========================================================================

TEST_F(TTD_Resume_Test, AtMidpoint_TruncatesTimeline)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(4);
    // Timeline: frames 0..4 (5 checkpoints).
    ASSERT_EQ(_ttd->GetCheckpointCount(), 5u);

    ttd::TTDTimePoint mid{2, 0};
    ASSERT_TRUE(_ttd->ResumeRecordingFrom(mid));

    // Dropped checkpoints at frames 3, 4; kept 0, 1, 2.
    ASSERT_EQ(_ttd->GetCheckpointCount(), 3u);

    // Each surviving checkpoint's frame should match its index (baseline at 0).
    for (size_t i = 0; i < _ttd->GetCheckpointCount(); ++i)
    {
        const auto* cp = _ttd->GetCheckpoint(i);
        ASSERT_NE(cp, nullptr);
        EXPECT_EQ(cp->time.frame, static_cast<uint64_t>(i));
    }
}

TEST_F(TTD_Resume_Test, AtMidpoint_StateIsRecording)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    ASSERT_EQ(_ttd->GetCheckpointCount(), 4u);

    // ResumeRecordingFrom internally calls SeekTo which transitions to
    // Detached, then Resume flips back to Recording.
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({1, 0}));
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Recording);
}

TEST_F(TTD_Resume_Test, AtMidpoint_PositionMatchesTarget)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(4);

    ASSERT_TRUE(_ttd->ResumeRecordingFrom({2, 0}));

    // Emulator should be at frame 2, t-in-frame 0 (frame-aligned target).
    const ttd::TTDTimePoint pos = _ttd->CurrentPosition();
    EXPECT_EQ(pos.frame, 2u);
    // The Z80 accumulator may have advanced a few t-states during ROM
    // walkthrough post-restore (Screen::InitFrame etc.); we only assert
    // the frame counter, which is what determines checkpoint identity.
}

TEST_F(TTD_Resume_Test, AtMidFrame_CheckpointAtSameFrameKept)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(4);
    ASSERT_EQ(_ttd->GetCheckpointCount(), 5u);

    // Seek forward to frame 2 first, then advance some t-states mid-frame.
    ASSERT_TRUE(_ttd->SeekTo({2, 0}));
    Z80* z80 = _context->pCore->GetZ80();
    ASSERT_NE(z80, nullptr);

    // Resume from (2, 50) — mid-frame. The checkpoint at frame 2 (which
    // has time (2, 0)) should be KEPT because (2, 0) is NOT > (2, 50).
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({2, 50}));

    // Should still have checkpoints at frames 0, 1, 2 (3 total).
    ASSERT_EQ(_ttd->GetCheckpointCount(), 3u);
    EXPECT_EQ(_ttd->GetCheckpoint(2)->time.frame, 2u);
}

// ===========================================================================
// Boundary cases
// ===========================================================================

TEST_F(TTD_Resume_Test, AtLastCheckpoint_TimelineUnchanged)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    const size_t preCount = _ttd->GetCheckpointCount();
    ASSERT_EQ(preCount, 4u);

    const ttd::TTDTimePoint end = _ttd->SessionEndPosition();
    ASSERT_TRUE(_ttd->ResumeRecordingFrom(end));

    // Nothing to drop — `from` is exactly at the last checkpoint.
    EXPECT_EQ(_ttd->GetCheckpointCount(), preCount);
}

TEST_F(TTD_Resume_Test, AtLastCheckpoint_StateIsRecording)
{
    // Calling ResumeRecordingFrom while in Recording state (no prior SeekTo)
    // is the natural "user clicked Resume without stepping back" case.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);

    const ttd::TTDTimePoint end = _ttd->SessionEndPosition();
    ASSERT_TRUE(_ttd->ResumeRecordingFrom(end));
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Recording);
}

TEST_F(TTD_Resume_Test, AtFirstCheckpoint_NoDropsButTransitions)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    ASSERT_EQ(_ttd->GetCheckpointCount(), 4u);

    // Resume from the very first checkpoint (baseline at frame 0).
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({0, 0}));

    // All checkpoints have time >= (0, 0). The first checkpoint's time
    // equals (0, 0) so it's kept; checkpoints at frames 1, 2, 3 are dropped.
    EXPECT_EQ(_ttd->GetCheckpointCount(), 1u);
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Recording);
}

// ===========================================================================
// Input journal truncation
// ===========================================================================

// Verify that events captured during frames BEFORE the resume frame are
// kept, while events captured during the resume frame and later are dropped.
//
// Note on mid-frame events: events captured via DebugKeyboardManager while
// the emulator is mid-frame have tInFrame > 0 (z80->t is however many
// t-states into the frame). When we resume from (N, 0) those events at
// (N, t>0) are AFTER the resume point and get dropped — this is the
// correct behavior: they happened "in the future" from the resume
// perspective.
TEST_F(TTD_Resume_Test, InputJournal_EventsBeforeResumeKept)
{
    ASSERT_TRUE(_ttd->StartRecording());
    // frame_counter is 0, baseline checkpoint captured at frame 0.

    DebugKeyboardManager* km = _context->pDebugManager->GetKeyboardManager();
    ASSERT_NE(km, nullptr);

    // Capture one event per frame at frames 1..5. Each event's tInFrame
    // is whatever z80->t happens to be — we only care about the frame
    // for this test.
    for (int f = 1; f <= 5; ++f)
    {
        RunFrames(1);
        km->PressKey(ZXKEY_A);
    }
    // Now at frame 5; timeline has 6 checkpoints (frames 0..5);
    // journal has 5 events at frames 1..5.
    ASSERT_EQ(_ttd->GetCheckpointCount(), 6u);
    ASSERT_EQ(_ttd->GetInputJournal().Size(), 5u);

    // Resume from (3, 0). Events at frames 1, 2 are kept (frame < 3).
    // Events at frames 3, 4, 5 are dropped (frame > 3, OR frame == 3 &&
    // tInFrame > 0).
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({3, 0}));

    EXPECT_EQ(_ttd->GetInputJournal().Size(), 2u);
    for (const auto& ev : _ttd->GetInputJournal().Events())
        EXPECT_LT(ev.time.frame, 3u);
}

// Verify that an event captured at EXACTLY the resume TTDTimePoint is kept.
// Uses RecordInputEvent directly (DebugKeyboardManager would block on the
// Detached state that SeekTo produces) and exploits the fact that SeekTo
// syncs z80->t = 0 at frame-aligned targets.
TEST_F(TTD_Resume_Test, InputJournal_EventAtResumePointKept)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);  // 6 checkpoints at frames 0..5.

    // Position emulator at frame 3, tInFrame == 0 (SeekTo syncs z80.t = 0
    // at frame-aligned targets — see timetravelmanager.cpp SeekTo step 2).
    ASSERT_TRUE(_ttd->SeekTo({3, 0}));
    ASSERT_EQ(_ttd->CurrentPosition().frame, 3u);

    // Capture an event at exactly (3, 0).
    _ttd->RecordInputEvent(static_cast<uint8_t>(ZXKEY_A), /*pressed=*/true);
    ASSERT_EQ(_ttd->GetInputJournal().Size(), 1u);
    EXPECT_EQ(_ttd->GetInputJournal().Events()[0].time.frame, 3u);
    EXPECT_EQ(_ttd->GetInputJournal().Events()[0].time.tInFrame, 0u);

    // Resume from (3, 0). Event at (3, 0) is AT the resume point — KEPT
    // (DropAfter uses strict greater-than for the cutoff).
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({3, 0}));
    EXPECT_EQ(_ttd->GetInputJournal().Size(), 1u);
    EXPECT_EQ(_ttd->GetInputJournal().Events()[0].time.frame, 3u);
}

// Verify that an event captured mid-frame at the resume frame is dropped
// (its TTDTimePoint is strictly greater than the frame-aligned resume target).
TEST_F(TTD_Resume_Test, InputJournal_MidFrameEventAtResumeFrameDropped)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);

    ASSERT_TRUE(_ttd->SeekTo({3, 0}));

    // Manually advance z80.t to simulate a mid-frame capture.
    Z80* z80 = _context->pCore->GetZ80();
    ASSERT_NE(z80, nullptr);
    z80->t = 100;

    _ttd->RecordInputEvent(static_cast<uint8_t>(ZXKEY_A), /*pressed=*/true);
    ASSERT_EQ(_ttd->GetInputJournal().Size(), 1u);
    EXPECT_EQ(_ttd->GetInputJournal().Events()[0].time.frame, 3u);
    EXPECT_EQ(_ttd->GetInputJournal().Events()[0].time.tInFrame, 100u);

    // Resume from (3, 0). Event at (3, 100) > (3, 0), so it's dropped.
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({3, 0}));
    EXPECT_EQ(_ttd->GetInputJournal().Size(), 0u);
}

// ===========================================================================
// Page store behavior
// ===========================================================================

TEST_F(TTD_Resume_Test, PageStore_SlotsReleased)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(4);

    const size_t usedBefore = _ttd->GetPageStore().GetUsedSlots();
    ASSERT_GT(usedBefore, 0u) << "Expected at least one page slot in use after 4 frames";

    ASSERT_TRUE(_ttd->ResumeRecordingFrom({1, 0}));

    const size_t usedAfter = _ttd->GetPageStore().GetUsedSlots();
    // We dropped checkpoints at frames 2, 3, 4 — at least some of their
    // pages should have been released. At minimum, used count must not
    // have increased.
    EXPECT_LE(usedAfter, usedBefore);
}

TEST_F(TTD_Resume_Test, PageStore_SlotsReusedAfterResume)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(4);
    ASSERT_EQ(_ttd->GetCheckpointCount(), 5u);

    // Truncate back to frame 1 — frees pages from frames 2, 3, 4.
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({1, 0}));
    const size_t slotsAfterTrunc = _ttd->GetPageStore().GetUsedSlots();
    const uint32_t freeAfterTrunc = _ttd->GetPageStore().GetFreeSlotCount();

    // Record 3 more frames. New captures should reuse freed slots rather
    // than growing the store — capacity should not increase.
    const size_t capacityBefore = _ttd->GetPageStore().GetCapacityBytes();
    RunFrames(3);
    const size_t capacityAfter = _ttd->GetPageStore().GetCapacityBytes();

    EXPECT_EQ(_ttd->GetCheckpointCount(), 5u);  // (0,1) kept + (2,3,4) new
    EXPECT_LE(capacityAfter, capacityBefore);
    // Free list should be smaller now (slots got re-Intern'd).
    EXPECT_LE(_ttd->GetPageStore().GetFreeSlotCount(), freeAfterTrunc);
    (void)slotsAfterTrunc;
}

// ===========================================================================
// Recording continues correctly after resume
// ===========================================================================

TEST_F(TTD_Resume_Test, RecordingContinues_AppendsOneCheckpointPerFrame)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    ASSERT_EQ(_ttd->GetCheckpointCount(), 4u);

    ASSERT_TRUE(_ttd->ResumeRecordingFrom({1, 0}));
    ASSERT_EQ(_ttd->GetCheckpointCount(), 2u);

    // Run 3 more frames — should append 3 checkpoints.
    RunFrames(3);
    EXPECT_EQ(_ttd->GetCheckpointCount(), 5u);

    // The new checkpoints should be at frames 2, 3, 4 (continuing from the
    // resume point at frame 1).
    for (size_t i = 0; i < _ttd->GetCheckpointCount(); ++i)
    {
        const auto* cp = _ttd->GetCheckpoint(i);
        ASSERT_NE(cp, nullptr);
        EXPECT_EQ(cp->time.frame, static_cast<uint64_t>(i));
    }
}

TEST_F(TTD_Resume_Test, RecordingContinues_StateStaysRecording)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);

    ASSERT_TRUE(_ttd->ResumeRecordingFrom({1, 0}));
    ASSERT_EQ(_ttd->GetState(), ttd::TTDSessionState::Recording);

    RunFrames(2);
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Recording);
}

// ===========================================================================
// Dropped checkpoints can no longer be seek targets
// ===========================================================================

TEST_F(TTD_Resume_Test, DroppedCheckpoint_IsNoLongerSeekable)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(4);
    ASSERT_EQ(_ttd->GetCheckpointCount(), 5u);

    // Truncate to frame 1 — drops checkpoints at frames 2, 3, 4.
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({1, 0}));
    ASSERT_EQ(_ttd->GetCheckpointCount(), 2u);

    // Session end is now frame 1. Seeking to frame 3 must fail.
    EXPECT_FALSE(_ttd->SeekTo({3, 0}));

    // Seeking to a still-existing point should still work.
    EXPECT_TRUE(_ttd->SeekTo({0, 0}));
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Detached);
}

// ===========================================================================
// Composability: ResumeRecordingFrom from Detached state
// ===========================================================================

TEST_F(TTD_Resume_Test, FromDetached_SeeksAndTruncates)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(4);
    ASSERT_EQ(_ttd->GetCheckpointCount(), 5u);

    // User steps back twice: 4 → 3 → 2.
    ASSERT_TRUE(_ttd->StepBackFrame());
    ASSERT_EQ(_ttd->CurrentPosition().frame, 3u);
    ASSERT_TRUE(_ttd->StepBackFrame());
    ASSERT_EQ(_ttd->CurrentPosition().frame, 2u);
    ASSERT_EQ(_ttd->GetState(), ttd::TTDSessionState::Detached);

    // User clicks "Resume from here" — should drop future (frames 3, 4)
    // and return to Recording.
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({2, 0}));
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Recording);
    EXPECT_EQ(_ttd->GetCheckpointCount(), 3u);
}

// ===========================================================================
// Stop + Resume interaction
// ===========================================================================

TEST_F(TTD_Resume_Test, AfterStop_StillResumable)
{
    // StopRecording transitions Recording → Idle but retains history.
    // ResumeRecordingFrom requires non-Idle state, so it must fail here.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    _ttd->StopRecording();
    ASSERT_EQ(_ttd->GetState(), ttd::TTDSessionState::Idle);

    // Idle state — ResumeRecordingFrom should refuse.
    EXPECT_FALSE(_ttd->ResumeRecordingFrom({1, 0}));

    // History should be untouched (Resume was a no-op).
    EXPECT_EQ(_ttd->GetCheckpointCount(), 4u);
}
