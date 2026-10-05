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
#include <sstream>
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

    /// Same, for [MISC] keys (HIMEM / RamSize keep their parser defaults)
    bool LoadMiscKeys(const std::string& keys)
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath("misc_config_test.ini");
        {
            std::ofstream file(path, std::ios::binary);
            file << "[MISC]\n" << keys;
        }
        Config config(_context);
        return config.LoadConfigFile(path);
    }
};

TEST_F(Config_Test, RamPowerOnParsesZeroAnyCase)
{
    ASSERT_TRUE(LoadMiscKeys("RAMPowerOn=zero\n"));
    EXPECT_EQ(_context->config.ramPowerOn, RamPowerOn::Zero);
}

TEST_F(Config_Test, RamPowerOnUnknownFallsBackToRandom)
{
    ASSERT_TRUE(LoadMiscKeys("RAMPowerOn=banana\n"));
    EXPECT_EQ(_context->config.ramPowerOn, RamPowerOn::Random);
}

TEST_F(Config_Test, RamPowerOnMissingResetsStaleZero)
{
    // Config repopulates the same CONFIG in place: a file without the key
    // must not keep ZERO from the previous parse
    ASSERT_TRUE(LoadMiscKeys("RAMPowerOn=ZERO\n"));
    EXPECT_EQ(_context->config.ramPowerOn, RamPowerOn::Zero);
    ASSERT_TRUE(LoadMiscKeys("RAMSize=128\n"));
    EXPECT_EQ(_context->config.ramPowerOn, RamPowerOn::Random);
}

/// The spelling every automation surface accepts and reports
TEST(Config_RamPowerOn_Test, NamesRoundTrip)
{
    for (RamPowerOn mode : {RamPowerOn::Random, RamPowerOn::Zero})
    {
        RamPowerOn parsed = mode == RamPowerOn::Zero ? RamPowerOn::Random : RamPowerOn::Zero;
        ASSERT_TRUE(Config::ParseRamPowerOn(Config::RamPowerOnName(mode), parsed));
        EXPECT_EQ(parsed, mode);
    }
    EXPECT_STREQ(Config::RamPowerOnName(RamPowerOn::Zero), "zero");
    EXPECT_STREQ(Config::RamPowerOnName(RamPowerOn::Random), "random");
    RamPowerOn untouched = RamPowerOn::Zero;
    EXPECT_FALSE(Config::ParseRamPowerOn("", untouched));
    EXPECT_FALSE(Config::ParseRamPowerOn("0", untouched));
    EXPECT_EQ(untouched, RamPowerOn::Zero);
}

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
        EXPECT_EQ(ngs.stereoMode, NeoGSConfig::StereoMode::Separated) << name;
    }
}

TEST_F(Config_Test, NeoGSMp3DecoderDefaultsToSoftware)
{
    // A config without an [NGS] section: the card decodes and plays MP3
    ASSERT_TRUE(LoadSoundKeys("GSType=NGS\n"));
    EXPECT_EQ(_context->config.ngs.mp3Support, NGSMP3SupportKind::Software);
    ASSERT_TRUE(LoadSoundKeys("GSType=NGS\n[NGS]\nMP3Support=stub\n"));
    EXPECT_EQ(_context->config.ngs.mp3Support, NGSMP3SupportKind::Stub);
    EXPECT_EQ(_context->config.ngs.stereoMode, NeoGSConfig::StereoMode::Separated) << "the default: as on the board";
    ASSERT_TRUE(LoadSoundKeys("GSType=NGS\n[NGS]\nStereoMode=gs\n"));
    EXPECT_EQ(_context->config.ngs.stereoMode, NeoGSConfig::StereoMode::GS);
    ASSERT_TRUE(LoadSoundKeys("GSType=NGS\n[NGS]\nStereoMode=mono\n"));
    EXPECT_EQ(_context->config.ngs.stereoMode, NeoGSConfig::StereoMode::Mono);
}

