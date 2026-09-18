/// @file ttd_replay_mode_test.cpp
/// @brief Phase 2 Item 2 — Silent-replay mode tests.
///
/// Per parent TDD §8.2 + Appendix C. The replay-mode flag
/// (`EmulatorContext::ttdReplayActive`) must suppress:
///   - Breakpoints (Handle* methods return BRK_INVALID)
///   - Analyzers (dispatchFrameStart/End are no-ops)
///   - Keyboard matrix mutation (PressKey/ReleaseKey are no-ops)
///   - Recording capture (CaptureFrame is a no-op)
///   - Video frame refresh notifications (NC_VIDEO_FRAME_REFRESH not posted)
///   - Audio host-buffer submission (SoundManager mute forced true)
///
/// Critical invariant (TDD §8.2 last paragraph): AY/envelope device state
/// must still advance during replay. That's covered by SoundManager's
/// existing mute() contract — handleStep/handleFrameStart are unaffected by
/// the mute flag; only handleFrameEnd's host callback gets zeros. We assert
/// the mute flag is set, not the broader invariant (which is exercised by
/// the divergence corpus in Phase 2 Item 7).

#include <gtest/gtest.h>

#include <cstdint>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/ianalyzer.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/sound/soundmanager.h"

namespace
{

/// Minimal IAnalyzer implementation with public counters, mirroring the
/// pattern in analyzermanager_test.cpp. We define it locally rather than
/// reusing the other test's MockAnalyzer so this file is self-contained.
class CountingAnalyzer : public IAnalyzer
{
public:
    int frameStartCount = 0;
    int frameEndCount = 0;

    void onActivate(AnalyzerManager* mgr) override { (void)mgr; }
    void onDeactivate() override {}
    void onFrameStart() override { ++frameStartCount; }
    void onFrameEnd() override { ++frameEndCount; }
    void onBreakpointHit(uint16_t address, Z80* cpu) override
    {
        (void)address;
        (void)cpu;
    }
    std::string getName() const override { return "CountingAnalyzer"; }
    std::string getUUID() const override { return "test.counting-analyzer"; }
};

} // anonymous namespace

// ===========================================================================
// Fixture
// ===========================================================================

class TTD_ReplayMode_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    Memory* _memory = nullptr;
    FeatureManager* _fm = nullptr;
    CountingAnalyzer* _analyzer = nullptr;  // Raw ptr; owned by AnalyzerManager after RegisterAnalyzer

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        ASSERT_TRUE(_emulator->Init()) << "Failed to initialize emulator";

        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        _memory = _context->pMemory;
        ASSERT_NE(_memory, nullptr);
        _fm = _emulator->GetFeatureManager();
        ASSERT_NE(_fm, nullptr);

        // Enable TTD features
        _fm->setFeature(Features::kDebugMode, true);
        _fm->setFeature(Features::kTimeTravel, true);
        _memory->UpdateFeatureCache();

        // Sanity: replay flag starts false
        EXPECT_FALSE(_context->ttdReplayActive) << "Replay flag should start clear";
        EXPECT_FALSE(_ttd->IsReplayActive());

        // Register a counting analyzer so we can observe dispatch suppression
        ASSERT_NE(_context->pDebugManager, nullptr);
        AnalyzerManager* am = _context->pDebugManager->GetAnalyzerManager();
        ASSERT_NE(am, nullptr);
        _analyzer = new CountingAnalyzer();
        am->registerAnalyzer(_analyzer->getUUID(), std::unique_ptr<IAnalyzer>(_analyzer));
        ASSERT_TRUE(am->activate(_analyzer->getUUID()));
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
};

// ===========================================================================
// Flag mechanics — EnterReplayMode / ExitReplayMode / IsReplayActive
// ===========================================================================

TEST_F(TTD_ReplayMode_Test, EnterReplayMode_SetsFlag)
{
    _ttd->EnterReplayMode();
    EXPECT_TRUE(_context->ttdReplayActive);
    EXPECT_TRUE(_ttd->IsReplayActive());
}

TEST_F(TTD_ReplayMode_Test, ExitReplayMode_ClearsFlag)
{
    _ttd->EnterReplayMode();
    _ttd->ExitReplayMode();
    EXPECT_FALSE(_context->ttdReplayActive);
    EXPECT_FALSE(_ttd->IsReplayActive());
}

