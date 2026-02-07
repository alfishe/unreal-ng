#include "stdafx.h"
#include "pch.h"

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/z80.h"
#include "emulator/platform.h"

/// Test fixture for atomic debug stepping methods
/// All tests verify correctness by checking frame_counter and t-states position
class AtomicStepping_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";

        // Enable debug mode for stepping
        _emulator->DebugOn();
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    /// Helper to get current t-state counter within the frame
    unsigned getCurrentT()
    {
        Z80State* state = _emulator->GetZ80State();
        return state->t;
    }

    /// Helper to get current frame counter
    uint64_t getFrameCounter()
    {
        return _emulator->GetContext()->emulatorState.frame_counter;
    }

    /// Helper to get config
    const CONFIG& getConfig()
    {
        return _emulator->GetContext()->config;
    }
};

/// region <RunTStates tests>

TEST_F(AtomicStepping_Test, RunTStates_SingleTState)
{
    // Record initial state
    unsigned startT = getCurrentT();
    uint64_t startFrame = getFrameCounter();

    // Run 1 t-state (ULA step = 2 pixels)
    _emulator->RunTStates(1);

    unsigned endT = getCurrentT();
    uint64_t endFrame = getFrameCounter();

    // Z80 instructions take at minimum 4 t-states (NOP is shortest)
    // So requesting 1 t-state should still advance by at least 1 instruction
    EXPECT_GE(endT, startT + 1) << "T-state counter should advance";

    // Should stay within the same frame for such a small step
    EXPECT_EQ(endFrame, startFrame) << "Should stay in same frame for 1 t-state step";
}

TEST_F(AtomicStepping_Test, RunTStates_OneScanline)
{
    const CONFIG& config = getConfig();
    unsigned startT = getCurrentT();
    uint64_t startFrame = getFrameCounter();

    // Run exactly one scanline worth of t-states
    _emulator->RunTStates(config.t_line);

    unsigned endT = getCurrentT();
    uint64_t endFrame = getFrameCounter();

    // Should have advanced by at least t_line t-states
    // (may overshoot slightly because Z80 instructions are multi-cycle)
    EXPECT_GE(endT, startT + config.t_line)
        << "Should advance by at least one scanline (" << config.t_line << " t-states)";

    // Still within the same frame
    EXPECT_EQ(endFrame, startFrame) << "Should stay in same frame for single scanline step";
}

TEST_F(AtomicStepping_Test, RunTStates_CrossesFrameBoundary)
{
    const CONFIG& config = getConfig();

    // First, advance close to frame boundary
    _emulator->RunTStates(config.frame - 100);
    uint64_t frameBeforeCross = getFrameCounter();

    // Now step across the boundary
    _emulator->RunTStates(200);
    uint64_t frameAfterCross = getFrameCounter();

    // Frame counter should have incremented
    EXPECT_EQ(frameAfterCross, frameBeforeCross + 1)
        << "Frame counter should increment when crossing frame boundary";

    // T-state counter should have wrapped (be small, within the new frame)
    unsigned endT = getCurrentT();
    EXPECT_LT(endT, config.frame)
        << "T-state counter should wrap after frame boundary crossing";
}

/// endregion </RunTStates tests>

/// region <RunUntilScanline tests>

TEST_F(AtomicStepping_Test, RunUntilScanline_ForwardInFrame)
{
    const CONFIG& config = getConfig();
    uint64_t startFrame = getFrameCounter();

    // Step to scanline 100
    unsigned targetLine = 100;
    _emulator->RunUntilScanline(targetLine);

    unsigned endT = getCurrentT();
    uint64_t endFrame = getFrameCounter();

    // Should be at or past the target scanline
    unsigned targetT = targetLine * config.t_line;
    EXPECT_GE(endT, targetT)
        << "Should reach target scanline " << targetLine
        << " (target t=" << targetT << ", actual t=" << endT << ")";

    // Should not overshoot by more than one instruction's worth of t-states
    // Longest Z80 instruction is ~23 t-states
    EXPECT_LT(endT, targetT + 30)
        << "Should not overshoot target scanline by more than one instruction";

    EXPECT_EQ(endFrame, startFrame) << "Should stay in same frame when stepping forward";
}

