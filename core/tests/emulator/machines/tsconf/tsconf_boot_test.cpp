// TS-Conf boot on the real data/rom/zxevo.rom (TSConf implementation-plan
// BOOT-1 / BOOT-2). Characterized 2026-09-30 on the TS-BIOS build of
// 28.04.2018 in the image's ROM group 0:
//  - With blank NVRAM (CMOS cells #B0-#E7 fail their CRC) the BIOS loads its
//    defaults and opens the Setup Utility: TXT mode (V_CONFIG #83, 320x240
//    geometry), text page #F6 (font #F7), main loop waiting for a key at
//    #1883-#1887 in ROM page 0.
//  - ENTER changes the highlighted option (CPU speed 3.5 -> 7 -> 14 -> 3.5)
//    and writes NVRAM with a valid CRC. After a reset the BIOS skips Setup
//    and starts the default "Reset to: ROM #00, bank TR-DOS": TR-DOS 5.04T
//    at its A> prompt, ZX mode on page 5.
// The CMOS has no NVRAM file in the ts-conf config, so every run starts blank.

#include <emulator/cpu/core.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/keyboard/keyboard.h>
#include <emulator/memory/memory.h>
#include <emulator/ports/models/portdecoder_tsconf.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"

#include "_helpers/emulatortesthelper.h"
#include "pch.h"
#include "stdafx.h"

class TsConfBoot_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_TSConf* _decoder = nullptr;

    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);

        _emulator = _manager->CreateEmulatorWithModelAndRAM("tsconf-boot", "TSL", 4096, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_TSConf*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
        _decoder->GetRtc().SetFixedTime(1767268830);  // 2026-01-01 12:00:30 UTC
        // No assertion looks at pixels
        _emulator->EnableTurboMode();
    }

    void TearDown() override
    {
        _emulator.reset();
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);
    }

    Z80& Cpu() { return *_context->pCore->GetZ80(); }

    /// Characters of a TS text-mode row (the 128 character bytes of row `row` at page `page`)
    std::string TextRow(uint8_t page, uint8_t row)
    {
        const uint8_t* base = _context->pMemory->RAMPageAddress(page) + row * 256;
        return std::string(reinterpret_cast<const char*>(base), 128);
    }

    bool SetupMenuShown()
    {
        const TsConfState& ts = _decoder->GetState();
        if (ts.regs[TsConfReg::VConfig] != 0x83 || ts.regs[TsConfReg::VPage] != 0xF6)
            return false;
        for (uint8_t row = 0; row < 8; row++)
        {
            if (TextRow(0xF6, row).find("TS-BIOS Setup Utility") != std::string::npos)
                return true;
        }
        return false;
    }

    /// The Setup main loop polls #5D04 at #1883-#1887. A frame boundary lands
    /// right after the frame INT is taken, i.e. on the handler's first
    /// instruction (#1635) with the loop address on the stack
    /// Render two frames and hash the framebuffer (VID-2 style golden).
    /// UNREALNG_DUMP_TSCONF_FRAMES=1 also writes the raw RGBA frame
    /// (720x288) to the test scratch folder for a look before re-recording
    uint64_t FrameHash(const char* name)
    {
        _emulator->DisableTurboMode();
        _emulator->RunNFrames(2, true);
        uint32_t* buffer = nullptr;
        size_t size = 0;
        _context->pScreen->GetFramebufferData(&buffer, &size);
        if (std::getenv("UNREALNG_DUMP_TSCONF_FRAMES"))
        {
            const std::string path = TestPathHelper::GetTestScratchPath(std::string(name) + ".rgba");
            FileHelper::SaveBufferToFile(path, reinterpret_cast<uint8_t*>(buffer), size);
            std::printf("dumped %s\n", path.c_str());
        }
        uint64_t h = 0xcbf29ce484222325ULL;
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(buffer);
        for (size_t i = 0; i < size; i++)
        {
            h ^= bytes[i];
            h *= 0x100000001b3ULL;
        }
        return h;
    }

    bool InSetupKeyLoop()
    {
        auto inLoop = [](uint16_t pc) { return pc >= 0x1883 && pc <= 0x1887; };
        if (inLoop(Cpu().pc))
            return true;
        Memory* memory = _context->pMemory;
        const uint16_t sp = Cpu().sp;
        const uint16_t ret = static_cast<uint16_t>(memory->DirectReadFromZ80Memory(sp) |
                                                   (memory->DirectReadFromZ80Memory(static_cast<uint16_t>(sp + 1)) << 8));
        return Cpu().pc == 0x1635 && inLoop(ret);
    }

    void TapEnter()
    {
        _context->pKeyboard->PressKey(ZXKEY_ENTER);
        _emulator->RunNFrames(3, true);
        _context->pKeyboard->ReleaseKey(ZXKEY_ENTER);
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return InSetupKeyLoop(); }, 50, 1);
    }
};

