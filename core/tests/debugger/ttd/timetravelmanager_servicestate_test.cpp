/// @file timetravelmanager_servicestate_test.cpp
/// @brief Service registers a restore must bring back, whole machine (state
/// registry gaps 5-9, 14, 16; engine decisions 34, 37): the keyboard matrix,
/// the Scorpion turbo latch, the SMUC board, the ZX-Evo AVR's volatile bytes
/// on TS-Conf, a pending NMI. Each test records a few frames, changes the live
/// state, restores a checkpoint and checks the state of that checkpoint came
/// back, not the live one.
///
/// Each case boots a model from ROM and records a few frames: slower than the
/// 50 ms guideline, one acceptance check per item.

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/atm/evoavr.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"
#include "emulator/ports/models/portdecoder_tsconf.h"
#include "emulator/video/ulacontention.h"

class TimeTravelManager_ServiceState_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;

    void Start(const char* model)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << model;
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelManager;
        _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
        ASSERT_TRUE(_ttd->StartRecording());
    }

    /// Run one frame and return the index of the checkpoint it recorded
    size_t Frame()
    {
        _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        return _ttd->GetCheckpointCount() - 1;
    }

    void TearDown() override
    {
        if (_ttd)
            _ttd->StopRecording();
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
};

/// Gap 5: the matrix is the keyboard's state, in every checkpoint
TEST_F(TimeTravelManager_ServiceState_Test, KeyboardMatrixComesBackFromTheCheckpoint)
{
    ASSERT_NO_FATAL_FAILURE(Start("PENTAGON"));
    Keyboard* keyboard = _context->pKeyboard;
    const size_t released = Frame();
    keyboard->PressKey(ZXKEY_A);
    const size_t held = Frame();
    keyboard->ReleaseKey(ZXKEY_A);
    Frame();
    ASSERT_EQ(keyboard->HandlePortIn(0xFDFE) & 0x01, 0x01) << "A is up live";

    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(held));
    EXPECT_EQ(keyboard->HandlePortIn(0xFDFE) & 0x01, 0x00) << "A held at that checkpoint";
    keyboard->ReleaseKey(ZXKEY_A);
    EXPECT_EQ(keyboard->HandlePortIn(0xFDFE) & 0x01, 0x01) << "the pressed-key count came back too: one release frees it";

    keyboard->PressKey(ZXKEY_A);
    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(released));
    EXPECT_EQ(keyboard->HandlePortIn(0xFDFE), 0xFF) << "no key at that checkpoint";
}

/// Gap 6: the Turbo+ latch is not the clock (it stays set through the /INT drop)
TEST_F(TimeTravelManager_ServiceState_Test, ScorpionTurboLatchAndItsHookComeBack)
{
    ASSERT_NO_FATAL_FAILURE(Start("SCORPION"));
    auto* scorpion = dynamic_cast<PortDecoder_Scorpion256*>(_context->pPortDecoder);
    ASSERT_NE(scorpion, nullptr);
    Z80* z80 = _context->pCore->GetZ80();

    const size_t slow = Frame();
    scorpion->DecodePortIn(0x7FFD, 0x0000);   // the #7FFD read family sets the latch
    const size_t fast = Frame();
    ASSERT_EQ(_context->emulatorState.scorpion_turbo, 1);
    scorpion->DecodePortIn(0x1FFD, 0x0000);   // the #1FFD read family clears it
    Frame();
    ASSERT_EQ(_context->emulatorState.scorpion_turbo, 0);

    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(fast));
    EXPECT_EQ(_context->emulatorState.scorpion_turbo, 1);
    EXPECT_EQ(z80->GetMachineStepHook(), scorpion) << "the /INT-pulse hook follows the latch";

    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(slow));
    EXPECT_EQ(_context->emulatorState.scorpion_turbo, 0);
    EXPECT_EQ(z80->GetMachineStepHook(), nullptr);
}

