#include <gtest/gtest.h>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskautostart.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/trdosbootinjector.h"
#include "emulator/io/fdc/trdoscatalog.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/memory/memory.h"
#include "loaders/disk/loader_trd.h"

class DiskAutostart_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();
        _emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
    }

    void TearDown() override
    {
        if (_emulator != nullptr)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    static std::string TrdPath(const char* name) { return TestPathHelper::GetTestDataPath(std::string("loaders/trd/") + name); }

    /// Load an image standalone (not mounted)
    std::unique_ptr<DiskImage> LoadImage(const char* name)
    {
        LoaderTRD loader(_context, TrdPath(name));
        if (!loader.loadImage())
            return nullptr;
        // The loader owns the image: take a private copy of its content into a fresh image
        DiskImage* src = loader.getImage();
        auto copy = std::make_unique<DiskImage>(src->getCylinders(), src->getSides());
        for (uint8_t t = 0; t < src->getCylinders() * src->getSides(); t++)
            for (uint8_t s = 0; s < 16; s++)
            {
                uint8_t* data = src->getTrack(t)->getDataForSector(s);
                if (data)
                    copy->getTrack(t)->writeSectorData(s, data, 256);
            }
        copy->markClean();
        return copy;
    }
};

TEST_F(DiskAutostart_Test, Catalog_ParsesRealDisks)
{
    auto across = LoadImage("across_the_edge_by_demarche.trd");
    ASSERT_NE(across, nullptr);
    TrdosCatalog catalog;
    ASSERT_TRUE(catalog.Parse(*across));
    EXPECT_EQ(catalog.Files().size(), 16u);
    EXPECT_EQ(catalog.FindBoot(), nullptr);
    ASSERT_EQ(catalog.BasicFiles().size(), 1u);
    EXPECT_EQ(catalog.BasicFiles()[0]->TrimmedName(), "ACROSS");
    EXPECT_EQ(catalog.FreeSectors(), 1905u);

    auto atarin = LoadImage("atarin.trd");
    ASSERT_NE(atarin, nullptr);
    ASSERT_TRUE(catalog.Parse(*atarin));
    ASSERT_NE(catalog.FindBoot(), nullptr);
    EXPECT_EQ(catalog.FindBoot()->slot, 0);

    auto multi = LoadImage("zx-format8.trd");
    ASSERT_NE(multi, nullptr);
    ASSERT_TRUE(catalog.Parse(*multi));
    EXPECT_NE(catalog.FindBoot(), nullptr);
    EXPECT_EQ(catalog.BasicFiles().size(), 7u);
}

TEST_F(DiskAutostart_Test, Catalog_RejectsNonTrdos)
{
    DiskImage blank(80, 2);  // Formatted but without TR-DOS info sector content
    TrdosCatalog catalog;
    EXPECT_FALSE(catalog.Parse(blank));
}

TEST_F(DiskAutostart_Test, Injector_GeneratedBootIsVirtual)
{
    auto image = LoadImage("across_the_edge_by_demarche.trd");
    ASSERT_NE(image, nullptr);
    ASSERT_FALSE(image->isDirty());

    ASSERT_TRUE(TrdosBootInjector::InjectNamedBoot(*image, "ACROSS"));
    EXPECT_FALSE(image->isDirty()) << "Injected boot must not make the image dirty";
    EXPECT_FALSE(image->computeDirtyState());

    TrdosCatalog catalog;
    ASSERT_TRUE(catalog.Parse(*image));
    const TrdosFile* boot = catalog.FindBoot();
    ASSERT_NE(boot, nullptr);
    EXPECT_EQ(catalog.Files().size(), 17u);
    EXPECT_EQ(catalog.FreeSectors(), 1904u);
    EXPECT_EQ(boot->sectors, 1);

    // Sector CRC of the boot file's data is valid
    DiskImage::Track* track = image->getTrack(boot->firstTrack);
    ASSERT_NE(track, nullptr);
    DiskImage::Sector* sector = track->getSector(boot->firstSector);
    ASSERT_NE(sector, nullptr);
    EXPECT_TRUE(sector->isDataCRCValid());

    // Tokenised line: RANDOMIZE USR 15619: REM : RUN "ACROSS"
    const uint8_t* body = sector->data;
    EXPECT_EQ(body[0], 0x00);
    EXPECT_EQ(body[1], 0x01);
    EXPECT_EQ(body[4], 0xF9);
    EXPECT_EQ(body[5], 0xC0);

    // A second injection is refused (boot already present)
    EXPECT_FALSE(TrdosBootInjector::InjectNamedBoot(*image, "ACROSS"));
}