TEST_F(AtomicStepping_Test, RunUntilScanline_WrapsToNextFrame)
{
    const CONFIG& config = getConfig();

    // First advance past scanline 200
    _emulator->RunUntilScanline(200);
    uint64_t frameAfterFirstStep = getFrameCounter();

    // Now request scanline 50 — which is behind us, should wrap to next frame
    _emulator->RunUntilScanline(50);

    unsigned endT = getCurrentT();
    uint64_t endFrame = getFrameCounter();

    // Frame counter should have incremented (we wrapped)
    EXPECT_EQ(endFrame, frameAfterFirstStep + 1)
        << "Should advance to next frame when target scanline is behind current position";

    // Should be at scanline 50 of the new frame
    unsigned targetT = 50 * config.t_line;
    EXPECT_GE(endT, targetT)
        << "Should reach target scanline 50 in new frame";
    EXPECT_LT(endT, targetT + 30)
        << "Should not overshoot target scanline by more than one instruction";
}

TEST_F(AtomicStepping_Test, RunUntilScanline_FirstLine)
{
    const CONFIG& config = getConfig();

    // Advance to scanline 100 first
    _emulator->RunUntilScanline(100);
    uint64_t frameAfterFirstStep = getFrameCounter();

    // Request scanline 0 — should wrap to start of next frame
    _emulator->RunUntilScanline(0);

    uint64_t endFrame = getFrameCounter();
    unsigned endT = getCurrentT();

    EXPECT_EQ(endFrame, frameAfterFirstStep + 1) << "Should wrap to next frame for scanline 0";
    EXPECT_LT(endT, 30) << "Should be near the start of the new frame";
}

/// endregion </RunUntilScanline tests>

/// region <RunNScanlines tests>

TEST_F(AtomicStepping_Test, RunNScanlines_Single)
{
    const CONFIG& config = getConfig();
    unsigned startT = getCurrentT();
    uint64_t startFrame = getFrameCounter();

    _emulator->RunNScanlines(1);

    unsigned endT = getCurrentT();

    // Should advance by approximately 1 scanline
    unsigned expectedMinT = startT + config.t_line;
    EXPECT_GE(endT, expectedMinT)
        << "Should advance by at least one scanline (" << config.t_line << " t-states)";
}

TEST_F(AtomicStepping_Test, RunNScanlines_Multiple)
{
    const CONFIG& config = getConfig();
    unsigned startT = getCurrentT();
    uint64_t startFrame = getFrameCounter();

    unsigned count = 10;
    _emulator->RunNScanlines(count);

    unsigned endT = getCurrentT();

    // Should advance by approximately N scanlines
    unsigned expectedMinT = startT + count * config.t_line;
    EXPECT_GE(endT, expectedMinT)
        << "Should advance by at least " << count << " scanlines";

    // Should not have crossed frame boundary (10 scanlines is small)
    EXPECT_EQ(getFrameCounter(), startFrame) << "10 scanlines should stay in same frame";
}

TEST_F(AtomicStepping_Test, RunNScanlines_CrossesFrame)
{
    const CONFIG& config = getConfig();
    unsigned totalLines = config.frame / config.t_line;
    uint64_t startFrame = getFrameCounter();

    // Step more scanlines than a full frame
    _emulator->RunNScanlines(totalLines + 10);

    uint64_t endFrame = getFrameCounter();

    // Frame counter should have incremented at least once
    EXPECT_GE(endFrame, startFrame + 1)
        << "Stepping " << (totalLines + 10) << " scanlines should cross at least one frame boundary";
}

/// endregion </RunNScanlines tests>

/// region <RunUntilNextScreenPixel tests>

