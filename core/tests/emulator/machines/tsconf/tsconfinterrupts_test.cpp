// TS-Conf interrupt controller (TSConf implementation-plan phase 2,
// hardware-spec §5): event positions, pulse length, priority, masking, clock.
// The controller is driven directly with frame T-states, as the Z80 drives it.

#include "tsconffixture.h"

class TsConfInterrupts_Test : public TsConfFixture
{
protected:
    TsConfInterrupts& Ints() { return _decoder->GetInterrupts(); }

    void SetMultiplier(uint8_t multiplier) { _context->emulatorState.current_z80_frequency_multiplier = multiplier; }

    /// Frame T-states [from, to) at which /INT is asserted, acknowledging none
    std::vector<uint32_t> AssertedAt(uint32_t from, uint32_t to)
    {
        std::vector<uint32_t> result;
        for (uint32_t t = from; t < to; t++)
            if (Ints().IsIntAsserted(t))
                result.push_back(t);
        return result;
    }
};

/// The decoder installs the controller on the CPU
TEST_F(TsConfInterrupts_Test, InstalledOnTheCpu)
{
    EXPECT_EQ(_z80->GetInterruptSource(), &Ints());
    EXPECT_EQ(_z80->GetMachineStepHook(), &Ints());
}

/// INT-1: after reset one frame INT at tact 1, vector 0xFF, a 32-clock pulse
TEST_F(TsConfInterrupts_Test, INT1_ResetFramePulse)
{
    const std::vector<uint32_t> asserted = AssertedAt(0, 200);
    ASSERT_EQ(asserted.size(), 32u);
    EXPECT_EQ(asserted.front(), 1u);
    EXPECT_EQ(asserted.back(), 32u) << "an EI taking effect at tact 33 misses it";
    EXPECT_TRUE(AssertedAt(200, TsConfInterrupts::kFrameTacts).empty());
}

TEST_F(TsConfInterrupts_Test, INT1_AcknowledgeGivesVectorFFAndClears)
{
    ASSERT_TRUE(Ints().IsIntAsserted(5));
    EXPECT_EQ(Ints().AcknowledgeInterrupt(5), 0xFF);
    EXPECT_FALSE(Ints().IsIntAsserted(6));
}

/// INT-2: the frame INT position; out-of-range positions never match
TEST_F(TsConfInterrupts_Test, INT2_FramePosition)
{
    Reg(TsConfReg::VsIntL, 100);
    Reg(TsConfReg::HsInt, 10);
    EXPECT_FALSE(Ints().IsIntAsserted(22409));
    EXPECT_TRUE(Ints().IsIntAsserted(22410));

    Ints().Reset();
    Reg(TsConfReg::HsInt, 224);
    EXPECT_TRUE(AssertedAt(0, TsConfInterrupts::kFrameTacts).empty());

    Ints().Reset();
    Reg(TsConfReg::HsInt, 0);
    Reg(TsConfReg::VsIntL, 0x40);
    Reg(TsConfReg::VsIntH, 0x01);  // 320
    EXPECT_TRUE(AssertedAt(0, TsConfInterrupts::kFrameTacts).empty());
}

/// INT-3: the line INT, 320 per frame, first at tact 223
TEST_F(TsConfInterrupts_Test, INT3_LineInterrupts)
{
    Reg(TsConfReg::IntMask, TsConfInt::Line);
    std::vector<uint32_t> taken;
    for (uint32_t t = 0; t < TsConfInterrupts::kFrameTacts; t++)
    {
        if (Ints().IsIntAsserted(t))
        {
            EXPECT_EQ(Ints().AcknowledgeInterrupt(t), 0xFD);
            taken.push_back(t);
        }
    }
    ASSERT_EQ(taken.size(), 320u);
    EXPECT_EQ(taken.front(), 223u);
    EXPECT_EQ(taken[1], 447u);
    EXPECT_EQ(taken.back(), TsConfInterrupts::kFrameTacts - 1);
}

