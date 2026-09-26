#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/sound/soundmanager.h"

/// Config::SetConfigLoadedHook: a process-wide hook called for every config
/// that loaded and validated, before devices are created from it - what it
/// sets wins over the .ini. The test runner uses it for its sound-card policy
/// (SoundCardScope), so every case restores that policy when done.
class ConfigLoadedHook_Test : public ::testing::Test
{
protected:
    void TearDown() override { SoundCardScope::InstallPolicy(); }

    static Emulator* Create() { return EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError); }
};

TEST_F(ConfigLoadedHook_Test, HookSeesTheValidatedConfigAndWinsOverTheIni)
{
    int calls = 0;
    uint32_t seenFrame = 0;
    Config::SetConfigLoadedHook([&](CONFIG& config)
    {
        calls++;
        seenFrame = config.frame;         // model timing defaults already applied
        config.sound.gsTypeKind = GSTypeKind::NONE;
        config.sound.moonsound = 0;
        config.sound.turboSoundKind = TurboSoundKind::AY;  // the Pentagon ini says FM
    });

    Emulator* emulator = Create();
    ASSERT_NE(emulator, nullptr);
    EXPECT_GE(calls, 1);
    EXPECT_EQ(seenFrame, emulator->GetContext()->config.frame) << "the hook runs after validation";
    EXPECT_EQ(emulator->GetContext()->config.sound.turboSoundKind, TurboSoundKind::AY);
    EXPECT_FALSE(emulator->GetContext()->pSoundManager->hasGeneralSound()) << "devices are created from the hooked config";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST_F(ConfigLoadedHook_Test, RunnerPolicyLeavesTheSoundDevicesOutUnlessATestAsks)
{
    SoundCardScope::InstallPolicy();

    Emulator* plain = Create();
    ASSERT_NE(plain, nullptr);
    SoundManager* sm = plain->GetContext()->pSoundManager;
    EXPECT_FALSE(sm->hasGeneralSound());
    EXPECT_EQ(plain->GetContext()->config.sound.moonsound, 0);
    EXPECT_EQ(plain->GetContext()->config.sound.turboSoundKind, TurboSoundKind::None);
    EXPECT_FALSE(sm->hasTurboSound()) << "the TurboSound slot is empty by default";
    EXPECT_EQ(sm->getAYChipCount(), 0);
    EmulatorTestHelper::CleanupEmulator(plain);

    {
        SoundCardScope withCards;
        Emulator* fitted = Create();
        ASSERT_NE(fitted, nullptr);
        SoundManager* fittedSm = fitted->GetContext()->pSoundManager;
        EXPECT_TRUE(fittedSm->hasGeneralSound()) << "the Pentagon config fits GS";
#ifdef UNREALNG_HAVE_OPL4
        EXPECT_TRUE(fittedSm->hasMoonSound()) << "the Pentagon config fits MoonSound";
#endif
        ASSERT_TRUE(fittedSm->hasTurboSound());
        EXPECT_TRUE(fittedSm->getTurboSound()->hasFm()) << "the Pentagon config fits TSFM";
        EmulatorTestHelper::CleanupEmulator(fitted);
    }
}

TEST_F(ConfigLoadedHook_Test, ScopeBringsBackOnlyTheNamedDevices)
{
    SoundCardScope::InstallPolicy();

    SoundCardScope turboSound(TestSound::TurboSound);
    Emulator* emulator = Create();
    ASSERT_NE(emulator, nullptr);
    SoundManager* sm = emulator->GetContext()->pSoundManager;
    EXPECT_TRUE(sm->hasTurboSound());
    EXPECT_EQ(sm->getAYChipCount(), 2);
    EXPECT_FALSE(sm->hasGeneralSound()) << "GS was not asked for";
    EXPECT_EQ(emulator->GetContext()->config.sound.moonsound, 0) << "MoonSound was not asked for";
    EmulatorTestHelper::CleanupEmulator(emulator);
}
