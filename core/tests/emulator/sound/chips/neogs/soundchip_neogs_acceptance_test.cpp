// NeoGS acceptance: the card authors' own host test program on an emulated
// Pentagon 128 (neogs-tdd.md §8, "Acceptance with host programs").
//
// test_ngs (NedoPC ngs/zx/test_ngs) resets the card, detects it, reads the
// firmware version, asks COM23 for the page count, and writes then reads back
// a marker in every MPAG page from 2 up. It prints with its own 6x8 font on a
// 42-column grid, so the screen is read here by matching character cells
// against that font (testdata/sound/neogs/programs/altstd.fnt) - exact, not OCR.
//
// Runtime justification: a real machine boots TR-DOS, loads the program from
// an SCL image and runs it; the card boots its flash firmware meanwhile. That
// is several emulated seconds and the only faithful end-to-end check.

#include <gtest/gtest.h>

#include <memory>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/neogstestsdcard.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/trdostesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/audiodeviceinfo.h"
#include "emulator/sound/soundmanager.h"

namespace
{
std::vector<uint8_t> readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// Screen text of a 42-column 6x8 font printer (top six bits of each glyph
/// byte). Unmatched cells read as '?'. Returns one string per text row, in
/// the program's own 8-bit encoding (CP866 for its Russian messages).
std::vector<std::string> readScreen42(Memory* memory, const std::vector<uint8_t>& font)
{
    std::vector<std::string> rows;
    for (int row = 0; row < 24; row++)
    {
        std::string text;
        for (int col = 0; col < 42; col++)
        {
            uint8_t cell[8];
            const int x = col * 6;
            for (int line = 0; line < 8; line++)
            {
                const int y = row * 8 + line;
                const uint16_t base = static_cast<uint16_t>(0x4000 | ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2));
                const uint8_t b0 = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(base + (x >> 3)));
                const uint8_t b1 = (x >> 3) < 31 ? memory->DirectReadFromZ80Memory(static_cast<uint16_t>(base + (x >> 3) + 1)) : 0;
                const unsigned pair = (static_cast<unsigned>(b0) << 8) | b1;
                cell[line] = static_cast<uint8_t>((pair >> (10 - (x & 7))) & 0x3F);
            }
            char found = '?';
            for (int c = 32; c < 256 && found == '?'; c++)
            {
                bool match = true;
                for (int line = 0; line < 8 && match; line++)
                    match = ((font[c * 8 + line] >> 2) & 0x3F) == cell[line];
                if (match)
                    found = static_cast<char>(c);
            }
            text += found;
        }
        rows.push_back(text);
    }
    return rows;
}

/// Screen text of an 8x8 printer on the byte grid (32 columns) with a given font
std::vector<std::string> readScreen32(Memory* memory, const std::vector<uint8_t>& font)
{
    std::vector<std::string> rows;
    for (int row = 0; row < 24; row++)
    {
        std::string text;
        for (int col = 0; col < 32; col++)
        {
            uint8_t cell[8];
            for (int line = 0; line < 8; line++)
            {
                const int y = row * 8 + line;
                cell[line] = memory->DirectReadFromZ80Memory(
                    static_cast<uint16_t>(0x4000 | ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2) | col));
            }
            char found = '?';
            for (int c = 32; c < 256 && found == '?'; c++)
            {
                if (memcmp(&font[c * 8], cell, 8) == 0)
                    found = static_cast<char>(c);
            }
            text += found;
        }
        rows.push_back(text);
    }
    return rows;
}

std::string joined(const std::vector<std::string>& rows)
{
    std::string all;
    for (const std::string& row : rows)
        all += row + "\n";
    return all;
}

/// Printable dump for failure messages (non-ASCII shown as '.')
std::string printable(const std::string& text)
{
    std::string out;
    for (char c : text)
        out += (c == '\n' || (c >= 32 && c < 127)) ? c : '.';
    return out;
}

class NeoGSAcceptance_Test : public ::testing::Test
{
protected:
    SoundCardScope _gs{TestSound::GeneralSound};
    std::unique_ptr<ScratchFatImage> _sdImage; // outlives the emulator (removed after TearDown)
    Emulator* _emulator = nullptr;
    std::vector<uint8_t> _font;