TEST_F(TTD_ReplayMode_Test, EnterReplayMode_Idempotent_NestSafe)
{
    // Mute audio before entering replay — this is the "saved" state.
    ASSERT_NE(_context->pSoundManager, nullptr);
    _context->pSoundManager->mute();
    const bool preExistingMute = _context->pSoundManager->isMuted();
    ASSERT_TRUE(preExistingMute);

    // Double-enter: the second call must NOT overwrite the saved mute state.
    _ttd->EnterReplayMode();
    _ttd->EnterReplayMode();

    // Exit once — should clear the flag and restore the saved mute state.
    _ttd->ExitReplayMode();
    EXPECT_FALSE(_context->ttdReplayActive);
    EXPECT_TRUE(_context->pSoundManager->isMuted())
        << "Pre-existing mute state must be preserved across nested EnterReplayMode calls";
}

TEST_F(TTD_ReplayMode_Test, ExitReplayMode_Idempotent_WhenNotInReplay)
{
    // Calling ExitReplayMode without a matching EnterReplayMode is a no-op.
    // The sound manager's current state must be untouched.
    ASSERT_NE(_context->pSoundManager, nullptr);
    _context->pSoundManager->unmute();
    _ttd->ExitReplayMode();
    EXPECT_FALSE(_context->ttdReplayActive);
    EXPECT_FALSE(_context->pSoundManager->isMuted())
        << "Idempotent ExitReplayMode must not flip the mute flag";
}

// ===========================================================================
// SoundManager mute save/restore
// ===========================================================================

TEST_F(TTD_ReplayMode_Test, EnterReplayMode_ForcesMute)
{
    ASSERT_NE(_context->pSoundManager, nullptr);
    _context->pSoundManager->unmute();
    ASSERT_FALSE(_context->pSoundManager->isMuted());

    _ttd->EnterReplayMode();
    EXPECT_TRUE(_context->pSoundManager->isMuted())
        << "Replay must force SoundManager muted so host buffer receives silence";
}

TEST_F(TTD_ReplayMode_Test, ExitReplayMode_RestoresUnmutedState)
{
    ASSERT_NE(_context->pSoundManager, nullptr);
    _context->pSoundManager->unmute();

    _ttd->EnterReplayMode();
    ASSERT_TRUE(_context->pSoundManager->isMuted());

    _ttd->ExitReplayMode();
    EXPECT_FALSE(_context->pSoundManager->isMuted())
        << "ExitReplayMode must restore the pre-replay unmuted state";
}

TEST_F(TTD_ReplayMode_Test, ExitReplayMode_PreservesPreExistingMute)
{
    // User had audio muted before the seek — exit replay must keep it muted.
    ASSERT_NE(_context->pSoundManager, nullptr);
    _context->pSoundManager->mute();

    _ttd->EnterReplayMode();
    _ttd->ExitReplayMode();
    EXPECT_TRUE(_context->pSoundManager->isMuted());
}

// ===========================================================================
// BreakpointManager suppression (parent TDD §8.2 row 1)
// ===========================================================================

TEST_F(TTD_ReplayMode_Test, Breakpoint_HandlePCChange_SkippedDuringReplay)
{
    BreakpointManager* bpm = _context->pDebugManager->GetBreakpointsManager();
    ASSERT_NE(bpm, nullptr);

    // Plant an execution breakpoint at the current PC.
    Z80* cpu = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    ASSERT_NE(cpu, nullptr);
    const uint16_t pc = cpu->pc;
    uint16_t id = bpm->AddExecutionBreakpoint(pc);
    ASSERT_NE(id, BRK_INVALID);

    // Outside replay: the breakpoint fires.
    bpm->ClearLastTriggeredBreakpoint();
    uint16_t hit = bpm->HandlePCChange(pc);
    EXPECT_EQ(hit, id) << "Breakpoint must fire when not in replay mode";

    // During replay: the breakpoint is suppressed.
    _ttd->EnterReplayMode();
    bpm->ClearLastTriggeredBreakpoint();
    hit = bpm->HandlePCChange(pc);
    EXPECT_EQ(hit, BRK_INVALID)
        << "Breakpoint must NOT fire during replay (parent TDD §8.2)";

    _ttd->ExitReplayMode();

    // After exit: the breakpoint fires again.
    bpm->ClearLastTriggeredBreakpoint();
    hit = bpm->HandlePCChange(pc);
    EXPECT_EQ(hit, id) << "Breakpoint must fire again after replay exits";
}