/// Gap 8: the SMUC latches, IDE window registers and serial link
TEST_F(TimeTravelManager_ServiceState_Test, SmucLatchesAndLinkComeBack)
{
    ASSERT_NO_FATAL_FAILURE(Start("SCORPION"));
    auto* scorpion = dynamic_cast<PortDecoder_Scorpion256*>(_context->pPortDecoder);
    ASSERT_NE(scorpion, nullptr);
    EmulatorState& state = _context->emulatorState;

    state.pFFBA = 0x80;
    state.p7FBA = 0x40;
    scorpion->GetSmucIdeRegs()[3] = 0x5A;
    SMUCNvram::LinkState link = scorpion->GetSMUCNvram().GetLinkState();
    link.mode = 2;          // NV_ADR: an address byte is coming in
    link.bitCount = 5;
    link.data = 0xA3;
    scorpion->GetSMUCNvram().SetLinkState(link);
    const size_t mid = Frame();

    state.pFFBA = 0;
    state.p7FBA = 0;
    scorpion->GetSmucIdeRegs()[3] = 0;
    scorpion->GetSMUCNvram().ResetSerialLinkState();
    Frame();

    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(mid));
    EXPECT_EQ(state.pFFBA, 0x80) << "routes #DFBA to the clock's data register";
    EXPECT_EQ(state.p7FBA, 0x40);
    EXPECT_EQ(scorpion->GetSmucIdeRegs()[3], 0x5A);
    const SMUCNvram::LinkState back = scorpion->GetSMUCNvram().GetLinkState();
    EXPECT_EQ(back.mode, 2);
    EXPECT_EQ(back.bitCount, 5);
    EXPECT_EQ(back.data, 0xA3);
}

/// Gap 9: TS-Conf carries the ZX-Evo AVR's volatile bytes in a blob of their own
TEST_F(TimeTravelManager_ServiceState_Test, TsConfEvoAvrVolatileBytesComeBack)
{
    ASSERT_NO_FATAL_FAILURE(Start("TSL"));
    auto* tsconf = dynamic_cast<PortDecoder_TSConf*>(_context->pPortDecoder);
    ASSERT_NE(tsconf, nullptr);
    EvoAvr& avr = tsconf->GetEvoAvr();

    avr.SetVolatileState(0x0C, 0x07, 0x03);   // ext type, EEPROM page, EEPROM mode + Caps LED
    const size_t set = Frame();
    avr.SetVolatileState(0x00, 0x00, 0x00);
    Frame();

    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(set));
    uint8_t extType = 0, page = 0, flags = 0;
    avr.GetVolatileState(extType, page, flags);
    EXPECT_EQ(extType, 0x0C);
    EXPECT_EQ(page, 0x07);
    EXPECT_EQ(flags, 0x03);
}

/// Gap 16: an NMI asked for and not taken yet is CPU state; a restore never
/// keeps one the live machine had queued
TEST_F(TimeTravelManager_ServiceState_Test, PendingNmiFollowsTheCheckpoint)
{
    ASSERT_NO_FATAL_FAILURE(Start("PENTAGON"));
    Z80* z80 = _context->pCore->GetZ80();
    const size_t cp = Frame();
    EXPECT_EQ(_ttd->GetCheckpoint(cp)->cpu.nmi_pending, 0);

    z80->SetNmiPending(true);
    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(cp));
    EXPECT_FALSE(z80->IsNmiPending());
}

/// Gap 16: the +2A/+3 gate array's floating-bus byte (what unattached port reads return)
TEST_F(TimeTravelManager_ServiceState_Test, Plus3FloatingBusByteComesBack)
{
    ASSERT_NO_FATAL_FAILURE(Start("PLUS3"));
    UlaContention* ula = _context->pUlaContention;
    ASSERT_NE(ula, nullptr);
    // A running frame latches bytes of its own; a new session's checkpoint 0 is
    // captured on the spot, so it holds exactly the byte set here
    _ttd->StopRecording();
    ula->LatchContendedByte(0x5A);
    ASSERT_TRUE(_ttd->StartRecording());
    ula->LatchContendedByte(0x11);

    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(0));
    EXPECT_EQ(ula->GetLatchedByte(), 0x5A);
}