    void SetUp() override
    {
        _font = readFile(TestPathHelper::FindProjectRoot() / "testdata/sound/neogs/programs/altstd.fnt");
        ASSERT_EQ(_font.size(), 2048u);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// Pentagon 128 with the NeoGS fitted in the GS slot
    bool createMachine(unsigned ramKB)
    {
        return createMachineWithSdPath(ramKB, std::string());
    }

    /// With one of the run-time test SD cards in the slot
    bool createMachine(unsigned ramKB, NeoGSTestSd card)
    {
        return makeSdImage(card) && createMachineWithSdPath(ramKB, _sdImage->path());
    }

    bool makeSdImage(NeoGSTestSd card)
    {
        _sdImage = MakeNeoGSTestSd(card);
        EXPECT_TRUE(_sdImage->ok()) << _sdImage->error();
        return _sdImage->ok();
    }

    bool createMachineWithSdPath(unsigned ramKB, const std::string& sd)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON");
        if (!_emulator)
            return false;
        EmulatorContext* ctx = _emulator->GetContext();
        ctx->config.ngs.ramKB = ramKB;
        strncpy(ctx->config.ngs.sdCardPath, sd.c_str(), sizeof ctx->config.ngs.sdCardPath - 1);
        SoundManager* sound = ctx->pSoundManager;
        return sound && sound->switchGeneralSoundCard(GSTypeKind::NGS) && sound->getGeneralSound() &&
               sound->getGeneralSound()->implementation() == GSCardImplementation::NGS;
    }
};
} // namespace

TEST_F(NeoGSAcceptance_Test, TestNgsDetectsTheCardAndTestsItsPages)
{
    for (unsigned ramKB : {2048u, 4096u})
    {
        ASSERT_TRUE(createMachine(ramKB)) << "Pentagon with NeoGS";
        const auto scl = TestPathHelper::FindProjectRoot() / "testdata/sound/neogs/programs/test_ngs.scl";
        ASSERT_TRUE(_emulator->LoadDisk(scl.string(), 0));

        std::string screen;
        {
            TRDOSTestHelper trdos(_emulator, false);
            trdos.startCommand("RUN \"testngs\"");
            Memory* memory = _emulator->GetContext()->pMemory;
            // Done once the page test has printed its verdict
            trdos.runUntil(
                [&]
                {
                    screen = joined(readScreen42(memory, _font));
                    return screen.find("Test pages: ok") != std::string::npos ||
                           screen.find("Page error:") != std::string::npos;
                },
                60ull * 3'500'000ull);
        }

        SCOPED_TRACE(printable(screen));
        EXPECT_NE(screen.find("Test NeoGS build"), std::string::npos) << "the program started";
        EXPECT_NE(screen.find("1.11"), std::string::npos) << "firmware version v1.11";

        // Its clock test switches GSCFG0 through 10, 12, 20 and 24 MHz and
        // checks the card still answers each time
        size_t oks = 0;
        for (size_t at = screen.find(": Ok"); at != std::string::npos; at = screen.find(": Ok", at + 1))
            oks++;
        EXPECT_EQ(oks, 4u) << "all four card clocks";
        if (ramKB == 2048)
        {
            EXPECT_NE(screen.find("0x40"), std::string::npos) << "COM23: #40 pages";
            EXPECT_NE(screen.find("Test pages: ok"), std::string::npos);
        }
        else
        {
            // 4 MB: the firmware's RAMRO also protects pages 128/129, which
            // MPAG #40 maps - a genuine hardware quirk (neogs-tdd.md §3.2), so
            // the authors' page test fails there. It prints the value it read
            // back (the page kept its power-on 00), not the page number
            EXPECT_NE(screen.find("0x80"), std::string::npos) << "COM23: #80 pages";
            EXPECT_NE(screen.find("Page error: 00"), std::string::npos);
            EXPECT_EQ(screen.find("Test pages: ok"), std::string::npos);
        }

        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }
}

