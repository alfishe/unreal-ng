#include "stdafx.h"
#include "pch.h"

#include <cstring>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/atm/ttdevops2.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/keyboard/pckey.h"
#include "emulator/memory/atm/evoavr.h"
#include "emulator/ports/models/portdecoder_atm3.h"

/// @file ttdevops2_test.cpp
/// @brief PS2-4: the ZX-Evo AVR's PS/2 state (PeripheralId::EvoPs2) under TTD.
/// A physical key is journaled once as its own input event; the checkpoints
/// carry the log; a seek into the middle of a frame replays the journaled key
/// and rebuilds the same log bytes at the same T-state.
///
/// Runtime: one ZX-Evo instance, a few recorded frames (~60 ms: the machine is
/// created with its ROM set)

namespace
{
    std::vector<uint8_t> Blob(EvoAvr& avr)
    {
        std::vector<uint8_t> bytes(sizeof(EvoAvr::Ps2State));
        std::memcpy(bytes.data(), &avr.GetPs2State(), bytes.size());
        return bytes;
    }
}  // namespace

TEST(TTDEvoPs2_Test, JournaledKeyReplaysIntoTheSameLog)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* atm3 = dynamic_cast<PortDecoder_ATM3*>(context->pPortDecoder);
    ASSERT_NE(atm3, nullptr);
    EvoAvr& avr = atm3->GetEvoAvr();
    ASSERT_EQ(context->pKeyboard->GetPs2Sink(), &avr) << "the ZX-Evo decoder attaches its AVR";

    ttd::TimeTravelController* ttd = context->pTimeTravelController;
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    ASSERT_TRUE(ttd->StartRecording());
    EXPECT_TRUE(ttd->GetPeripheralRegistry().IsRegistered(ttd::PeripheralId::EvoPs2));

    emulator->RunNFrames(2);
    emulator->RunTStates(20000);  // the key goes in the middle of a frame
    DebugKeyboardManager* keys = emulator->GetDebugManager()->GetKeyboardManager();
    ASSERT_NE(keys, nullptr);
    keys->PressKey(ZXKEY_EXT_UP);  // one host-level key: matrix Caps Shift + 7, PS/2 E0 75
    const uint64_t keyFrame = context->emulatorState.frame_counter;
    emulator->RunTStates(10000);
    const ttd::TTDTimePoint afterKey{keyFrame, context->pCore->GetZ80()->t};
    const std::vector<uint8_t> live = Blob(avr);
    EXPECT_EQ(avr.GetPs2LogCount(), 2u) << "E0 75 from the physical key, not Shift + 7";

    // Exactly one PcKey event in the journal for the one key
    size_t pcEvents = 0;
    for (const ttd::TTDInputEvent& ev : ttd->GetInputJournal().Events())
    {
        if (ev.kind == ttd::TTDInputKind::PcKey)
        {
            pcEvents++;
            EXPECT_EQ(static_cast<PcKey>(ev.key), PcKey::Up);
            EXPECT_TRUE(ev.pressed);
        }
    }
    EXPECT_EQ(pcEvents, 1u);

    emulator->RunNFrames(2);
    ttd->StopRecording();

    // Back before the key: the log is empty again
    ASSERT_TRUE(ttd->SeekTo({keyFrame, 0}));
    EXPECT_EQ(avr.GetPs2LogCount(), 0u);

    // Into the frame past the key: the replay applies the journaled key
    ASSERT_TRUE(ttd->SeekTo(afterKey));
    EXPECT_EQ(Blob(avr), live) << "the same PS/2 state at the same T-state";

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// PS2-6: machines without a PS/2 controller journal no physical keys and their
/// matrix is the same with or without the physical key code
TEST(TTDEvoPs2_Test, MachinesWithoutPs2PayNothing)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    EXPECT_FALSE(context->pKeyboard->HasPs2Sink());

    ttd::TimeTravelController* ttd = context->pTimeTravelController;
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    ASSERT_TRUE(ttd->StartRecording());
    DebugKeyboardManager* keys = emulator->GetDebugManager()->GetKeyboardManager();
    keys->PressKey(ZXKEY_EXT_UP);
    keys->PressKey("f1");  // a PC key with no ZX equivalent: nothing to do here
    for (const ttd::TTDInputEvent& ev : ttd->GetInputJournal().Events())
        EXPECT_NE(ev.kind, ttd::TTDInputKind::PcKey);
    ttd->StopRecording();

    EmulatorTestHelper::CleanupEmulator(emulator);
}
