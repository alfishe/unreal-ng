// Scorpion GMX with the shipped gmx644.rom: the shadow monitor's test screen in 640x200 and Edge Grinder from its TRD
// (GMX design section 7 phase 3; research-2026-10-30-software.md 5.1 fixtures 1 and 2).

#include "stdafx.h"
#include "pch.h"

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/keyboard/keyboard.h>
#include <emulator/memory/memory.h>
#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testtier.h"
#include "emulator/media/mediamanager.h"
#include "emulator/ports/models/portdecoder_scorpiongmx.h"
#include "emulator/video/gmx/gmxgeometry.h"
#include "emulator/video/screen.h"

class GmxBoot_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
        _emulator = _manager->CreateEmulatorWithModelAndRAM("gmx-boot", "GMX", 2048, LoggerLevel::LogError);
        if (!_emulator)
            GTEST_SKIP() << "GMX is not creatable (missing configs/gmx or data/rom/gmx644.rom)";
        _context = _emulator->GetContext();
    }

    void TearDown() override
    {
        if (_emulator)
        {
            const std::string id = _emulator->GetId();
            _emulator.reset();
            _manager->RemoveEmulator(id);
        }
    }

    uint8_t P7EFD() const { return _context->emulatorState.scorpion.p7EFD; }

    /// Bytes of the shown 640x200 pixel page (#39 / #3B by #7FFD bit 3) that are neither 0 nor #FF
    uint32_t DrawnPixelBytes() const
    {
        const uint8_t* page = _context->pMemory->RAMPageAddress(GmxGeometry::PixelPage(_context->emulatorState.p7FFD));
        uint32_t count = 0;
        for (uint32_t i = 0; i < GmxGeometry::kScreenBytes; ++i)
            count += (page[i] != 0x00 && page[i] != 0xFF) ? 1 : 0;
        return count;
    }
};

/// @brief After the loader and the 128 menu hand-over the v6.44 firmware runs its fast test in the shadow monitor
///        (plane 5) with the 640x200 mode on: the screen switches to the 736x288 frame and the text is drawn
TEST_F(GmxBoot_Test, ShadowMonitorTestScreenIsDrawnIn640x200)
{
    _emulator->EnableTurboMode();
    const int frames = EmulatorTestHelper::RunUntil(
        _emulator.get(), [&] { return (P7EFD() & 0x78) == 0x58 && DrawnPixelBytes() > 200; }, 600, 10);

    EXPECT_LT(frames, 600) << "7EFD=" << std::hex << int(P7EFD());
    EXPECT_EQ(P7EFD() & 0x70, 0x50) << "plane 5: the shadow service monitor";
    ASSERT_EQ(_context->pScreen->GetVideoMode(), M_GMX);
    EXPECT_EQ(_context->pScreen->GetFramebufferDescriptor().width, 736u);
    EXPECT_EQ(_context->pScreen->GetFramebufferDescriptor().height, 288u);
    EXPECT_GT(DrawnPixelBytes(), 200u) << "the monitor's frames and the fast test text";
}

/// @brief Edge Grinder (izzx, 2021): the monitor's IDE probe times out (no drive: the status reads #FF, as in
///        Unreal), the 128 menu's first entry boots TR-DOS, the game's loader and its 640x200 picture follow
///        (#7EFD = #C8). Long tier: about 6000 frames of emulated time
TEST_F(GmxBoot_Test, EdgeGrinderBootsIntoItsExtendedMode)
{
    if (!TestTier::Long())
        GTEST_SKIP() << "UNREAL_LONG_TESTS=1: ~6000 frames through the IDE probe and the game's loader";

    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(TestPathHelper::GetTestDataPath("machines/gmx/edgegrinder/EGGMX.TRD"), 0, &error))
        << error;
    _emulator->EnableTurboMode();

    const int toMenu = EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return P7EFD() == 0x40; }, 5000, 25);
    ASSERT_LT(toMenu, 5000) << "the 128 menu (plane 4, turbo off) never came, 7EFD=" << std::hex << int(P7EFD());
    _emulator->RunNFrames(50, true);

    Keyboard* keyboard = _context->pKeyboard;
    keyboard->PressKey(ZXKEY_ENTER);   // "128 TR-DOS": the autoboot runs `boot`
    _emulator->RunNFrames(6, true);
    keyboard->ReleaseKey(ZXKEY_ENTER);

    // The controls screen waits for a key before the DISC loader: Space every 4 s of emulated time until the game
    // turns its 640x200 mode on
    int toGame = 0;
    while (toGame < 4000 && !(P7EFD() == 0xC8 && DrawnPixelBytes() > 1000))
    {
        keyboard->PressKey(ZXKEY_SPACE);
        _emulator->RunNFrames(6, true);
        keyboard->ReleaseKey(ZXKEY_SPACE);
        toGame += 6 + EmulatorTestHelper::RunUntil(
                          _emulator.get(), [&] { return P7EFD() == 0xC8 && DrawnPixelBytes() > 1000; }, 200, 25);
    }
    EXPECT_LT(toGame, 4000) << "7EFD=" << std::hex << int(P7EFD());
    EXPECT_EQ(_context->pScreen->GetVideoMode(), M_GMX);
    EXPECT_GT(DrawnPixelBytes(), 1000u) << "the game's picture in the pixel page";
}