TEST_F(NeoGSAcceptance_Test, FlasherReprogramsTheFlashFromTheSdCard)
{
    // The NedoPC flasher (ngs/z80/flasher) on the host: it loads NGS_ROM.UPD
    // from the card's SD slot through the loader's command mode, shows its
    // menu, and on ENTER erases and reprograms the loader and main ROM blocks
    // with the card's own CPU, then restarts the card through CPLD port #80.
    ASSERT_TRUE(createMachine(4096, NeoGSTestSd::Fat16Mbr));
    auto* ngs = dynamic_cast<SoundChip_NeoGS*>(_emulator->GetContext()->pSoundManager->getGeneralSound());
    ASSERT_NE(ngs, nullptr);
    ASSERT_TRUE(ngs->sdCardPresent());

    // Junk in the unused parts of flash sectors 0 and 1: the update erases
    // them, so the junk must be gone afterwards
    const std::vector<uint8_t> shipped = readFile(TestPathHelper::FindProjectRoot() / "data/rom/neogs/full_ngs.rom");
    ASSERT_EQ(shipped.size(), 512u * 1024u);
    uint8_t* flash = ngs->flash().data();
    memset(flash + 0x4000, 0x00, 0x1000);
    memset(flash + 0x19000, 0x00, 0x1000);
    ngs->flash().clearModified();

    // Expected sectors, from the update file itself (build_update.a80): a
    // header of entries {DD offset << 8 | page, DW length, DW crc, name[6],
    // DW date}; each block lands at the start of its 64 KB sector, the rest
    // erased, and name + date form the sector's 8-byte trailer
    const std::vector<uint8_t> upd = readFile(TestPathHelper::FindProjectRoot() / "tools/neogs/parts/ngs_rom.upd");
    ASSERT_GT(upd.size(), 0x28u);
    ASSERT_EQ(0, memcmp(upd.data() + 2, "NGSF", 4));
    const size_t headerSize = upd[0] | (upd[1] << 8);
    std::vector<uint8_t> expected(Flash29F040B::SIZE, 0xFF);
    std::vector<int> sectors;
    for (size_t e = 8; e + 16 <= headerSize; e += 16)
    {
        const uint32_t dd = upd[e] | (upd[e + 1] << 8) | (upd[e + 2] << 16) | (static_cast<uint32_t>(upd[e + 3]) << 24);
        const int sector = static_cast<int>(dd & 0xFF);
        const size_t offset = dd >> 8;
        const size_t length = upd[e + 4] | (upd[e + 5] << 8);
        const size_t base = static_cast<size_t>(sector) * Flash29F040B::SECTOR_SIZE;
        memcpy(expected.data() + base, upd.data() + offset, length);
        memcpy(expected.data() + base + Flash29F040B::SECTOR_SIZE - 8, upd.data() + e + 8, 8); // name + date
        sectors.push_back(sector);
    }
    const auto scl = TestPathHelper::FindProjectRoot() / "testdata/sound/neogs/programs/flasher.scl";
    ASSERT_TRUE(_emulator->LoadDisk(scl.string(), 0));
    Keyboard* keyboard = _emulator->GetContext()->pKeyboard;
    {
        TRDOSTestHelper trdos(_emulator, false);
        trdos.startCommand("RUN \"FLASHNGS\"");
        // The update file is read and checked; then the menu waits for a key
        trdos.runFrames(8 * 50);
        keyboard->PressKey(ZXKEY_ENTER);
        trdos.runFrames(5);
        keyboard->ReleaseKey(ZXKEY_ENTER);
        // Done when the last block's trailer is in place (the flasher writes
        // it after the block's data) and the chip is idle again
        const size_t lastTrailer = static_cast<size_t>(sectors.back() + 1) * Flash29F040B::SECTOR_SIZE - 8;
        trdos.runUntil([&] { return ngs->flash().arrayMode() &&
                                    memcmp(flash + lastTrailer, expected.data() + lastTrailer, 8) == 0; },
                       30ull * 3'500'000ull);
    }

    EXPECT_TRUE(ngs->flash().modified()) << "the flasher never programmed the chip";

    ASSERT_EQ(sectors.size(), 2u) << "loader and main ROM blocks";
    for (int sector : sectors)
    {
        const size_t base = static_cast<size_t>(sector) * Flash29F040B::SECTOR_SIZE;
        EXPECT_EQ(0, memcmp(flash + base, expected.data() + base, Flash29F040B::SECTOR_SIZE))
            << "sector " << sector << " is exactly the update's block";
    }
    EXPECT_EQ(0, memcmp(flash + 0x70000, shipped.data() + 0x70000, 0x10000)) << "the FPGA sector is untouched";
}