namespace
{
/// The shipped configs without a General Sound card. Owner decision 2026-10-04: the standard Sinclair models (48K,
/// 128K, +2, +2A, +3), the Profi (v5, v3) and the Sprinter ship without the NeoGS / GS card - on real hardware none
/// of them takes a ZX-bus card without an adapter (the Sinclair edge has no IORQGE, the Profi bus has no known
/// adapter, the Sprinter needs an ISA ZX-bus adapter), so the card was fitted only as `fit = unrealistic`. A user
/// still adds one in [SLOTS]. Every other shipped config keeps its NeoGS.
bool ShipsWithoutGeneralSound(const std::string& folder)
{
    static const char* const kFolders[] = {"spectrum48", "spectrum128", "spectrum2", "spectrum2a",
                                           "spectrum3",  "profi",       "profi3",    "sprinter"};
    for (const char* name : kFolders)
    {
        if (folder == name)
            return true;
    }
    return false;
}
} // namespace

TEST_F(Config_Test, ShippedConfigsFitNeoGSExceptTheMachinesShippedWithout)
{
    // Every shipped model fits the NeoGS card, except the eight shipped without a GS (ShipsWithoutGeneralSound:
    // owner decision 2026-10-04), which fit none. (The runner leaves GS out unless a scope keeps it)
    SoundCardScope gs(TestSound::GeneralSound);
    size_t checked = 0;
    size_t without = 0;
    for (const auto& entry : fs::directory_iterator(TestPathHelper::FindProjectRoot() / "data" / "configs"))
    {
        const fs::path ini = entry.path() / "unreal.ini";
        if (!fs::exists(ini))
            continue;
        const std::string name = entry.path().filename().string();
        Config config(_context);
        ASSERT_TRUE(config.LoadConfigFile(ini.string())) << ini;
        const bool none = ShipsWithoutGeneralSound(name);
        EXPECT_EQ(_context->config.sound.gsTypeKind, none ? GSTypeKind::NONE : GSTypeKind::NGS) << name;
        checked++;
        without += none ? 1 : 0;
    }
    EXPECT_GE(checked, 14u);
    EXPECT_EQ(without, 8u) << "every machine of the owner decision has a shipped config";
}

