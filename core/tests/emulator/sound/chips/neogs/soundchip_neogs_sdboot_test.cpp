// NeoGS loader booting NEOGS.ROM from an SD card (neogs-tdd.md §4.2, §5.5, §8).
//
// The test blanks the main ROM in its copy of the flash (#10000-#17FFF), so a
// card that reaches the main ROM's command loop can only have got it from the
// SD image. The images are built at run time (_helpers/neogstestsdcard.h).
//
// Runtime justification: the real loader initialises the card, walks the FAT
// and loads 32 KB over SPI at 24 MHz - tens of milliseconds of card time.

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>

#include "_helpers/neogstestsdcard.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"

namespace
{
struct SdBoot
{
    EmulatorContext ctx{LoggerLevel::LogError};
    NeoGSConfig config;
    std::unique_ptr<SoundChip_NeoGS> chip;

    SdBoot(const std::string& path, NeoGSConfig::SDType type)
    {
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_shift_applied = 0;
        strncpy(config.sdCardPath, path.c_str(), sizeof config.sdCardPath - 1);
        config.sdType = type;
        chip = std::make_unique<SoundChip_NeoGS>(&ctx, config, 44100);
        chip->loadROM("rom/neogs/full_ngs.rom");
        // No main ROM in flash: only the SD card can supply it
        memset(chip->flash().data() + SoundChip_NeoGS::FLASH_MAIN_ROM, 0xFF, SoundChip_NeoGS::MAIN_ROM_SIZE);
        chip->reset();
    }

    int framesToReady(int maxFrames)
    {
        for (int i = 0; i < maxFrames; i++)
        {
            if (chip->isReadyForCommands())
                return i;
            chip->handleFrameStart();
            chip->handleFrameEnd(SAMPLES_PER_FRAME);
        }
        return -1;
    }
};
} // namespace

TEST(SoundChip_NeoGS_SdBoot, LoaderBootsNeogsRomFromEveryLayout)
{
    const struct
    {
        NeoGSTestSd card;
        NeoGSConfig::SDType type;
    } cases[] = {
        {NeoGSTestSd::Fat16Mbr, NeoGSConfig::SDType::Auto},
        {NeoGSTestSd::Fat16NoMbr, NeoGSConfig::SDType::Auto},
        {NeoGSTestSd::Fat32Mbr, NeoGSConfig::SDType::Auto},
        {NeoGSTestSd::Fat32Mbr, NeoGSConfig::SDType::SDHC},
        {NeoGSTestSd::Fat16Mbr, NeoGSConfig::SDType::SDHC},
    };
    for (const auto& c : cases)
    {
        const auto image = MakeNeoGSTestSd(c.card);
        ASSERT_TRUE(image->ok()) << image->error();
        const char* name = NeoGSTestSdName(c.card);
        SdBoot boot(image->path(), c.type);
        ASSERT_TRUE(boot.chip->sdCardPresent()) << name;
        EXPECT_EQ(boot.chip->sdCard()->isSdhc(), c.type == NeoGSConfig::SDType::SDHC) << name;
        const int frames = boot.framesToReady(100);
        EXPECT_GE(frames, 0) << name << (c.type == NeoGSConfig::SDType::SDHC ? " as SDHC" : "")
                             << ": the main ROM never started (last SD command " << int(boot.chip->sdCard()->lastCommand()) << ")";
        EXPECT_TRUE(boot.chip->sdCard()->initialized()) << name;
        EXPECT_GE(boot.chip->sdCard()->blocksRead(), 64u) << name << ": 32 KB read";
        EXPECT_EQ(boot.chip->gscfg0(), 0x23) << name;
    }
}

TEST(SoundChip_NeoGS_SdBoot, NoCardFallsBackToFlash)
{
    SdBoot boot(TestPathHelper::GetUniqueTestScratchPath("missing.img"), NeoGSConfig::SDType::Auto);
    EXPECT_FALSE(boot.chip->sdCardPresent());
    EXPECT_LT(boot.framesToReady(100), 0) << "flash main ROM blanked and no SD: nothing to run";
}

/// The SD slot `sd.ngs` takes a host folder like any other SD slot: the media
/// manager builds a FAT volume from it and the loader boots NEOGS.ROM from
/// that, on FAT16 and on FAT32
TEST(SoundChip_NeoGS_SdBoot, LoaderBootsNeogsRomFromAHostFolder)
{
    const std::vector<uint8_t> rom = NeoGSTestSdReadFile("tools/neogs/parts/neogs.rom");
    ASSERT_GE(rom.size(), 32768u);
    ScratchFolder folder("neogs-sd-folder");
    folder.File("NEOGS.ROM", std::string(rom.begin(), rom.end()));
    folder.File("README.TXT", "host folder as the NeoGS SD card\n");

    for (const FatType fs : {FatType::Fat16, FatType::Fat32})
    {
        const char* name = fs == FatType::Fat16 ? "FAT16" : "FAT32";
        EmulatorContext ctx{LoggerLevel::LogError};
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_shift_applied = 0;
        MediaManager manager(&ctx); // no emulator: every change applies at once
        ctx.pMediaManager = &manager;

        auto chip = std::make_unique<SoundChip_NeoGS>(&ctx, NeoGSConfig{}, 44100);
        ASSERT_TRUE(manager.HasSlot(SoundChip_NeoGS::SD_SLOT_ID));
        MediaSource source;
        const auto u8 = folder.Path().u8string();
        source.path = std::string(u8.begin(), u8.end());
        source.type = MediaSourceType::Folder;
        InsertOptions options;
        options.fs = fs;
        const MediaResult inserted = manager.Insert(SoundChip_NeoGS::SD_SLOT_ID, source, options);
        ASSERT_TRUE(inserted.Ok()) << name << ": " << inserted.message;
        ASSERT_TRUE(chip->sdCardPresent()) << name;
        EXPECT_EQ(chip->sdCardImage(), source.path) << name << ": the slot reports the folder, not the volume";

        chip->loadROM("rom/neogs/full_ngs.rom");
        memset(chip->flash().data() + SoundChip_NeoGS::FLASH_MAIN_ROM, 0xFF, SoundChip_NeoGS::MAIN_ROM_SIZE);
        chip->reset();
        int frames = -1;
        for (int i = 0; i < 100 && frames < 0; i++)
        {
            if (chip->isReadyForCommands())
                frames = i;
            chip->handleFrameStart();
            chip->handleFrameEnd(SAMPLES_PER_FRAME);
        }
        EXPECT_GE(frames, 0) << name << ": the main ROM never started (last SD command "
                             << int(chip->sdCard()->lastCommand()) << ")";
        EXPECT_GE(chip->sdCard()->blocksRead(), 64u) << name << ": 32 KB read";

        chip.reset(); // the card goes: its slot with it, the folder volume is parked
        EXPECT_FALSE(manager.HasSlot(SoundChip_NeoGS::SD_SLOT_ID));
        ctx.pMediaManager = nullptr;
    }
}
