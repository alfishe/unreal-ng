#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <limits>

#include "emulator/tstaterunbudget.h"

/// The T-state budget of RunNFrames / RunUntilInterrupt / RunUntilCondition must not overflow: the Sprinter at
/// 21 MHz has 71 680 x 6 = 430 080 CPU T-states per frame, so frames x frame length passed 32 bits above 9 986
/// frames and RunNFrames(10000) ran 14 frames. Running 4.3 G T-states for real takes minutes, so these tests drive
/// the budget with a mocked frame loop: a Z80 counter that advances in instruction-sized steps and is rebased at
/// the frame limit exactly as Z80::StepInstruction + the frame boundary do.
class TStateRunBudget_Test : public ::testing::Test
{
protected:
    static constexpr uint32_t kSprinterBaseFrame = 71680;
    static constexpr uint32_t kSprinter21MHzMultiplier = 6;
    static constexpr uint32_t kSprinter21MHzFrame = kSprinterBaseFrame * kSprinter21MHzMultiplier;

    /// The mocked machine: t inside the frame and the frame limit
    uint32_t _t = 0;
    uint32_t _limit = kSprinter21MHzFrame;
    uint64_t _framesCompleted = 0;

    /// One mocked step of `stepT` T-states; true when it closed a frame (t rebased by the limit)
    bool Step(uint32_t stepT)
    {
        _t += stepT;
        if (_t >= _limit)
        {
            _t -= _limit;
            _framesCompleted++;
            return true;
        }
        return false;
    }

    /// RunNFrames' loop over the mocked machine. One step is a quarter frame plus change, so a frame takes
    /// 4-5 steps and the boundary lands at a different place each frame (overshoot as with real opcodes)
    void RunFrames(TStateRunBudget& budget, uint64_t switchAtFrame = 0, uint32_t newMultiplier = 0)
    {
        while (!budget.Reached())
        {
            const uint32_t prevT = _t;
            const uint32_t limitBefore = _limit;
            const bool completed = Step(_limit / 4 + 7);
            if (switchAtFrame && _framesCompleted == switchAtFrame && completed)
            {
                // A clock switch at the boundary: the limit and t are rescaled together (Z80::SetClockMultiplier)
                const uint32_t newLimit = kSprinterBaseFrame * newMultiplier;
                _t = static_cast<uint32_t>(static_cast<uint64_t>(_t) * newLimit / _limit);
                _limit = newLimit;
            }
            budget.Step(prevT, _t, limitBefore, _limit, completed);
        }
    }
};

TEST_F(TStateRunBudget_Test, FrameBudgetAt21MHzIsAbove32Bits)
{
    const TStateRunBudget budget = TStateRunBudget::Frames(kSprinter21MHzFrame, 10001);
    EXPECT_EQ(budget.Target(), uint64_t(kSprinter21MHzFrame) * 10001);
    EXPECT_GT(budget.Target(), uint64_t(std::numeric_limits<uint32_t>::max()));
}

TEST_F(TStateRunBudget_Test, TenThousandFramesAt21MHzRunTenThousandFrames)
{
    TStateRunBudget budget = TStateRunBudget::Frames(_limit, 10001);
    RunFrames(budget);
    // The run ends with the step that crosses the budget: the last frame's boundary
    EXPECT_EQ(_framesCompleted, 10001u);
    EXPECT_GE(budget.Elapsed(), budget.Target());
    EXPECT_LT(budget.Elapsed() - budget.Target(), uint64_t(_limit / 4 + 7));
}

TEST_F(TStateRunBudget_Test, ClockSwitchMidRunKeepsTheFrameCount)
{
    // 21 MHz for 5 000 frames, then 3.5 MHz: still 10 001 frames of emulated time in all
    TStateRunBudget budget = TStateRunBudget::Frames(_limit, 10001);
    RunFrames(budget, 5000, 1);
    EXPECT_EQ(_limit, kSprinterBaseFrame);
    EXPECT_EQ(_framesCompleted, 10001u);
}

TEST_F(TStateRunBudget_Test, PlainStepsAddTheirTStates)
{
    TStateRunBudget budget(1000);
    budget.Step(100, 104, 71680, 71680, false);
    EXPECT_EQ(budget.Elapsed(), 4u);
    // A step that closed the frame: t was rebased by the limit
    budget.Step(71678, 3, 71680, 71680, true);
    EXPECT_EQ(budget.Elapsed(), 9u);
    EXPECT_FALSE(budget.Reached());
}
