// NeoGS boot with the real flash image (neogs-tdd.md §4.2, §8).
//
// Runtime justification: these tests run the card's own loader and main ROM
// from data/rom/neogs/full_ngs.rom. The loader's GS105 copy alone is ~140 ms
// of card time at 10 MHz; module playback needs ~1 s more. That is the only
// faithful check that the firmware runs unchanged.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"

namespace
{
constexpr uint16_t kPortData = GeneralSoundCard::PORT_DATA;
constexpr uint16_t kPortCommand = GeneralSoundCard::PORT_COMMAND;
constexpr uint16_t kPortControl = GeneralSoundCard::PORT_CONTROL;

struct NeoGSHarness
{
    EmulatorContext ctx{LoggerLevel::LogError};
    NeoGSConfig config;
    std::unique_ptr<SoundChip_NeoGS> chip;
    size_t nonSilentSamples = 0;

    explicit NeoGSHarness(unsigned ramKB = 4096, NeoGSConfig::Boot boot = NeoGSConfig::Boot::Loader)
    {
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_shift_applied = 0;
        config.ramKB = ramKB;
        config.boot = boot;
        chip = std::make_unique<SoundChip_NeoGS>(&ctx, config, 44100);
        chip->loadROM("rom/neogs/full_ngs.rom");
    }

    void runFrames(int n)
    {
        for (int i = 0; i < n; i++)
        {
            chip->handleFrameStart();
            chip->handleFrameEnd(SAMPLES_PER_FRAME);
            const int16_t* samples = chip->getBuffer();
            for (int s = 0; s < SAMPLES_PER_FRAME * 2; s++)
                nonSilentSamples += samples[s] != 0 ? 1 : 0;
        }
    }

    int framesToReady(int maxFrames)
    {
        for (int i = 0; i < maxFrames; i++)
        {
            if (chip->isReadyForCommands())
                return i;
            runFrames(1);
        }
        return -1;
    }

    /// Loader-style pacing: two timer periods at a time, bounded at ~4 s
    bool waitFlagClear(uint8_t mask)
    {
        for (int i = 0; i < 75000; i++)
        {
            if (!(chip->readStatus() & mask))
                return true;
            chip->runFor(6400);
        }
        return false;
    }

    int readReply(int maxFrames = 200)
    {
        for (int i = 0; i < maxFrames; i++)
        {
            if (chip->readStatus() & 0x80)
                return chip->portDeviceInMethod(kPortData);
            runFrames(1);
        }
        return -1;
    }

    /// GS command with a one-byte reply
    int query(uint8_t command)
    {
        chip->portDeviceOutMethod(kPortCommand, command);
        if (!waitFlagClear(0x01))
            return -1;
        return readReply();
    }

    bool upload(const std::vector<uint8_t>& bytes)
    {
        chip->portDeviceOutMethod(kPortData, 0x01);
        chip->portDeviceOutMethod(kPortCommand, 0x30);
        if (!waitFlagClear(0x01))
            return false;
        runFrames(1);
        (void)chip->portDeviceInMethod(kPortData); // slot reply latch
        for (uint8_t b : bytes)
        {
            chip->portDeviceOutMethod(kPortData, b);
            if (!waitFlagClear(0x80))
                return false;
        }
        chip->portDeviceOutMethod(kPortCommand, 0xD2);
        return waitFlagClear(0x01);
    }
};

// Synthetic ProTracker module: one looped 64-byte square wave, two patterns
std::vector<uint8_t> buildModule()
{
    std::vector<uint8_t> m(1084 + 2 * 1024 + 64, 0x00);
    m[20 + 23] = 32;
    m[20 + 25] = 63;
    m[20 + 29] = 32;
    m[950] = 2;
    m[953] = 1;
    memcpy(&m[1080], "M.K.", 4);
    m[1084 + 0] = 0x01;
    m[1084 + 1] = 0xAC;
    m[1084 + 2] = 0x10;
    for (size_t i = 0; i < 64; i++)
        m[1084 + 2 * 1024 + i] = (i / 16) % 2 ? 0x30 : 0xB0;
    return m;
}
} // namespace

