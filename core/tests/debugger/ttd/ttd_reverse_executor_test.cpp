/// @file ttd_reverse_executor_test.cpp
/// @brief Tests for TimeTravelManager reverse-execution primitives.
///
/// Phase 4 reverse execution. Tests the three public primitives:
///   - ReverseStepInstructions(n)  — back N M1 cycles
///   - ReverseStepTStates(n)       — back N t-states (lands at nearest M1)
///   - ReverseContinue(pcs)        — back until any PC matches
///
/// Plus the strategy-selection invariant (A_seq vs B_m1list), probe-disarm
/// safety, and serialization round-trip preservation.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <filesystem>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_probe.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

class TTD_Reverse_Executor_Test : public ::testing::Test
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

    /// @brief Current PC of the live Z80.
    uint16_t CurrentPC() const
    {
        if (!_context || !_context->pCore) return 0xFFFF;
        Z80* z80 = _context->pCore->GetZ80();
        return z80 ? z80->pc : 0xFFFF;
    }
};

// ===========================================================================
// State guards
// ===========================================================================

TEST_F(TTD_Reverse_Executor_Test, ReverseStepInstructions_WhileRecording_Refused)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);

    EXPECT_FALSE(_ttd->ReverseStepInstructions(5));

    _ttd->StopRecording();
}

TEST_F(TTD_Reverse_Executor_Test, ReverseStepInstructions_EmptyTimeline_Refused)
{
    EXPECT_FALSE(_ttd->ReverseStepInstructions(1));
    EXPECT_FALSE(_ttd->ReverseStepInstructions(64));
}

TEST_F(TTD_Reverse_Executor_Test, ReverseStepInstructions_AtSessionStart_Refused)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    _ttd->StopRecording();

    // Position at session start
    const uint64_t startFrame = _ttd->GetSessionInfo().sessionStartFrame;
    ASSERT_TRUE(_ttd->SeekTo({startFrame, 0}));

    // Can't go back from origin
    EXPECT_FALSE(_ttd->ReverseStepInstructions(1));
    EXPECT_FALSE(_ttd->ReverseStepInstructions(64));
}

TEST_F(TTD_Reverse_Executor_Test, ReverseStepTStates_WhileRecording_Refused)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);

    EXPECT_FALSE(_ttd->ReverseStepTStates(100));

    _ttd->StopRecording();
}

TEST_F(TTD_Reverse_Executor_Test, ReverseStepTStates_AtSessionStart_Refused)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    _ttd->StopRecording();

    const uint64_t startFrame = _ttd->GetSessionInfo().sessionStartFrame;
    ASSERT_TRUE(_ttd->SeekTo({startFrame, 0}));

    EXPECT_FALSE(_ttd->ReverseStepTStates(1));
    EXPECT_FALSE(_ttd->ReverseStepTStates(100000));
}

TEST_F(TTD_Reverse_Executor_Test, ReverseContinue_WhileRecording_Refused)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);

    auto r = _ttd->ReverseContinue({0x0000});
    EXPECT_FALSE(r.matched);

    _ttd->StopRecording();
}

TEST_F(TTD_Reverse_Executor_Test, ReverseContinue_EmptyBreakpointSet_NoMatch)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);
    _ttd->StopRecording();

    _ttd->SeekTo(_ttd->SessionEndPosition());

    auto r = _ttd->ReverseContinue({});
    EXPECT_FALSE(r.matched);
}

// ===========================================================================
// ReverseStepInstructions — basic operation
// ===========================================================================

TEST_F(TTD_Reverse_Executor_Test, ReverseStepInstructions_N1_MatchesStepBackInstruction)
{
    // After recording + seek to end + ReverseStepInstructions(1), the
    // resulting position must match what StepBackInstruction() would have
    // produced from the same start.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);
    _ttd->StopRecording();

    // Path A: call StepBackInstruction directly.
    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    ASSERT_TRUE(_ttd->StepBackInstruction());
    ttd::TTDTimePoint posA = _ttd->CurrentPosition();

    // Path B: call ReverseStepInstructions(1).
    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    ASSERT_TRUE(_ttd->ReverseStepInstructions(1));
    ttd::TTDTimePoint posB = _ttd->CurrentPosition();

    EXPECT_EQ(posA, posB);
}

TEST_F(TTD_Reverse_Executor_Test, ReverseStepInstructions_N16_PositionMovesBack)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(10);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    const ttd::TTDTimePoint before = _ttd->CurrentPosition();

    ASSERT_TRUE(_ttd->ReverseStepInstructions(16));

    const ttd::TTDTimePoint after = _ttd->CurrentPosition();
    EXPECT_TRUE(after < before)
        << "before=(frame=" << before.frame << ", t=" << before.tInFrame << ") "
        << "after=(frame=" << after.frame << ", t=" << after.tInFrame << ")";
}

TEST_F(TTD_Reverse_Executor_Test, ReverseStepInstructions_N64_Succeeds)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(20);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    EXPECT_TRUE(_ttd->ReverseStepInstructions(64));
}

