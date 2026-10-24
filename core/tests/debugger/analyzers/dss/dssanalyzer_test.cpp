// DssAnalyzer tests: pure unit tests of the kernel fingerprint (against the
// real 1.71.66 system.dos), the MEMTAB block audit and the FM handle audit,
// plus one integration test that boots DSS 1.71.66 from a CompactFlash card
// (the Bios306_Dss171BootsFromACompactFlashCard flow) with the analyzer armed.

#include "pch.h"

#include "emulator/machines/sprinter/sprinterdssmedia.h"
#include "emulator/machines/sprinter/sprinterfixture.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/dss/dssanalyzer.h"
#include "debugger/analyzers/osaware/osaware_event.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/video/screen.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace
{

std::vector<uint8_t> ReadAllBytes(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>());
}

} // namespace

TEST(DssAnalyzer_Unit, FingerprintMatchesTheReal17166Kernel)
{
    const std::string path = TestPathHelper::GetTestDataPath("machines/sprinter/dss/1.71.66/system.dos");
    const auto kernel = ReadAllBytes(path);
    if (kernel.empty())
        GTEST_SKIP() << "testdata/machines/sprinter/dss/1.71.66/system.dos is missing";

    ASSERT_GE(kernel.size(), 0x4000u);
    EXPECT_EQ(DssAnalyzer::matchProfile(kernel.data()), 0) << "the shipped build must fingerprint";
    EXPECT_TRUE(DssAnalyzer::hasDssShape(kernel.data()));
}

TEST(DssAnalyzer_Unit, FingerprintRejectsForeignAndDriftedKernels)
{
    std::vector<uint8_t> empty(0x4000, 0);
    EXPECT_FALSE(DssAnalyzer::hasDssShape(empty.data()));
    EXPECT_EQ(DssAnalyzer::matchProfile(empty.data()), -1);
    EXPECT_FALSE(DssAnalyzer::hasDssShape(nullptr));

    // Shape without a known word pair -> -1 (the analyzer's unknown-build mode)
    std::vector<uint8_t> shape(0x4000, 0);
    const uint8_t stub[] = {0xC3, 0x34, 0x12, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                            0xF5, 0x3E, 0x00, 0xD3, 0x7C, 0xF1, 0xC9, 0xFF,
                            0xC3, 0x78, 0x56};
    std::copy(stub, stub + sizeof(stub), shape.begin());
    EXPECT_TRUE(DssAnalyzer::hasDssShape(shape.data()));
    EXPECT_EQ(DssAnalyzer::matchProfile(shape.data()), -1);
}

TEST(DssAnalyzer_Unit, OwnedBlockIdsFindsOnlyTheTaskEntries)
{
    uint8_t memtab[256] = {0};
    memtab[3] = 1;
    memtab[7] = 1;
    memtab[9] = 2;
    memtab[200] = 0xFF; // system-owned

    const auto ofOne = DssAnalyzer::ownedBlockIds(memtab, 1);
    ASSERT_EQ(ofOne.size(), 2u);
    EXPECT_EQ(ofOne[0], 3);
    EXPECT_EQ(ofOne[1], 7);

    EXPECT_TRUE(DssAnalyzer::ownedBlockIds(memtab, 5).empty());
    EXPECT_TRUE(DssAnalyzer::ownedBlockIds(memtab, 0).empty()); // 0 is "free", not a task
    EXPECT_TRUE(DssAnalyzer::ownedBlockIds(nullptr, 1).empty());
}

TEST(DssAnalyzer_Unit, FmHandlesOfTaskReturnsNamesAndOwners)
{
    const DssProfile& profile = DssAnalyzer::knownProfiles()[0];
    ASSERT_NE(profile.fmBuf, 0);

    std::vector<uint8_t> fm(static_cast<uint32_t>(profile.fmCount) * profile.fmStride, 0);
    // Entry 0: owned by task 2, name "SYSTEM  DOS"
    const char name[] = "SYSTEM  DOS";
    std::copy(name, name + 11, fm.begin());
    fm[profile.fmOwnerOff] = 2;
    // Entry 1: free
    // Entry 2: owned by task 3, name "LOG     TXT"
    const char name2[] = "LOG     TXT";
    std::copy(name2, name2 + 11, fm.begin() + 2u * profile.fmStride);
    fm[2u * profile.fmStride + profile.fmOwnerOff] = 3;

    const auto ofTwo = DssAnalyzer::fmHandlesOfTask(fm.data(), profile, 2);
    ASSERT_EQ(ofTwo.size(), 1u);
    EXPECT_EQ(ofTwo[0].index, 0);
    EXPECT_EQ(ofTwo[0].owner, 2);
    EXPECT_EQ(ofTwo[0].name, "SYSTEM  DOS");

    const auto ofThree = DssAnalyzer::fmHandlesOfTask(fm.data(), profile, 3);
    ASSERT_EQ(ofThree.size(), 1u);
    EXPECT_EQ(ofThree[0].name, "LOG     TXT");

    EXPECT_TRUE(DssAnalyzer::fmHandlesOfTask(fm.data(), profile, 1).empty());
}

