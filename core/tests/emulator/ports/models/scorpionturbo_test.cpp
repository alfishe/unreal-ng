#include "stdafx.h"
#include "pch.h"

#include <atomic>
#include <memory>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/testwaithelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "emulator/ports/portdecoder.h"

/// @brief Scorpion hardware-turbo tests (hardware-reference 13).
///
/// These run on a REAL emulator instance, not on ScorpionMachineFixture, and the
/// distinction is load-bearing. The assertions are about what happens at a frame
/// boundary, which means driving a whole frame - and a frame is an Emulator-layer
/// operation: Z80FrameCycle() calls OnCPUStep() once per instruction, which
/// dispatches peripherals through MainLoop, and MainLoop is created by
/// Emulator::Init(), never by Core::Init(). Calling Z80FrameCycle() on a
/// Core-only context dereferences a null pMainLoop on the first instruction.
///
/// That crash stayed hidden while these tests lived on the Core-level fixture
/// only because the fixture left config.frame at 0, so the frame loop had a zero
/// T-state budget and executed nothing: the tests passed while testing only the
/// frame-start derivation and never running the frame body.
class ScorpionTurbo_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);

        _emulator = _manager->CreateEmulatorWithModel("", "SCORPION", LoggerLevel::LogError);
        ASSERT_TRUE(_emulator) << "SCORPION instance could not be created";

        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);

        // Host-side only: mutes audio and skips rendering. Every assertion here
        // reads emulated state, and turbo never touches
        // current_z80_frequency_multiplier, so it cannot disturb what is measured.
        _emulator->EnableTurboMode();
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _manager->RemoveEmulator(_emulator->GetId());
            _emulator.reset();
        }
        _context = nullptr;
    }

    /// Run one frame through the path the running emulator actually uses:
    /// MainLoop::Run -> Core::CPUFrameCycle -> Z80::Z80FrameCycle, whose
    /// prologue applies the queued host multiplier and re-derives the frame
    /// geometry. Deliberately NOT Emulator::RunNFrames: that carries its own
    /// frame loop which re-implements the same prologue ("must match
    /// Z80FrameCycle pattern"), so driving the tests through it would assert
    /// against the copy and leave Z80FrameCycle itself uncovered - verified by
    /// mutation (commenting out Z80FrameCycle's ApplyQueuedFrequencyMultiplier
    /// leaves a RunNFrames-driven version of these tests green).
    void CrossFrameBoundary() { _context->pCore->CPUFrameCycle(); }
};

/// @brief The flip-flop doubles the T-states executed per 50 Hz frame and the
///        INT window scales with the frame, so the interrupt rate stays 50 Hz
///        (hardware-reference 13: only the Z80 runs 2x, video/AY/FDC do not)
TEST_F(ScorpionTurbo_Test, TurboFlipFlopDoublesFrameTStates)
{
    EmulatorState& state = _context->emulatorState;
    Z80* z80 = _context->pCore->GetZ80();

    CrossFrameBoundary();
    EXPECT_EQ(state.current_z80_frequency_multiplier, 1) << "power-on default is 3.5 MHz";

    state.scorpion_turbo = 1;
    state.hw_turbo_shift = 1;  // what the decoder strobe sets alongside the flip-flop
    CrossFrameBoundary();
    EXPECT_EQ(state.current_z80_frequency_multiplier, 2);
    EXPECT_EQ(state.current_z80_frequency, state.base_z80_frequency * 2) << "7 MHz reporting";

    state.scorpion_turbo = 0;
    state.hw_turbo_shift = 0;
    CrossFrameBoundary();
    EXPECT_EQ(state.current_z80_frequency_multiplier, 1) << "IN (#1FFD) family restores 3.5 MHz";
}

/// @brief Host speed control and hardware turbo multiply (4x host with turbo
///        runs 8x T-states per frame); dropping turbo returns to the host
///        setting untouched - guest code cannot clobber the host speed menu
TEST_F(ScorpionTurbo_Test, TurboComposesWithHostSpeedMultiplier)
{
    EmulatorState& state = _context->emulatorState;
    Z80* z80 = _context->pCore->GetZ80();

    state.next_z80_frequency_multiplier = 4;
    state.scorpion_turbo = 1;
    state.hw_turbo_shift = 1;  // what the decoder strobe sets alongside the flip-flop
    CrossFrameBoundary();
    EXPECT_EQ(state.current_z80_frequency_multiplier, 8) << "host 4x x turbo 2x";

    state.scorpion_turbo = 0;
    state.hw_turbo_shift = 0;
    CrossFrameBoundary();
    EXPECT_EQ(state.current_z80_frequency_multiplier, 4) << "turbo off returns to the host setting";
    EXPECT_EQ(state.next_z80_frequency_multiplier, 4) << "host intent is preserved across turbo toggles";
}