TEST_F(NeoGSAcceptance_Test, TestEmuNgsPassesWithAnSdCard)
{
    // test_emu_ngs (NedoPC ngs/zx/test_emu_ngs, written for emulators):
    // patches the v1.11 main ROM, switches to extended paging, runs a
    // 256-step mailbox echo, then reads 256 SD sectors through its own card
    // driver without checking the data.
    //
    // Its driver packs the sector number as {B,C,D,E} = {15-8, 7-0, 31-24,
    // 23-16} (main.a80 READ_SD_SECTOR, sd_on_ngs.a80 SECM200), so the host's
    // sectors #0100..#01FF become SDHC block numbers #01000000..#01FF0000 -
    // 32 MB apart, from 8 GiB up to 16 GiB. It therefore needs an SDHC card of
    // more than 16 GiB: a sparse 17 GiB image (zeros, no real disk space).
    const std::string sparse = TestPathHelper::GetUniqueTestScratchPath("neogs-17g.img");
    {
        std::ofstream create(sparse, std::ios::binary);
    }
    std::filesystem::resize_file(sparse, 17ull * 1024 * 1024 * 1024);
    ASSERT_TRUE(createMachineWithSdPath(4096, sparse));
    auto* ngs = dynamic_cast<SoundChip_NeoGS*>(_emulator->GetContext()->pSoundManager->getGeneralSound());
    ASSERT_NE(ngs, nullptr);
    ASSERT_TRUE(ngs->sdCard()->isSdhc());

    const auto scl = TestPathHelper::FindProjectRoot() / "testdata/sound/neogs/programs/test_emu_ngs.scl";
    ASSERT_TRUE(_emulator->LoadDisk(scl.string(), 0));

    std::string screen;
    {
        TRDOSTestHelper trdos(_emulator, false);
        trdos.startCommand("RUN \"testngs\"");
        Memory* memory = _emulator->GetContext()->pMemory;
        trdos.runUntil(
            [&]
            {
                screen = joined(readScreen32(memory, _font));
                return screen.find("Test OK") != std::string::npos || screen.find("Error test") != std::string::npos;
            },
            120ull * 3'500'000ull);
    }
    std::filesystem::remove(sparse);

    SCOPED_TRACE(printable(screen));
    EXPECT_NE(screen.find("Test ports"), std::string::npos);
    EXPECT_NE(screen.find("Test SD card read"), std::string::npos);
    EXPECT_NE(screen.find("Test OK"), std::string::npos);
    EXPECT_EQ(screen.find("Error test"), std::string::npos);
    EXPECT_GE(ngs->sdCard()->blocksRead(), 256u);
}


