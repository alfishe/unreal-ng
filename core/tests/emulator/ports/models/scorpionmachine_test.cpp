#include "stdafx.h"
#include "pch.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/notifications.h"

#include "scorpionmachine_test.h"

#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"

/// region <Fixture wiring>

/// @brief The synthetic bundle and pattern fill must be visible through every
///        Z80 window: fixed banks 5/2, default bank 3 = page 0, and the BASIC 128
///        ROM (bundle page 0) at #0000 after the decoder reset.
TEST_F(ScorpionMachine_Test, PowerOnBankMap)
{
    EXPECT_EQ(BankTag(0x0000), 0xC0) << "boot ROM is bundle page 0 (BASIC 128)";
    EXPECT_EQ(BankTag(0x4000), 0x45) << "fixed window #4000 maps RAM page 5";
    EXPECT_EQ(BankTag(0x8000), 0x42) << "fixed window #8000 maps RAM page 2";
    EXPECT_EQ(BankTag(0xC000), 0x40) << "default bank 3 maps RAM page 0";
}

/// @brief WritePort must reach the decoder's #7FFD handler (bank select).
TEST_F(ScorpionMachine_Test, WritePortSelectsBank)
{
    WritePort(0x7FFD, 0x03);
    EXPECT_EQ(BankTag(0xC000), 0x43) << "OUT (#7FFD),03h maps RAM page 3 at #C000";

    // Fixed windows are unaffected by bank paging
    EXPECT_EQ(BankTag(0x4000), 0x45);
    EXPECT_EQ(BankTag(0x8000), 0x42);
}

/// @brief RunTStates executes real Z80 code: a hand-assembled OUT (C),A at #8000
///        (RAM bank 2) selects page 3 through the full CPU -> decoder -> memory path.
TEST_F(ScorpionMachine_Test, ScriptedOutPagingProgram)
{
    // LD BC,#7FFD / LD A,#03 / OUT (C),A / JR $
    static const uint8_t program[] = {0x01, 0xFD, 0x7F, 0x3E, 0x03, 0xED, 0x79, 0x18, 0xFE};
    for (size_t i = 0; i < sizeof(program); i++)
    {
        DirectWrite(static_cast<uint16_t>(0x8000 + i), program[i]);
    }

    Z80* z80 = _core->GetZ80();
    z80->pc = 0x8000;

    RunTStates(1000);

    EXPECT_EQ(BankTag(0xC000), 0x43) << "the scripted OUT (C),A paged RAM page 3 in at #C000";
}

/// @brief Reads of a memory-mapping port that no peripheral answers must return
///        0xFF (floating bus) - pinned here as the pre-Task-4 baseline.
TEST_F(ScorpionMachine_Test, ReadUndecodedPortReturnsHigh)
{
    EXPECT_EQ(ReadPort(0x1FFD), 0xFF);
}

/// @brief The Scorpion power-on border latch is black (hardware-reference 6):
///        v2.9x ROMs never write #FF during boot, so the machine shows a black
///        border until software sets one. Other models keep their white reset.
TEST_F(ScorpionMachine_Test, PowerOnBorderIsBlack)
{
    EXPECT_EQ(_context->pScreen->GetBorderColor(), COLOR_BLACK);
    EXPECT_EQ(_context->emulatorState.border_attr, 0x00);
    EXPECT_EQ(_context->emulatorState.pFE & 0x07, 0x00) << "pFE border bits must match the black latch";
}

/// endregion </Fixture wiring>

/// region <Hardware turbo (hardware-reference 13)>

/// @brief Guest IN A,(C) from #7FFD clocks the turbo flip-flop through the
///        real CPU read path (Z80::in -> PortDecoder::DecodePortIn), exactly
///        like the ROM's own speed-detection / turbo-enable stubs do
TEST_F(ScorpionMachine_Test, ScriptedInSevenFFDSetsTurbo)
{
    // LD BC,#7FFD / IN A,(C) / JR $
    static const uint8_t program[] = {0x01, 0xFD, 0x7F, 0xED, 0x78, 0x18, 0xFE};
    for (size_t i = 0; i < sizeof(program); i++)
    {
        DirectWrite(static_cast<uint16_t>(0x8000 + i), program[i]);
    }

    Z80* z80 = _core->GetZ80();
    z80->pc = 0x8000;

    RunTStates(100);

    EXPECT_EQ(_context->emulatorState.scorpion_turbo, 1) << "the scripted IN A,(C) set the flip-flop";
}

