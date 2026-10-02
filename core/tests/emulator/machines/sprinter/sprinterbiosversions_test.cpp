// Sprinter Sp2000 BIOS builds other than 3.04 (Sprinter bios-versions.md): which
// community builds boot on unreal-ng and how far.
//
// Two parts:
//  - SprinterBiosVersions_Test: the images kept in data/rom/sprinter (the table
//    kBiosImages): each boots with the fast start to its boot screen.
//  - SprinterBiosProbe_Test.ProbeDirectory: a manual probe for new builds. Point
//    SPRINTER_BIOS_PROBE_DIR at a folder of 256 KB images; the test boots each one
//    (fast start, then full start through its own loader) and prints how far it
//    got: the bitstream hashes, the port-table CRC, the screen text, the PC and its
//    bank. Skipped when the variable is not set.

#include <emulator/cpu/core.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/memory/memory.h>
#include <emulator/memory/rom.h>
#include <emulator/ports/models/portdecoder_sprinter.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "3rdparty/lodepng/lodepng.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "emulator/ports/models/sprinter/sprinterpldconfig.h"
#include "pch.h"
#include "stdafx.h"

namespace
{
uint32_t Crc32(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
}  // namespace

class SprinterBiosBoot : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;

    void TearDown() override { Destroy(); }

    void Destroy()
    {
        _emulator.reset();
        if (_manager)
        {
            for (const auto& id : _manager->GetEmulatorIds())
                _manager->RemoveEmulator(id);
        }
        _context = nullptr;
        _decoder = nullptr;
    }

    /// A Sprinter from the shipped config with `romPath` as [ROM] SPRINTER=, reset with the
    /// fast start or the full start (the BIOS image's own loader configures the PLD)
    bool Boot(const std::string& romPath, bool fastStart)
    {
        Destroy();
        _manager = EmulatorManager::GetInstance();
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);
        _emulator = _manager->CreateEmulatorWithModelAndRAM("sprinter-bios", "SPRINTER", 4096, LoggerLevel::LogError);
        if (!_emulator)
            return false;
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        if (!_decoder)
            return false;
        _decoder->GetRtc().SetFixedTime(1767268830);  // 2026-01-01 12:00:30 UTC

        CONFIG& config = _context->config;
        std::memset(config.sprinter_rom_path, 0, sizeof(config.sprinter_rom_path));
        std::strncpy(config.sprinter_rom_path, romPath.c_str(), sizeof(config.sprinter_rom_path) - 1);
        if (!_context->pCore->GetROM()->LoadROM())
            return false;
        config.sprinter.fast_start = fastStart ? 1 : 0;
        _emulator->Reset();
        // No assertion looks at pixels
        _emulator->EnableTurboMode();
        return true;
    }

    /// The text of the mode table, both column sets, both mode pages (as SprinterBoot_Test)
    std::string ScreenText()
    {
        const SprinterVideoRam& vram = _decoder->GetVideoRam();
        std::string text;
        for (uint8_t page = 0; page < 2; page++)
        {
            for (uint8_t b = 0; b < 32; b++)
            {
                std::string row;
                for (uint8_t a = 0; a < 40; a++)
                {
                    for (uint8_t half = 0; half < 2; half++)
                    {
                        const uint32_t line = (1u + 2u * a + half + 0x80u * page) * 1024u;
                        const uint8_t c = vram.Read(line + 0x301 + 4u * b);
                        row.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ');
                    }
                }
                const size_t end = row.find_last_not_of(' ');
                if (end != std::string::npos)
                    text += row.substr(0, end + 1) + '\n';
            }
        }
        return text;
    }

    bool ScreenHas(const std::string& needle) { return ScreenText().find(needle) != std::string::npos; }

    uint32_t TableCrc() { return Crc32(_context->pMemory->RAMPageAddress(0x40), PAGE_SIZE); }

    std::string Where()
    {
        const uint16_t pc = _context->pCore->GetZ80()->pc;
        return StringHelper::Format("PC=%04X (%s) frame=%llu", pc, _context->pMemory->GetBankNameForAddress(pc).c_str(),
                                    static_cast<unsigned long long>(_context->emulatorState.frame_counter));
    }

    /// Answer each "[Press F4" IDE wait with F4 (AT set 2 #0C, make + break on SIO A)
    /// until `done` holds or `maxFrames` pass
    void RunPressingF4(const std::function<bool()>& done, int maxFrames)
    {
        int frames = 0;
        while (frames < maxFrames && !done())
        {
            if (ScreenHas("Press F4"))
            {
                for (uint8_t code : {0x0C, 0xF0, 0x0C})
                    _decoder->GetZ84().sio.Receive(0, code);
            }
            frames += EmulatorTestHelper::RunUntil(_emulator.get(), done, 50, 5);
        }
    }
};

/// region <The kept images>

struct SprinterBiosImage
{
    const char* name;           ///< the test name suffix
    const char* file;           ///< in data/rom/sprinter
    const char* banner;         ///< the BIOS line of the boot screen
    uint32_t fullHash;          ///< the bitstream's full-stream hash (SprinterPldConfig)
    uint32_t tableCrc;          ///< CRC32 of RAM page #40 right after the BIOS opened the port decoder
};

