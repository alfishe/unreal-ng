// SPG snapshots (TSConf implementation-plan phase 6 SPG-1...3): the parser,
// the MegaLZ / Hrust depackers on real TS-Conf SDK programs
// (testdata/machines/tsconf/spg, README there) and loading into the machine.
//
// The depacked block hashes were recorded after a byte-for-byte comparison of
// every compressed block with lvd's mhmt depacker (`mhmt -d -mlz` / `-hst`,
// the reference implementation) - 26 blocks, all equal.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/ports/models/portdecoder_tsconf.h"
#include "emulator/video/screen.h"
#include "loaders/snapshot/loaderspg.h"
#include "loaders/snapshot/snapshotlauncher.h"
#include "loaders/snapshot/zxdepackers.h"
#include "pch.h"
#include "stdafx.h"

namespace
{
    std::vector<uint8_t> ReadSpg(const std::string& name)
    {
        const std::string path = (TestPathHelper::FindProjectRoot() / "testdata" / "machines" / "tsconf" / "spg" / name).string();
        std::vector<uint8_t> data(FileHelper::GetFileSize(path));
        if (!data.empty())
            FileHelper::ReadFileToBuffer(path, data.data(), data.size());
        return data;
    }

    uint64_t Fnv(uint64_t h, const std::vector<uint8_t>& bytes)
    {
        for (uint8_t b : bytes)
        {
            h ^= b;
            h *= 0x100000001b3ULL;
        }
        return h;
    }

    struct SpgGolden
    {
        const char* file;
        size_t blocks;
        uint16_t pc, sp;
        uint8_t page3, clock;
        bool interrupts;
        uint64_t hash;  // FNV-1a over every block's depacked bytes, in order
    };

    const SpgGolden kSpgGolden[] = {
        {"empty.spg", 4, 0xE000, 0xDFFF, 0x17, 2, true, 0x72ED070C52ED9632ull},
        {"sprites.spg", 8, 0xE000, 0xDFFF, 0x17, 2, true, 0xA7A14F3FF21BFDEEull},
        {"slideshow.spg", 14, 0xE000, 0xDFFF, 0x17, 2, true, 0xE63E6AA164785EDCull},
    };
}  // namespace

/// SPG-1 / SPG-2: header fields and every block (Hrust and MegaLZ) depacked
TEST(LoaderSPG_Test, SPG12_ParsesAndDepacksRealPrograms)
{
    const bool record = std::getenv("UNREALNG_RECORD_SPG_GOLDEN") != nullptr;
    for (const SpgGolden& g : kSpgGolden)
    {
        SCOPED_TRACE(g.file);
        const std::vector<uint8_t> data = ReadSpg(g.file);
        ASSERT_FALSE(data.empty()) << "testdata missing";
        LoaderSPG::Image image;
        std::string error;
        ASSERT_TRUE(LoaderSPG::Parse(data, image, error)) << error;
        EXPECT_EQ(image.version, 0x10);
        EXPECT_EQ(image.blocks.size(), g.blocks);
        EXPECT_EQ(image.pc, g.pc);
        EXPECT_EQ(image.sp, g.sp);
        EXPECT_EQ(image.page3, g.page3);
        EXPECT_EQ(image.clock, g.clock);
        EXPECT_EQ(image.interrupts, g.interrupts);

        uint64_t hash = 0xcbf29ce484222325ULL;
        for (const LoaderSPG::Block& block : image.blocks)
        {
            EXPECT_EQ(block.data.size(), 16384u) << "every SDK block depacks to one page";
            hash = Fnv(hash, block.data);
        }
        if (record)
            std::printf("    {\"%s\", ..., 0x%016llXull},\n", g.file, static_cast<unsigned long long>(hash));
        else
            EXPECT_EQ(hash, g.hash) << "depacked bytes differ from the mhmt-verified ones";
    }
}

