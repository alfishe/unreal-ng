// Pentagon 2.666 LE with real software from testdata/machines/p2666/software (README there: sources, checksums).

#include <emulator/cpu/core.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/memory/memory.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testtier.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/media/mediamanager.h"
#include "pch.h"
#include "stdafx.h"

class P2666Software_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("P2666", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _context->pCore->EnableTurboMode();  // boot-bound, no pixel assertions
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    static std::filesystem::path Software(const char* name)
    {
        return TestPathHelper::FindProjectRoot() / "testdata/machines/p2666/software" / name;
    }

    void Frames(int count)
    {
        for (int f = 0; f < count; f++)
            _emulator->RunFrame();
    }

    /// Hold the key for 5 frames, then let the program see it released
    void Tap(ZXKeysEnum key, int framesAfter = 15)
    {
        _context->pKeyboard->PressKey(key);
        Frames(5);
        _context->pKeyboard->ReleaseKey(key);
        Frames(framesAfter);
    }

    bool InPage(uint16_t page, const std::string& needle) const
    {
        const uint8_t* bytes = _context->pMemory->RAMPageAddress(page);
        return bytes && std::search(bytes, bytes + PAGE_SIZE, needle.begin(), needle.end()) != bytes + PAGE_SIZE;
    }

    bool InRam(const std::string& needle) const
    {
        for (uint16_t page = 0; page < 128; page++)
        {
            if (InPage(page, needle))
                return true;
        }
        return false;
    }
};

/// Gluk's "trdos" entry boots drive A, which is the RAM disk holding the image (the board's default Beta Disk): the
/// `boot` program of Jan Bobrowski's Pentagon timing tests loads from it and lists the tests. The list ("TINT1.10" is
/// its last entry) is in no RAM page but the RAM disk's before the boot and in page 2 (#8000, where BASIC put the
/// program's data) after it
TEST_F(P2666Software_Test, GlukTrDosBootsTheTimingTestsDisk)
{
    const std::filesystem::path disk = Software("pentagon-tests.trd");
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(disk.string(), 0, &error)) << error;

    Frames(50);  // the Gluk menu
    EXPECT_FALSE(InPage(2, "TINT1.10"));
    for (int k = 0; k < 7; k++)  // the cursor from "Gluk boot" down to "trdos"
        Tap(ZXKEY_EXT_DOWN, 10);
    Tap(ZXKEY_ENTER, 0);
    const int frames = EmulatorTestHelper::RunUntil(_emulator, [&] { return InPage(2, "TINT1.10"); }, 400, 10);
    EXPECT_TRUE(InPage(2, "TINT1.10")) << "tests.trd's boot program never listed its tests (" << frames << " frames)";
}

/// NedoOS from the SD card: Gluk's "SD boot" (key 5) loads SD_BOOT.$C from the Z-Controller card (a host folder the
/// media manager presents as FAT16); the kernel runs term.com and cmd.com, and the shell runs autoexec.bat, which
/// echoes a marker. The prompt and the marker in RAM mean paging (x7F7 two maps, #FD), the SD port, the 28 MHz clock
/// and the IM2 task switching all held. A whole OS boot (~400 frames at 28 MHz): long tier
TEST_F(P2666Software_Test, NedoOsBootsFromTheSdCard)
{
    if (!TestTier::Long())
        GTEST_SKIP() << "whole NedoOS boot: UNREAL_LONG_TESTS=1";

    const std::filesystem::path card = Software("nedoos/sdcard");
    ASSERT_TRUE(std::filesystem::is_directory(card)) << card.string();
    MediaSource source;
    source.path = card.string();
    InsertOptions options;
    options.freeBytes = 16 * 1024 * 1024;
    const MediaResult inserted = _context->pMediaManager->Insert("sd.zc", source, options);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;

    Frames(50);  // the Gluk menu
    Tap(ZXKEY_5, 0);
    EmulatorTestHelper::RunUntil(_emulator, [&] { return InRam("M:/bin>") && InRam("UNREALNGSDBOOT"); }, 800, 20);
    EXPECT_TRUE(InRam("M:/bin>")) << "the NedoOS shell prompt";
    EXPECT_TRUE(InRam("UNREALNGSDBOOT")) << "the shell ran autoexec.bat from the card";
    EXPECT_FALSE(_context->pMediaManager->Info("sd.zc")->dirty) << "booting writes nothing to the card";
}
