#include "stdafx.h"
#include "pch.h"

#include "_helpers/testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "loaders/snapshot/loaderzxp.h"

#include <fstream>
#include <iterator>
#include <vector>

/// Parser tests of the ZX-Poly .zxp format against the public corpus
/// (testdata/machines/zxpoly/zxp, measurements in its README.md)
class LoaderZXP_Test : public ::testing::Test
{
protected:
    static std::string CorpusPath(const std::string& name)
    {
        return TestPathHelper::GetTestDataPath("machines/zxpoly/zxp/" + name);
    }

    static std::vector<uint8_t> ReadFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }
};

struct ZXPCorpusEntry
{
    const char* file;
    uint8_t port3D00;
    uint8_t pagesPerModule;
    uint16_t pc;
};

class LoaderZXPCorpus_Test : public LoaderZXP_Test, public ::testing::WithParamInterface<ZXPCorpusEntry>
{
};

TEST_P(LoaderZXPCorpus_Test, ParsesHeaderPagesAndRegisters)
{
    const ZXPCorpusEntry& entry = GetParam();
    LoaderZXP loader(nullptr, CorpusPath(entry.file));
    ASSERT_TRUE(loader.Parse()) << loader.GetError();

    const ZXPSnapshot& zxp = loader.GetSnapshot();
    EXPECT_EQ(zxp.port3D00, entry.port3D00);
    EXPECT_TRUE(zxp.IsLocked());
    EXPECT_TRUE(zxp.AreSlavesRunning());
    EXPECT_EQ(zxp.MappedCPU(), 0);

    for (size_t m = 0; m < ZXPSnapshot::MODULE_COUNT; m++)
    {
        const ZXPModuleState& module = zxp.modules[m];
        size_t used = 0;
        for (bool u : module.pageUsed)
            used += u ? 1 : 0;
        EXPECT_EQ(used, entry.pagesPerModule) << "module " << m;
        EXPECT_EQ(module.pc, entry.pc) << "module " << m;

        // Registers are identical across modules (the corpus measurement)
        EXPECT_EQ(module.sp, zxp.modules[0].sp);
        EXPECT_EQ(module.af, zxp.modules[0].af);
        EXPECT_EQ(module.port7FFD, zxp.modules[0].port7FFD);
    }

    // Slaves: IO writes disabled (R0 bit 4), heap windows at 128K/256K/384K
    EXPECT_EQ(zxp.modules[0].reg[0], 0x00);
    EXPECT_EQ(zxp.modules[1].reg[0], 0x12);
    EXPECT_EQ(zxp.modules[2].reg[0], 0x14);
    EXPECT_EQ(zxp.modules[3].reg[0], 0x16);
}

INSTANTIATE_TEST_SUITE_P(Corpus, LoaderZXPCorpus_Test,
    ::testing::Values(
        ZXPCorpusEntry{"Alien8.zxp", 0x91, 3, 0x1F3E},
        ZXPCorpusEntry{"buratino_adventures.zxp", 0x95, 8, 0x97A7},
        ZXPCorpusEntry{"ComandoQuatro.zxp", 0x91, 3, 0xFFFF},
        ZXPCorpusEntry{"flyshark.zxp", 0x9D, 8, 0xAA5B},
        ZXPCorpusEntry{"OFCZXPOLY.zxp", 0x99, 3, 0xFC6C},
        ZXPCorpusEntry{"SummerSanta2022.zxp", 0x91, 8, 0x7F5D},
        ZXPCorpusEntry{"fh.zxp", 0x91, 3, 0x65A7}),
    [](const ::testing::TestParamInfo<ZXPCorpusEntry>& info) {
        std::string name = info.param.file;
        return name.substr(0, name.find('.'));
    });

TEST_F(LoaderZXP_Test, RejectsBadMagic)
{
    std::vector<uint8_t> data = ReadFile(CorpusPath("fh.zxp"));
    ASSERT_FALSE(data.empty());
    data[0] ^= 0xFF;
    LoaderZXP loader(nullptr, "bad-magic");
    EXPECT_FALSE(loader.ParseBuffer(data.data(), data.size()));
    EXPECT_NE(loader.GetError().find("magic"), std::string::npos);
}

TEST_F(LoaderZXP_Test, RejectsTruncatedAndTrailingData)
{
    std::vector<uint8_t> data = ReadFile(CorpusPath("fh.zxp"));
    ASSERT_FALSE(data.empty());

    LoaderZXP truncated(nullptr, "truncated");
    EXPECT_FALSE(truncated.ParseBuffer(data.data(), data.size() - 1));

    data.push_back(0);
    LoaderZXP trailing(nullptr, "trailing");
    EXPECT_FALSE(trailing.ParseBuffer(data.data(), data.size()));
    EXPECT_NE(trailing.GetError().find("trailing"), std::string::npos);
}

