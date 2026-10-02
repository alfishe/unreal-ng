// The per-device mixer by name (audiomixer.h; automation audit G13): keys, changes, the reports and the
// capture source the audio tap follows. Every interface (WebAPI /audio/mixer, CLI mixer, Lua / Python
// audio_mixer, MCP audio_mixer) calls these.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "../../../automation/cli/src/commands/cli-audio-mixer.h"
#include "_helpers/emulatortesthelper.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/audiocapture/audiocaptureanalyzer.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/sound/audiomixer.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicestate.h"

TEST(AudioMixerKeys_Test, KeysAndChanges)
{
    AudioSourceType type = AudioSourceType::Custom;
    ASSERT_TRUE(AudioMixer::FromKey("COVOX", type));
    EXPECT_EQ(type, AudioSourceType::COVOX);
    EXPECT_STREQ(AudioMixer::Key(AudioSourceType::AY1_All), "ay1");
    EXPECT_FALSE(AudioMixer::FromKey("piano", type));

    AudioMixer::Change change;
    std::string error;
    ASSERT_TRUE(AudioMixer::ChangeFromStrings("1", "", "0.5", "", change, error)) << error;
    EXPECT_EQ(change.muted, 1);
    EXPECT_EQ(change.solo, -1);
    EXPECT_DOUBLE_EQ(change.volume, 0.5);
    EXPECT_FALSE(AudioMixer::ChangeFromStrings("", "", "1.5", "", change, error));
    EXPECT_FALSE(AudioMixer::ChangeFromStrings("", "", "", "3", change, error)) << "no gain above 0 dB";
}

class AudioMixer_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
    }
    void TearDown() override { EmulatorTestHelper::CleanupEmulator(_emulator); }
};

TEST_F(AudioMixer_Test, ApplyAndReport)
{
    std::string error;
    AudioMixer::Change change;
    ASSERT_TRUE(AudioMixer::ChangeFromStrings("1", "", "", "-6", change, error));
    ASSERT_TRUE(AudioMixer::Apply(_context, "beeper", change, error)) << error;
    const AudioDeviceInfo* beeper = _context->pSoundManager->device(AudioSourceType::Beeper);
    ASSERT_NE(beeper, nullptr);
    EXPECT_TRUE(beeper->mute);
    EXPECT_NEAR(beeper->volume, std::pow(10.0, -6.0 / 20.0), 1e-4);

    const StateNode mixer = DeviceState::AudioMixer(_context);
    const StateNode* devices = mixer.find("devices");
    ASSERT_NE(devices, nullptr);
    bool found = false;
    for (const StateNode& d : devices->items)
    {
        if (d.find("source")->s != "beeper")
            continue;
        found = true;
        EXPECT_TRUE(d.find("muted")->b);
        EXPECT_FALSE(d.find("audible")->b);
        EXPECT_NEAR(d.find("gain_db")->d, -6.0, 0.05);
    }
    EXPECT_TRUE(found);

    // master takes muted only; an absent device is refused with the fitted ones listed
    ASSERT_TRUE(AudioMixer::ChangeFromStrings("", "1", "", "", change, error));
    EXPECT_FALSE(AudioMixer::Apply(_context, "master", change, error));
    EXPECT_FALSE(AudioMixer::Apply(_context, "moonsound_pcm", change, error));
    EXPECT_NE(error.find("beeper"), std::string::npos) << error;

    // The channels overview carries the mixer and the beeper's real state
    const StateNode channels = DeviceState::AudioChannels(_context);
    EXPECT_TRUE(channels.find("beeper")->find("muted")->b);
    ASSERT_NE(channels.find("mixer"), nullptr);

    const std::string text = CliAudioMixer::Text(_context, {"mixer", "beeper", "muted=0"});
    EXPECT_NE(text.find("beeper         no"), std::string::npos) << text;
    EXPECT_NE(CliAudioMixer::Text(_context, {"mixer", "piano"}).find("Error: unknown source"), std::string::npos);
}

// A capture of one device makes the audio tap read that device's buffer
TEST_F(AudioMixer_Test, CaptureSourceSetsTheTap)
{
    AnalyzerManager* manager = _context->pDebugManager->GetAnalyzerManager();
    auto* capture = manager->getAnalyzer<AudioCaptureAnalyzer>("audiocapture");
    ASSERT_NE(capture, nullptr);
    AudioSourceType source = AudioSourceType::MasterMix;
    std::string error;
    ASSERT_TRUE(AudioMixer::Capturable(_context, "beeper", source, error)) << error;
    manager->activate("audiocapture");
    capture->startCapture(1000, source);
    EXPECT_EQ(manager->audioTapSource(), static_cast<int>(AudioSourceType::Beeper));
    EXPECT_EQ(DeviceState::AudioMixer(_context).find("capture_source")->s, "beeper");
    EmulatorTestHelper::RunFramesFast(_emulator, 1);
    EXPECT_GT(capture->getCapturedSamples(), 0u) << "the beeper buffer feeds the capture";
    manager->deactivate("audiocapture");
    EXPECT_EQ(manager->audioTapSource(), static_cast<int>(AudioSourceType::MasterMix));
    EXPECT_FALSE(AudioMixer::Capturable(_context, "moonsound_fm", source, error));
}