TEST_F(DiskAutostart_Test, Injector_CommanderOnManyBasicFiles)
{
    auto image = LoadImage("zx-format8.trd");
    ASSERT_NE(image, nullptr);

    // Remove the existing boot: mark its catalog entry deleted (first byte 0x01)
    uint8_t dir[256];
    memcpy(dir, image->getTrack(0)->getDataForSector(0), 256);
    TrdosCatalog before;
    ASSERT_TRUE(before.Parse(*image));
    dir[before.FindBoot()->slot * 16] = 0x01;
    image->getTrack(0)->writeSectorData(0, dir, 256);
    image->markClean();

    std::vector<uint8_t> commander = TrdosBootInjector::LoadBundledCommander();
    ASSERT_FALSE(commander.empty()) << "boot/boot.$b must be found next to the test binary";
    EXPECT_EQ(commander.size(), TrdosBootInjector::HOBETA_HEADER_SIZE + 27u * 256u);

    ASSERT_TRUE(TrdosBootInjector::InjectHobeta(*image, commander));
    EXPECT_FALSE(image->isDirty());

    TrdosCatalog after;
    ASSERT_TRUE(after.Parse(*image));
    ASSERT_NE(after.FindBoot(), nullptr);
    EXPECT_EQ(after.FindBoot()->sectors, 27);
    EXPECT_EQ(after.FreeSectors(), before.FreeSectors() - 27);
}

TEST_F(DiskAutostart_Test, Injector_RefusesWhenNoSpace)
{
    auto image = LoadImage("across_the_edge_by_demarche.trd");
    ASSERT_NE(image, nullptr);

    // Claim the disk is full
    uint8_t info[256];
    memcpy(info, image->getTrack(0)->getDataForSector(8), 256);
    info[TrdosCatalog::INFO_FREE_SECTORS] = 0;
    info[TrdosCatalog::INFO_FREE_SECTORS + 1] = 0;
    image->getTrack(0)->writeSectorData(8, info, 256);
    image->markClean();

    EXPECT_FALSE(TrdosBootInjector::InjectNamedBoot(*image, "ACROSS"));
}

TEST_F(DiskAutostart_Test, Plan_PerDiskContent)
{
    DiskAutostart* autostart = _context->pDiskAutostart;
    ASSERT_NE(autostart, nullptr);
    ASSERT_TRUE(autostart->IsTrdosCapable());
    EXPECT_TRUE(autostart->IsNameHookSupported());

    auto across = LoadImage("across_the_edge_by_demarche.trd");
    DiskAutostart::Plan plan = autostart->MakePlan(*across);
    EXPECT_EQ(plan.action, DiskAutostart::Action::BootNamed);
    EXPECT_EQ(plan.bootName, "ACROSS");

    auto atarin = LoadImage("atarin.trd");
    EXPECT_EQ(autostart->MakePlan(*atarin).action, DiskAutostart::Action::Boot);

    // No BASIC files at all: delete the only BASIC entry of across (keep code files)
    uint8_t dir[256];
    memcpy(dir, across->getTrack(0)->getDataForSector(0), 256);
    dir[0] = 0x01;
    across->getTrack(0)->writeSectorData(0, dir, 256);
    EXPECT_EQ(autostart->MakePlan(*across).action, DiskAutostart::Action::MountOnly);

    DiskImage blank(80, 2);
    EXPECT_EQ(autostart->MakePlan(blank).action, DiskAutostart::Action::MountOnly);
}

TEST_F(DiskAutostart_Test, Plan_UnsupportedMachine)
{
    EmulatorTestHelper::CleanupEmulator(_emulator);
    _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(_emulator, nullptr);
    _context = _emulator->GetContext();

    std::string reason;
    EXPECT_EQ(_context->pMemory->base_dos_rom == nullptr, !_context->pDiskAutostart->IsTrdosCapable(&reason));
    if (_context->pMemory->base_dos_rom == nullptr)
        EXPECT_FALSE(reason.empty());
}

/// End-to-end: real TR-DOS ROM boots the disk after AutostartDisk()
class DiskAutostart_Boot_Test : public DiskAutostart_Test
{
protected:
    /// First bytes of a catalog file's data
    std::vector<uint8_t> FileHead(DiskImage& image, const TrdosFile& file, size_t count)
    {
        uint8_t* data = image.getTrack(file.firstTrack)->getDataForSector(file.firstSector);
        return std::vector<uint8_t>(data, data + count);
    }

    /// True when RAM at the BASIC program start (PROG) begins with the given bytes
    bool ProgramIs(const std::vector<uint8_t>& head)
    {
        const uint16_t prog = EmulatorTestHelper::ReadSysVar16(_emulator, 0x5C53);
        if (prog < 0x5CC0)
            return false;
        for (size_t i = 0; i < head.size(); i++)
        {
            if (_context->pMemory->MapZ80AddressToPhysicalAddress(static_cast<uint16_t>(prog + i))[0] != head[i])
                return false;
        }
        return true;
    }