TEST_F(TTD_Reverse_Executor_Test, ReverseStepInstructions_N256_Succeeds)
{
    // Needs a longer history — 50 frames of HALT loop yields ~50 * 17000 =
    // 850K M1 cycles, well above 256.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(50);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    EXPECT_TRUE(_ttd->ReverseStepInstructions(256));
}

TEST_F(TTD_Reverse_Executor_Test, ReverseStepInstructions_LargerThanHistory_Fails)
{
    // Record just one frame, then try to step back more opcodes than
    // could possibly exist.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);
    _ttd->StopRecording();

    // 100000 is far more than one frame of M1 cycles (~17000 for HALT loop).
    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    EXPECT_FALSE(_ttd->ReverseStepInstructions(100000));
}

TEST_F(TTD_Reverse_Executor_Test, ReverseStepInstructions_N0_NoOp)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    const ttd::TTDTimePoint before = _ttd->CurrentPosition();

    EXPECT_TRUE(_ttd->ReverseStepInstructions(0));

    EXPECT_EQ(_ttd->CurrentPosition(), before);
}

// ===========================================================================
// ReverseStepTStates
// ===========================================================================

TEST_F(TTD_Reverse_Executor_Test, ReverseStepTStates_N1_Succeeds)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    EXPECT_TRUE(_ttd->ReverseStepTStates(1));
}

TEST_F(TTD_Reverse_Executor_Test, ReverseStepTStates_N100_PositionMovesBackByAtLeast100)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);
    _ttd->StopRecording();

    const uint32_t frameT = _context->config.frame;
    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));

    const uint64_t beforeGlobalT =
        static_cast<uint64_t>(_ttd->CurrentPosition().frame) * frameT
        + _ttd->CurrentPosition().tInFrame;

    ASSERT_TRUE(_ttd->ReverseStepTStates(100));

    const uint64_t afterGlobalT =
        static_cast<uint64_t>(_ttd->CurrentPosition().frame) * frameT
        + _ttd->CurrentPosition().tInFrame;

    // The landing M1 must be at globalT <= beforeGlobalT - 100. It may be
    // less (the nearest M1 ≤ target could be a few t-states earlier).
    EXPECT_LE(afterGlobalT, beforeGlobalT - 100);

    // The landing M1 must be within ~23 t-states of the target (longest Z80
    // instruction). Use a conservative bound of 30.
    EXPECT_GT(afterGlobalT, beforeGlobalT - 130)
        << "Landing M1 is too far before target (overcorrection)";
}

TEST_F(TTD_Reverse_Executor_Test, ReverseStepTStates_NearestM1_LandsAtInstructionBoundary)
{
    // After ReverseStepTStates, the landing position must be at an M1
    // boundary (instruction start). We verify this by checking that
    // StepForwardInstruction succeeds from the landing position — if we
    // were mid-instruction, the next M1 would not be reachable by a
    // single-instruction forward step.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    const ttd::TTDTimePoint before = _ttd->CurrentPosition();

    ASSERT_TRUE(_ttd->ReverseStepTStates(500));

    const ttd::TTDTimePoint after = _ttd->CurrentPosition();

    // Position must have moved backward.
    EXPECT_TRUE(after < before);

    // The landing must be at an instruction boundary: StepForwardInstruction
    // must succeed and advance the position.
    ASSERT_TRUE(_ttd->StepForwardInstruction());
    const ttd::TTDTimePoint fwd = _ttd->CurrentPosition();
    EXPECT_TRUE(after < fwd)
        << "StepForwardInstruction did not advance past the reverse landing";
}

TEST_F(TTD_Reverse_Executor_Test, ReverseStepTStates_MultiFrameJump_Succeeds)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(20);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));

    // Step back ~5 frames worth of t-states.
    const uint32_t frameT = _context->config.frame;
    EXPECT_TRUE(_ttd->ReverseStepTStates(static_cast<uint64_t>(frameT) * 5));
}

// ===========================================================================
// ReverseContinue
// ===========================================================================

TEST_F(TTD_Reverse_Executor_Test, ReverseContinue_NoMatchAtAnyPC_ReturnsFalse)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));

    // Pick a PC that's certainly not in the HALT loop's history.
    auto r = _ttd->ReverseContinue({0xFFFE});
    EXPECT_FALSE(r.matched);
}

TEST_F(TTD_Reverse_Executor_Test, ReverseContinue_MatchesCurrentLoopPC)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));

    // The HALT loop sits at a fixed PC. ReverseContinue with that PC as
    // the breakpoint should match somewhere in the recent past.
    const uint16_t loopPC = CurrentPC();
    auto r = _ttd->ReverseContinue({loopPC});

    EXPECT_TRUE(r.matched);
    EXPECT_EQ(r.pc, loopPC);
}

