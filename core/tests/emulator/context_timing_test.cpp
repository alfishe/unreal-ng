#include "stdafx.h"
#include "pch.h"

#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"

/// Tests for the EmulatorContext timing helpers.
///
/// CONFIG stores RASTER BASE timings (Pentagon: 71680 T-states per frame, 224 per
/// line; ZX48/128 and Scorpion ZS-256: 69888 / 224). The Z80 executes those scaled
/// by the composed clock multiplier, which folds together the guest hardware turbo
/// (Scorpion 7 MHz, ATM/Profi 14 MHz) and the host speed control. Reading
/// config.frame raw therefore reports 3.5 MHz figures while the CPU is actually
/// running at 7 or 14 MHz - the bug these helpers exist to prevent.
class ContextTiming_Test : public ::testing::Test
{
protected:
    /// Bare context with Pentagon raster timings and an explicit clock multiplier
    static std::unique_ptr<EmulatorContext> MakeContext(uint32_t frame, uint32_t tLine, uint8_t multiplier)
    {
        auto context = std::make_unique<EmulatorContext>(LoggerLevel::LogError);
        context->config.frame = frame;
        context->config.t_line = tLine;
        context->emulatorState.current_z80_frequency_multiplier = multiplier;
        return context;
    }

    static constexpr uint32_t PENTAGON_FRAME = 71680;
    static constexpr uint32_t ZX128_FRAME = 69888;
    static constexpr uint32_t T_LINE = 224;
};

/// region <Frame T-states>

TEST_F(ContextTiming_Test, FrameTStates_BaseClock_MatchesRaster)
{
    // At x1 the effective frame length is the raster figure itself
    EXPECT_EQ(MakeContext(PENTAGON_FRAME, T_LINE, 1)->GetFrameTStates(), PENTAGON_FRAME);
    EXPECT_EQ(MakeContext(ZX128_FRAME, T_LINE, 1)->GetFrameTStates(), ZX128_FRAME);
}

TEST_F(ContextTiming_Test, FrameTStates_ScalesWithClockMultiplier)
{
    // 7 MHz (x2) and 14 MHz (x4) genuinely give the guest N x T-states per frame
    EXPECT_EQ(MakeContext(PENTAGON_FRAME, T_LINE, 2)->GetFrameTStates(), 143360u);
    EXPECT_EQ(MakeContext(PENTAGON_FRAME, T_LINE, 4)->GetFrameTStates(), 286720u);

    // Scorpion ZS-256 Turbo+ is the 7 MHz case on a 69888 T raster
    EXPECT_EQ(MakeContext(ZX128_FRAME, T_LINE, 2)->GetFrameTStates(), 139776u);
}

TEST_F(ContextTiming_Test, BaseFrameTStates_IgnoresClockMultiplier)
{
    // The raster figure is a hardware property and must not move with the CPU clock
    for (uint8_t multiplier : {1, 2, 4})
    {
        EXPECT_EQ(MakeContext(PENTAGON_FRAME, T_LINE, multiplier)->GetBaseFrameTStates(), PENTAGON_FRAME)
            << "base frame moved at x" << static_cast<int>(multiplier);
    }
}

/// endregion </Frame T-states>

/// region <Line T-states>

TEST_F(ContextTiming_Test, LineTStates_ScalesWhileBaseDoesNot)
{
    auto context = MakeContext(PENTAGON_FRAME, T_LINE, 4);

    EXPECT_EQ(context->GetLineTStates(), 896u);       // 224 x 4
    EXPECT_EQ(context->GetBaseLineTStates(), T_LINE); // unchanged
}

/// endregion </Line T-states>

/// region <INT pulse window>

TEST_F(ContextTiming_Test, IntPulseWindow_ScalesWithClockMultiplier)
{
    auto context = MakeContext(PENTAGON_FRAME, T_LINE, 2);
    context->config.intstart = 7;   // Pentagon INT start (MiSTer-anchored)
    context->config.intlen = 32;

    EXPECT_EQ(context->GetIntStartTState(), 14u);  // 7 x 2
    EXPECT_EQ(context->GetIntEndTState(), 78u);    // (7 + 32) x 2
}

/// endregion </INT pulse window>

/// region <Clock multiplier>

TEST_F(ContextTiming_Test, CpuClockMultiplier_ReportsConfiguredValue)
{
    EXPECT_EQ(MakeContext(PENTAGON_FRAME, T_LINE, 1)->GetCpuClockMultiplier(), 1);
    EXPECT_EQ(MakeContext(PENTAGON_FRAME, T_LINE, 2)->GetCpuClockMultiplier(), 2);
    EXPECT_EQ(MakeContext(PENTAGON_FRAME, T_LINE, 4)->GetCpuClockMultiplier(), 4);
}

TEST_F(ContextTiming_Test, CpuClockMultiplier_UninitialisedStateReportsX1)
{
    // current_z80_frequency_multiplier is 0 until Core initialises it. Callers that
    // poll during start-up (the status bar ticks every 200 ms) must never see a
    // zero-length frame, so the transient is reported as x1
    auto context = MakeContext(PENTAGON_FRAME, T_LINE, 0);

    EXPECT_EQ(context->GetCpuClockMultiplier(), 1);
    EXPECT_EQ(context->GetFrameTStates(), PENTAGON_FRAME);
    EXPECT_EQ(context->GetLineTStates(), T_LINE);
}

TEST_F(ContextTiming_Test, ScaledValuesStayConsistentWithBaseTimesMultiplier)
{
    // The invariant every caller relies on, across the whole supported range
    for (uint8_t multiplier : {0, 1, 2, 4, 8})
    {
        auto context = MakeContext(PENTAGON_FRAME, T_LINE, multiplier);
        const uint32_t effective = context->GetCpuClockMultiplier();

        EXPECT_EQ(context->GetFrameTStates(), context->GetBaseFrameTStates() * effective);
        EXPECT_EQ(context->GetLineTStates(), context->GetBaseLineTStates() * effective);
    }
}

/// endregion </Clock multiplier>