/// INT-4: frame and line together: frame first, only the served source clears
TEST_F(TsConfInterrupts_Test, INT4_PriorityAndSelectiveClear)
{
    Reg(TsConfReg::IntMask, TsConfInt::Frame | TsConfInt::Line);
    Reg(TsConfReg::HsInt, 223);
    ASSERT_TRUE(Ints().IsIntAsserted(223));
    EXPECT_EQ(Ints().AcknowledgeInterrupt(223), 0xFF);
    ASSERT_TRUE(Ints().IsIntAsserted(224));
    EXPECT_EQ(Ints().AcknowledgeInterrupt(224), 0xFD);
    EXPECT_FALSE(Ints().IsIntAsserted(225));
}

/// INT-5: masking clears the latch; unmasking does not create one
TEST_F(TsConfInterrupts_Test, INT5_MaskClearsPending)
{
    Reg(TsConfReg::IntMask, TsConfInt::Line);
    ASSERT_TRUE(Ints().IsIntAsserted(300));  // latched at 223, not acknowledged (DI)
    Reg(TsConfReg::IntMask, 0x00);
    Reg(TsConfReg::IntMask, TsConfInt::Line);
    EXPECT_FALSE(Ints().IsIntAsserted(301));
    EXPECT_FALSE(Ints().IsIntAsserted(446));
    EXPECT_TRUE(Ints().IsIntAsserted(447));
}

/// INT-7: at 14 MHz the pulse is 32 CPU clocks = 8 raster tacts
TEST_F(TsConfInterrupts_Test, INT7_PulseIsCpuClocks)
{
    SetMultiplier(4);
    const std::vector<uint32_t> asserted = AssertedAt(0, 400);
    ASSERT_EQ(asserted.size(), 32u);
    EXPECT_EQ(asserted.front(), 4u) << "raster tact 1";
    EXPECT_EQ(asserted.back(), 35u);
}

/// A frame pulse that starts on the last tact runs on into the next frame
TEST_F(TsConfInterrupts_Test, FramePulseCrossesTheFrameEnd)
{
    Reg(TsConfReg::VsIntL, 0x3F);
    Reg(TsConfReg::VsIntH, 0x01);  // line 319
    Reg(TsConfReg::HsInt, 223);
    EXPECT_TRUE(AssertedAt(0, TsConfInterrupts::kFrameTacts - 1).empty());
    EXPECT_TRUE(Ints().IsIntAsserted(TsConfInterrupts::kFrameTacts - 1));
    Ints().OnMachineFrameRollover(TsConfInterrupts::kFrameTacts);
    const std::vector<uint32_t> asserted = AssertedAt(0, 100);
    ASSERT_EQ(asserted.size(), 31u) << "32 clocks in all";
    EXPECT_EQ(asserted.back(), 30u);
}

/// Events on the frame's last tact are not lost at the rollover (the step
/// hook may not have seen that tact)
TEST_F(TsConfInterrupts_Test, RolloverLatchesTheLastLineEvent)
{
    Reg(TsConfReg::IntMask, TsConfInt::Line);
    ASSERT_TRUE(Ints().IsIntAsserted(71600));
    Ints().AcknowledgeInterrupt(71600);
    Ints().OnMachineFrameRollover(TsConfInterrupts::kFrameTacts);
    EXPECT_TRUE(Ints().IsIntAsserted(0)) << "the line event at tact 71679";
}

/// INT-8 (partial, before vdos exists): the output is gated, the latch stays
TEST_F(TsConfInterrupts_Test, INT8_VdosGatesWithoutLosing)
{
    _decoder->GetState().vdos = 1;
    EXPECT_FALSE(Ints().IsIntAsserted(5));
    _decoder->GetState().vdos = 0;
    EXPECT_TRUE(Ints().IsIntAsserted(6));
}

/// TTD-2: the latches live in the TTD blob
TEST_F(TsConfInterrupts_Test, TTD2_LatchesAreState)
{
    ASSERT_TRUE(Ints().IsIntAsserted(10));
    const TsConfState saved = _decoder->GetState();
    Ints().AcknowledgeInterrupt(10);
    EXPECT_FALSE(Ints().IsIntAsserted(11));
    _decoder->GetState() = saved;
    EXPECT_TRUE(Ints().IsIntAsserted(11));
}