TEST_F(TTD_Reverse_Executor_Test, ReverseContinue_MultipleBreakpoints_MatchesOne)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));

    // Pass several PCs, only one of which is the live loop PC.
    const uint16_t loopPC = CurrentPC();
    auto r = _ttd->ReverseContinue({0x1234, 0x5678, loopPC, 0x9ABC});

    EXPECT_TRUE(r.matched);
    EXPECT_EQ(r.pc, loopPC);
}

// ===========================================================================
// Strategy selection
// ===========================================================================

TEST_F(TTD_Reverse_Executor_Test, StrategySelection_N1_UsesSeqStep)
{
    // For n=1, ReverseStepInstructions should delegate to StepBackInstruction
    // (strategy A_seq). The proof of which path was taken is the final
    // position — both paths must land at the same place, so equivalence
    // with StepBackInstruction is the contract.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    ASSERT_TRUE(_ttd->StepBackInstruction());
    const ttd::TTDTimePoint posA = _ttd->CurrentPosition();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    ASSERT_TRUE(_ttd->ReverseStepInstructions(1));
    const ttd::TTDTimePoint posB = _ttd->CurrentPosition();

    EXPECT_EQ(posA, posB);
}

TEST_F(TTD_Reverse_Executor_Test, StrategySelection_N64_UsesM1List)
{
    // For n=64, B_m1list is used (n > kReverseSeqStepMaxN). Verify the
    // operation succeeds and lands at a plausible position.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(20);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    const ttd::TTDTimePoint end = _ttd->CurrentPosition();

    ASSERT_TRUE(_ttd->ReverseStepInstructions(64));

    const ttd::TTDTimePoint pos = _ttd->CurrentPosition();
    EXPECT_TRUE(pos < end);
}

// ===========================================================================
// Probe safety (no leaked armed state)
// ===========================================================================

TEST_F(TTD_Reverse_Executor_Test, ProbeDisarmed_AfterReverseStepInstructions)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(10);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    ASSERT_TRUE(_ttd->ReverseStepInstructions(16));

    EXPECT_FALSE(_context->ttdProbe.IsArmed());
    EXPECT_EQ(_context->ttdProbe.Hits().size(), 0u);
}

TEST_F(TTD_Reverse_Executor_Test, ProbeDisarmed_AfterReverseStepTStates)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(10);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    ASSERT_TRUE(_ttd->ReverseStepTStates(500));

    EXPECT_FALSE(_context->ttdProbe.IsArmed());
    EXPECT_EQ(_context->ttdProbe.Hits().size(), 0u);
}

TEST_F(TTD_Reverse_Executor_Test, ProbeDisarmed_AfterReverseContinue)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(10);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    const uint16_t loopPC = CurrentPC();
    auto r = _ttd->ReverseContinue({loopPC});
    ASSERT_TRUE(r.matched);

    EXPECT_FALSE(_context->ttdProbe.IsArmed());
    EXPECT_EQ(_context->ttdProbe.Hits().size(), 0u);
}

TEST_F(TTD_Reverse_Executor_Test, ProbeDisarmed_AfterReverseContinue_NoMatch)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(10);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    auto r = _ttd->ReverseContinue({0xFFFE});
    ASSERT_FALSE(r.matched);

    EXPECT_FALSE(_context->ttdProbe.IsArmed());
    EXPECT_EQ(_context->ttdProbe.Hits().size(), 0u);
}

// ===========================================================================
// Serialization round-trip preservation
// ===========================================================================

TEST_F(TTD_Reverse_Executor_Test, SerializationRoundTrip_AfterReverseOp_PreservesPosition)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(10);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    ASSERT_TRUE(_ttd->ReverseStepInstructions(8));
    const ttd::TTDTimePoint posBeforeDump = _ttd->CurrentPosition();

    // Serialize to a temp file, deserialize into a fresh manager state.
    const std::string tmpl =
        (std::filesystem::temp_directory_path() / "ttd_reverse_serialize.bin").string();

    {
        std::ofstream out(tmpl, std::ios::binary);
        std::string err;
        ASSERT_TRUE(_ttd->SerializeSession(out, err)) << err;
    }

    // Read back.
    {
        std::ifstream in(tmpl, std::ios::binary);
        std::string err;
        ASSERT_TRUE(_ttd->DeserializeSession(in, err)) << err;
    }

    std::remove(tmpl.c_str());

    // DeserializeSession resets state to Idle. After a SeekTo, the position
    // must be queryable.
    ASSERT_TRUE(_ttd->SeekTo(posBeforeDump));
    EXPECT_EQ(_ttd->CurrentPosition(), posBeforeDump);
}

// ===========================================================================
// Repeated calls don't accumulate probe hits (idempotent safety)
// ===========================================================================

TEST_F(TTD_Reverse_Executor_Test, RepeatedReverseStep_NoStateAccumulation)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(20);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));

    for (int i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(_ttd->ReverseStepInstructions(8));
        EXPECT_FALSE(_context->ttdProbe.IsArmed()) << "iteration " << i;
        EXPECT_EQ(_context->ttdProbe.Hits().size(), 0u) << "iteration " << i;
    }
}
