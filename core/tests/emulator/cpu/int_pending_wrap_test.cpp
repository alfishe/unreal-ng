#include "stdafx.h"
#include "pch.h"

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

/// Regression tests for the stale INT latch across frame wrap.
///
/// ProcessInterrupts clears cpu.int_pending via "t >= int_end", but when an
/// instruction (typically the INT acceptance itself) carries t across the
/// frame boundary, that clear never fires. The stale flag then delivered a
/// SECOND interrupt in the new frame as soon as the program executed EI -
/// observed as variable 1.5-2x music speedup in EI:HALT-synced IM2 demos
/// (Insult megademo) on Pentagon, where the INT window [71635, 71667) ends
/// only 13 t-states before the 71680 t-state frame wrap.
/// Fix: Core::AdjustFrameCounters drops int_pending on frame wrap.
class IntPendingWrap_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    Core* _core = nullptr;
    Z80* _z80 = nullptr;
    uint32_t _frame = 0;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";

        _core = _emulator->GetContext()->pCore;
        _z80 = _core->GetZ80();
        _frame = _emulator->GetContext()->config.frame;
        ASSERT_EQ(_frame, 71680u);  // Pentagon
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }
};

TEST_F(IntPendingWrap_Test, FrameWrap_ClearsStaleIntPending)
{
    // INT accepted at t=71664 (inside the window), HandleINT added ~19T:
    // t crossed the frame boundary without ever satisfying "t >= int_end"
    _z80->t = _frame + 3;
    _z80->int_pending = true;

    _core->AdjustFrameCounters();

    EXPECT_EQ(_z80->t, 3u) << "t must wrap by exactly one frame";
    EXPECT_FALSE(_z80->int_pending)
        << "Stale INT latch must not survive the frame wrap - it would deliver "
           "a second interrupt after the program's next EI";
}

TEST_F(IntPendingWrap_Test, NoWrap_LeavesIntPendingUntouched)
{
    // Mid-frame, inside the INT window: the request must stay pending
    // (program may still be in DI and accept later within the window)
    _z80->t = 71640;  // Inside [71635, 71667)
    _z80->int_pending = true;

    _core->AdjustFrameCounters();  // t < frame: early-return, no changes

    EXPECT_EQ(_z80->t, 71640u);
    EXPECT_TRUE(_z80->int_pending) << "In-window INT request must not be dropped mid-frame";
}