TEST_F(Config_Test, ShippedConfigsFitTheirGeneralSoundCard)
{
    // Each shipped config as shipped: the ones with a NeoGS name it in [SLOTS], the parsed config selects NeoGS, and
    // a SoundManager built from it fits the card under the mixer name "NeoGS" with its MP3 source. The eight shipped
    // without a GS (ShipsWithoutGeneralSound, owner decision 2026-10-04) name no GS card in [SLOTS] and the
    // SoundManager fits no GS source at all. (ZX-bus slots SL-4 step 7: the card is a [SLOTS] entry, no longer
    // [SOUND] GSType=, so the copy is the shipped file)
    SoundCardScope gs(TestSound::GeneralSound);
    for (const auto& entry : fs::directory_iterator(TestPathHelper::FindProjectRoot() / "data" / "configs"))
    {
        const fs::path ini = entry.path() / "unreal.ini";
        if (!fs::exists(ini))
            continue;
        const std::string name = entry.path().filename().string();
        const bool none = ShipsWithoutGeneralSound(name);

        std::ifstream in(ini, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t slotsSection = text.find("\n[SLOTS]");
        ASSERT_NE(slotsSection, std::string::npos) << name;
        const size_t slotsEnd = text.find("\n[", slotsSection + 1);
        const std::string slots = text.substr(slotsSection, slotsEnd - slotsSection);
        if (none)
        {
            // A card entry is a "<slot> = <card>" line; the comments only mention how to add one
            std::istringstream lines(slots);
            for (std::string line; std::getline(lines, line);)
            {
                line = line.substr(0, line.find(';'));
                const size_t eq = line.find('=');
                if (eq == std::string::npos)
                    continue;
                std::string card = line.substr(eq + 1);
                card.erase(0, card.find_first_not_of(" \t"));
                card.erase(card.find_last_not_of(" \t\r") + 1);
                EXPECT_TRUE(card != "neogs" && card != "gs" && card != "gs-lw") << name << ": " << line;
            }
        }
        else
        {
            ASSERT_NE(slots.find(" = neogs"), std::string::npos) << name;
        }

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
        EXPECT_EQ(_context->config.sound.gsTypeKind, none ? GSTypeKind::NONE : GSTypeKind::NGS) << name;
        EXPECT_EQ(_context->config.ngs.mp3Support, NGSMP3SupportKind::Software) << name;

        SoundManager sm(_context);
        EXPECT_EQ(sm.fittedGeneralSoundKind(), none ? GSTypeKind::NONE : GSTypeKind::NGS) << name;
        std::string gsName;
        bool mp3 = false;
        for (const AudioDeviceInfo& d : sm.devices())
        {
            if (d.type == AudioSourceType::GeneralSound)
                gsName = d.name;
            mp3 |= d.type == AudioSourceType::GeneralSoundMp3;
        }
        EXPECT_EQ(gsName, none ? "" : "NeoGS") << name;
        EXPECT_EQ(mp3, !none) << name;
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

/// region <[HDD] (IDE board)>

/// [HDD] Scheme, CHSn, CDn; an ISO image makes its unit a CD drive even when
/// the config says CDn=0 (the shipped files carry CD0=0 / CD1=0)
TEST_F(Config_Test, HddSectionSetsTheBoardAndItsUnits)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("hdd_config_test.ini");
    {
        std::ofstream file(path, std::ios::binary);
        file << "[HDD]\nScheme=nemo-divide ; comment\nCHS0=100/4/32\nCD0=0\nImage1=disc.iso\nCD1=0\n";
    }
    Config config(_context);
    ASSERT_TRUE(config.LoadConfigFile(path));
    EXPECT_EQ(_context->config.ide_scheme, IDE_NEMO_DIVIDE);
    EXPECT_EQ(_context->config.ide[0].c, 100u);
    EXPECT_EQ(_context->config.ide[0].h, 4u);
    EXPECT_EQ(_context->config.ide[0].s, 32u);
    EXPECT_EQ(_context->config.ide[0].cd, 0);
    EXPECT_EQ(_context->config.ide[1].cd, 1) << "an .iso image is a CD";

    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "[HDD]\nScheme=WILD\nCD1=1\nCF0=1\nCF1=1\n";
    }
    ASSERT_TRUE(config.LoadConfigFile(path));
    EXPECT_EQ(_context->config.ide_scheme, IDE_NONE) << "an unknown board: no IDE";
    EXPECT_EQ(_context->config.ide[1].cd, 1);
    EXPECT_EQ(_context->config.ide[0].cf, 1) << "CF0=1: a CompactFlash card on the master";
    EXPECT_EQ(_context->config.ide[1].cf, 0) << "a CD unit is no CF card";
    std::remove(path.c_str());
}

/// The board each shipped machine comes with (docs/inprogress/2026-09-28-ide-atapi/implementation-plan.md §2.1)
TEST_F(Config_Test, ShippedConfigsFitTheirIdeBoard)
{
    const std::unordered_map<std::string, IDE_SCHEME> expected = {
        {"profi", IDE_PROFI},         {"atm3", IDE_NEMO_DIVIDE},  {"atm710", IDE_ATM},
        {"pentagon128k", IDE_NEMO},   {"pentagon512k", IDE_NEMO}, {"scorpion", IDE_NONE},
        {"profscorp", IDE_NONE},      {"spectrum48", IDE_NONE},   {"spectrum128", IDE_NONE},
        {"spectrum3", IDE_NONE},      {"ts-conf", IDE_NEMO_DIVIDE},
    };
    for (const auto& [folder, scheme] : expected)
    {
        const fs::path ini = TestPathHelper::FindProjectRoot() / "data" / "configs" / folder / "unreal.ini";
        ASSERT_TRUE(fs::exists(ini)) << ini;
        Config config(_context);
        ASSERT_TRUE(config.LoadConfigFile(ini.string())) << ini;
        EXPECT_EQ(_context->config.ide_scheme, scheme) << folder;
        // Only ZX-Evo ships a CD drive: the slave, where the ERS "D. CD boot" looks for it
        EXPECT_EQ(_context->config.ide[1].cd, folder == "atm3" ? 1 : 0) << folder;
        EXPECT_EQ(_context->config.ide[0].cd, 0) << folder << ": the master is a hard disk";
    }
}

/// TS-Conf VDAC2 build (vdac2-integration-design.md §3): the card occupies the
/// IDE connector, so TS_VDAC2=1 leaves the machine without IDE whatever
/// [HDD] Scheme says; other machines and the other TS-Conf builds keep theirs
TEST_F(Config_Test, Vdac2TakesTheIdeConnector)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("vdac2_ide_config_test.ini");
    auto load = [&](const std::string& text) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << text;
        file.close();
        Config config(_context);
        return config.LoadConfigFile(path);
    };