    /// Runs frames until the condition holds. Returns frames used or -1
    template <typename F>
    int RunUntilTrue(F cond, int maxFrames)
    {
        for (int frames = 0; frames < maxFrames; frames += 5)
        {
            _emulator->RunNFrames(5, true);
            if (cond())
                return frames + 5;
        }
        return -1;
    }

    std::string State()
    {
        char buf[200];
        snprintf(buf, sizeof(buf), "pc=%04X flags=%02X p7FFD=%02X prog=%04X eline=%04X", _context->pCore->GetZ80()->pc,
                 (unsigned)_context->emulatorState.flags, _context->emulatorState.p7FFD,
                 EmulatorTestHelper::ReadSysVar16(_emulator, 0x5C53), EmulatorTestHelper::ReadSysVar16(_emulator, 0x5C59));
        return buf;
    }
};

TEST_F(DiskAutostart_Boot_Test, ExistingBoot_RunsThroughRom)
{
    auto result = _emulator->AutostartDisk(TrdPath("atarin.trd"));
    ASSERT_TRUE(result.mounted);
    ASSERT_TRUE(result.started) << result.message;

    DiskImage* image = _context->coreState.diskImages[0];
    TrdosCatalog catalog;
    ASSERT_TRUE(catalog.Parse(*image));
    std::vector<uint8_t> head = FileHead(*image, *catalog.FindBoot(), 8);

    int frames = RunUntilTrue([&] { return ProgramIs(head); }, 1500);
    EXPECT_GT(frames, 0) << "boot.B did not get loaded: " << State();
}

TEST_F(DiskAutostart_Boot_Test, SingleBasic_RunsByName_DiskUntouched)
{
    auto result = _emulator->AutostartDisk(TrdPath("across_the_edge_by_demarche.trd"));
    ASSERT_TRUE(result.mounted);
    ASSERT_TRUE(result.started) << result.message;

    DiskImage* image = _context->coreState.diskImages[0];
    TrdosCatalog catalog;
    ASSERT_TRUE(catalog.Parse(*image));
    EXPECT_EQ(catalog.FindBoot(), nullptr) << "Named autostart must not inject anything";
    std::vector<uint8_t> head = FileHead(*image, *catalog.BasicFiles()[0], 8);

    int frames = RunUntilTrue([&] { return ProgramIs(head); }, 1500);
    EXPECT_GT(frames, 0) << "RUN \"ACROSS\" did not load the program: " << State();
    EXPECT_FALSE(image->isDirty());
    EXPECT_FALSE(_context->pDiskAutostart->IsArmed()) << "Hook must be one-shot";
}

TEST_F(DiskAutostart_Boot_Test, ManyBasic_InjectedCommanderRuns)
{
    // zx-format8 minus its boot: several BASIC programs, no boot
    auto prepared = LoadImage("zx-format8.trd");
    ASSERT_NE(prepared, nullptr);
    // Work on the mounted image: mount first, then delete boot in place and mark clean
    ASSERT_TRUE(_emulator->LoadDisk(TrdPath("zx-format8.trd")));
    DiskImage* image = _context->coreState.diskImages[0];
    TrdosCatalog before;
    ASSERT_TRUE(before.Parse(*image));
    uint8_t dir[256];
    memcpy(dir, image->getTrack(0)->getDataForSector(0), 256);
    dir[before.FindBoot()->slot * 16] = 0x01;
    image->getTrack(0)->writeSectorData(0, dir, 256);
    image->markClean();

    // Autostart again on the already mounted (modified) image
    DiskAutostart* autostart = _context->pDiskAutostart;
    DiskAutostart::Plan plan = autostart->Prepare(*image);
    ASSERT_EQ(plan.action, DiskAutostart::Action::BootCommander) << plan.message;
    EXPECT_FALSE(image->isDirty());
    _context->pCore->Reset(RM_DOS);

    TrdosCatalog after;
    ASSERT_TRUE(after.Parse(*image));
    ASSERT_NE(after.FindBoot(), nullptr);
    std::vector<uint8_t> head = FileHead(*image, *after.FindBoot(), 8);

    int frames = RunUntilTrue([&] { return ProgramIs(head); }, 1500);
    EXPECT_GT(frames, 0) << "Commander boot did not load: " << State();
}

