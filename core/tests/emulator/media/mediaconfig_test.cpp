// MediaConfig: [MEDIA] keyed by slot id, legacy [ZC] / [NGS] / [HDD] keys,
// paths relative to the config file (technical design §7)

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "_helpers/scratchfolder.h"
#include "common/filehelper.h"
#include "common/inifile.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/media/mediaconfig.h"
#include "emulator/media/mediamanager.h"

namespace
{
    const MediaSetEntry* Find(const std::vector<MediaSetEntry>& set, const std::string& slot)
    {
        auto it = std::find_if(set.begin(), set.end(), [&slot](const MediaSetEntry& e) { return e.slotId == slot; });
        return it == set.end() ? nullptr : &*it;
    }

    std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    class FakeSlot : public IMediaSlot
    {
    public:
        explicit FakeSlot(std::string id)
        {
            _d.id = std::move(id);
            _d.acceptsFolder = true;
            _d.swapDelayMs = 500;
        }
        const SlotDescriptor& Descriptor() const override { return _d; }
        void Attach(Medium& medium) override { attached = &medium; }
        void Detach() override { attached = nullptr; }
        void SetWriteProtectSwitch(bool on) override { wp = on; }
        Medium* attached = nullptr;
        bool wp = false;

    private:
        SlotDescriptor _d;
    };
}  // namespace

TEST(MediaConfig_Test, MediaSectionKeysOptionsAndPaths)
{
    ScratchFolder config("mediaconfig");
    config.File("images/card.img", std::string(4096, '\0'));
    config.Folder("sdfolder");
    const std::string folder = Utf8(config.Path());

    IniFile ini;
    ini.LoadData(
        "[MEDIA]\n"
        "sd.zc = sdfolder\n"
        "sd.zc.access = readonly\n"
        "sd.zc.fs = FAT32\n"
        "sd.zc.codepage = cp1251\n"
        "sd.zc.free = 1048576\n"
        "sd.zc.wp = 1\n"
        "sd.zc.swapdelay = 250\n"
        "ide0.master = images/card.img\n"
        "fdd.a.access = sometimes\n");
    std::vector<std::string> report;
    const auto set = MediaConfig::FromIni(ini, folder, &report);

    const MediaSetEntry* sd = Find(set, "sd.zc");
    ASSERT_NE(sd, nullptr) << "a slot id with a dot, options split off only when known";
    EXPECT_EQ(sd->source.type, MediaSourceType::Folder);
    EXPECT_EQ(FileHelper::AbsolutePath(sd->source.path), FileHelper::AbsolutePath(Utf8(config.Path() / "sdfolder")))
        << "relative to the config folder";
    EXPECT_EQ(sd->access.value_or(AccessMode::Session), AccessMode::ReadOnly);
    EXPECT_EQ(sd->fs.value_or(FatType::Fat16), FatType::Fat32);
    EXPECT_EQ(sd->codePage.value_or(CodePage::Cp866), CodePage::Cp1251);
    EXPECT_EQ(sd->freeBytes.value_or(0), 1048576u);
    EXPECT_TRUE(sd->writeProtect.value_or(false));
    EXPECT_EQ(sd->swapDelayMs.value_or(0), 250u);
    EXPECT_FALSE(sd->legacy);

    const MediaSetEntry* hdd = Find(set, "ide0.master");
    ASSERT_NE(hdd, nullptr);
    EXPECT_EQ(hdd->source.type, MediaSourceType::File);

    ASSERT_EQ(report.size(), 1u);
    EXPECT_NE(report[0].find("fdd.a.access"), std::string::npos);
}