TEST(SoundChip_NeoGS_Boot, LoaderReachesMainRomCommandLoop)
{
    NeoGSHarness h;
    ASSERT_TRUE(h.chip->isROMLoaded());
    EXPECT_EQ(h.chip->flashTitle(), "NeoGS flash v1.11");

    const int frames = h.framesToReady(50);
    ASSERT_GE(frames, 0) << "the main ROM never reached COMINT_";
    EXPECT_LE(frames, 10) << "~165 ms of card time (neogs-tdd.md §4.2)";
    EXPECT_EQ(h.chip->gscfg0(), 0x23) << "RAM mode, RAMRO, 20 MHz";
    EXPECT_EQ(h.chip->cardClockHz(), 20000000u);
    EXPECT_EQ(h.chip->pageRegister(0), 0);
    EXPECT_EQ(h.chip->pageRegister(1), 3);
    EXPECT_EQ(h.chip->spi().readSctrl(), 0x0A) << "the SD probe leaves the chip select asserted";
    EXPECT_EQ(h.chip->getDataToHost(), 0x7E) << "NUMPG sent at boot: 4 MB";
}

TEST(SoundChip_NeoGS_Boot, Com23ReportsRamSize)
{
    for (unsigned ramKB : {4096u, 2048u})
    {
        NeoGSHarness h(ramKB);
        ASSERT_GE(h.framesToReady(50), 0);
        h.runFrames(2);
        EXPECT_EQ(h.query(0x23), ramKB == 4096 ? 0x7E : 0x3E) << ramKB << " KB";
    }
}

TEST(SoundChip_NeoGS_Boot, DirectBootMatchesLoader)
{
    NeoGSHarness loader;
    NeoGSHarness direct(4096, NeoGSConfig::Boot::Direct);
    ASSERT_GE(loader.framesToReady(50), 0);
    ASSERT_GE(direct.framesToReady(50), 0);
    EXPECT_LE(direct.framesToReady(50), 2) << "direct boot skips the ~165 ms loader";

    EXPECT_EQ(direct.chip->gscfg0(), loader.chip->gscfg0());
    for (int w = 0; w < 4; w++)
        EXPECT_EQ(direct.chip->pageRegister(w), loader.chip->pageRegister(w)) << "window " << w;
    EXPECT_EQ(direct.chip->spi().readSctrl(), loader.chip->spi().readSctrl());
    EXPECT_EQ(direct.chip->getDataToHost(), loader.chip->getDataToHost());
    EXPECT_EQ(0, memcmp(direct.chip->memory().ram(), loader.chip->memory().ram(), 2 * 0x4000))
        << "RAM pages 0-1 hold the same main ROM copy";
}

TEST(SoundChip_NeoGS_Boot, LoaderHandshakeEntersCommandMode)
{
    NeoGSHarness h;
    // The host preloads #55 into both latches, then resets the card: the
    // mailbox survives the reset and the loader sees the handshake at once
    h.chip->portDeviceOutMethod(kPortData, 0x55);
    h.chip->portDeviceOutMethod(kPortCommand, 0x55);
    h.chip->portDeviceOutMethod(kPortControl, 0x80);

    // Wait (in small steps) until the loader has taken #55
    int steps = 0;
    while ((h.chip->getStatusRaw() & 0x01) && steps++ < 1000)
        h.chip->runFor(12000); // 0.1 ms
    ASSERT_LT(steps, 1000);
    h.chip->portDeviceOutMethod(kPortData, 0xAA);
    h.chip->portDeviceOutMethod(kPortCommand, 0xAA);
    h.chip->runFor(10 * 120000); // the loader copies its command-mode code first (~4 ms LDIR)

    EXPECT_EQ(h.chip->gscfg0(), 0x11) << "loader command mode: RAM mode, 12 MHz";
    EXPECT_EQ(h.query(0x1D), 0x76) << "loader identification";
    EXPECT_FALSE(h.chip->isReadyForCommands()) << "the main ROM is not running";
}