TEST_F(NeoGSAcceptance_Test, NeoPlayerLightPlaysAnMp3FromTheSdCard)
{
    // Neo Player Light v0.60 (NedoPC ngs/zx/Neo_Player_Light): finds the MP3
    // files on the card, and on "2" streams one to the decoder through the
    // SPI ports (no DMA), polling DREQ
    _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON");
    ASSERT_NE(_emulator, nullptr);
    EmulatorContext* ctx = _emulator->GetContext();
    ASSERT_TRUE(makeSdImage(NeoGSTestSd::Fat16Mbr));
    strncpy(ctx->config.ngs.sdCardPath, _sdImage->path().c_str(), sizeof ctx->config.ngs.sdCardPath - 1);
    ctx->config.ngs.mp3Support = NGSMP3SupportKind::Software;
    ASSERT_TRUE(ctx->pSoundManager->switchGeneralSoundCard(GSTypeKind::NGS));
    auto* ngs = dynamic_cast<SoundChip_NeoGS*>(ctx->pSoundManager->getGeneralSound());
    ASSERT_NE(ngs, nullptr);
    ASSERT_NE(ngs->mp3Decoder(), nullptr);
    EXPECT_NE(ctx->pSoundManager->device(AudioSourceType::GeneralSoundMp3), nullptr) << "the MP3 source is registered";

    const auto scl = TestPathHelper::FindProjectRoot() / "testdata/sound/neogs/programs/neo_player_light.scl";
    ASSERT_TRUE(_emulator->LoadDisk(scl.string(), 0));
    std::string screen;
    size_t audibleFrames = 0;
    {
        TRDOSTestHelper trdos(_emulator, false);
        trdos.startCommand("RUN \"NPL\"");
        Memory* memory = ctx->pMemory;
        trdos.runUntil([&] { return joined(readScreen32(memory, _font)).find("EYEACHE .MP3") != std::string::npos; },
                       20ull * 3'500'000ull);
        ctx->pKeyboard->PressKey(ZXKEY_2);
        trdos.runFrames(5);
        ctx->pKeyboard->ReleaseKey(ZXKEY_2);
        for (int frame = 0; frame < 10 * 50; frame++)
        {
            trdos.runFrames(1);
            audibleFrames += ngs->hadAuxAudioActivityLastFrame() ? 1 : 0;
        }
        screen = joined(readScreen32(memory, _font));
    }

    SCOPED_TRACE(printable(screen));
    Vs10xxDecoder* mp3 = ngs->mp3Decoder();
    EXPECT_EQ(mp3->streamRate(), 44100u);
    EXPECT_GT(mp3->framesDecoded(), 300u);
    // ~10 s of real-time playback (the player starts within a frame or two)
    EXPECT_NEAR(static_cast<double>(mp3->samplesPlayed()) / 44100.0, 10.0, 0.5);
    EXPECT_NE(screen.find("44100 Hz"), std::string::npos);
    EXPECT_NE(screen.find("128 kbps"), std::string::npos);
    EXPECT_NE(screen.find("Time Play: 00:00:"), std::string::npos) << "DECODE_TIME on screen";
    EXPECT_GT(audibleFrames, 400u) << "the MP3 source carries the audio";
}

/// Neo Player Light v0.44 (NedoPC ngs/zx/npl_044): the card-side FAT driver
/// finds the MP3 files, and on "2" streams the first one to the decoder
/// through the SPI ports. Needs at least two MP3 files on the card: with one,
/// its FINDMP3 returns with the directory page still mapped, OPENFIL reads a
/// null file descriptor and follows a garbage cluster chain past the end of
/// the card - a bug of the player, the same on the board (neogs-tdd.md §14.2)
TEST_F(NeoGSAcceptance_Test, NeoPlayerLight044PlaysAnMp3FromTheSdCard)
{
    _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON");
    ASSERT_NE(_emulator, nullptr);
    EmulatorContext* ctx = _emulator->GetContext();
    ASSERT_TRUE(makeSdImage(NeoGSTestSd::Fat16Mbr));
    strncpy(ctx->config.ngs.sdCardPath, _sdImage->path().c_str(), sizeof ctx->config.ngs.sdCardPath - 1);
    ctx->config.ngs.mp3Support = NGSMP3SupportKind::Software;
    ASSERT_TRUE(ctx->pSoundManager->switchGeneralSoundCard(GSTypeKind::NGS));
    auto* ngs = dynamic_cast<SoundChip_NeoGS*>(ctx->pSoundManager->getGeneralSound());
    ASSERT_NE(ngs, nullptr);

    const auto scl = TestPathHelper::FindProjectRoot() / "testdata/sound/neogs/programs/npl044.scl";
    ASSERT_TRUE(_emulator->LoadDisk(scl.string(), 0));
    std::string screen;
    {
        TRDOSTestHelper trdos(_emulator, false);
        trdos.startCommand("RUN \"NPL044\"");
        Memory* memory = ctx->pMemory;
        trdos.runUntil([&] { return joined(readScreen32(memory, _font)).find("EYEACHE .MP3") != std::string::npos; },
                       20ull * 3'500'000ull);
        screen = joined(readScreen32(memory, _font));
        EXPECT_NE(screen.find("Found MP3:     2"), std::string::npos) << printable(screen);
        ctx->pKeyboard->PressKey(ZXKEY_2);
        trdos.runFrames(5);
        ctx->pKeyboard->ReleaseKey(ZXKEY_2);
        trdos.runFrames(5 * 50);
        screen = joined(readScreen32(memory, _font));
    }

    SCOPED_TRACE(printable(screen));
    Vs10xxDecoder* mp3 = ngs->mp3Decoder();
    EXPECT_EQ(mp3->streamRate(), 44100u);
    EXPECT_GT(mp3->framesDecoded(), 150u);
    EXPECT_NEAR(static_cast<double>(mp3->samplesPlayed()) / 44100.0, 5.0, 0.5) << "~5 s in real time";
    EXPECT_NE(screen.find("44100 Hz"), std::string::npos);
    EXPECT_NE(screen.find("128 kbps"), std::string::npos);
    EXPECT_NE(screen.find("Time Play: 00:00:0"), std::string::npos);
}