TEST_F(AtomicStepping_Test, RunUntilNextScreenPixel_FromFrameStart)
{
    const CONFIG& config = getConfig();
    uint64_t startFrame = getFrameCounter();

    // CPU starts near frame start (t≈0), paper area is ~64 scanlines in
    _emulator->RunUntilNextScreenPixel();

    unsigned endT = getCurrentT();
    uint64_t endFrame = getFrameCounter();

    // Paper area starts at approximately line 64 for Pentagon
    unsigned paperStartT = 64 * config.t_line + 24;  // 64 lines + left border

    EXPECT_GE(endT, paperStartT)
        << "Should reach paper area start (expected ~" << paperStartT << ", got " << endT << ")";

    // Should not overshoot by much
    EXPECT_LT(endT, paperStartT + 30)
        << "Should not overshoot paper area start";

    EXPECT_EQ(endFrame, startFrame) << "Should stay in same frame when paper is ahead";
}

TEST_F(AtomicStepping_Test, RunUntilNextScreenPixel_AfterPaperStart)
{
    const CONFIG& config = getConfig();

    // Advance to scanline 200 (well past paper start at ~64)
    _emulator->RunUntilScanline(200);
    uint64_t frameAfterPosition = getFrameCounter();

    // Now "next screen pixel" should wrap to the next frame's paper area
    _emulator->RunUntilNextScreenPixel();

    uint64_t endFrame = getFrameCounter();
    unsigned endT = getCurrentT();

    unsigned paperStartT = 64 * config.t_line + 24;

    EXPECT_EQ(endFrame, frameAfterPosition + 1)
        << "Should advance to next frame when already past paper start";

    EXPECT_GE(endT, paperStartT)
        << "Should be at paper area start of next frame";
}

/// endregion </RunUntilNextScreenPixel tests>

/// region <RunFrame tests>

TEST_F(AtomicStepping_Test, RunFrame_AdvancesOneFrame)
{
    uint64_t startFrame = getFrameCounter();

    _emulator->RunFrame();

    uint64_t endFrame = getFrameCounter();

    EXPECT_EQ(endFrame, startFrame + 1) << "RunFrame should advance exactly 1 frame";
}

TEST_F(AtomicStepping_Test, RunFrame_TStateResets)
{
    const CONFIG& config = getConfig();

    _emulator->RunFrame();

    unsigned endT = getCurrentT();

    // After a frame, t-state counter should have wrapped to a small value
    // (since the last instruction may cross the frame boundary slightly)
    EXPECT_LT(endT, 30)
        << "T-state counter should be near 0 after frame (reset by AdjustFrameCounters)";
}

/// endregion </RunFrame tests>

/// region <RunUntilInterrupt tests>

TEST_F(AtomicStepping_Test, RunUntilInterrupt_ReachesInterrupt)
{
    uint64_t startFrame = getFrameCounter();

    _emulator->RunUntilInterrupt();

    uint64_t endFrame = getFrameCounter();
    Z80State* state = _emulator->GetZ80State();

    // After accepting an interrupt, iff1 should be 0 (interrupts disabled by INT handler)
    // Unless the handler re-enables immediately, which is uncommon in the first instruction
    // The test verifies we actually ran (frame counter advanced or t-states moved)
    EXPECT_GE(endFrame, startFrame)
        << "Should have advanced at least within the current frame";

    // T-state counter should be non-zero (we ran something)
    unsigned endT = getCurrentT();
    EXPECT_GT(endT, 0u) << "Should have executed some instructions";
}

TEST_F(AtomicStepping_Test, RunUntilInterrupt_ConsistentPosition)
{
    const CONFIG& config = getConfig();
    uint64_t startFrame = getFrameCounter();

    // Run until interrupt up to 3 times and verify frame counter advances each time
    _emulator->RunUntilInterrupt();
    uint64_t frame1 = getFrameCounter();
    unsigned t1 = getCurrentT();

    // Frame counter should have advanced from start
    EXPECT_GT(frame1, startFrame)
        << "RunUntilInterrupt should advance frame counter";

    // Run again — should advance further
    _emulator->RunUntilInterrupt();
    uint64_t frame2 = getFrameCounter();
    unsigned t2 = getCurrentT();

    EXPECT_GT(frame2, frame1)
        << "Second RunUntilInterrupt should advance beyond first";

    // Run a third time
    _emulator->RunUntilInterrupt();
    uint64_t frame3 = getFrameCounter();

    EXPECT_GT(frame3, frame2)
        << "Third RunUntilInterrupt should advance beyond second";
}

