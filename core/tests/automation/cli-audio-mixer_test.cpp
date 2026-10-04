/// @file cli-audio-mixer_test.cpp
/// @brief CLI `mixer` (cli-audio-mixer.h): the table shows what the host speakers get - held or playing, the
/// holders by reason and the counters - from DeviceState::AudioMixer on a real machine. No CLI socket.

#include <gtest/gtest.h>

#include <string>

#include "../../automation/cli/src/commands/cli-audio-mixer.h"
#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"

TEST(CliAudioMixer_Test, TableShowsHostOutputHolders)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    SoundManager* sound = context->pSoundManager;
    ASSERT_NE(sound, nullptr);

    std::string table = CliAudioMixer::Table(DeviceState::AudioMixer(context), "\n");
    EXPECT_NE(table.find("host output: playing (holders: direct_run 0, ttd_replay 0, turbo 0; stale cleared 0)"),
              std::string::npos)
        << table;

    {
        SoundManager::HostOutputHold replay(sound, SoundManager::HostHoldReason::TtdReplay);
        table = CliAudioMixer::Table(DeviceState::AudioMixer(context), "\n");
        EXPECT_NE(table.find("host output: held (holders: direct_run 0, ttd_replay 1, turbo 0;"), std::string::npos)
            << table;
    }
    table = CliAudioMixer::Table(DeviceState::AudioMixer(context), "\n");
    EXPECT_NE(table.find("host output: playing"), std::string::npos) << table;

    EmulatorTestHelper::CleanupEmulator(emulator);
}
