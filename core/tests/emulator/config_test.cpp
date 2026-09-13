/// @file config_test.cpp
/// @brief [SOUND] TurboSound / TSFM_FmTrimDb parsing (TSFM design §3.1, plan P3).
///
/// The key selects which device occupies the TurboSound slot (legacy two-AY
/// pair vs TSFM); unknown values fall back to AY with a warning, a missing
/// key keeps the AY default. Also pins the shipped per-machine slot kinds
/// (TSFM enabled on pentagon128k, scorpion and spectrum128).

#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <unordered_map>

#include "_helpers/testpathhelper.h"
#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"

class Config_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }

    /// Write an ini carrying the given [SOUND] keys and parse it into the live
    /// context. HIMEM/RamSize keep their parser defaults (PENTAGON/128), so a
    /// [SOUND]-only file is a valid model config.
    bool LoadSoundKeys(const std::string& keys)
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath("tsfm_config_test.ini");
        {
            std::ofstream file(path, std::ios::binary);
            file << "[SOUND]\n" << keys;
        }
        Config config(_context);
        return config.LoadConfigFile(path);
    }
};

TEST_F(Config_Test, TurboSoundKindParsesAy)
{
    ASSERT_TRUE(LoadSoundKeys("TurboSound=AY\n"));
    EXPECT_EQ(_context->config.sound.turboSoundKind, TurboSoundKind::AY);
}

TEST_F(Config_Test, TurboSoundKindParsesFm)
{
    ASSERT_TRUE(LoadSoundKeys("TurboSound=FM\n"));
    EXPECT_EQ(_context->config.sound.turboSoundKind, TurboSoundKind::FM);
}

// Same acceptance as the RESET= mapping idiom: exact value, any case
TEST_F(Config_Test, TurboSoundKindCaseInsensitive)
{
    ASSERT_TRUE(LoadSoundKeys("TurboSound=fm\n"));
    EXPECT_EQ(_context->config.sound.turboSoundKind, TurboSoundKind::FM);
}

TEST_F(Config_Test, TurboSoundKindUnknownFallsBackToAy)
{
    ASSERT_TRUE(LoadSoundKeys("TurboSound=banana\n"));
    EXPECT_EQ(_context->config.sound.turboSoundKind, TurboSoundKind::AY);
}

TEST_F(Config_Test, TurboSoundKindMissingKeepsDefaultAy)
{
    ASSERT_TRUE(LoadSoundKeys("Fq=44100\n"));
    EXPECT_EQ(_context->config.sound.turboSoundKind, TurboSoundKind::AY);
}

TEST_F(Config_Test, TurboSoundKindMissingResetsStaleFm)
{
    // A second parse without the key must not leak FM from the previous one
    // (Config repopulates the same CONFIG struct in place)
    ASSERT_TRUE(LoadSoundKeys("TurboSound=FM\n"));
    EXPECT_EQ(_context->config.sound.turboSoundKind, TurboSoundKind::FM);
    ASSERT_TRUE(LoadSoundKeys("Fq=44100\n"));
    EXPECT_EQ(_context->config.sound.turboSoundKind, TurboSoundKind::AY);
}

TEST_F(Config_Test, FmTrimDbParsed)
{
    ASSERT_TRUE(LoadSoundKeys("TurboSound=FM\nTSFM_FmTrimDb=-3.5\n"));
    EXPECT_EQ(_context->config.sound.turboSoundKind, TurboSoundKind::FM);
    EXPECT_DOUBLE_EQ(_context->config.sound.tsfmFmTrimDb, -3.5);
}

TEST_F(Config_Test, FmTrimDbDefaultsToZero)
{
    ASSERT_TRUE(LoadSoundKeys("TurboSound=AY\n"));
    EXPECT_DOUBLE_EQ(_context->config.sound.tsfmFmTrimDb, 0.0);
}

TEST_F(Config_Test, ShippedConfigsProduceExpectedTurboSoundKind)
{
    // TSFM ships enabled on the capable machines; the rest keep the legacy
    // AY slot. Loading each shipped ini must produce exactly this map - a
    // future ini edit flipping a machine's slot kind shows up here rather
    // than as a surprise TTD session mismatch
    const std::unordered_map<std::string, TurboSoundKind> expected = {
        {"pentagon128k", TurboSoundKind::FM},
        {"pentagon512k", TurboSoundKind::AY},
        {"scorpion", TurboSoundKind::FM},
        {"profscorp", TurboSoundKind::AY},
        {"spectrum48", TurboSoundKind::AY},
        {"spectrum128", TurboSoundKind::FM},
        {"spectrum3", TurboSoundKind::AY},
    };
    for (const auto& [folder, kind] : expected)
    {
        const fs::path ini =
            TestPathHelper::FindProjectRoot() / "data" / "configs" / folder / "unreal.ini";
        ASSERT_TRUE(fs::exists(ini)) << ini;
        Config config(_context);
        ASSERT_TRUE(config.LoadConfigFile(ini.string())) << ini;
        EXPECT_EQ(_context->config.sound.turboSoundKind, kind) << folder;
    }
}
