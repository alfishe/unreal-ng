/// @file config_test.cpp
/// @brief [SOUND] TurboSound / TSFM_FmTrimDb / DecimatorQuality / AYVoicing and [NGS] parsing
/// (TSFM design §3.1, plan P3; AY tone voicing design §5.1).
///
/// The key selects which device occupies the TurboSound slot (legacy two-AY
/// pair vs TSFM); unknown values fall back to AY with a warning, a missing
/// key keeps the AY default. Also pins the shipped per-machine slot kinds
/// (TSFM enabled on pentagon128k, scorpion and spectrum128).

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <unordered_map>

#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/sound/soundmanager.h"

class Config_Test : public ::testing::Test
{
protected:
    // Parser tests: the slot kind must reach the config as written, not the
    // runner's empty-slot policy
    SoundCardScope _turboSound{TestSound::TurboSound};
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

TEST_F(Config_Test, TurboSoundKindParsesNone)
{
    ASSERT_TRUE(LoadSoundKeys("TurboSound=None\n"));
    EXPECT_EQ(_context->config.sound.turboSoundKind, TurboSoundKind::None);
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

TEST_F(Config_Test, DecimatorQualityParsed)
{
    ASSERT_TRUE(LoadSoundKeys("DecimatorQuality=HighFidelity\n"));
    EXPECT_TRUE(_context->config.sound.decimatorHighFidelity);
    ASSERT_TRUE(LoadSoundKeys("DecimatorQuality=highfidelity\n"));
    EXPECT_TRUE(_context->config.sound.decimatorHighFidelity) << "case-insensitive";
    ASSERT_TRUE(LoadSoundKeys("DecimatorQuality=Reference\n"));
    EXPECT_FALSE(_context->config.sound.decimatorHighFidelity);
}

TEST_F(Config_Test, DecimatorQualityDefaultsToReference)
{
    // Missing key resets a stale HighFidelity; unknown values keep Reference
    ASSERT_TRUE(LoadSoundKeys("DecimatorQuality=HighFidelity\n"));
    ASSERT_TRUE(LoadSoundKeys("TurboSound=AY\n"));
    EXPECT_FALSE(_context->config.sound.decimatorHighFidelity);
    ASSERT_TRUE(LoadSoundKeys("DecimatorQuality=ultra\n"));
    EXPECT_FALSE(_context->config.sound.decimatorHighFidelity);
}

TEST_F(Config_Test, AYVoicingParsed)
{
    // [SOUND] AYVoicing: profile IDs and the legacy alias, case-insensitive
    ASSERT_TRUE(LoadSoundKeys("AYVoicing=flat\n"));
    EXPECT_EQ(_context->config.sound.ayVoicing, FilterVoicing::Preset::Flat);
    ASSERT_TRUE(LoadSoundKeys("AYVoicing=Classic\n"));
    EXPECT_EQ(_context->config.sound.ayVoicing, FilterVoicing::Preset::Classic);
    ASSERT_TRUE(LoadSoundKeys("AYVoicing=legacy\n"));
    EXPECT_EQ(_context->config.sound.ayVoicing, FilterVoicing::Preset::Classic) << "legacy is an alias of classic";
    ASSERT_TRUE(LoadSoundKeys("AYVoicing=tv\n"));
    EXPECT_EQ(_context->config.sound.ayVoicing, FilterVoicing::Preset::Tv);
    ASSERT_TRUE(LoadSoundKeys("AYVoicing=warm\n"));
    EXPECT_EQ(_context->config.sound.ayVoicing, FilterVoicing::Preset::Warm);
    ASSERT_TRUE(LoadSoundKeys("AYVoicing=headphones\n"));
    EXPECT_EQ(_context->config.sound.ayVoicing, FilterVoicing::Preset::Headphones);
    ASSERT_TRUE(LoadSoundKeys("AYVoicing=Small_Speaker\n"));
    EXPECT_EQ(_context->config.sound.ayVoicing, FilterVoicing::Preset::SmallSpeaker);
}

TEST_F(Config_Test, AYVoicingDefaultsToSoftHighs)
{
    // Missing key resets a stale value; unknown profile IDs warn and keep the
    // default, which is the soft-highs headphones profile
    ASSERT_EQ(FilterVoicing::DEFAULT_PRESET, FilterVoicing::Preset::Headphones);
    ASSERT_TRUE(LoadSoundKeys("AYVoicing=flat\n"));
    ASSERT_TRUE(LoadSoundKeys("TurboSound=AY\n"));
    EXPECT_EQ(_context->config.sound.ayVoicing, FilterVoicing::Preset::Headphones);
    ASSERT_TRUE(LoadSoundKeys("AYVoicing=loud\n"));
    EXPECT_EQ(_context->config.sound.ayVoicing, FilterVoicing::Preset::Headphones);
}

TEST_F(Config_Test, ShippedConfigsProduceExpectedTurboSoundKind)
{
    // TSFM ships enabled on all machines as the default sound device.
    // Loading each shipped ini must produce exactly this map - a future ini
    // edit flipping a machine's slot kind shows up here rather than as a
    // surprise TTD session mismatch
    const std::unordered_map<std::string, TurboSoundKind> expected = {
        {"pentagon128k", TurboSoundKind::FM},
        {"pentagon512k", TurboSoundKind::FM},
        {"scorpion", TurboSoundKind::FM},
        {"profscorp", TurboSoundKind::FM},
        {"spectrum48", TurboSoundKind::FM},
        {"spectrum128", TurboSoundKind::FM},
        {"spectrum3", TurboSoundKind::FM},
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

/// region <[NGS] (NeoGS card)>

TEST_F(Config_Test, ShippedConfigsCarryTheFullNeoGSSection)
{
    // Every shipped ini lists each [NGS] key with its documented value
    // (neogs-tdd.md §6). Parsing them all catches a comment that breaks a
    // value (a second ';' survives IniFile's inline-comment strip) - such a
    // key would fall back silently or warn
    for (const auto& entry : fs::directory_iterator(TestPathHelper::FindProjectRoot() / "data" / "configs"))
    {
        const fs::path ini = entry.path() / "unreal.ini";
        if (!fs::exists(ini))
            continue;
        Config config(_context);
        ASSERT_TRUE(config.LoadConfigFile(ini.string())) << ini;
        const NeoGSConfig& ngs = _context->config.ngs;
        const std::string name = entry.path().filename().string();
        EXPECT_STREQ(ngs.flashPath, "rom/neogs/full_ngs.rom") << name;
        EXPECT_EQ(ngs.flashId, NeoGSConfig::FlashId::ST) << name;
        EXPECT_EQ(ngs.fpga, NeoGSConfig::Fpga::Current) << name;
        EXPECT_EQ(ngs.ramKB, 2048u) << name;
        EXPECT_EQ(ngs.boot, NeoGSConfig::Boot::Loader) << name;
        EXPECT_EQ(ngs.bootDelayMs, 0u) << name;
        EXPECT_STREQ(ngs.sdCardPath, "") << name;
        EXPECT_EQ(ngs.sdType, NeoGSConfig::SDType::Auto) << name;
        EXPECT_FALSE(ngs.sdWriteProtect) << name;
        EXPECT_EQ(ngs.sdWrite, NeoGSConfig::WriteMode::Session) << name;
        EXPECT_EQ(ngs.mp3Support, NGSMP3SupportKind::Software) << name;
        EXPECT_EQ(ngs.mp3Chip, NeoGSConfig::Mp3Chip::VS1001) << name;
        EXPECT_DOUBLE_EQ(ngs.mp3Gain, 1.0) << name;
        EXPECT_EQ(ngs.flashWrite, NeoGSConfig::WriteMode::Session) << name;
        EXPECT_EQ(ngs.volume, 8000u) << name;
        EXPECT_EQ(ngs.zxDmaWatch, NeoGSConfig::ZxDmaWatch::Selected) << name;
        EXPECT_EQ(ngs.zxDmaWatchFrames, 5u) << name;
    }
}

TEST_F(Config_Test, NeoGSMp3DecoderDefaultsToSoftware)
{
    // A config without an [NGS] section: the card decodes and plays MP3
    ASSERT_TRUE(LoadSoundKeys("GSType=NGS\n"));
    EXPECT_EQ(_context->config.ngs.mp3Support, NGSMP3SupportKind::Software);
    ASSERT_TRUE(LoadSoundKeys("GSType=NGS\n[NGS]\nMP3Support=stub\n"));
    EXPECT_EQ(_context->config.ngs.mp3Support, NGSMP3SupportKind::Stub);
}

TEST_F(Config_Test, ShippedConfigsFitNeoGS)
{
    // Every shipped model fits the NeoGS card (the runner leaves GS out
    // unless a scope keeps it)
    SoundCardScope gs(TestSound::GeneralSound);
    size_t checked = 0;
    for (const auto& entry : fs::directory_iterator(TestPathHelper::FindProjectRoot() / "data" / "configs"))
    {
        const fs::path ini = entry.path() / "unreal.ini";
        if (!fs::exists(ini))
            continue;
        Config config(_context);
        ASSERT_TRUE(config.LoadConfigFile(ini.string())) << ini;
        EXPECT_EQ(_context->config.sound.gsTypeKind, GSTypeKind::NGS) << entry.path().filename();
        checked++;
    }
    EXPECT_GE(checked, 14u);
}

TEST_F(Config_Test, EveryShippedConfigFitsNeoGSWithGSTypeNGS)
{
    // Each shipped config with only its GSType value changed to NGS: the
    // parsed config selects NeoGS, and a SoundManager built from it fits the
    // card under the mixer name "NeoGS" with its MP3 source
    SoundCardScope gs(TestSound::GeneralSound);
    for (const auto& entry : fs::directory_iterator(TestPathHelper::FindProjectRoot() / "data" / "configs"))
    {
        const fs::path ini = entry.path() / "unreal.ini";
        if (!fs::exists(ini))
            continue;
        const std::string name = entry.path().filename().string();

        std::ifstream in(ini, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t key = text.find("\nGSType=");
        ASSERT_NE(key, std::string::npos) << name;
        const size_t value = key + strlen("\nGSType=");
        const size_t valueEnd = text.find_first_of(" \t;\r\n", value);
        text.replace(value, valueEnd - value, "NGS");

        // Written next to the original so relative paths resolve the same way
        const fs::path copy = entry.path() / "unreal-ngs-test.ini";
        {
            std::ofstream out(copy, std::ios::binary);
            out << text;
        }
        Config config(_context);
        const bool loaded = config.LoadConfigFile(copy.string());
        fs::remove(copy);
        ASSERT_TRUE(loaded) << name;
        EXPECT_EQ(_context->config.sound.gsTypeKind, GSTypeKind::NGS) << name;
        EXPECT_EQ(_context->config.ngs.mp3Support, NGSMP3SupportKind::Software) << name;

        SoundManager sm(_context);
        EXPECT_EQ(sm.fittedGeneralSoundKind(), GSTypeKind::NGS) << name;
        std::string gsName;
        bool mp3 = false;
        for (const AudioDeviceInfo& d : sm.devices())
        {
            if (d.type == AudioSourceType::GeneralSound)
                gsName = d.name;
            mp3 |= d.type == AudioSourceType::GeneralSoundMp3;
        }
        EXPECT_EQ(gsName, "NeoGS") << name;
        EXPECT_TRUE(mp3) << name;
    }
}

/// endregion </[NGS] (NeoGS card)>

/// region <[EVO] Fpga (ZX-Evo BaseConf FPGA variant)>

TEST(ConfigEvoFpga_Test, ParsesVariantNamesCaseInsensitive)
{
    EXPECT_TRUE(Config::ParseEvoFpgaVariant("legacy"));
    EXPECT_TRUE(Config::ParseEvoFpgaVariant("LEGACY"));
    EXPECT_FALSE(Config::ParseEvoFpgaVariant("trdemu"));
    EXPECT_FALSE(Config::ParseEvoFpgaVariant("TrDemu"));
}

TEST(ConfigEvoFpga_Test, MissingOrUnknownMeansCurrentTrdemu)
{
    EXPECT_FALSE(Config::ParseEvoFpgaVariant(nullptr));
    EXPECT_FALSE(Config::ParseEvoFpgaVariant(""));
    EXPECT_FALSE(Config::ParseEvoFpgaVariant("legacyX")) << "prefix match must not select legacy";
    EXPECT_FALSE(Config::ParseEvoFpgaVariant("baseconf"));
}

TEST_F(Config_Test, EvoFpgaKeyReachesConfig)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("evo_fpga_config_test.ini");
    {
        std::ofstream file(path, std::ios::binary);
        file << "[EVO]\nFpga=legacy\n";
    }
    Config config(_context);
    ASSERT_TRUE(config.LoadConfigFile(path));
    EXPECT_EQ(_context->config.atm.evo_legacy_fpga, 1);

    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "[MISC]\nRamSize=128\n";
    }
    ASSERT_TRUE(config.LoadConfigFile(path));
    EXPECT_EQ(_context->config.atm.evo_legacy_fpga, 0) << "a config without [EVO] selects the current tree";

    std::remove(path.c_str());
}

/// endregion </[EVO] Fpga>