TEST(MediaConfig_Test, LegacyKeysFillOnlyWhatMediaLeavesUnset)
{
    IniFile ini;
    ini.LoadData(
        "[ZC]\nSDCARD=wc.img\nSDWrite=persist\nSDWriteProtect=1\n"
        "[NGS]\nSDCardImage=/cards/ngs.img\n"
        "[HDD]\nImage0=hdd.img\nHD0RO=1\nImage1=\n");
    const auto legacyOnly = MediaConfig::FromIni(ini, "/configs/atm3");

    const MediaSetEntry* zc = Find(legacyOnly, "sd.zc");
    ASSERT_NE(zc, nullptr);
    EXPECT_TRUE(zc->legacy);
    EXPECT_EQ(zc->source.path, FileHelper::LexicallyNormalPath("/configs/atm3/wc.img"));
    EXPECT_EQ(zc->access.value_or(AccessMode::Session), AccessMode::WriteThrough) << "SDWrite=persist";
    EXPECT_TRUE(zc->writeProtect.value_or(false));
    const MediaSetEntry* ngs = Find(legacyOnly, "sd.ngs");
    ASSERT_NE(ngs, nullptr);
    EXPECT_FALSE(ngs->access.has_value()) << "no [NGS] SDWrite: the slot's default";
    EXPECT_FALSE(ngs->writeProtect.value_or(false));
    const MediaSetEntry* master = Find(legacyOnly, "ide0.master");
    ASSERT_NE(master, nullptr);
    EXPECT_EQ(master->access.value_or(AccessMode::Session), AccessMode::ReadOnly) << "HD0RO=1";
    EXPECT_EQ(Find(legacyOnly, "ide0.slave"), nullptr) << "an empty Image1 sets nothing";

    IniFile both;
    both.LoadData("[MEDIA]\nsd.zc = /new/card.img\n[ZC]\nSDCARD=wc.img\n");
    const auto set = MediaConfig::FromIni(both, "/configs/atm3");
    ASSERT_NE(Find(set, "sd.zc"), nullptr);
    EXPECT_EQ(Find(set, "sd.zc")->source.path, FileHelper::LexicallyNormalPath("/new/card.img")) << "[MEDIA] wins";
    EXPECT_FALSE(Find(set, "sd.zc")->legacy);
}

/// [HDD] Image2 / Image3 (HD2RO / HD3RO): the Sprinter's secondary channel, ide1.master / ide1.slave
TEST(MediaConfig_Test, LegacyKeysForTheSecondChannel)
{
    IniFile ini;
    ini.LoadData("[HDD]\nImage2=c.img\nHD2RO=1\nImage3=d.img\n");
    const auto set = MediaConfig::FromIni(ini, "/configs/sprinter");
    const MediaSetEntry* master = Find(set, "ide1.master");
    ASSERT_NE(master, nullptr);
    EXPECT_EQ(master->source.path, FileHelper::LexicallyNormalPath("/configs/sprinter/c.img"));
    EXPECT_EQ(master->access.value_or(AccessMode::Session), AccessMode::ReadOnly) << "HD2RO=1";
    const MediaSetEntry* slave = Find(set, "ide1.slave");
    ASSERT_NE(slave, nullptr);
    EXPECT_FALSE(slave->access.has_value());
    EXPECT_EQ(Find(set, "ide0.master"), nullptr);
}

/// [NGS] SDWrite (session / persist / off) and SDWriteProtect describe the
/// NeoGS card's slot sd.ngs, as [ZC] SDWrite / SDWriteProtect describe sd.zc
TEST(MediaConfig_Test, LegacyNeoGSWriteModeAndSwitch)
{
    const struct
    {
        const char* write;
        AccessMode access;
    } cases[] = {{"session", AccessMode::Session}, {"persist", AccessMode::WriteThrough}, {"off", AccessMode::ReadOnly}};
    for (const auto& c : cases)
    {
        IniFile ini;
        ini.LoadData(std::string("[NGS]\nSDCardImage=ngs.img\nSDWriteProtect=1\nSDWrite=") + c.write + "\n");
        const auto set = MediaConfig::FromIni(ini, "/configs/p1024");
        const MediaSetEntry* ngs = Find(set, "sd.ngs");
        ASSERT_NE(ngs, nullptr) << c.write;
        EXPECT_EQ(ngs->source.path, FileHelper::LexicallyNormalPath("/configs/p1024/ngs.img"));
        EXPECT_EQ(ngs->access.value_or(AccessMode::Session), c.access) << c.write;
        EXPECT_TRUE(ngs->writeProtect.value_or(false)) << c.write;
    }
}

TEST(MediaConfig_Test, ManagerAppliesTheSetBeforeTheFirstReset)
{
    ScratchFolder config("mediaconfig-apply");
    config.File("card.img", std::string(8 * 512, '\0'));
    IniFile ini;
    ini.LoadData("[MEDIA]\nsd.zc = card.img\nsd.zc.wp = 1\nsd.zc.swapdelay = 0\nnope.slot = card.img\n[NGS]\nSDCARD=ngs.img\n");
    const auto set = MediaConfig::FromIni(ini, Utf8(config.Path()));

    MediaManager manager(nullptr);
    FakeSlot slot("sd.zc");
    manager.RegisterSlot(slot);
    const auto problems = manager.ApplyConfiguredMedia(set);

    ASSERT_NE(slot.attached, nullptr) << "inserted at once";
    EXPECT_TRUE(slot.wp);
    ASSERT_EQ(problems.size(), 1u) << "the legacy sd.ngs entry is quiet on a machine without NeoGS";
    EXPECT_NE(problems[0].find("nope.slot"), std::string::npos);
    manager.UnregisterSlot("sd.zc");
}