/// @brief The strobe applies the turbo IMMEDIATELY, mid-frame: the frame
///        length doubles for the frame in progress and the in-frame position is
///        rescaled so the raster instant is preserved. This is what lets the
///        service monitor's IN (#7FFD) + INT-bounded count loop detect the
///        7 MHz clock (profrom-service-monitor-turbo.md)
TEST_F(ScorpionTurbo_Test, TurboStrobeAppliesMidFrame)
{
    EmulatorState& state = _context->emulatorState;
    Z80* z80 = _context->pCore->GetZ80();

    CrossFrameBoundary();  // settle: 1x, geometry derived
    z80->t = 1000;
    EXPECT_EQ(state.current_z80_frequency_multiplier, 1);

    _context->pPortDecoder->DecodePortIn(0x7FFD, 0x8000);  // the turbo-on strobe

    EXPECT_EQ(state.current_z80_frequency_multiplier, 2) << "applied at the strobe, not at the frame boundary";
    EXPECT_EQ(state.hw_turbo_shift_applied, 1);
    EXPECT_EQ(z80->t, 2000u) << "in-frame position rescaled to the 2x T-domain (same raster instant)";

    _context->pPortDecoder->DecodePortIn(0x1FFD, 0x8000);  // the turbo-off strobe

    EXPECT_EQ(state.current_z80_frequency_multiplier, 1);
    EXPECT_EQ(z80->t, 1000u);
}

/// @brief The mid-frame hardware-turbo strobe must post NC_CPU_FREQ_CHANGED:
///        unlike host speed-menu changes it never passes through a frame
///        boundary, so without this message the strobe stays invisible to every
///        event-driven consumer (status bar poll, WebAPI/MCP listeners). The
///        ProfROM monitor applies its exit-path speed change exactly this way
///        (#04CE: IN A,(#7FFD/#1FFD) x2, profrom-service-monitor-turbo.md 3.1)
TEST_F(ScorpionTurbo_Test, TurboStrobePostsCpuFreqChanged)
{
    EmulatorState& state = _context->emulatorState;
    Z80* z80 = _context->pCore->GetZ80();

    CrossFrameBoundary();  // settle: 1x applied at the frame boundary

    std::atomic<int> messages{0};
    std::atomic<uint32_t> lastFreq{0};
    std::atomic<uint8_t> lastMult{0};

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    auto handler = [&messages, &lastFreq, &lastMult](int id, Message* message) {
        (void)id;
        if (auto* payload = message ? dynamic_cast<CPUFreqPayload*>(message->obj) : nullptr)
        {
            lastFreq.store(payload->_frequencyHz);
            lastMult.store(payload->_freqMultiplier);
            messages.fetch_add(1);
        }
    };
    uint64_t handlerId = messageCenter.AddObserver(NC_CPU_FREQ_CHANGED, handler);

    // Notification dispatch is asynchronous (MessageCenter thread), so every
    // wait below goes through TestWait: it returns the moment the predicate
    // holds, and the timeout only bounds the failure case.
    auto waitForMessages = [&](int count) { TestWait::ForAtLeast(messages, count); };

    _context->pPortDecoder->DecodePortIn(0x7FFD, 0x8000);  // the turbo-on strobe
    waitForMessages(1);
    EXPECT_EQ(state.current_z80_frequency_multiplier, 2);
    EXPECT_EQ(lastMult.load(), 2) << "the ON strobe must notify the applied 2x multiplier";
    EXPECT_EQ(lastFreq.load(), state.base_z80_frequency * 2) << "7 MHz reporting";

    _context->pPortDecoder->DecodePortIn(0x1FFD, 0x8000);  // the turbo-off strobe
    waitForMessages(2);
    EXPECT_EQ(state.current_z80_frequency_multiplier, 1);
    EXPECT_EQ(lastMult.load(), 1) << "the OFF strobe must notify the applied 1x multiplier";
    EXPECT_EQ(lastFreq.load(), state.base_z80_frequency) << "3.5 MHz reporting";

    // A strobe that does not change the effective multiplier stays silent
    _context->pPortDecoder->DecodePortIn(0x1FFD, 0x8000);
    EXPECT_TRUE(TestWait::ForExactly(messages, 2)) << "a no-op strobe must not re-notify";

    messageCenter.RemoveObserverById(NC_CPU_FREQ_CHANGED, handlerId);
}