// The kept community builds (data/rom/README-ROMS.md). The bitstream hashes are new to the
// configuration registry: the machine runs them on Standard (bios-versions.md §4). The page #40
// CRCs equal tools/sprinter/dcp-table.py --records over each build's own bios/exp/DCP.ASM
static const SprinterBiosImage kBiosImages[] = {
    { "Bios306Hotfix2", "sp2k-3.06-hf2.rom", "Firmware v3.06 Hotfix 2", 0xF9F42E59, 0xB77BDEA9 },
    { "Bios307Beta1", "sp2k-3.07-beta1.rom", "Firmware v3.07 BETA 1", 0x29641AB3, 0x8E1916C5 },
};

class SprinterBiosVersions_Test : public SprinterBiosBoot, public ::testing::WithParamInterface<SprinterBiosImage>
{
};

// The community BIOS boots through POST, its port table, the logo and the IDE scan (no drive,
// F4 at each unit: "Skipped") to the boot prompt, which offers SETUP and the ZX mode.
// Boot-bound (330-520 frames of real ROM: the logo delay, the IDE scan), the turbo mode on
TEST_P(SprinterBiosVersions_Test, FastStart_ReachesTheBootPrompt)
{
    const SprinterBiosImage& image = GetParam();
    const std::string path = (TestPathHelper::FindProjectRoot() / "data" / "rom" / "sprinter" / image.file).string();
    if (!FileHelper::FileExists(path))
        GTEST_SKIP() << path << " not found";
    ASSERT_TRUE(Boot(path, true));

    EXPECT_EQ(_decoder->GetPldState().bitstreamHashFull, image.fullHash);
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return _decoder->DcpOpenedFrame() >= 0; }, 50, 1);
    ASSERT_GE(_decoder->DcpOpenedFrame(), 0) << "the BIOS never opened the port decoder; " << Where();
    EXPECT_EQ(TableCrc(), image.tableCrc);

    // The IDE scan covers four units (3.04: two); with no drive each waits for BSY until F4
    const char* prompt = "PRESS <ENTER> TO REBOOT, <DEL> TO ENTER SETUP OR <ESC> TO ZX-MODE";
    RunPressingF4([&] { return ScreenHas(prompt); }, 800);
    EXPECT_TRUE(ScreenHas(prompt)) << Where() << "\n" << ScreenText();
    EXPECT_TRUE(ScreenHas(image.banner)) << ScreenText();
    EXPECT_TRUE(ScreenHas("Memory    : 4096K")) << ScreenText();
    EXPECT_TRUE(ScreenHas("Alternative Boot from Diskette fail")) << ScreenText();
}

// The full start: the image's own PLD loader (the community "universal" 1K30 / 1K50 loader)
// streams the bitstream - the same hash as the fast start - and the BIOS then opens the port decoder.
// Boot-bound (the loader: ~165 frames of real ROM), the turbo mode on
TEST_P(SprinterBiosVersions_Test, FullStart_OwnLoaderConfiguresThePld)
{
    const SprinterBiosImage& image = GetParam();
    const std::string path = (TestPathHelper::FindProjectRoot() / "data" / "rom" / "sprinter" / image.file).string();
    if (!FileHelper::FileExists(path))
        GTEST_SKIP() << path << " not found";
    ASSERT_TRUE(Boot(path, false));

    const SprinterPldState& pld = _decoder->GetPldState();
    EXPECT_EQ(pld.configState, SprinterConfigState::Loading);
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return pld.configState == SprinterConfigState::Configured; }, 300, 5);
    ASSERT_EQ(pld.configState, SprinterConfigState::Configured) << "the loader did not finish; " << Where();
    EXPECT_EQ(pld.bitstreamCount, SprinterPldConfig::kPldConfigurationWrites);
    EXPECT_EQ(pld.bitstreamHashFull, image.fullHash);

    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return _decoder->DcpOpenedFrame() >= 0; }, 100, 1);
    ASSERT_GE(_decoder->DcpOpenedFrame(), 0) << "the BIOS never opened the port decoder; " << Where();
    EXPECT_EQ(TableCrc(), image.tableCrc);
}

INSTANTIATE_TEST_SUITE_P(Images, SprinterBiosVersions_Test, ::testing::ValuesIn(kBiosImages),
                         [](const ::testing::TestParamInfo<SprinterBiosImage>& info) { return std::string(info.param.name); });

/// endregion </The kept images>

/// region <Manual probe>