/// @brief The Z-Controller SD is the media manager's slot `sd.zc` (alias `sd` on the planes): their media routes reach it.
///        A folder becomes a FAT32 volume behind an MBR, the layout of the web Unreal's osz.vhd (FAT32 at LBA 63)
TEST_F(GmxBoot_Test, SdCardIsAMediaSlotTakingTheOszFolder)
{
    MediaManager* media = _context->pMediaManager;
    ASSERT_NE(media, nullptr);
    const auto slot = media->Info("sd.zc");
    ASSERT_TRUE(slot.has_value());
    EXPECT_NE(std::find(slot->tags.begin(), slot->tags.end(), "zcontroller"), slot->tags.end());
    EXPECT_FALSE(slot->present);

    MediaSource source;
    source.path = TestPathHelper::GetTestDataPath("machines/gmx/osz/sdcard");
    InsertOptions options;
    options.immediate = true;
    const MediaResult inserted = media->Insert("sd.zc", source, options);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    for (const std::string& line : inserted.report)
        EXPECT_EQ(line.find("no MBR support"), std::string::npos) << "the monitor finds the FAT32 partition: " << line;
    _emulator->RunNFrames(1, true);  // the emulation thread attaches it at the frame boundary
    const auto info = media->Info("sd.zc");
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->present);
    EXPECT_EQ(info->format, "folder-fat32") << "the 6.44 monitor reads FAT32 only (GMX Q20)";
}

/// @brief OSZ (OS ver 2025.10.08, research-2026-10-30-software.md 5.1 fixture 3): OS.TRD in A: loads the kernel's
///        loader, which finds the FAT32 partition on the Z-Controller SD through the monitor (RST 8), reads the system
///        and the None Commander from `sd.zc`, and runs them in 640x200. Long tier: the IDE probe and the SD reads
TEST_F(GmxBoot_Test, OszBootsFromTheSdCard)
{
    if (!TestTier::Long())
        GTEST_SKIP() << "UNREAL_LONG_TESTS=1: ~4000 frames through the IDE probe and the SD reads";

    MediaSource sd;
    sd.path = TestPathHelper::GetTestDataPath("machines/gmx/osz/sdcard");
    InsertOptions options;
    options.immediate = true;
    const MediaResult inserted = _context->pMediaManager->Insert("sd.zc", sd, options);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(TestPathHelper::GetTestDataPath("machines/gmx/osz/OS.TRD"), 0, &error)) << error;
    _emulator->EnableTurboMode();

    const int toMenu = EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return P7EFD() == 0x40; }, 5000, 25);
    ASSERT_LT(toMenu, 5000) << "the 128 menu never came, 7EFD=" << std::hex << int(P7EFD());
    _emulator->RunNFrames(50, true);

    Keyboard* keyboard = _context->pKeyboard;
    keyboard->PressKey(ZXKEY_ENTER);   // "128 TR-DOS": the autoboot runs `boot`
    _emulator->RunNFrames(6, true);
    keyboard->ReleaseKey(ZXKEY_ENTER);

    auto* gmx = dynamic_cast<PortDecoder_ScorpionGMX*>(_context->pPortDecoder);
    ASSERT_NE(gmx, nullptr);
    const SdCardSpi& card = gmx->GetSdCard();
    const int toOs = EmulatorTestHelper::RunUntil(
        _emulator.get(), [&] { return card.blocksRead() > 40 && P7EFD() == 0xC8 && DrawnPixelBytes() > 500; }, 2000,
        25);
    EXPECT_LT(toOs, 2000) << "7EFD=" << std::hex << int(P7EFD()) << std::dec << " blocks read " << card.blocksRead();
    EXPECT_TRUE(card.initialized()) << "the monitor's SD driver ran the card's init (CMD0, CMD8, ACMD41)";
    EXPECT_GT(card.blocksRead(), 40u) << "the system and the None Commander come from the SD card";
    EXPECT_EQ(_context->pScreen->GetVideoMode(), M_GMX);
}