/// Neo Player Light v0.44 built for SD and MP3 DMA (ngs/zx/npl_044_dma):
/// the search works as in the SPI build, but this build cannot play - its
/// LDI_MP3 has the CMD17 call commented out and leaves the card deselected,
/// so the SD DMA module clocks #FF for ever waiting for a data token (the
/// module sends no command itself, dma_sd.v, and has no timeout). The test
/// pins that the emulation ends in exactly that state
TEST_F(NeoGSAcceptance_Test, NeoPlayerLight044DmaFindsTheFilesAndWaitsInTheSdDma)
{
    _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON");
    ASSERT_NE(_emulator, nullptr);
    EmulatorContext* ctx = _emulator->GetContext();
    ASSERT_TRUE(makeSdImage(NeoGSTestSd::Fat16Mbr));
    strncpy(ctx->config.ngs.sdCardPath, _sdImage->path().c_str(), sizeof ctx->config.ngs.sdCardPath - 1);
    ctx->config.ngs.mp3Support = NGSMP3SupportKind::Software;
    ASSERT_TRUE(ctx->pSoundManager->switchGeneralSoundCard(GSTypeKind::NGS));
    auto* ngs = dynamic_cast<SoundChip_NeoGS*>(ctx->pSoundManager->getGeneralSound());
    ASSERT_NE(ngs, nullptr);

    const auto scl = TestPathHelper::FindProjectRoot() / "testdata/sound/neogs/programs/npl044_dma.scl";
    ASSERT_TRUE(_emulator->LoadDisk(scl.string(), 0));
    TRDOSTestHelper trdos(_emulator, false);
    trdos.startCommand("RUN \"NPL044\"");
    Memory* memory = ctx->pMemory;
    trdos.runUntil([&] { return joined(readScreen32(memory, _font)).find("EYEACHE .MP3") != std::string::npos; },
                   20ull * 3'500'000ull);
    const std::string screen = joined(readScreen32(memory, _font));
    EXPECT_NE(screen.find("Found MP3:     2"), std::string::npos) << printable(screen);

    ctx->pKeyboard->PressKey(ZXKEY_2);
    trdos.runFrames(5);
    ctx->pKeyboard->ReleaseKey(ZXKEY_2);
    trdos.runFrames(50);
    EXPECT_TRUE(ngs->dma().running(NeoGSDma::SD)) << "the SD DMA module waits for a token";
    EXPECT_EQ(ngs->sdCard()->lastCommand(), 17) << "the last command was the directory read";
    EXPECT_EQ(ngs->mp3Decoder()->framesDecoded(), 0u);
    const uint16_t pc = ngs->getCPUReg(GSCpuRegister::PC);
    const uint8_t loop[] = {0xDB, 0x1F, 0xE6, 0x80, 0x20, 0xFA}; // IN A,(#1F) : AND #80 : JR NZ,$-4
    bool inLoop = false;
    for (int back = 0; back <= 4 && !inLoop; back += 2)
    {
        inLoop = true;
        for (int i = 0; i < 6; i++)
            inLoop = inLoop && ngs->peek(static_cast<uint16_t>(pc - back + i)) == loop[i];
    }
    EXPECT_TRUE(inLoop) << "the card CPU polls DMA_CST";
}