/// @brief The flip-flop doubles the T-states executed per 50 Hz frame and the
///        INT window scales with the frame, so the interrupt rate stays 50 Hz
///        (hardware-reference 13: only the Z80 runs 2x, video/AY/FDC do not)
TEST_F(ScorpionMachine_Test, TurboFlipFlopDoublesFrameTStates)
{
    EmulatorState& state = _context->emulatorState;
    Z80* z80 = _core->GetZ80();

    z80->Z80FrameCycle();
    EXPECT_EQ(state.current_z80_frequency_multiplier, 1) << "power-on default is 3.5 MHz";

    state.scorpion_turbo = 1;
    state.hw_turbo_shift = 1;  // what the decoder strobe sets alongside the flip-flop
    z80->Z80FrameCycle();
    EXPECT_EQ(state.current_z80_frequency_multiplier, 2);
    EXPECT_EQ(state.current_z80_frequency, state.base_z80_frequency * 2) << "7 MHz reporting";

    state.scorpion_turbo = 0;
    state.hw_turbo_shift = 0;
    z80->Z80FrameCycle();
    EXPECT_EQ(state.current_z80_frequency_multiplier, 1) << "IN (#1FFD) family restores 3.5 MHz";
}

/// @brief Host speed control and hardware turbo multiply (4x host with turbo
///        runs 8x T-states per frame); dropping turbo returns to the host
///        setting untouched - guest code cannot clobber the host speed menu
TEST_F(ScorpionMachine_Test, TurboComposesWithHostSpeedMultiplier)
{
    EmulatorState& state = _context->emulatorState;
    Z80* z80 = _core->GetZ80();

    state.next_z80_frequency_multiplier = 4;
    state.scorpion_turbo = 1;
    state.hw_turbo_shift = 1;  // what the decoder strobe sets alongside the flip-flop
    z80->Z80FrameCycle();
    EXPECT_EQ(state.current_z80_frequency_multiplier, 8) << "host 4x x turbo 2x";

    state.scorpion_turbo = 0;
    state.hw_turbo_shift = 0;
    z80->Z80FrameCycle();
    EXPECT_EQ(state.current_z80_frequency_multiplier, 4) << "turbo off returns to the host setting";
    EXPECT_EQ(state.next_z80_frequency_multiplier, 4) << "host intent is preserved across turbo toggles";
}

/// endregion <Hardware turbo (hardware-reference 13)>

/// @brief The strobe applies the turbo IMMEDIATELY, mid-frame: the frame
///        length doubles for the frame in progress and the in-frame position is
///        rescaled so the raster instant is preserved. This is what lets the
///        service monitor's IN (#7FFD) + INT-bounded count loop detect the
///        7 MHz clock (profrom-service-monitor-turbo.md)
TEST_F(ScorpionMachine_Test, TurboStrobeAppliesMidFrame)
{
    EmulatorState& state = _context->emulatorState;
    Z80* z80 = _core->GetZ80();

    z80->Z80FrameCycle();  // settle: 1x, geometry derived
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
TEST_F(ScorpionMachine_Test, TurboStrobePostsCpuFreqChanged)
{
    EmulatorState& state = _context->emulatorState;
    Z80* z80 = _core->GetZ80();

    z80->Z80FrameCycle();  // settle: 1x applied at the frame boundary

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

    // Notification dispatch is asynchronous (MessageCenter thread)
    auto waitForMessages = [&](int count) {
        auto start = std::chrono::steady_clock::now();
        while (messages.load() < count &&
               std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500))
            std::this_thread::sleep_for(std::chrono::microseconds(250));
    };

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
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(messages.load(), 2) << "a no-op strobe must not re-notify";

    messageCenter.RemoveObserverById(NC_CPU_FREQ_CHANGED, handlerId);
}