TEST_F(LoaderZXP_Test, RejectsPageIndexOutOfRange)
{
    std::vector<uint8_t> data = ReadFile(CorpusPath("fh.zxp"));
    ASSERT_FALSE(data.empty());
    data[ZXPSnapshot::HEADER_SIZE + 1] = 8;    // module 0, first page index
    LoaderZXP loader(nullptr, "bad-page");
    EXPECT_FALSE(loader.ParseBuffer(data.data(), data.size()));
}

/// A .zxp is four machines: the group is planned first (each module's image on its own machine), then committed. One refusal
/// stops the load with no machine touched, and the reason names the module
class LoaderZXPGroup_Test : public LoaderZXP_Test
{
protected:
    void TearDown() override
    {
        for (const auto& id : EmulatorManager::GetInstance()->GetEmulatorIds())
            EmulatorManager::GetInstance()->RemoveEmulator(id);
    }

    std::array<EmulatorContext*, ZXPSnapshot::MODULE_COUNT> Machines(const char* thirdModel)
    {
        std::array<EmulatorContext*, ZXPSnapshot::MODULE_COUNT> contexts{};
        for (size_t m = 0; m < contexts.size(); m++)
        {
            auto emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("zxp-module-" + std::to_string(m),
                                                                                   m == 2 ? thirdModel : "128k", LoggerLevel::LogError);
            EXPECT_NE(emulator, nullptr);
            contexts[m] = emulator ? emulator->GetContext() : nullptr;
        }
        return contexts;
    }

    static uint64_t RamHash(EmulatorContext* context)
    {
        uint64_t h = 14695981038346656037ull;
        for (uint16_t page = 0; page < 8; page++)
        {
            const uint8_t* bytes = context->pMemory->RAMPageAddress(page);
            for (uint32_t i = 0; i < 16384; i++)
            {
                h ^= bytes[i];
                h *= 1099511628211ull;
            }
        }
        return h;
    }
};

TEST_F(LoaderZXPGroup_Test, TheGroupIsPlannedThenEachModuleIsCommittedFromItsImage)
{
    LoaderZXP loader(nullptr, CorpusPath("Alien8.zxp"));
    ASSERT_TRUE(loader.Parse()) << loader.GetError();
    const auto contexts = Machines("128k");
    ASSERT_TRUE(loader.Apply(contexts)) << loader.GetError();

    for (size_t m = 0; m < contexts.size(); m++)
    {
        const ZXPModuleState& module = loader.GetSnapshot().modules[m];
        EXPECT_EQ(loader.GetReport(m).format, "zxp") << "module " << m;
        EXPECT_EQ(loader.GetReport(m).commit, "legacy") << "module " << m;
        EXPECT_FALSE(loader.GetReport(m).refused);
        EXPECT_EQ(contexts[m]->pCore->GetZ80()->pc, module.pc) << "module " << m;
        EXPECT_EQ(contexts[m]->emulatorState.p7FFD, module.port7FFD) << "module " << m;
        for (uint8_t page = 0; page < 8; page++)
        {
            if (module.pageUsed[page])
                EXPECT_EQ(0, std::memcmp(contexts[m]->pMemory->RAMPageAddress(page), module.pages.data() + page * PAGE_SIZE, PAGE_SIZE))
                    << "module " << m << " page " << int(page);
        }
    }
}

TEST_F(LoaderZXPGroup_Test, OneRefusingModuleStopsTheWholeLoadBeforeAnythingIsWritten)
{
    LoaderZXP loader(nullptr, CorpusPath("flyshark.zxp"));
    ASSERT_TRUE(loader.Parse()) << loader.GetError();
    const auto contexts = Machines("48K");   // module 2 is a 48K: it has no banks 0, 1, 3, 4, 6, 7
    std::array<uint64_t, ZXPSnapshot::MODULE_COUNT> before{};
    for (size_t m = 0; m < contexts.size(); m++)
        before[m] = m == 2 ? 0 : RamHash(contexts[m]);
    const uint16_t pc0 = contexts[0]->pCore->GetZ80()->pc;

    EXPECT_FALSE(loader.Apply(contexts));
    EXPECT_NE(loader.GetError().find("module 2"), std::string::npos) << loader.GetError();
    EXPECT_TRUE(loader.GetReport(2).refused);
    EXPECT_EQ(loader.GetReport(2).needs, "model:128K");
    for (size_t m = 0; m < contexts.size(); m++)
    {
        if (m != 2)
            EXPECT_EQ(RamHash(contexts[m]), before[m]) << "module " << m << " was not touched";
    }
    EXPECT_EQ(contexts[0]->pCore->GetZ80()->pc, pc0) << "no CPU was written either";
}
