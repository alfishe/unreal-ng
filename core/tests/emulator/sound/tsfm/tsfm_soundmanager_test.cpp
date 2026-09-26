#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"

/// TSFM mixer-source registry tests (TSFM design §7.2, implementation plan
/// P7). The Audio Settings Sources list, the recording source pickers and the
/// mixer loop are all driven by the SoundManager device registry, so "FM 1 /
/// FM 2 appear in the mixer" is asserted here: a TurboSound = FM config
/// registers the FM entries next to the SSG pair, a classic TurboSound = AY
/// config registers none, and the registry mute/solo/volume plumbing reaches
/// the FM entries.

namespace
{

/// Boot a Pentagon emulator whose TurboSound slot is the TSFM device: the
/// shipped pentagon128k ini is copied to unique scratch space with
/// TurboSound=FM, and loaded as the custom config. Caller owns the emulator.
Emulator* CreateFmEmulator(LoggerLevel level)
{
    namespace fs = std::filesystem;
    const fs::path source = TestPathHelper::FindProjectRoot() / "data/configs/pentagon128k/unreal.ini";
    std::string ini;
    {
        std::ifstream in(source, std::ios::binary);
        ini.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    // The shipped slot kind flips between releases (FM ships enabled now);
    // force FM from whichever value the ini carries
    const std::string to = "TurboSound=FM";
    const size_t atAy = ini.find("TurboSound=AY");
    if (ini.empty() || (atAy == std::string::npos && ini.find(to) == std::string::npos))
        return nullptr;
    if (atAy != std::string::npos)
        ini.replace(atAy, to.size(), to);  // both slot literals are the same length

    const fs::path target = TestPathHelper::GetUniqueTestScratchPath("tsfm-mixer-pentagon.ini");
    {
        std::ofstream out(target, std::ios::binary);
        out.write(ini.data(), static_cast<std::streamsize>(ini.size()));
    }

    SoundCardScope turboSound(TestSound::TurboSound);  // FM asked for: keep the slot
    Emulator* emulator = new Emulator(level);
    emulator->SetCustomConfigPath(target.string());
    if (!emulator->Init())
    {
        emulator->Release();
        delete emulator;
        return nullptr;
    }
    return emulator;
}

/// Detach audio callbacks and dispose the emulator (FM-booted or not)
void ReleaseEmulator(Emulator* emulator)
{
    if (emulator)
    {
        emulator->GetContext()->pAudioCallback.store(nullptr, std::memory_order_release);
        emulator->GetContext()->pAudioManagerObj.store(nullptr, std::memory_order_release);
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}

/// Registry lookup by source type (nullptr when absent)
const AudioDeviceInfo* FindDevice(const SoundManager& soundManager, AudioSourceType type)
{
    for (const AudioDeviceInfo& device : soundManager.devices())
        if (device.type == type)
            return &device;
    return nullptr;
}

}  // namespace

class TsfmMixer_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;

    void TearDown() override
    {
        ReleaseEmulator(_emulator);
        _emulator = nullptr;
    }
};

TEST_F(TsfmMixer_Test, FmConfigRegistersFmSources)
{
    _emulator = CreateFmEmulator(LoggerLevel::LogError);
    ASSERT_NE(_emulator, nullptr) << "failed to boot a TurboSound=FM emulator";

    SoundManager* soundManager = _emulator->GetContext()->pSoundManager;
    ASSERT_NE(soundManager, nullptr);

    // The signal the Audio Settings block switching keys on (§8.3)
    ASSERT_NE(soundManager->getTurboSound(), nullptr);
    EXPECT_TRUE(soundManager->getTurboSound()->hasFm());

    // TSFM keeps the SSG pair as sources and adds the FM channels (§7.2)
    EXPECT_NE(FindDevice(*soundManager, AudioSourceType::AY1_All), nullptr) << "SSG chip 0 source missing";
    EXPECT_NE(FindDevice(*soundManager, AudioSourceType::AY2_All), nullptr) << "SSG chip 1 source missing";

    const AudioDeviceInfo* fm1 = FindDevice(*soundManager, AudioSourceType::FM1);
    const AudioDeviceInfo* fm2 = FindDevice(*soundManager, AudioSourceType::FM2);
    ASSERT_NE(fm1, nullptr) << "FM 1 not registered";
    ASSERT_NE(fm2, nullptr) << "FM 2 not registered";
    EXPECT_EQ(fm1->name, "FM 1");
    EXPECT_EQ(fm2->name, "FM 2");

    // Fresh registry entries: audible at unity volume
    EXPECT_FALSE(fm1->mute);
    EXPECT_FALSE(fm1->solo);
    EXPECT_FLOAT_EQ(fm1->volume, 1.0f);
}

TEST_F(TsfmMixer_Test, AyConfigRegistersNoFmSources)
{
    _emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind(
        "PENTAGON", TurboSoundKind::AY, LoggerLevel::LogError);
    ASSERT_NE(_emulator, nullptr) << "failed to boot a TurboSound=AY emulator";

    SoundManager* soundManager = _emulator->GetContext()->pSoundManager;
    ASSERT_NE(soundManager, nullptr);

    ASSERT_NE(soundManager->getTurboSound(), nullptr);
    EXPECT_FALSE(soundManager->getTurboSound()->hasFm());

    // The classic two-AY pair: both chips present, no FM entries anywhere
    EXPECT_NE(FindDevice(*soundManager, AudioSourceType::AY1_All), nullptr);
    EXPECT_NE(FindDevice(*soundManager, AudioSourceType::AY2_All), nullptr);
    EXPECT_EQ(FindDevice(*soundManager, AudioSourceType::FM1), nullptr);
    EXPECT_EQ(FindDevice(*soundManager, AudioSourceType::FM2), nullptr);
    EXPECT_EQ(soundManager->deviceBuffer(AudioSourceType::FM1), nullptr);
}

TEST_F(TsfmMixer_Test, MuteSoloVolumeReachFmSources)
{
    _emulator = CreateFmEmulator(LoggerLevel::LogError);
    ASSERT_NE(_emulator, nullptr) << "failed to boot a TurboSound=FM emulator";

    SoundManager* soundManager = _emulator->GetContext()->pSoundManager;
    ASSERT_NE(soundManager, nullptr);

    // The mixer row controls land on the registry entries the mixer loop reads
    soundManager->setDeviceMute(AudioSourceType::FM1, true);
    soundManager->setDeviceSolo(AudioSourceType::FM2, true);
    soundManager->setDeviceVolume(AudioSourceType::FM1, 0.25f);

    const AudioDeviceInfo* fm1 = FindDevice(*soundManager, AudioSourceType::FM1);
    const AudioDeviceInfo* fm2 = FindDevice(*soundManager, AudioSourceType::FM2);
    ASSERT_NE(fm1, nullptr);
    ASSERT_NE(fm2, nullptr);
    EXPECT_TRUE(fm1->mute);
    EXPECT_FALSE(fm2->mute);
    EXPECT_TRUE(fm2->solo);
    EXPECT_FALSE(fm1->solo);
    EXPECT_FLOAT_EQ(fm1->volume, 0.25f);

    // The FM mixer arms mix distinct buffers (chip 0 / chip 1 FM output)
    const int16_t* fm1Buffer = soundManager->deviceBuffer(AudioSourceType::FM1);
    const int16_t* fm2Buffer = soundManager->deviceBuffer(AudioSourceType::FM2);
    const int16_t* ay1Buffer = soundManager->deviceBuffer(AudioSourceType::AY1_All);
    ASSERT_NE(fm1Buffer, nullptr);
    ASSERT_NE(fm2Buffer, nullptr);
    EXPECT_NE(fm1Buffer, fm2Buffer);
    EXPECT_NE(fm1Buffer, ay1Buffer);
}
