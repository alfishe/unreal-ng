// NeoGS TTD state while the devices are busy (neogs-tdd.md §7.4).
//
// A card is saved mid-transfer and the blob loaded into a second card; both
// then run on and must stay identical: the SD card's protocol, the decoder's
// FIFO and minimp3 state, the DMA modules' phase and FIFOs all travel in the
// blob. The card RAM and the flash are not in the blob (large memories are not
// snapshotted in TTD v1 - they wait for TTD v2 memory regions): the test copies
// them itself, standing in for those regions, so the blob is checked for
// everything else. The replay through the TTD engine itself is in
// debugger/ttd/ttdneogs_test.cpp.
//
// Runtime justification: the loader reads 32 KB over SPI, and a decoder needs
// several frames of MP3 input before it plays.

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/neogstestsdcard.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"

namespace
{
struct Card
{
    EmulatorContext ctx{LoggerLevel::LogError};
    NeoGSConfig config;
    std::unique_ptr<SoundChip_NeoGS> chip;

    explicit Card(const NeoGSConfig& cfg) : config(cfg)
    {
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_ratio_applied = 1;
        chip = std::make_unique<SoundChip_NeoGS>(&ctx, config, 44100);
    }

    /// One frame; returns the MP3 output of the frame
    std::vector<int16_t> frame()
    {
        chip->handleFrameStart();
        chip->handleFrameEnd(SAMPLES_PER_FRAME);
        const int16_t* aux = chip->getAuxBuffer();
        return aux ? std::vector<int16_t>(aux, aux + SAMPLES_PER_FRAME * 2) : std::vector<int16_t>{};
    }

    std::vector<uint8_t> save() const
    {
        std::vector<uint8_t> blob(chip->TTDStateSize());
        chip->TTDSaveState(blob.data());
        return blob;
    }

    /// Card RAM and flash: the large memories the blob leaves out
    std::vector<uint8_t> memories() const
    {
        const NeoGSMemory& mem = chip->memory();
        std::vector<uint8_t> out(mem.ram(), mem.ram() + mem.ramSize());
        const uint8_t* flash = chip->flash().data();
        out.insert(out.end(), flash, flash + Flash29F040B::SIZE);
        return out;
    }

    /// Stand-in for the TTD v2 memory regions: copies another card's memories
    void copyMemoriesFrom(Card& other)
    {
        memcpy(chip->memory().ram(), other.chip->memory().ram(), chip->memory().ramSize());
        memcpy(chip->flash().data(), other.chip->flash().data(), Flash29F040B::SIZE);
    }
};

/// Runs `a` and a copy made from its blob side by side
void expectContinuesIdentically(Card& a, Card& b, int frames, const std::string& from)
{
    ASSERT_LT(a.save().size(), a.chip->getRamSizeKB() * 1024) << from << ": the blob must leave the card memory out (TTD v1)";
    b.copyMemoriesFrom(a);
    b.chip->TTDLoadState(a.save().data());
    ASSERT_EQ(b.chip->TTDHashState(), a.chip->TTDHashState()) << from;
    ASSERT_EQ(b.save(), a.save()) << from << ": the loaded blob saves back unchanged";
    for (int i = 0; i < frames; i++)
    {
        const std::vector<int16_t> audioA = a.frame();
        const std::vector<int16_t> audioB = b.frame();
        ASSERT_EQ(b.chip->TTDHashState(), a.chip->TTDHashState()) << from << ", frame " << i;
        ASSERT_EQ(audioB, audioA) << from << ", frame " << i << ": MP3 output";
    }
    EXPECT_EQ(b.save(), a.save()) << from << ": devices at the end";
    EXPECT_TRUE(b.memories() == a.memories()) << from << ": RAM and flash at the end";
}
} // namespace

