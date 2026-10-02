// PLAN #83, real software on the ATM Turbo 2+ (ATM710, the ATM IDE board): NedoOS
// for ATM2 (the floppy kernel of release/osatm2.trd) runs its audio CD player,
// cdplay.com, from the floppy; the player reaches the CD drive on the IDE slave
// through the ATM ports (#FEEF ... #FF0F) and plays, pauses and stops an audio
// track. The floppy is testdata/machines/atm/nedoos-cdplay.scl: the kernel's
// boot, code, reset, term and cmd files, cdplay.com, and an autoexec.bat that
// starts it (testdata/machines/atm/README.md).
//
// Slow (~3 s of host time): boots the ATM BIOS, TR-DOS and NedoOS.

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/keyboard/keyboard.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <gtest/gtest.h>

#include "_helpers/cdtestdisc.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/testpathhelper.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "emulator/io/ide/ata/atapicdrom.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/media/mediamanager.h"
#include "emulator/state/devicestate.h"
#include "pch.h"
#include "stdafx.h"

namespace
{
    std::string ScreenText(EmulatorContext* context)
    {
        std::string text;
        const StateNode lines = DeviceState::VideoText(context);
        if (const StateNode* list = lines.find("lines"))
        {
            for (const StateNode& line : list->items)
                text += line.find("text")->s + "\n";
        }
        return text;
    }

    /// The ATM BIOS menu in the 80-column text mode (atm710_trdos_boot_test.cpp)
    std::string MenuText(EmulatorContext* context)
    {
        EmulatorState& state = context->emulatorState;
        const uint8_t* vp = context->pMemory->RAMPageAddress((state.p7FFD & 0x08) ? 7 : 5);
        std::string result;
        for (uint32_t row = 0; row < 24; row++)
        {
            for (uint32_t n = 0; n < 80; n++)
            {
                const uint32_t at = 0x1C0 + 64 * row + n / 2;
                const uint8_t code = (n % 2 == 0) ? vp[at] : vp[0x2000 + at];
                if (code > 0x20 && code < 0x7F)
                    result += static_cast<char>(code);
            }
        }
        return result;
    }

    void PressDown(Emulator* emulator)
    {
        Keyboard* keyboard = emulator->GetContext()->pKeyboard;
        keyboard->PressKey(ZXKEY_CAPS_SHIFT);
        keyboard->PressKey(ZXKEY_6);
        emulator->RunNFrames(8, true);
        keyboard->ReleaseKey(ZXKEY_6);
        keyboard->ReleaseKey(ZXKEY_CAPS_SHIFT);
        emulator->RunNFrames(8, true);
    }

    void PressEnter(Emulator* emulator)
    {
        Keyboard* keyboard = emulator->GetContext()->pKeyboard;
        keyboard->PressKey(ZXKEY_ENTER);
        emulator->RunNFrames(12, true);
        keyboard->ReleaseKey(ZXKEY_ENTER);
    }
}  // namespace

TEST(ATM710NedoOsCdplay_Test, PlaysPausesAndStopsATrack)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModelAndRAM("atm710-cdplay", "ATM710", 1024, LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    struct Remove
    {
        EmulatorManager* manager;
        std::string id;
        ~Remove() { manager->RemoveEmulator(id); }
    } remove{manager, emulator->GetId()};
    EmulatorContext* context = emulator->GetContext();
    emulator->EnableTurboMode();

    // The ATM board's slave becomes a CD drive with a disc of a data track and two audio tracks
    ASSERT_TRUE(context->pIdeController->SetUnitKind(1, true));
    ScratchFolder discFolder("atm710-cdplay-disc");
    MediaSource source;
    source.path = cdtest::WriteMusicDisc(discFolder.Path(), 2, 6, 75);
    InsertOptions options;
    options.immediate = true;
    MediaResult inserted = context->pMediaManager->Insert("ide0.slave", source, options);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    source.path = TestPathHelper::GetTestDataPath("machines/atm/nedoos-cdplay.scl");
    inserted = context->pMediaManager->Insert("fdd.a", source, options);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;

    // BIOS menu -> SPECTRUM 128 -> 128K menu "TR-DOS": TR-DOS runs "boot", the NedoOS loader
    EmulatorTestHelper::RunUntil(emulator.get(), [&] {
        const std::string menu = MenuText(context);
        return menu.find("SPECTRUM128") != std::string::npos && menu.find("MicroART") != std::string::npos;
    }, 300);
    emulator->RunNFrames(10, true);
    PressDown(emulator.get());
    PressDown(emulator.get());
    PressEnter(emulator.get());
    emulator->RunNFrames(150, true);
    for (int i = 0; i < 4; i++)
        PressDown(emulator.get());
    PressEnter(emulator.get());
    // TR-DOS 5.03 at its A> prompt: RUN (the R key in K mode) runs "boot"
    emulator->RunNFrames(100, true);
    Keyboard* keyboard = context->pKeyboard;
    keyboard->PressKey(ZXKEY_R);
    emulator->RunNFrames(6, true);
    keyboard->ReleaseKey(ZXKEY_R);
    emulator->RunNFrames(6, true);
    PressEnter(emulator.get());

    auto shows = [&](const std::string& needle) { return ScreenText(context).find(needle) != std::string::npos; };
    EmulatorTestHelper::RunUntil(emulator.get(), [&] { return shows("Track 03 [AUDIO]"); }, 3000, 25);
    ASSERT_TRUE(shows("Audio CD Player")) << ScreenText(context);
    ASSERT_TRUE(shows("Track 02 [AUDIO]  Start 00:05:00")) << ScreenText(context);

    AtapiCdrom* cd = static_cast<AtapiCdrom*>(context->pIdeController->Channel().Unit(1));
    DebugKeyboardManager* keys = emulator->GetDebugManager()->GetKeyboardManager();
    ASSERT_NE(keys, nullptr);
    keys->TypeText("2");
    EmulatorTestHelper::RunUntil(emulator.get(), [&] { return cd->Audio().PeekStatus() == CdAudioStatus::Playing; }, 300, 5);
    ASSERT_EQ(cd->Audio().PeekStatus(), CdAudioStatus::Playing) << ScreenText(context);
    EXPECT_EQ(cd->Audio().State().playStartLba, 225u);
    emulator->RunNFrames(75, true);
    EXPECT_TRUE(shows("[PLAYING] Track: 02 / 03")) << ScreenText(context);
    EXPECT_TRUE(shows("Time:   00:01 / 00:08")) << ScreenText(context);

    keys->TypeText(" ");
    EmulatorTestHelper::RunUntil(emulator.get(), [&] { return cd->Audio().PeekStatus() == CdAudioStatus::Paused; }, 300, 5);
    ASSERT_EQ(cd->Audio().PeekStatus(), CdAudioStatus::Paused) << ScreenText(context);
    keys->TypeText(" ");
    EmulatorTestHelper::RunUntil(emulator.get(), [&] { return cd->Audio().PeekStatus() == CdAudioStatus::Playing; }, 300, 5);
    EXPECT_EQ(cd->Audio().PeekStatus(), CdAudioStatus::Playing) << ScreenText(context);
    keys->TypeText("s");
    EmulatorTestHelper::RunUntil(emulator.get(), [&] { return cd->Audio().PeekStatus() == CdAudioStatus::Idle; }, 300, 5);
    EXPECT_EQ(cd->Audio().PeekStatus(), CdAudioStatus::Idle) << ScreenText(context);

}