#ifdef ENABLE_VDAC2
    ASSERT_TRUE(load("[MISC]\nHIMEM=TSL\nRamSize=4096\nTS_VDAC2=1\n[HDD]\nScheme=nemo-divide\n"));
#else
    // A build without the FT812 library refuses the card (design §2); the
    // fields are parsed all the same
    EXPECT_FALSE(load("[MISC]\nHIMEM=TSL\nRamSize=4096\nTS_VDAC2=1\n[HDD]\nScheme=nemo-divide\n"))
        << "no VDAC2 support in this build";
#endif
    EXPECT_EQ(_context->config.ts_vdac, 7);
    EXPECT_EQ(_context->config.ide_scheme, IDE_NONE) << "VDAC2: no IDE";

    ASSERT_TRUE(load("[MISC]\nHIMEM=TSL\nRamSize=4096\n[HDD]\nScheme=nemo-divide\n"));
    EXPECT_EQ(_context->config.ide_scheme, IDE_NEMO_DIVIDE) << "standard TS-Conf build keeps the IDE";

    ASSERT_TRUE(load("[MISC]\nHIMEM=PROFI\nRamSize=1024\nTS_VDAC2=1\n[HDD]\nScheme=profi\n"));
    EXPECT_EQ(_context->config.ide_scheme, IDE_PROFI) << "TS_VDAC2 means nothing outside TS-Conf";
    std::remove(path.c_str());
}

/// [VDAC2] RomImage: the FT812's ROM fonts (design §3, §10); default rom/ft81x.rom
TEST_F(Config_Test, Vdac2RomImagePath)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("vdac2_rom_config_test.ini");
    auto load = [&](const std::string& text) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << text;
        file.close();
        Config config(_context);
        return config.LoadConfigFile(path);
    };

    ASSERT_TRUE(load("[MISC]\nHIMEM=TSL\nRamSize=4096\n"));
    EXPECT_STREQ(_context->config.vdac2_rom_path, "rom/ft81x.rom");
    ASSERT_TRUE(load("[MISC]\nHIMEM=TSL\nRamSize=4096\n[VDAC2]\nRomImage=roms/my-ft81x.bin\n"));
    EXPECT_STREQ(_context->config.vdac2_rom_path, "roms/my-ft81x.bin");
    std::remove(path.c_str());
}

/// endregion </[HDD] (IDE board)>

/// TSConf model names (PLAN #41 INF-8, technical-design D3): the canonical
/// short name stays TSL, TSCONF is accepted wherever a short name is
TEST(ConfigModelLookup_Test, TsconfIsAnAliasOfTsl)
{
    for (const char* name : {"TSL", "tsl", "TSCONF", "TSConf", "tsconf"})
    {
        const TMemModel* model = Config::FindModelByShortName(name);
        ASSERT_NE(model, nullptr) << name;
        EXPECT_EQ(model->Model, MM_TSL) << name;
        EXPECT_STREQ(model->ShortName, "TSL") << name << ": the table row, not a second model";
    }
    for (const char* name : {"TSCONFX", "TSCON", "TS", "TSLX"})
        EXPECT_EQ(Config::FindModelByShortName(name), nullptr) << name;
    EXPECT_EQ(Config::FindModelByShortName("PLUS2A")->Model, MM_PLUS2A) << "whole-name match, not a prefix";
}