/// BOOT-1: blank NVRAM -> the TS-BIOS Setup Utility in TXT mode
TEST_F(TsConfBoot_Test, BOOT1_BlankNvramOpensSetup)
{
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return SetupMenuShown() && InSetupKeyLoop(); }, 150);
    EXPECT_TRUE(SetupMenuShown());
    EXPECT_TRUE(InSetupKeyLoop()) << "pc=" << std::hex << Cpu().pc;
    EXPECT_EQ(_context->pScreen->GetVideoMode(), M_TSTX);
    EXPECT_TRUE(_context->pMemory->IsBank0ROM());
    EXPECT_EQ(_context->pMemory->GetROMPage(), 0u) << "TS-BIOS";
    // The Setup screen, TXT mode (scratch/tsconf-setup.png when re-recording)
    EXPECT_EQ(FrameHash("tsconf-setup"), 7388729261909274829ull);
}

/// BOOT-2: an option change saves NVRAM; after a reset the BIOS boots TR-DOS
TEST_F(TsConfBoot_Test, BOOT2_SavedNvramBootsTrDos)
{
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return SetupMenuShown() && InSetupKeyLoop(); }, 150);
    ASSERT_TRUE(SetupMenuShown());

    for (int press = 0; press < 3; press++)  // CPU speed 3.5 -> 7 -> 14 -> 3.5
        TapEnter();
    ASSERT_TRUE(InSetupKeyLoop()) << "pc=" << std::hex << Cpu().pc;

    _emulator->Reset();
    const TsConfState& ts = _decoder->GetState();
    auto atTrDosPrompt = [&] {
        return ts.dos && _context->pMemory->IsBank0ROM() && _context->pMemory->GetROMPage() == 1u &&
               (ts.regs[TsConfReg::VConfig] & 0x03) == 0 && ts.regs[TsConfReg::VPage] == 0x05;
    };
    EmulatorTestHelper::RunUntil(_emulator.get(), atTrDosPrompt, 150);
    ASSERT_TRUE(atTrDosPrompt()) << "pc=" << std::hex << Cpu().pc << " vconf=" << int(ts.regs[TsConfReg::VConfig])
                                 << " rom=" << _context->pMemory->GetROMPage();
    EXPECT_EQ(_context->pScreen->GetVideoMode(), M_TSZX);
    EXPECT_FALSE(SetupMenuShown());
    EXPECT_EQ(_context->emulatorState.hw_turbo_ratio, 1) << "3.5 MHz saved";
    // The TR-DOS screen, ZX mode in the rres 0 window, once TR-DOS has drawn
    // its banner and prompt (the ROM is paged in before it clears the screen)
    _emulator->RunNFrames(100, true);
    EXPECT_EQ(FrameHash("tsconf-trdos"), 4746447736014558365ull);
}

/// BOOT-3: TS-BIOS boots Wild Commander from the SD card (Z-Controller, FAT32
/// image, WC is the file boot.$C in the root). Setup: "Reset to" set to
/// "BD boot.$c" (ROM #00 -> ROM #04 -> RAM #F8 -> BD boot.$c), then a reset.
/// The image is not in the repository (testdata/machines/tsconf/wildcommander,
/// README there, fetched on demand): skipped without it. The card is mounted
/// with session writes, the image stays unchanged
TEST_F(TsConfBoot_Test, BOOT3_BootsWildCommanderFromSd)
{
    const std::string image = (TestPathHelper::FindProjectRoot() / "testdata" / "machines" / "tsconf" / "wildcommander" /
                               "sd-images" / "wc-tslabs-v1.11rc7.img")
                                  .string();
    if (!FileHelper::FileExists(image))
        GTEST_SKIP() << "Wild Commander SD image not present: " << image;
    ASSERT_TRUE(_decoder->InsertSdCard(image, SdCardSpi::WriteMode::Session));

    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return SetupMenuShown() && InSetupKeyLoop(); }, 150);
    ASSERT_TRUE(SetupMenuShown());
    for (int down = 0; down < 3; down++)  // CPU speed -> CPU cache -> #7FFD span -> Reset to
    {
        _context->pKeyboard->PressKey(ZXKEY_CAPS_SHIFT);
        _context->pKeyboard->PressKey(ZXKEY_6);
        _emulator->RunNFrames(3, true);
        _context->pKeyboard->ReleaseKey(ZXKEY_6);
        _context->pKeyboard->ReleaseKey(ZXKEY_CAPS_SHIFT);
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return InSetupKeyLoop(); }, 50, 1);
    }
    for (int press = 0; press < 3; press++)
        TapEnter();

    _emulator->Reset();
    const TsConfState& ts = _decoder->GetState();
    auto wildCommander = [&] {
        if ((ts.regs[TsConfReg::VConfig] & 0x03) != 0x03)
            return false;
        for (uint8_t row = 0; row < 4; row++)
            if (TextRow(ts.regs[TsConfReg::VPage], row).find("Wild Commander") != std::string::npos)
                return true;
        return false;
    };
    EmulatorTestHelper::RunUntil(_emulator.get(), wildCommander, 500);
    ASSERT_TRUE(wildCommander()) << "pc=" << std::hex << Cpu().pc << " vconf=" << int(ts.regs[TsConfReg::VConfig])
                                 << " vpage=" << int(ts.regs[TsConfReg::VPage]);
    // Its panels list the card's root
    bool listsBoot = false;
    for (uint8_t row = 0; row < 30 && !listsBoot; row++)
        listsBoot = TextRow(ts.regs[TsConfReg::VPage], row).find("boot.$c") != std::string::npos;
    EXPECT_TRUE(listsBoot) << "the file panel lists boot.$c";
}