TEST(SoundChip_NeoGS_Ttd, SdBootSnapshotsContinueIdentically)
{
    NeoGSConfig config;
    const auto image = MakeNeoGSTestSd(NeoGSTestSd::Fat16Mbr);
    ASSERT_TRUE(image->ok()) << image->error();
    strncpy(config.sdCardPath, image->path().c_str(), sizeof config.sdCardPath - 1);
    config.mp3Support = NGSMP3SupportKind::None;

    const auto boot = [](Card& c)
    {
        c.chip->loadROM("rom/neogs/full_ngs.rom");
        c.chip->reset();
    };
    int ready = 0;
    {
        Card probe(config);
        boot(probe);
        while (!probe.chip->isReadyForCommands() && ready < 100)
        {
            probe.frame();
            ready++;
        }
        ASSERT_GE(ready, 3) << "the boot must span a few frames";
        ASSERT_LT(ready, 100);
    }

    // Snapshots early in the SD init, mid-way and in the last boot frame
    for (const int at : {1, ready / 2, ready - 1})
    {
        Card a(config);
        boot(a);
        for (int i = 0; i < at; i++)
            a.frame();
        ASSERT_FALSE(a.chip->isReadyForCommands()) << "frame " << at << " must be inside the boot";

        Card b(config);
        boot(b);
        expectContinuesIdentically(a, b, 30, "SD boot, frame " + std::to_string(at));
        EXPECT_TRUE(b.chip->isReadyForCommands());
        EXPECT_EQ(b.chip->sdCard()->blocksRead(), a.chip->sdCard()->blocksRead());
    }
}

TEST(SoundChip_NeoGS_Ttd, Mp3DmaSnapshotsContinueIdentically)
{
    const auto path = TestPathHelper::FindProjectRoot() / "testdata/sound/neogs/mp3/eyeache1-44k-128k-cbr.mp3";
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> stream((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_GT(stream.size(), 65536u);

    // Feeds the decoder from card RAM #40000 with the MP3 DMA module, one
    // block after another, forever
    const uint8_t program[] = {
        0xF3,             // DI
        0x3E, 0x84,       // LD A,#84        SCTRL: XRESET high (decoder runs)
        0xD3, 0x11,       // OUT (#11),A
        0x3E, 0x03,       // LD A,3          DMA module: MP3
        0xD3, 0x1B,       // OUT (#1B),A
        0x3E, 0x04,       // LD A,#04        address #040000
        0xD3, 0x1C,       // OUT (#1C),A
        0xAF,             // XOR A
        0xD3, 0x1D,       // OUT (#1D),A
        0xD3, 0x1E,       // OUT (#1E),A
        0xDB, 0x1F,       // loop: IN A,(#1F)
        0x17,             // RLA             CST bit 7: still running
        0x38, 0xFB,       // JR C,loop
        0x3E, 0x80,       // LD A,#80        next block
        0xD3, 0x1F,       // OUT (#1F),A
        0x18, 0xF5,       // JR loop
    };
    NeoGSConfig config;
    config.mp3Support = NGSMP3SupportKind::Software;
    const auto boot = [&](Card& c)
    {
        c.chip->flash().load(program, sizeof program);
        c.chip->reset(); // a cold boot clears the RAM
        std::memcpy(c.chip->memory().ram() + 0x40000, stream.data(), 65536);
    };

    // Snapshots: DREQ still held by the hard reset, the first frames decoded,
    // steady playback with the FIFO full
    for (const int at : {0, 4, 15})
    {
        Card a(config);
        boot(a);
        for (int i = 0; i < at; i++)
            a.frame();

        Card b(config);
        boot(b);
        expectContinuesIdentically(a, b, 25, "MP3 DMA, frame " + std::to_string(at));
        EXPECT_GT(b.chip->mp3Decoder()->framesDecoded(), 15u) << "the decoder must have played";
        EXPECT_EQ(b.chip->mp3Decoder()->bytesReceived(), a.chip->mp3Decoder()->bytesReceived());
    }
}
