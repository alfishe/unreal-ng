// A minimal MP3 player on the NeoGS card's own Z80, driving the SD card and
// the decoder through the DMA modules (neogs-tdd.md §3.9, §5.7): the
// program-level check that NedoPC's npl044_dma cannot give (its CMD17 is
// commented out, §14.2).
//
// Per sector the program:
//   1. sends CMD17 (byte address) on the SD master and polls R1;
//   2. starts the SD DMA module: it waits for the #FE token, receives 512
//      bytes and bursts them into card RAM #100000;
//   3. starts the MP3 DMA module on the same RAM: a burst into its FIFO,
//      then one byte at a time to the decoder while DREQ is up.
// The card image, the SD card model and the decoder are the real ones; the
// file is EYEACHE.MP3 from the run-time test card.
//
// Runtime justification: ~6 s of MP3 at 128 kbit/s are paced by DREQ in
// emulated time (about 320 frames of the card).

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/neogstestsdcard.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"

namespace
{
/// Z80 bytes with labels and relative-jump fixups
struct Program
{
    std::vector<uint8_t> code;
    std::map<std::string, size_t> labels;
    std::vector<std::pair<size_t, std::string>> relFixups;
    std::vector<std::pair<size_t, std::string>> absFixups;

    Program& b(std::initializer_list<uint8_t> bytes)
    {
        code.insert(code.end(), bytes);
        return *this;
    }
    Program& label(const std::string& name)
    {
        labels[name] = code.size();
        return *this;
    }
    Program& jr(uint8_t opcode, const std::string& target) // JR / JR cc / DJNZ
    {
        code.push_back(opcode);
        relFixups.push_back({code.size(), target});
        code.push_back(0);
        return *this;
    }
    Program& call(const std::string& target)
    {
        code.push_back(0xCD);
        absFixups.push_back({code.size(), target});
        code.push_back(0);
        code.push_back(0);
        return *this;
    }
    std::vector<uint8_t> link()
    {
        for (const auto& [at, name] : relFixups)
            code[at] = static_cast<uint8_t>(static_cast<int>(labels.at(name)) - static_cast<int>(at + 1));
        for (const auto& [at, name] : absFixups)
        {
            code[at] = static_cast<uint8_t>(labels.at(name));
            code[at + 1] = static_cast<uint8_t>(labels.at(name) >> 8);
        }
        return code;
    }
};

constexpr uint16_t kAddrVar = 0x4000; // big-endian SD byte address of the next sector
constexpr uint16_t kCountVar = 0x4004;
constexpr uint16_t kDoneVar = 0x4010;
constexpr uint32_t kDmaBuffer = 0x100000;

/// The player: `sectors` sectors from the address in kAddrVar
std::vector<uint8_t> playerProgram(uint8_t sectors)
{
    Program p;
    p.b({0xF3,                   // DI
         0x31, 0x00, 0x7F,       // LD SP,#7F00
         0x3E, 0x84, 0xD3, 0x11, // SCTRL: XRESET high - the decoder runs
         0x01, 0x00, 0x00});     // LD BC,0: wait out the decoder's reset (~70 ms)
    p.label("rst").b({0x0B, 0x78, 0xB1}).jr(0x20, "rst"); // DEC BC : LD A,B : OR C : JR NZ
    p.b({0x3E, 0x01, 0xD3, 0x11,        // SCTRL: SD selected (nCS low)
         0x06, sectors});               // LD B,sectors
    p.label("sector").b({0xC5});        // PUSH BC
    // CMD17 with the byte address
    p.b({0x3E, 0x51}).call("send");
    for (uint16_t i = 0; i < 4; i++)
        p.b({0x3A, uint8_t(kAddrVar + i), uint8_t((kAddrVar + i) >> 8)}).call("send"); // LD A,(addr+i)
    p.b({0x3E, 0xFF}).call("send");      // CRC (off)
    // R1: read-and-restart until not #FF
    p.label("r1").b({0xDB, 0x14, 0xFE, 0xFF}).jr(0x28, "r1"); // IN A,(SD_RSTR) : CP #FF : JR Z
    p.b({0x32, uint8_t(kDoneVar + 1), uint8_t((kDoneVar + 1) >> 8)}); // keep R1 for the test
    // SD DMA into #100000
    p.b({0x3E, 0x02, 0xD3, 0x1B,
         0x3E, uint8_t(kDmaBuffer >> 16), 0xD3, 0x1C, 0xAF, 0xD3, 0x1D, 0xD3, 0x1E,
         0x3E, 0x80, 0xD3, 0x1F});
    p.label("sdw").b({0xDB, 0x1F, 0x17}).jr(0x38, "sdw"); // IN A,(DMA_CST) : RLA : JR C
    // MP3 DMA from #100000
    p.b({0x3E, 0x03, 0xD3, 0x1B,
         0x3E, uint8_t(kDmaBuffer >> 16), 0xD3, 0x1C, 0xAF, 0xD3, 0x1D, 0xD3, 0x1E,
         0x3E, 0x80, 0xD3, 0x1F});
    p.label("mpw").b({0xDB, 0x1F, 0x17}).jr(0x38, "mpw");
    // address += 512 (big-endian: +2 in byte 2, carry up)
    p.b({0x21, uint8_t(kAddrVar + 2), uint8_t((kAddrVar + 2) >> 8), // LD HL,addr+2
         0x7E, 0xC6, 0x02, 0x77,                                     // LD A,(HL) : ADD A,2 : LD (HL),A
         0x2B, 0x7E, 0xCE, 0x00, 0x77,                               // DEC HL : LD A,(HL) : ADC A,0 : LD (HL),A
         0x2B, 0x7E, 0xCE, 0x00, 0x77,
         0x21, uint8_t(kCountVar), uint8_t(kCountVar >> 8), 0x34,    // LD HL,count : INC (HL)
         0xC1});                                                     // POP BC
    p.jr(0x10, "sector");                                            // DJNZ sector
    p.b({0x3E, 0xAA, 0x32, uint8_t(kDoneVar), uint8_t(kDoneVar >> 8)}); // done
    p.label("end").jr(0x18, "end");
    // send: OUT (SD_SEND),A, then let the byte clock out (16 card clocks)
    p.label("send").b({0xD3, 0x13, 0xC5, 0xC1, 0xC5, 0xC1, 0xC9}); // OUT (#13),A : PUSH BC : POP BC x2 : RET
    return p.link();
}

struct Card
{
    EmulatorContext ctx{LoggerLevel::LogError};
    NeoGSConfig config;
    std::unique_ptr<ScratchFatImage> image;
    std::unique_ptr<SoundChip_NeoGS> chip;