namespace
{

/// The Sprinter boot fixture (SprinterBoot_Test) reduced to what the DSS
/// analyzer test needs: BIOS 3.06 Hotfix 2, a CF-booted DSS 1.71.66 disk.
class DssAnalyzer_Boot_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;

    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);

        _emulator = _manager->CreateEmulatorWithModelAndRAM("dss-analyzer", "SPRINTER", 4096, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
        _decoder->GetRtc().SetFixedTime(1767268830);
        ASSERT_TRUE(SprinterFixture::SelectBios(_context, "sp2k-3.06-hf2.rom"));
        _context->config.sprinter.fast_start = 1;
        _emulator->Reset();
        _emulator->EnableTurboMode();
    }

    void TearDown() override
    {
        _emulator.reset();
        if (_manager)
        {
            for (const auto& id : _manager->GetEmulatorIds())
                _manager->RemoveEmulator(id);
        }
    }

    std::string ScreenText()
    {
        const SprinterVideoRam& vram = _decoder->GetVideoRam();
        std::string text;
        for (uint8_t page = 0; page < 2; page++)
        {
            for (uint8_t b = 0; b < 32; b++)
            {
                for (uint8_t a = 0; a < 40; a++)
                {
                    for (uint8_t half = 0; half < 2; half++)
                    {
                        const uint32_t line = (1u + 2u * a + half + 0x80u * page) * 1024u;
                        const uint8_t c = vram.Read(line + 0x301 + 4u * b);
                        text.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ');
                    }
                }
                text.push_back('\n');
            }
        }
        return text;
    }

    bool ScreenHas(const std::string& needle) { return ScreenText().find(needle) != std::string::npos; }

    void InsertHdd(const std::string& path, const char* slot = "ide0.master")
    {
        ASSERT_NE(_context->pMediaManager, nullptr);
        MediaSource source;
        source.path = path;
        InsertOptions options;
        options.immediate = true;
        options.access = AccessMode::Session;
        const auto result = _context->pMediaManager->Insert(slot, source, options);
        ASSERT_TRUE(result.Ok()) << slot << ": " << result.message;
    }

    IOSAnalyzer* OsAnalyzer()
    {
        AnalyzerManager* manager = _context->pDebugManager->GetAnalyzerManager();
        return dynamic_cast<IOSAnalyzer*>(manager->getAnalyzer("dss"));
    }
};

} // namespace

/// Plan #99, classes A/B/C wiring on the real kernel: the analyzer detects
/// the 1.71.66 build, streams DSS and BIOS calls, and the snapshot shows the
/// block owners of the running system. Boot-bound (BIOS POST + DSS boot), the
/// turbo mode on.
TEST_F(DssAnalyzer_Boot_Test, DetectsKernelStreamsCallsAndTracksBlocks)
{
    const std::string dss = TestPathHelper::GetTestDataPath("machines/sprinter/dss/1.71.66/");
    const auto loader = ReadAllBytes(dss + "DSSloader.bin");
    const auto kernel = ReadAllBytes(dss + "system.dos");
    const auto shell = ReadAllBytes(dss + "system.exe");
    if (loader.empty() || kernel.empty() || shell.empty())
        GTEST_SKIP() << "testdata/machines/sprinter/dss/1.71.66 is missing";
    constexpr size_t kSectors13 = 1443;
    ASSERT_EQ(loader.size(), kSectors13 + 276);
    const std::vector<uint8_t> sectors13(loader.begin(), loader.begin() + kSectors13);
    const std::vector<uint8_t> sector0(loader.begin() + kSectors13, loader.end());
    const std::string bat = "ver\r\n";
    std::vector<uint8_t> disk = BuildDssHdd(
        sectors13,
        {{"SYSTEM  DOS", kernel},
         {"SYSTEM  EXE", shell},
         {"SYSTEM  BAT", std::vector<uint8_t>(bat.begin(), bat.end())}},
        sector0);
    const std::string image = TestPathHelper::GetUniqueTestScratchPath("dss-analyzer-cf.img");
    ASSERT_TRUE(FileHelper::SaveBufferToFile(image, disk.data(), disk.size()));

    std::string error;
    ASSERT_TRUE(_context->pIdeController->SetUnitKind(0, IdeController::UnitKind::CompactFlash, &error)) << error;
    InsertHdd(image);

    AnalyzerManager* manager = _context->pDebugManager->GetAnalyzerManager();
    ASSERT_TRUE(manager->activate("dss"));
    IOSAnalyzer* osAnalyzer = OsAnalyzer();
    ASSERT_NE(osAnalyzer, nullptr);

    // RunFrame(false): the analyzer's traps fire only with breakpoints on
    // (EmulatorTestHelper::RunUntil hardcodes the skip-breakpoints mode)
    for (int i = 0; i < 1500; i++)
    {
        _emulator->RunFrame(false);
        if (((i + 1) % 5 == 0) && ScreenHas("Estex DSS version 1.71.66"))
            break;
    }
    ASSERT_TRUE(ScreenHas("Estex DSS version 1.71.66. Shell version 1.2.523.")) << ScreenText();

    const auto events = osAnalyzer->getOsEvents();
    const auto has = [&](OsEventKind kind) {
        return std::any_of(events.begin(), events.end(),
                           [kind](const OsEvent& e) { return e.kind == kind; });
    };
    EXPECT_TRUE(has(OsEventKind::OS_DETECTED));
    EXPECT_TRUE(has(OsEventKind::SYSCALL)) << "the boot must produce decoded DSS/BIOS calls";

    // The kernel's own allocations (DEPLOY blocks) show up as block owners
    const OsSnapshot snapshot = osAnalyzer->getOsSnapshot();
    EXPECT_TRUE(snapshot.detected);
    EXPECT_EQ(snapshot.profile, "dss-1.71.66");
    EXPECT_FALSE(snapshot.blocks.empty()) << "MEMTAB must show owned blocks after boot";

    std::remove(image.c_str());
}