class SprinterBiosProbe_Test : public SprinterBiosBoot
{
protected:
    /// Two frames without the turbo mode, then the framebuffer as a PNG
    void SaveScreen(const std::string& name)
    {
        _emulator->DisableTurboMode();
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 2);
        _emulator->EnableTurboMode();
        const FramebufferDescriptor& fb = _context->pScreen->GetFramebufferDescriptor();
        const uint32_t* pixels = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
        std::vector<unsigned char> rgba(static_cast<size_t>(fb.width) * fb.height * 4);
        for (size_t i = 0; i < static_cast<size_t>(fb.width) * fb.height; i++)
        {
            for (unsigned c = 0; c < 3; c++)
                rgba[i * 4 + c] = static_cast<unsigned char>(pixels[i] >> (8 * c));
            rgba[i * 4 + 3] = 0xFF;
        }
        // SPRINTER_BIOS_PROBE_OUT keeps the pictures (the per-process scratch folder is removed at exit)
        const char* out = std::getenv("SPRINTER_BIOS_PROBE_OUT");
        const std::string path = (out && *out) ? (std::filesystem::path(out) / name).string() : TestPathHelper::GetUniqueTestScratchPath(name);
        lodepng::encode(path, rgba, fb.width, fb.height);
        std::cout << "  screenshot: " << path << std::endl;
    }
};

// Manual: SPRINTER_BIOS_PROBE_DIR=<folder of .rom/.bin images>. Minutes per image (the full
// start runs the loader, the IDE detection waits): not part of the regular run
TEST_F(SprinterBiosProbe_Test, ProbeDirectory)
{
    const char* dir = std::getenv("SPRINTER_BIOS_PROBE_DIR");
    if (!dir || !*dir)
        GTEST_SKIP() << "set SPRINTER_BIOS_PROBE_DIR to probe BIOS images";

    std::vector<std::filesystem::path> images;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (entry.is_regular_file() && entry.file_size() == 16u * PAGE_SIZE)
            images.push_back(entry.path());
    }
    std::sort(images.begin(), images.end());
    const int frames = std::getenv("SPRINTER_BIOS_PROBE_FRAMES") ? std::atoi(std::getenv("SPRINTER_BIOS_PROBE_FRAMES")) : 3000;

    for (const auto& image : images)
    {
        for (bool fastStart : {true, false})
        {
            std::cout << "==== " << image.filename().string() << (fastStart ? " [fast start]" : " [full start]") << std::endl;
            if (!Boot(image.string(), fastStart))
            {
                std::cout << "  machine construction failed" << std::endl;
                continue;
            }
            if (!fastStart)
            {
                EmulatorTestHelper::RunUntil(
                    _emulator.get(), [&] { return _decoder->GetPldState().configState == SprinterConfigState::Configured; }, 200, 1);
                std::cout << "  loader: " << (_decoder->GetPldState().configState == SprinterConfigState::Configured ? "configured" : "NOT configured")
                          << " writes=" << _decoder->GetPldState().bitstreamCount << " " << Where() << std::endl;
            }
            const SprinterPldState& pld = _decoder->GetPldState();
            std::cout << StringHelper::Format("  bitstream full=%08X head=%08X module=%s", pld.bitstreamHashFull, pld.bitstreamHashHead,
                                              _decoder->ActiveModule().Descriptor().name.c_str())
                      << std::endl;

            EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return _decoder->DcpOpenedFrame() >= 0; }, 100, 1);
            if (_decoder->DcpOpenedFrame() >= 0)
                std::cout << StringHelper::Format("  DCP opened: frame %lld PC=%04X, page #40 CRC %08X", static_cast<long long>(_decoder->DcpOpenedFrame()),
                                                  _decoder->DcpOpenedPc(), TableCrc())
                          << std::endl;
            else
                std::cout << "  DCP never opened: " << Where() << std::endl;

            const char* prompt = "PRESS <ENTER> TO REBOOT";
            RunPressingF4([&] { return ScreenHas(prompt); }, frames);
            std::cout << "  " << (ScreenHas(prompt) ? "boot prompt reached" : "no boot prompt") << ", " << Where() << std::endl;

            // Where the CPU spends its time now: 20 samples, one per frame
            std::map<std::string, int> where;
            for (int i = 0; i < 20; i++)
            {
                EmulatorTestHelper::RunFramesFast(_emulator.get(), 1);
                where[StringHelper::Format("%04X %s", _context->pCore->GetZ80()->pc,
                                           _context->pMemory->GetBankNameForAddress(_context->pCore->GetZ80()->pc).c_str())]++;
            }
            std::cout << "  PC samples:";
            for (const auto& [pc, count] : where)
                std::cout << " [" << pc << "]x" << count;
            std::cout << std::endl << "  screen:\n" << ScreenText() << std::endl;
            SaveScreen(image.stem().string() + (fastStart ? "-fast-prompt.png" : "-full-prompt.png"));

            // ESC at the prompt: the community BIOS offers "<ESC> TO ZX-MODE" (Spectrum ROMs in
            // pages 2-4); BIOS 3.04 cancels the reboot. AT set 2: ESC = #76
            if (fastStart && ScreenHas(prompt))
            {
                for (uint8_t code : {0x76, 0xF0, 0x76})
                    _decoder->GetZ84().sio.Receive(0, code);
                EmulatorTestHelper::RunFramesFast(_emulator.get(), 300);
                std::cout << "  after ESC + 300 frames: " << Where() << std::endl;
                SaveScreen(image.stem().string() + "-fast-esc.png");
            }
        }
    }
}

/// endregion </Manual probe>