/// SPG-3: v1.1 is accepted (the ancestor refused it); other versions and a
/// truncated or corrupt file are refused with a reason
TEST(LoaderSPG_Test, SPG3_VersionsAndBrokenFiles)
{
    std::vector<uint8_t> data = ReadSpg("empty.spg");
    ASSERT_FALSE(data.empty());
    LoaderSPG::Image image;
    std::string error;

    data[0x2C] = 0x11;
    EXPECT_TRUE(LoaderSPG::Parse(data, image, error)) << error;
    data[0x2C] = 0x02;
    EXPECT_FALSE(LoaderSPG::Parse(data, image, error));
    EXPECT_NE(error.find("not supported"), std::string::npos) << error;

    data[0x2C] = 0x10;
    std::vector<uint8_t> truncated(data.begin(), data.begin() + 0x400 + 512);
    EXPECT_FALSE(LoaderSPG::Parse(truncated, image, error));
    EXPECT_NE(error.find("past the end"), std::string::npos) << error;

    std::vector<uint8_t> corrupt = data;
    for (size_t i = 0x400 + 8; i < 0x400 + 600; i++)
        corrupt[i] = 0x00;  // a Hrust stream of zero bits: matches before any data
    EXPECT_FALSE(LoaderSPG::Parse(corrupt, image, error));

    std::vector<uint8_t> notSpg(2048, 0);
    EXPECT_FALSE(LoaderSPG::Parse(notSpg, image, error));
}

/// Loading: only on TS-Conf; the registers, the memory map and the blocks
/// land; the SDK's empty project then runs from #E000 at 14 MHz
TEST(LoaderSPG_Test, LoadsAndRunsOnTsConf)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    const std::string path = (TestPathHelper::FindProjectRoot() / "testdata" / "machines" / "tsconf" / "spg" / "empty.spg").string();

    auto pentagon = manager->CreateEmulatorWithModel("spg-pentagon", "PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(pentagon, nullptr);
    LoaderSPG refused(pentagon->GetContext(), path);
    EXPECT_FALSE(refused.load());
    EXPECT_NE(refused.GetError().find("TS-Conf"), std::string::npos) << refused.GetError();
    manager->RemoveEmulator(pentagon->GetUUID());

    auto emulator = manager->CreateEmulatorWithModelAndRAM("spg-tsconf", "TSL", 4096, LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(emulator->LoadSnapshot(path));
    auto* decoder = dynamic_cast<PortDecoder_TSConf*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    const TsConfState& ts = decoder->GetState();
    Z80& z80 = *context->pCore->GetZ80();
    EXPECT_EQ(z80.pc, 0xE000);
    EXPECT_EQ(z80.sp, 0xDFFF);
    EXPECT_EQ(ts.regs[TsConfReg::Page3], 0x17);
    EXPECT_EQ(context->emulatorState.hw_turbo_ratio, 4) << "14 MHz";
    EXPECT_EQ(context->pMemory->DirectReadFromZ80Memory(0xE000), context->pMemory->RAMBase()[0x17 * 0x4000 + 0x2000])
        << "the code block is at #E000 (page #17)";

    // The empty project's main() returns at once; the SDK's crt0 then stops
    // with DI : HALT at #0013 in its code page #14 at #0000, after its
    // startup set 16C mode in the 320x200 geometry
    emulator->EnableTurboMode();
    emulator->RunNFrames(30, true);
    EXPECT_EQ(z80.pc, 0x0014);
    EXPECT_TRUE(z80.halted);
    EXPECT_EQ(z80.iff1, 0);
    EXPECT_EQ(ts.regs[TsConfReg::Page0], 0x14);
    EXPECT_EQ(ts.regs[TsConfReg::MemConfig], 0x0F) << "RAM at #0000, writable, normal mode";
    EXPECT_EQ(ts.regs[TsConfReg::VConfig], 0x41);
    manager->RemoveEmulator(emulator->GetUUID());
}

/// The SDK's sprite example: EVO SDK sprites are software sprites (the
/// library is BaseConf-compatible), drawn into a 16C bitmap in the 320x200
/// geometry - a real-program check of the 16C renderer. The frame is pinned
/// (looked at: balls over a magenta cloud background).
/// UNREALNG_DUMP_TSCONF_FRAMES=1 writes it (720x288 RGBA) to the test scratch
TEST(LoaderSPG_Test, SpritesExampleDrawsItsFrame)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModelAndRAM("spg-sprites", "TSL", 4096, LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    const std::string path = (TestPathHelper::FindProjectRoot() / "testdata" / "machines" / "tsconf" / "spg" / "sprites.spg").string();
    ASSERT_TRUE(emulator->LoadSnapshot(path));
    auto* decoder = dynamic_cast<PortDecoder_TSConf*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);

    emulator->EnableTurboMode();
    emulator->RunNFrames(100, true);
    emulator->DisableTurboMode();
    emulator->RunNFrames(2, true);

    EXPECT_EQ(decoder->GetState().regs[TsConfReg::VConfig], 0x41) << "16C, 320x200";
    uint32_t* buffer = nullptr;
    size_t size = 0;
    context->pScreen->GetFramebufferData(&buffer, &size);
    ASSERT_EQ(size, 720u * 288u * 4u);
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < size; i++)
    {
        hash ^= reinterpret_cast<const uint8_t*>(buffer)[i];
        hash *= 0x100000001b3ULL;
    }
    if (std::getenv("UNREALNG_DUMP_TSCONF_FRAMES"))
    {
        const std::string out = TestPathHelper::GetTestScratchPath("tsconf-sprites.rgba");
        FileHelper::SaveBufferToFile(out, reinterpret_cast<uint8_t*>(buffer), size);
        std::printf("dumped %s hash 0x%016llX\n", out.c_str(), static_cast<unsigned long long>(hash));
    }
    EXPECT_EQ(hash, 13780960561087948685ull);
    manager->RemoveEmulator(emulator->GetUUID());
}