TEST_F(DiskAutostart_Boot_Test, MachineWithoutTrdos_OnlyMounts)
{
    EmulatorTestHelper::CleanupEmulator(_emulator);
    _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(_emulator, nullptr);
    _context = _emulator->GetContext();
    if (_context->pDiskAutostart->IsTrdosCapable())
        GTEST_SKIP() << "This 48K configuration has a TR-DOS ROM";

    auto result = _emulator->AutostartDisk(TrdPath("atarin.trd"));
    EXPECT_TRUE(result.mounted);
    EXPECT_FALSE(result.started);
    EXPECT_FALSE(result.message.empty());
}

TEST_F(DiskAutostart_Boot_Test, SingleBasic_WithoutHook_DoesNotLoad)
{
    // Negative control: the cold start alone only tries RUN "boot" and fails on a disk without boot
    auto result = _emulator->AutostartDisk(TrdPath("across_the_edge_by_demarche.trd"));
    ASSERT_TRUE(result.started);
    _context->pDiskAutostart->Disarm();

    DiskImage* image = _context->coreState.diskImages[0];
    TrdosCatalog catalog;
    ASSERT_TRUE(catalog.Parse(*image));
    std::vector<uint8_t> head = FileHead(*image, *catalog.BasicFiles()[0], 8);

    EXPECT_EQ(RunUntilTrue([&] { return ProgramIs(head); }, 600), -1);
}

/// Models with a TR-DOS ROM set: the direct entry must boot on each of them
class DiskAutostart_Models_Test : public ::testing::TestWithParam<std::pair<const char*, int>>
{
};

TEST_P(DiskAutostart_Models_Test, ExistingBootRuns)
{
    MessageCenter::DisposeDefaultMessageCenter();
    const auto param = GetParam();
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(param.first, LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();

    if (!context->pDiskAutostart->IsTrdosCapable())
    {
        EmulatorTestHelper::CleanupEmulator(emulator);
        GTEST_SKIP() << param.first << " has no TR-DOS";
    }

    auto result = emulator->AutostartDisk(TestPathHelper::GetTestDataPath("loaders/trd/atarin.trd"));
    ASSERT_TRUE(result.started) << result.message;

    DiskImage* image = context->coreState.diskImages[0];
    TrdosCatalog catalog;
    ASSERT_TRUE(catalog.Parse(*image));
    uint8_t* data = image->getTrack(catalog.FindBoot()->firstTrack)->getDataForSector(catalog.FindBoot()->firstSector);
    std::vector<uint8_t> head(data, data + 8);

    bool loaded = false;
    for (int frames = 0; frames < 1500 && !loaded; frames += 5)
    {
        emulator->RunNFrames(5, true);
        const uint16_t prog = EmulatorTestHelper::ReadSysVar16(emulator, 0x5C53);
        if (prog >= 0x5CC0)
        {
            loaded = true;
            for (size_t i = 0; i < head.size(); i++)
                loaded = loaded && context->pMemory->MapZ80AddressToPhysicalAddress(static_cast<uint16_t>(prog + i))[0] == head[i];
        }
    }
    EXPECT_TRUE(loaded) << param.first << ": boot not loaded, pc=" << std::hex << context->pCore->GetZ80()->pc
                        << " flags=" << (unsigned)context->emulatorState.flags;

    EmulatorTestHelper::CleanupEmulator(emulator);
}

INSTANTIATE_TEST_SUITE_P(Models, DiskAutostart_Models_Test,
                         ::testing::Values(std::make_pair("Pentagon", 0), std::make_pair("Scorpion", 0),
                                           std::make_pair("ATM710", 0), std::make_pair("ProfScorp", 0)));

TEST(DiskAutostart_Unsupported, Atm3ReportsReason)
{
    MessageCenter::DisposeDefaultMessageCenter();
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    std::string reason;
    EXPECT_FALSE(emulator->GetContext()->pDiskAutostart->IsTrdosCapable(&reason));
    EXPECT_NE(reason.find("BaseConf"), std::string::npos);

    auto result = emulator->AutostartDisk(TestPathHelper::GetTestDataPath("loaders/trd/atarin.trd"));
    EXPECT_TRUE(result.mounted);
    EXPECT_FALSE(result.started);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(DiskAutostart_Unsupported, NoTrdosRomMachinesOnlyMount)
{
    for (const char* model : {"48K", "128K"})
    {
        MessageCenter::DisposeDefaultMessageCenter();
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        ASSERT_NE(emulator, nullptr) << model;
        EmulatorContext* context = emulator->GetContext();
        if (!context->pDiskAutostart->IsTrdosCapable())
        {
            auto result = emulator->AutostartDisk(TestPathHelper::GetTestDataPath("loaders/trd/atarin.trd"));
            EXPECT_TRUE(result.mounted) << model;
            EXPECT_FALSE(result.started) << model;
            EXPECT_EQ(result.message, "This machine has no TR-DOS") << model;
        }
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}