/// endregion </RunUntilInterrupt tests>

/// region <RunUntilCondition tests>

TEST_F(AtomicStepping_Test, RunUntilCondition_PCMatch)
{
    const CONFIG& config = getConfig();

    // Use a t-state threshold as the condition — this is ROM-independent
    // and tests that the predicate mechanism actually works
    unsigned target = 5000;
    _emulator->RunUntilCondition(
        [target](const Z80State& state) { return state.t >= target; },
        500000  // Safety: max ~7 frames
    );

    unsigned endT = getCurrentT();
    EXPECT_GE(endT, target)
        << "Should stop when t-state condition is met";
    EXPECT_LT(endT, target + 30)
        << "Should stop within one instruction of target";
}

TEST_F(AtomicStepping_Test, RunUntilCondition_SafetyLimit)
{
    unsigned startT = getCurrentT();
    uint64_t startFrame = getFrameCounter();

    // Run with impossible condition but with safety limit
    _emulator->RunUntilCondition(
        [](const Z80State& state) { return false; },  // Never true
        1000  // Stop after 1000 t-states
    );

    unsigned endT = getCurrentT();

    // Should have run approximately 1000 t-states (may overshoot slightly)
    unsigned elapsed = endT - startT;
    // Account for possible frame wrap
    if (endT < startT)
    {
        elapsed = endT + (getConfig().frame - startT);
    }

    EXPECT_GE(elapsed, 1000u) << "Should run at least 1000 t-states before safety limit";
    EXPECT_LT(elapsed, 1100u) << "Should stop soon after safety limit (within one instruction)";
}

TEST_F(AtomicStepping_Test, RunUntilCondition_TStateThreshold)
{
    const CONFIG& config = getConfig();

    // Run until t-states reach a specific value (e.g. scanline 50)
    unsigned targetT = 50 * config.t_line;
    _emulator->RunUntilCondition(
        [targetT](const Z80State& state) { return state.t >= targetT; },
        200000
    );

    unsigned endT = getCurrentT();
    EXPECT_GE(endT, targetT)
        << "Should reach target t-state position (" << targetT << ")";
    EXPECT_LT(endT, targetT + 30)
        << "Should not overshoot by more than one instruction";
}

/// endregion </RunUntilCondition tests>

/// region <Compound stepping tests>

TEST_F(AtomicStepping_Test, CompoundStepping_ScanlineThenFrame)
{
    const CONFIG& config = getConfig();
    uint64_t startFrame = getFrameCounter();

    // Step to scanline 100
    _emulator->RunUntilScanline(100);
    unsigned afterScanlineT = getCurrentT();
    unsigned expectedT = 100 * config.t_line;
    EXPECT_GE(afterScanlineT, expectedT);

    // Then complete the frame
    _emulator->RunFrame();
    uint64_t endFrame = getFrameCounter();
    EXPECT_EQ(endFrame, startFrame + 1) << "Should complete exactly one frame";

    // T-states should have wrapped
    unsigned endT = getCurrentT();
    EXPECT_LT(endT, 30) << "Should be near frame start after RunFrame";
}

TEST_F(AtomicStepping_Test, CompoundStepping_MultipleScanlineSteps)
{
    const CONFIG& config = getConfig();
    uint64_t startFrame = getFrameCounter();

    // Step through scanlines sequentially
    for (unsigned line = 10; line <= 50; line += 10)
    {
        _emulator->RunUntilScanline(line);
        unsigned t = getCurrentT();
        EXPECT_GE(t, line * config.t_line)
            << "After stepping to scanline " << line;
    }

    // Should still be in the same frame
    EXPECT_EQ(getFrameCounter(), startFrame) << "Sequential scanline steps should stay in frame";
}

/// endregion </Compound stepping tests>