    explicit Card(NeoGSTestSd layout, NeoGSConfig::SDType type = NeoGSConfig::SDType::Auto)
    {
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_ratio_applied = 1;
        image = MakeNeoGSTestSd(layout);
        strncpy(config.sdCardPath, image->path().c_str(), sizeof config.sdCardPath - 1);
        config.sdType = type;
        config.mp3Support = NGSMP3SupportKind::Software;
        chip = std::make_unique<SoundChip_NeoGS>(&ctx, config, 44100);
    }

    void frame()
    {
        chip->handleFrameStart();
        chip->handleFrameEnd(SAMPLES_PER_FRAME);
    }

    /// First block of a root-directory file, found through the card itself
    /// (MBR -> boot sector -> root directory); -1 when not found
    int64_t fileFirstBlock(const char* name11, uint32_t& size)
    {
        SdCardSpi* sd = chip->sdCard();
        uint8_t s[512];
        sd->readBlock(0, s);
        const uint32_t part = (s[450] != 0) ? (s[454] | s[455] << 8 | s[456] << 16 | uint32_t(s[457]) << 24) : 0;
        sd->readBlock(part, s);
        const uint32_t spc = s[13], reserved = s[14] | s[15] << 8, rootEntries = s[17] | s[18] << 8;
        const uint32_t fatSize = (s[22] | s[23] << 8) ? (s[22] | s[23] << 8) : (s[36] | s[37] << 8 | s[38] << 16 | uint32_t(s[39]) << 24);
        const uint32_t rootCluster = s[44] | s[45] << 8 | s[46] << 16 | uint32_t(s[47]) << 24;
        const uint32_t rootStart = part + reserved + 2 * fatSize;
        const uint32_t dataStart = rootStart + rootEntries * 32 / 512;
        const uint32_t rootBlock = rootEntries ? rootStart : dataStart + (rootCluster - 2) * spc;
        sd->readBlock(rootBlock, s);
        for (int e = 0; e < 16; e++)
        {
            const uint8_t* d = s + e * 32;
            if (memcmp(d, name11, 11) == 0)
            {
                const uint32_t cluster = (d[26] | d[27] << 8) | (uint32_t(d[20] | d[21] << 8) << 16);
                size = d[28] | d[29] << 8 | d[30] << 16 | uint32_t(d[31]) << 24;
                return dataStart + (cluster - 2) * spc;
            }
        }
        return -1;
    }