TEST_F(Config_Test, TsconfConfigHimemSelectsTsl)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("tsconf_himem.ini");
    {
        std::ofstream file(path, std::ios::binary);
        file << "[MISC]\nHIMEM=TSCONF\nRamSize=4096\n";
    }
    Config config(_context);
    ASSERT_TRUE(config.LoadConfigFile(path));
    EXPECT_EQ(_context->config.mem_model, MM_TSL);
}

/// TSConf frame geometry (PLAN #41 INF-9, technical-design §3.17): the
/// Pentagon raster, 320 lines x 224 T; INT comes from the machine's interrupt
/// source, so intstart / intlen are left alone
TEST_F(Config_Test, TsconfCanonicalTiming)
{
    CONFIG config = _context->config;
    config.mem_model = MM_TSL;
    config.frame = 69888;  // another model's values must not leak in
    config.t_line = 228;
    Config(_context).ApplyModelTimingDefaults(config, true);
    EXPECT_EQ(config.t_line, 224u);
    EXPECT_EQ(config.frame, 71680u);
    EXPECT_EQ(config.frame / config.t_line, 320u);
    EXPECT_EQ(config.frame_duration_us, 20480u) << "48.83 frames per second";
}

/// [MISC] ScorpionTurboLogic: SC15.1 by default; SC15.3 has no Even M1, whatever [ULA] EvenM1 says
/// (docs/inprogress/2026-09-29-machine-waits/research-scorpion-turbo.md section 4.2)
TEST_F(Config_Test, ScorpionTurboLogicDecidesEvenM1)
{
    auto load = [this](const std::string& misc) {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath("scorpion_logic_config_test.ini");
        {
            std::ofstream file(path, std::ios::binary);
            file << "[MISC]\nHIMEM=SCORPION\nRAMSize=256\n" << misc << "[ULA]\nEvenM1=1\n";
        }
        Config config(_context);
        return config.LoadConfigFile(path);
    };

    ASSERT_TRUE(load(""));
    EXPECT_EQ(_context->config.scorpionTurboLogic, ScorpionTurboLogic::SC151);
    EXPECT_EQ(_context->config.even_M1, 1);

    ASSERT_TRUE(load("ScorpionTurboLogic=SC15.3\n"));
    EXPECT_EQ(_context->config.scorpionTurboLogic, ScorpionTurboLogic::SC153);
    EXPECT_EQ(_context->config.even_M1, 0) << "SC15.3 has no Even M1";

    ASSERT_TRUE(load("ScorpionTurboLogic=SC15.2\n"));
    EXPECT_EQ(_context->config.scorpionTurboLogic, ScorpionTurboLogic::SC151) << "unknown: the default";
}

/// [NETWORK] RemoteAccess (PLAN #92 N0): on by default (host listeners on 0.0.0.0); off / 0 / false / no = 127.0.0.1;
/// an unknown value keeps the default with a warning
TEST_F(Config_Test, NetworkRemoteAccess)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("network_remote_access_config_test.ini");
    auto load = [&](const std::string& network) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "[NETWORK]\n" << network;
        file.close();
        Config config(_context);
        return config.LoadConfigFile(path);
    };

    ASSERT_TRUE(load(""));
    EXPECT_EQ(_context->config.network.remoteAccess, 1) << "default: on";
    for (const char* off : {"off", "OFF", "0", "false", "no", " Off "})
    {
        ASSERT_TRUE(load(std::string("RemoteAccess=") + off + "\n"));
        EXPECT_EQ(_context->config.network.remoteAccess, 0) << off;
    }
    for (const char* on : {"on", "1", "true", "yes", "lan"})
    {
        ASSERT_TRUE(load(std::string("RemoteAccess=") + on + "\n"));
        EXPECT_EQ(_context->config.network.remoteAccess, 1) << on;
    }
    std::remove(path.c_str());
}