/// SPG-4: every surface opens an SPG the same way (SnapshotLauncher): on
/// another model the machine is switched to TS-Conf first and the program
/// loads there; with switching off the load is refused and names the model
TEST(LoaderSPG_Test, SPG4_LauncherSwitchesToTsConf)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    const std::string path = (TestPathHelper::FindProjectRoot() / "testdata" / "machines" / "tsconf" / "spg" / "empty.spg").string();
    auto pentagon = manager->CreateEmulatorWithModel("spg-launch", "PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(pentagon, nullptr);
    const std::string pentagonId = pentagon->GetId();
    pentagon.reset();

    SnapshotLoadRequest refuse;
    refuse.emulatorId = pentagonId;
    refuse.path = path;
    refuse.switchModel = false;
    const SnapshotLoadResult refused = SnapshotLauncher::Load(refuse);
    EXPECT_FALSE(refused.ok);
    EXPECT_TRUE(refused.modelMismatch);
    EXPECT_EQ(refused.requiredModel, "TSL");
    EXPECT_EQ(refused.requiredRamKb, 4096u);
    EXPECT_FALSE(refused.modelSwitched);

    SnapshotLoadRequest request = refuse;
    request.switchModel = true;
    const SnapshotLoadResult result = SnapshotLauncher::Load(request);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_NE(result.emulator, nullptr);
    EXPECT_TRUE(result.modelSwitched);
    EXPECT_EQ(result.previousEmulatorId, pentagonId);
    EXPECT_EQ(manager->GetEmulator(pentagonId), nullptr) << "the Pentagon was replaced";
    EmulatorContext* context = result.emulator->GetContext();
    EXPECT_EQ(context->config.mem_model, MM_TSL);
    EXPECT_EQ(context->config.ramsize, 4096u);
    EXPECT_EQ(context->pCore->GetZ80()->pc, 0xE000);

    // On TS-Conf itself it just loads; a non-SPG file is refused by its probe
    request.emulatorId = result.emulator->GetId();
    const SnapshotLoadResult again = SnapshotLauncher::Load(request);
    EXPECT_TRUE(again.ok) << again.message;
    EXPECT_FALSE(again.modelSwitched);
    std::string model, error;
    uint32_t ramKb = 0;
    EXPECT_TRUE(SnapshotLauncher::RequiredModel("game.sna", model, ramKb, error));
    EXPECT_TRUE(model.empty());
    EXPECT_FALSE(SnapshotLauncher::RequiredModel("missing.spg", model, ramKb, error));
    manager->RemoveEmulator(result.emulator->GetUUID());
}