TEST(SoundChip_NeoGS_Boot, HostResetOfTheCardBootsAgain)
{
    NeoGSHarness h;
    ASSERT_GE(h.framesToReady(50), 0);
    h.chip->portDeviceOutMethod(kPortControl, 0x80);
    EXPECT_FALSE(h.chip->isReadyForCommands());
    EXPECT_EQ(h.chip->gscfg0(), 0x30);
    ASSERT_GE(h.framesToReady(50), 0);
    EXPECT_EQ(h.chip->gscfg0(), 0x23);
}

TEST(SoundChip_NeoGS_Boot, GsModulePlaysThroughNeoGS)
{
    NeoGSHarness h;
    ASSERT_GE(h.framesToReady(50), 0);
    h.runFrames(2);
    if (h.chip->readStatus() & 0x80)
        (void)h.chip->portDeviceInMethod(kPortData); // the boot NUMPG reply

    ASSERT_TRUE(h.upload(buildModule()));
    h.chip->portDeviceOutMethod(kPortData, 0x00);
    h.chip->portDeviceOutMethod(kPortCommand, 0x31);
    ASSERT_TRUE(h.waitFlagClear(0x01));
    const uint64_t fetchesBefore = h.chip->getActivityCounters().dacFetches;
    h.nonSilentSamples = 0;
    h.runFrames(50);

    EXPECT_GT(h.chip->getActivityCounters().dacFetches - fetchesBefore, 35000u)
        << "the interrupt handler latches the playing channel every 37.5 kHz period";
    EXPECT_GT(h.chip->getActivityCounters().interruptsAccepted, 30000u);
    EXPECT_GT(h.nonSilentSamples, 10000u) << "the module is audible";
}

TEST(SoundChip_NeoGS_Boot, ModuleReplayWaitsForTheMainRom)
{
    // A personality switch hands a freshly created, unbooted card the module:
    // the replay must wait for COMINT_, not fire COM30 into the loader
    NeoGSHarness h;
    h.chip->replayModuleUpload(buildModule(), true);
    EXPECT_TRUE(h.chip->isReadyForCommands());
    h.nonSilentSamples = 0;
    h.runFrames(30);
    EXPECT_GT(h.nonSilentSamples, 5000u) << "the replayed module plays";
}


TEST(SoundChip_NeoGS_Boot, FlashPersistRoundTrip)
{
    // FlashWrite=persist: a reprogrammed flash survives the card object and is
    // loaded in place of the shipped image next time; session mode forgets it
    const std::string folder = TestPathHelper::GetUniqueTestScratchPath("neogs-flash");
    std::filesystem::create_directories(folder);
    {
        EmulatorContext ctx(LoggerLevel::LogError);
        NeoGSConfig config;
        config.flashWrite = NeoGSConfig::WriteMode::Persist;
        SoundChip_NeoGS chip(&ctx, config);
        chip.setFlashPersistFolder(folder);
        chip.loadROM("rom/neogs/full_ngs.rom");
        // Program one byte through the chip's own command sequence
        Flash29F040B& flash = chip.flash();
        flash.write(0x555, 0xAA, 0);
        flash.write(0x2AA, 0x55, 0);
        flash.write(0x555, 0xA0, 0);
        flash.write(0x7FFF0, 0x42, 0);
        flash.update(1'000'000);
        ASSERT_EQ(flash.data()[0x7FFF0], 0x42);
        ASSERT_TRUE(flash.modified());
    }
    {
        EmulatorContext ctx(LoggerLevel::LogError);
        NeoGSConfig config;
        config.flashWrite = NeoGSConfig::WriteMode::Persist;
        SoundChip_NeoGS chip(&ctx, config);
        chip.setFlashPersistFolder(folder);
        chip.loadROM("rom/neogs/full_ngs.rom");
        EXPECT_EQ(chip.flash().data()[0x7FFF0], 0x42) << "the saved copy was loaded";
        EXPECT_EQ(chip.flashTitle(), "NeoGS flash v1.11 (reprogrammed)");
    }
    {
        EmulatorContext ctx(LoggerLevel::LogError);
        NeoGSConfig config; // session
        SoundChip_NeoGS chip(&ctx, config);
        chip.setFlashPersistFolder(folder);
        chip.loadROM("rom/neogs/full_ngs.rom");
        EXPECT_EQ(chip.flash().data()[0x7FFF0], 0xFF) << "session mode uses the shipped image";
    }
}