TEST_F(TTD_ReplayMode_Test, Breakpoint_HandleMemoryWrite_SkippedDuringReplay)
{
    BreakpointManager* bpm = _context->pDebugManager->GetBreakpointsManager();
    ASSERT_NE(bpm, nullptr);

    const uint16_t addr = 0x8000;
    uint16_t id = bpm->AddMemWriteBreakpoint(addr);
    ASSERT_NE(id, BRK_INVALID);

    // Outside replay: fires.
    EXPECT_EQ(bpm->HandleMemoryWrite(addr), id);

    // During replay: suppressed.
    _ttd->EnterReplayMode();
    EXPECT_EQ(bpm->HandleMemoryWrite(addr), BRK_INVALID)
        << "Memory-write breakpoint must NOT fire during replay";

    _ttd->ExitReplayMode();
    EXPECT_EQ(bpm->HandleMemoryWrite(addr), id);
}

// ===========================================================================
// AnalyzerManager suppression (parent TDD §8.2 row 5)
// ===========================================================================

TEST_F(TTD_ReplayMode_Test, Analyzer_dispatchFrameStart_NoOpDuringReplay)
{
    AnalyzerManager* am = _context->pDebugManager->GetAnalyzerManager();
    ASSERT_NE(am, nullptr);

    // Outside replay: dispatch advances the counter.
    am->dispatchFrameStart();
    am->dispatchFrameEnd();
    EXPECT_EQ(_analyzer->frameStartCount, 1);
    EXPECT_EQ(_analyzer->frameEndCount, 1);

    // During replay: dispatch is a no-op.
    _ttd->EnterReplayMode();
    am->dispatchFrameStart();
    am->dispatchFrameStart();
    am->dispatchFrameEnd();
    EXPECT_EQ(_analyzer->frameStartCount, 1)
        << "dispatchFrameStart must be suppressed during replay";
    EXPECT_EQ(_analyzer->frameEndCount, 1)
        << "dispatchFrameEnd must be suppressed during replay";

    _ttd->ExitReplayMode();

    // After exit: dispatch advances again.
    am->dispatchFrameStart();
    EXPECT_EQ(_analyzer->frameStartCount, 2);
}

// ===========================================================================
// DebugKeyboardManager suppression (parent TDD §8.2 row 7)
// ===========================================================================

TEST_F(TTD_ReplayMode_Test, Keyboard_PressKey_BlockedDuringReplay)
{
    DebugKeyboardManager* km = _context->pDebugManager->GetKeyboardManager();
    ASSERT_NE(km, nullptr);

    const ZXKeysEnum key = ZXKEY_SPACE;

    // Outside replay: press lands in the direct-pressed set.
    ASSERT_FALSE(km->IsKeyPressed(key));
    km->PressKey(key);
    EXPECT_TRUE(km->IsKeyPressed(key)) << "PressKey must work outside replay";
    km->ReleaseKey(key);
    EXPECT_FALSE(km->IsKeyPressed(key));

    // During replay: press is blocked.
    _ttd->EnterReplayMode();
    km->PressKey(key);
    EXPECT_FALSE(km->IsKeyPressed(key))
        << "PressKey must NOT mutate state during replay — input journal "
        << "(Phase 2 Item 3) injects recorded events instead";
    _ttd->ExitReplayMode();

    // After exit: press works again.
    km->PressKey(key);
    EXPECT_TRUE(km->IsKeyPressed(key));
    km->ReleaseKey(key);
}

// ===========================================================================
// Capture suppression — OnFrameBoundary is auto-suppressed (state != Recording)
// ===========================================================================

TEST_F(TTD_ReplayMode_Test, OnFrameBoundary_NoNewCheckpoint_InReplayContext)
{
    // This test exists to document an implicit invariant: the suppression
    // of checkpoint capture during replay is *automatic* because EnterReplayMode
    // would only ever be called from a state where OnFrameBoundary is already
    // a no-op (Recording paused, or Detached). We don't change OnFrameBoundary's
    // existing `_state != Recording` early-return — replay is always driven
    // from Detached.
    //
    // What we DO test here: that the timeline count does not grow when
    // OnFrameBoundary is called while replay-active but state is not Recording.
    ASSERT_TRUE(_ttd->StartRecording());
    const size_t before = _ttd->GetCheckpointCount();
    ASSERT_EQ(before, 1u);

    _ttd->StopRecording();  // Idle: history retained
    _ttd->EnterReplayMode();
    _ttd->OnFrameBoundary();  // Must NOT capture — state is Idle
    EXPECT_EQ(_ttd->GetCheckpointCount(), before)
        << "OnFrameBoundary must not capture when state is not Recording, "
        << "regardless of replay flag — this is the checkpoint-capture "
        << "suppression row of Appendix C";

    _ttd->ExitReplayMode();
}