    /// The SD card's init, as a card driver does it (CMD0, CMD8, ACMD41, CMD16)
    bool initSd()
    {
        SdCardSpi* sd = chip->sdCard();
        sd->select(true);
        const auto command = [sd](uint8_t index, uint32_t arg, uint8_t crc)
        {
            for (uint8_t b : {uint8_t(0x40 | index), uint8_t(arg >> 24), uint8_t(arg >> 16), uint8_t(arg >> 8), uint8_t(arg), crc})
                sd->exchange(b);
            for (int i = 0; i < 16; i++)
                if (const uint8_t r = sd->exchange(0xFF); r != 0xFF)
                    return r;
            return uint8_t(0xFF);
        };
        if (command(0, 0, 0x95) != 0x01 || command(8, 0x1AA, 0x87) != 0x01)
            return false;
        for (int i = 0; i < 4; i++)
            sd->exchange(0xFF);
        for (int tries = 0; tries < 100; tries++)
        {
            command(55, 0, 0xFF);
            if (command(41, 0x40000000, 0xFF) == 0x00)
                return command(16, 512, 0xFF) == 0x00;
        }
        return false;
    }
};
} // namespace

TEST(SoundChip_NeoGS_DmaPlayer, PlaysAnMp3FromTheSdCardThroughBothDmaModules)
{
    Card card(NeoGSTestSd::Fat16Mbr);
    ASSERT_TRUE(card.image->ok());
    uint32_t size = 0;
    const int64_t first = card.fileFirstBlock("EYEACHE MP3", size);
    ASSERT_GT(first, 0);
    ASSERT_GT(size, 200u * 512u);

    constexpr uint8_t kSectors = 200; // 100 KB: ~6 s at 128 kbit/s
    const std::vector<uint8_t> program = playerProgram(kSectors);
    card.chip->flash().load(program.data(), program.size());
    card.chip->reset(); // cold boot: SD card powered on, RAM cleared
    ASSERT_TRUE(card.initSd());
    ASSERT_FALSE(card.chip->sdCard()->isSdhc());
    const uint32_t address = static_cast<uint32_t>(first) * 512; // SDSC: byte addresses
    for (int i = 0; i < 4; i++)
        card.chip->poke(static_cast<uint16_t>(kAddrVar + i), static_cast<uint8_t>(address >> (24 - 8 * i)));

    const uint64_t blocksBefore = card.chip->sdCard()->blocksRead();
    int frames = 0;
    int dmaFrames = 0;
    for (; frames < 1000 && card.chip->peek(kDoneVar) != 0xAA; frames++)
    {
        card.frame();
        dmaFrames += card.chip->hadDmaActivityLastFrame() ? 1 : 0;
    }
    EXPECT_GT(dmaFrames, frames / 2) << "the HUD's 'NeoGS DMA': the DMA modules move data in most frames";
    EXPECT_FALSE(card.chip->hadHostTransferActivityLastFrame()) << "no ZX-DMA in this program";
    for (int i = 0; i < 25; i++) // the last sector drains to the decoder
        card.frame();

    ASSERT_EQ(card.chip->peek(kDoneVar), 0xAA) << "the program finished (sectors done: " << int(card.chip->peek(kCountVar)) << ")";
    EXPECT_EQ(card.chip->peek(kCountVar), kSectors);
    EXPECT_EQ(card.chip->peek(kDoneVar + 1), 0x00) << "the last R1";
    EXPECT_EQ(card.chip->sdCard()->blocksRead() - blocksBefore, kSectors);

    // The last sector sits in card RAM exactly as on the card
    uint8_t expected[512];
    ASSERT_TRUE(card.chip->sdCard()->readBlock(static_cast<uint64_t>(first) + kSectors - 1, expected));
    EXPECT_EQ(0, memcmp(card.chip->memory().ram() + kDmaBuffer, expected, 512)) << "SD DMA wrote the sector to #100000";

    Vs10xxDecoder* mp3 = card.chip->mp3Decoder();
    EXPECT_EQ(mp3->bytesReceived(), kSectors * 512u) << "MP3 DMA fed every byte";
    EXPECT_EQ(mp3->streamRate(), 44100u);
    // 100 KB of a 128 kbit/s stream is 6.4 s; DREQ paces the feed in real
    // time, so the program takes about that long
    EXPECT_GT(mp3->framesDecoded(), 200u);
    EXPECT_NEAR(frames * 0.02, 6.4, 1.0) << "paced by DREQ, not faster";
    EXPECT_EQ(card.chip->dma().address(NeoGSDma::SD), kDmaBuffer + 512) << "the SD module's address after its last block";
}
