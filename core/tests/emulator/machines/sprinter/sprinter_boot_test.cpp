// Sprinter Sp2000 BIOS 3.04 cold start on the real data/rom/sprinter/sp2k-3.04.rom
// (Sprinter roadmap S1 acceptance ACC-1a, test-plan R-1).
//
// The machine is created through EmulatorManager with the shipped
// configs/sprinter/unreal.ini, switched to the fast start and reset: the PLD
// starts configured, the BIOS POST writes the port table into RAM page #40 and
// opens the decoder ("DCP opened": the first IN, page 8 #0258), SETUP draws its
// text screen and, with no boot device, stops at its prompt.
//
// The text screen lives in the video RAM mode table: a text square's Mode1 byte
// is the character code (tdd-video §3), so the screen text is read back from
// VRAM without a renderer (phase S2).

#include <emulator/cpu/core.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/fdc/wd1793.h>
#include <emulator/io/keyboard/keyboard.h>
#include <emulator/media/mediamanager.h>
#include <emulator/memory/memory.h>
#include <emulator/ports/models/portdecoder_sprinter.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "3rdparty/lodepng/lodepng.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "pch.h"
#include "stdafx.h"
#include "sprinterfixture.h"

class SprinterBoot_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;

    void SetUp() override
    {
        if (SprinterFixture::Rom304Available() == false)
            GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";

        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);

        _emulator = _manager->CreateEmulatorWithModelAndRAM("sprinter-boot", "SPRINTER", 4096, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
        _decoder->GetRtc().SetFixedTime(1767268830);  // 2026-01-01 12:00:30 UTC

        // The test default: skip the loader (FastStart=1); the full start is covered by
        // SprinterPldConfig_Test.FastStartEqualsFullStart_Bios304
        _context->config.sprinter.fast_start = 1;
        _emulator->Reset();
        // No assertion looks at pixels
        _emulator->EnableTurboMode();
    }

    void TearDown() override
    {
        _emulator.reset();
        if (_manager)
        {
            for (const auto& id : _manager->GetEmulatorIds())
                _manager->RemoveEmulator(id);
        }
    }

    /// The text of mode-table row b, both column sets (40 squares; each 640-mode
    /// square holds two characters, Line1 then Line2), in both mode pages
    std::string ScreenText()
    {
        const SprinterVideoRam& vram = _decoder->GetVideoRam();
        std::string text;
        for (uint8_t page = 0; page < 2; page++)
        {
            for (uint8_t b = 0; b < 32; b++)
            {
                for (uint8_t a = 0; a < 40; a++)
                {
                    for (uint8_t half = 0; half < 2; half++)
                    {
                        const uint32_t line = (1u + 2u * a + half + 0x80u * page) * 1024u;
                        const uint8_t c = vram.Read(line + 0x301 + 4u * b);
                        text.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ');
                    }
                }
                text.push_back('\n');
            }
        }
        return text;
    }

    bool ScreenHas(const std::string& needle) { return ScreenText().find(needle) != std::string::npos; }

    /// SETUP's IDE detection waits ~31 s per empty unit (no IDE adapter before S3b): press F4 for both, as a
    /// user does. Set 2 scan codes through the Z84C15 SIO channel A, which SETUP's interrupt handler reads
    void SkipIdeDetection()
    {
        for (const char* unit : {"Primary Master   ... [Press F4", "Primary Slave    ... [Press F4"})
        {
            EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas(unit); }, 400, 5);
            ASSERT_TRUE(ScreenHas(unit)) << ScreenText();
            for (uint8_t code : {0x0C, 0xF0, 0x0C})
                _decoder->GetZ84().sio.Receive(0, code);
            EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return !ScreenHas(unit); }, 300, 1);
        }
    }

    /// A ZX key through the matrix (Spectrum mode reads port #FE, code #40)
    void Tap(ZXKeysEnum key)
    {
        _context->pKeyboard->PressKey(key);
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 4);
        _context->pKeyboard->ReleaseKey(key);
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 4);
    }
    void Chord(ZXKeysEnum shift, ZXKeysEnum key)
    {
        _context->pKeyboard->PressKey(shift);
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 2);
        Tap(key);
        _context->pKeyboard->ReleaseKey(shift);
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 4);
    }

    bool SpectrumScreenHas(const std::string& text) { return ScreenOCR::containsText(_emulator->GetId(), text); }

    static std::vector<uint8_t> ReadAll(const std::string& path)
    {
        std::vector<uint8_t> bytes;
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f)
            return bytes;
        std::fseek(f, 0, SEEK_END);
        bytes.resize(static_cast<size_t>(std::ftell(f)));
        std::fseek(f, 0, SEEK_SET);
        if (std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size())
            bytes.clear();
        std::fclose(f);
        return bytes;
    }
};

// ACC-1a: BIOS 3.04 reaches the boot menu.
// Boot-bound (BIOS POST, SETUP depacking, IDE detection): seconds of emulated time, the turbo mode on
TEST_F(SprinterBoot_Test, Bios304_ReachesTheBootMenu)
{
    // POST: the port table, then the first IN opens the decoder at page 8 #0258
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return _decoder->DcpOpenedFrame() >= 0; }, 50, 1);
    ASSERT_GE(_decoder->DcpOpenedFrame(), 0) << "the BIOS never opened the port decoder";
    EXPECT_EQ(_decoder->DcpOpenedPc(), 0x0CD8) << "DcpInit's IN A,(#E2) after the table";

    // The table the BIOS wrote equals the statically extracted 3.04 table (CRC b7f09600)
    const std::vector<uint8_t> rom = SprinterFixture::Rom304();
    const std::vector<uint8_t> expected = SprinterFixture::Table304(rom);
    const uint8_t* table = _context->pMemory->RAMPageAddress(0x40);
    size_t differences = 0;
    std::string report;
    for (size_t i = 0; i < expected.size(); i++)
    {
        if (table[i] == expected[i])
            continue;
        if (differences++ < 16)
            report += StringHelper::Format(" [%04X] %02X!=%02X", static_cast<unsigned>(i), table[i], expected[i]);
    }
    EXPECT_EQ(differences, 0u) << "page #40 right after DcpInit:" << report;

    // SETUP: the boot screen (BIOS id, memory, the CMOS clock; a blank CMOS loads the defaults)
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Memory    : 4096K"); }, 300, 5);
    EXPECT_TRUE(ScreenHas("Sprinter BIOS: ver 3.04")) << ScreenText();
    EXPECT_TRUE(ScreenHas("Memory    : 4096K")) << ScreenText();

    // IDE auto-detect: no drive answers (the IDE adapter comes in S3b), so each unit waits ~31 s
    // for BSY to drop. A user presses F4, as the screen says: the AT scan code arrives on
    // the Z84C15 SIO channel A, which SETUP's interrupt handler polls (set 2: F4 = #0C)
    for (const char* unit : {"Primary Master   ... [Press F4", "Primary Slave    ... [Press F4"})
    {
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas(unit); }, 300, 5);
        ASSERT_TRUE(ScreenHas(unit)) << ScreenText();
        for (uint8_t code : {0x0C, 0xF0, 0x0C})
            _decoder->GetZ84().sio.Receive(0, code);
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return !ScreenHas(unit); }, 300, 1);
    }

    // No boot device: the floppy and the hard disk fail, the BIOS offers ENTER / ESC
    const char* prompt = "PRESS <ENTER> TO REBOOT, <ESC> TO CANCEL";
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas(prompt); }, 1000, 5);
    EXPECT_TRUE(ScreenHas(prompt))
        << StringHelper::Format("PC=%04X frame=%llu\n", _context->pCore->GetZ80()->pc,
                                static_cast<unsigned long long>(_context->emulatorState.frame_counter))
        << ScreenText();
    EXPECT_TRUE(ScreenHas("fail")) << ScreenText();

    // Page #40 after POST and SETUP: the static table except what the BIOS changes at run time
    std::string after;
    size_t changed = 0;
    for (size_t i = 0; i < expected.size(); i++)
    {
        if (table[i] != expected[i] && changed++ < 32)
            after += StringHelper::Format(" [%04X] %02X!=%02X", static_cast<unsigned>(i), table[i], expected[i]);
    }
    EXPECT_EQ(changed, 0u) << "page #40 at the boot prompt:" << after;
}

// The floppy slots: the Sprinter's WD1793 serves four drives, so the media manager offers fdd.a - fdd.d
TEST_F(SprinterBoot_Test, FloppySlots_FourWd1793Drives)
{
    ASSERT_NE(_context->pMediaManager, nullptr);
    for (const char* id : {"fdd.a", "fdd.b", "fdd.c", "fdd.d"})
    {
        const auto info = _context->pMediaManager->Info(id);
        ASSERT_TRUE(info.has_value()) << id;
        EXPECT_EQ(info->descriptor.kind, MediaKind::Floppy) << id;
    }
}

// ACC-3: DSS 1.62 boots from the 1.44 MB floppy (testdata/machines/sprinter/dss_1_62_92.img) to its prompt.
// A blank CMOS boots the IDE master, then the alternative device: floppy B (SETUP defaults, CMOS #10 = #12).
// Function #51 runs the density probe: READ ADDRESS at 720 KB, the BIOS poll loop times out before the chip's
// Record Not Found, the #BD latch flips to 1.44 MB and the running command finds an ID. LBA 1 holds the DSS
// loader ("Starting..."); it loads SYSTEM.DOS, the shell SYSTEM.EXE runs SYSTEM.BAT: "ver", then the prompt
// echoes the batch's last line ("fn", Flex Navigator, which needs the S2 renderer).
// Boot-bound (BIOS POST, SETUP, DSS from the floppy at 21 MHz): ~2.7 s host time with the turbo mode
TEST_F(SprinterBoot_Test, Dss162_BootsFromTheHdFloppyToThePrompt)
{
    const std::string image = TestPathHelper::GetTestDataPath("machines/sprinter/dss_1_62_92.img");
    if (!FileHelper::FileExists(image))
        GTEST_SKIP() << "testdata/machines/sprinter/dss_1_62_92.img is missing";
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(image, 1, &error)) << error;

    WD1793* wd = _context->pBetaDisk;
    ASSERT_NE(wd, nullptr);
    EXPECT_EQ(wd->GetClockPolicy(), FdcClockPolicy::Latched);
    EXPECT_FALSE(_decoder->IsFdcHighDensity()) << "720 KB after reset";

    SkipIdeDetection();
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("B:\\>"); }, 3000, 5);
    EXPECT_TRUE(ScreenHas("Alternative Start from Diskette...Ok")) << ScreenText();
    EXPECT_TRUE(ScreenHas("Starting DOS...")) << ScreenText();
    EXPECT_TRUE(ScreenHas("Estex DSS Version 1.62.92")) << ScreenText();
    EXPECT_TRUE(ScreenHas("B:\\>"))
        << StringHelper::Format("PC=%04X frame=%llu\n", _context->pCore->GetZ80()->pc,
                                static_cast<unsigned long long>(_context->emulatorState.frame_counter))
        << ScreenText();

    RecordProperty("prompt_frame", std::to_string(_context->emulatorState.frame_counter));  // 20.48 ms frames

    // The probe left the latch at 1.44 MB: 2 MHz clock, 500 kbit/s separator
    EXPECT_TRUE(_decoder->IsFdcHighDensity());
    EXPECT_EQ(wd->GetClock(), FdcClock::Clock2MHz);
    EXPECT_EQ(wd->GetDataRate(), FdcDataRate::Rate500Kbps);
    EXPECT_EQ(wd->getSelectedDriveIndex(), 1) << "drive B";
}

// ACC-6: Spectrum mode with TR-DOS reads a TRD in drive A through the WD1793 at 720 KB.
// BIOS 3.04 has no Spectrum ROMs of its own (ESC at SETUP says "Spectrum ROM not installed. Use spectrum.exe"):
// DSS's ZX\SPECTRUM.EXE loads them from A:\ZX\ROMS. So the DSS floppy (its SYSTEM.BAT changed to
// "a:\zx\spectrum.exe a:\zx\pent128.zx") boots from drive B with a copy in drive A; the launcher leaves the
// Pentagon 128 menu (TR-DOS, 128 BASIC, ...) with the latch at 720 KB and the CPU at 21 MHz. Then the user puts
// a TRD into drive A, picks TR-DOS (Sprinter TR-DOS 7.01), LIST shows the catalog, LOAD "smReadMe" CODE puts the
// file's bytes at its start address.
// Boot-bound (BIOS, DSS, the launcher, TR-DOS): ~4 s host time with the turbo mode
TEST_F(SprinterBoot_Test, Dss162_SpectrumModeTrDosReadsATrd)
{
    const std::string image = TestPathHelper::GetTestDataPath("machines/sprinter/dss_1_62_92.img");
    const std::string trd = TestPathHelper::GetTestDataPath("loaders/trd/zx-format8.trd");
    if (!FileHelper::FileExists(image) || !FileHelper::FileExists(trd))
        GTEST_SKIP() << "the DSS floppy or the TRD fixture is missing";

    // SYSTEM.BAT is the third root entry (LBA 19), cluster 49 = LBA 80 (tools: the image README's layout)
    std::vector<uint8_t> disk = ReadAll(image);
    ASSERT_EQ(disk.size(), 1474560u);
    const std::string bat = "a:\\zx\\spectrum.exe a:\\zx\\pent128.zx\r\n";
    uint8_t* entry = disk.data() + 19 * 512 + 2 * 32;
    ASSERT_EQ(std::string(reinterpret_cast<const char*>(entry), 11), "SYSTEM  BAT");
    ASSERT_EQ(entry[26] | entry[27] << 8, 49);
    entry[28] = static_cast<uint8_t>(bat.size());
    entry[29] = entry[30] = entry[31] = 0;
    std::memcpy(disk.data() + 80 * 512, bat.data(), bat.size());
    std::string error;
    for (uint8_t drive : {1, 0})
    {
        // Two files: a session medium cannot share its source with another slot
        const char* leaf = drive ? "dss-spectrum-b.img" : "dss-spectrum-a.img";
        const std::string copy = TestPathHelper::GetUniqueTestScratchPath(leaf);
        ASSERT_TRUE(FileHelper::SaveBufferToFile(copy, disk.data(), disk.size()));
        ASSERT_TRUE(_emulator->LoadDisk(copy, drive, &error)) << error;
    }

    SkipIdeDetection();
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return SpectrumScreenHas("128 BASIC"); }, 3000, 10);
    ASSERT_TRUE(SpectrumScreenHas("TR-DOS")) << ScreenOCR::ocrScreen(_emulator->GetId()) << ScreenText();
    EXPECT_FALSE(_decoder->IsFdcHighDensity()) << "the launcher sets 720 KB";

    ASSERT_TRUE(_emulator->LoadDisk(trd, 0, &error)) << error;
    Tap(ZXKEY_ENTER);  // the menu's first entry: TR-DOS
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return SpectrumScreenHas("TR-DOS"); }, 300, 10);
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 50);

    Tap(ZXKEY_K);  // LIST (keyword mode)
    Tap(ZXKEY_ENTER);
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return SpectrumScreenHas("scroll?"); }, 400, 10);
    const std::string catalog = ScreenOCR::ocrScreen(_emulator->GetId());
    EXPECT_NE(catalog.find("Title: AMD4ever"), std::string::npos) << catalog;
    EXPECT_NE(catalog.find("20 File(s)"), std::string::npos) << catalog;
    EXPECT_NE(catalog.find("ZF#08.1 <C>"), std::string::npos) << catalog;
    Tap(ZXKEY_Y);  // the rest of the catalog
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return SpectrumScreenHas("mem_t.ba"); }, 200, 10);
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 50);  // back at the A> prompt, keyword mode

    // LOAD "smReadMe" CODE
    Tap(ZXKEY_J);
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_P);
    for (ZXKeysEnum key : {ZXKEY_S, ZXKEY_M})
        Tap(key);
    Chord(ZXKEY_CAPS_SHIFT, ZXKEY_R);
    for (ZXKeysEnum key : {ZXKEY_E, ZXKEY_A, ZXKEY_D})
        Tap(key);
    Chord(ZXKEY_CAPS_SHIFT, ZXKEY_M);
    Tap(ZXKEY_E);
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_P);
    _context->pKeyboard->PressKey(ZXKEY_CAPS_SHIFT);  // E mode, then I = CODE
    _context->pKeyboard->PressKey(ZXKEY_SYM_SHIFT);
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 4);
    _context->pKeyboard->ReleaseKey(ZXKEY_SYM_SHIFT);
    _context->pKeyboard->ReleaseKey(ZXKEY_CAPS_SHIFT);
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 4);
    Tap(ZXKEY_I);
    Tap(ZXKEY_ENTER);
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return SpectrumScreenHas("O.K."); }, 400, 10);
    ASSERT_TRUE(SpectrumScreenHas("O.K.")) << ScreenOCR::ocrScreen(_emulator->GetId());

    // The catalog entry (12th, smReadMe): start, length, first sector and track; the bytes in memory are the file's
    const std::vector<uint8_t> trdBytes = ReadAll(trd);
    ASSERT_EQ(trdBytes.size(), 655360u);
    const uint8_t* file = trdBytes.data() + 11 * 16;
    ASSERT_EQ(std::string(reinterpret_cast<const char*>(file), 8), "smReadMe");
    const uint16_t start = static_cast<uint16_t>(file[9] | file[10] << 8);
    const uint16_t length = static_cast<uint16_t>(file[11] | file[12] << 8);
    const size_t offset = (static_cast<size_t>(file[15]) * 16 + file[14]) * 256;
    EXPECT_EQ(start, 34928);
    EXPECT_EQ(length, 3342);
    size_t differ = 0;
    for (uint16_t i = 0; i < length; i++)
        differ += _context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(start + i)) != trdBytes[offset + i];
    EXPECT_EQ(differ, 0u) << "LOAD CODE through TR-DOS and the WD1793 at 250 kbit/s";
    EXPECT_FALSE(_decoder->IsFdcHighDensity());
}

namespace
{
/// BIOS SETUP's CMOS checksum (SETUP #9B32 CHEKSUM): registers #0E-#1F folded into
/// H = #DE as H = RLC(H - v) - v; the result lives in register #3F (TCHEKSM)
uint8_t SetupChecksum(const Ds12887& rtc)
{
    uint8_t h = 0xDE;
    for (uint8_t reg = 0x0E; reg < 0x0E + 0x12; reg++)
    {
        const uint8_t v = rtc.PeekRegister(reg);
        uint8_t a = static_cast<uint8_t>(h - v);
        a = static_cast<uint8_t>((a << 1) | (a >> 7));
        h = static_cast<uint8_t>(a - v);
    }
    return h;
}
}  // namespace

// ACC-2 (adapted to BIOS 3.04): enter SETUP with DEL, change a setting, save with F10.
// SETUP 1.58 of BIOS 3.04 has no date / time page (its 22 items are the START FEATURES:
// language, memory test, boot disks, IDE, screen position, TR-DOS drives), so the setting
// is "Memory Test". After the save the BIOS restarts SETUP with the saved values (no
// checksum warning any more), and the CMOS file ([SPRINTER] CmosFile) holds them.
// The keys go in as AT set 2 scan codes through the Z84C15 SIO channel A, a make and its
// break in separate frames (the SIO FIFO holds 3 bytes). SETUP's codes (KEY.ASM XLAT):
// DEL E0 71 -> #4F (TSETUP), Down E0 72 -> #52 (next item), PgDn E0 7A -> #53 (next
// value), F10 #09 -> #44 (save and exit).
// Boot-bound (the logo, SETUP and its restart: ~400 frames of real ROM), the turbo mode on
TEST_F(SprinterBoot_Test, Bios304_SetupSavesSettingToCmos)
{
    const std::string cmosPath = TestPathHelper::GetUniqueTestScratchPath("sprinter-acc2.cmos");
    std::strncpy(_context->config.sprinter.cmos_path, cmosPath.c_str(), sizeof(_context->config.sprinter.cmos_path) - 1);
    Ds12887& rtc = _decoder->GetRtc();

    auto key = [&](std::initializer_list<uint8_t> make, std::initializer_list<uint8_t> brk) {
        for (uint8_t code : make)
            _decoder->GetZ84().sio.Receive(0, code);
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 2);
        for (uint8_t code : brk)
            _decoder->GetZ84().sio.Receive(0, code);
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 2);
    };

    // The logo runs ~130 frames of HALTs; a key pressed then waits in SETUP's buffer
    // for TSETUP after the boot screen
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Sprinter BIOS: ver 3.04"); }, 300, 5);
    ASSERT_TRUE(ScreenHas("Sprinter BIOS: ver 3.04")) << ScreenText();
    EXPECT_TRUE(ScreenHas("CMOS CHECKSUM ERROR")) << "a blank CMOS: the defaults";
    key({0xE0, 0x71}, {0xE0, 0xF0, 0x71});  // DEL
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("SETUP UTILITY"); }, 400, 5);
    ASSERT_TRUE(ScreenHas("SPRINTER SETUP UTILITY Version 1.58")) << ScreenText();
    ASSERT_TRUE(ScreenHas("Memory Test")) << ScreenText();

    // The SETUP screen (80-column text, the BIOS palettes, a blue border) against its golden
    // image (ScreenSprinter's own render, reviewed by eye; no MAME capture: MAME's Sprinter
    // runs without a keyboard here). Two frames without the turbo mode render it in full
    _emulator->DisableTurboMode();
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 2);
    {
        const std::string golden =
            (TestPathHelper::FindProjectRoot() / "testdata" / "machines" / "sprinter" / "golden" / "setup-menu.png").string();
        const FramebufferDescriptor& fb = _context->pScreen->GetFramebufferDescriptor();
        const uint32_t* pixels = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
        std::vector<unsigned char> rgba(static_cast<size_t>(fb.width) * fb.height * 4);
        for (size_t i = 0; i < static_cast<size_t>(fb.width) * fb.height; i++)
        {
            for (unsigned c = 0; c < 3; c++)
                rgba[i * 4 + c] = static_cast<unsigned char>(pixels[i] >> (8 * c));
            rgba[i * 4 + 3] = 0xFF;
        }
        std::vector<unsigned char> expected;
        unsigned width = 0, height = 0;
        const bool loaded = lodepng::decode(expected, width, height, golden) == 0;
        if (!loaded || width != fb.width || height != fb.height || expected != rgba)
        {
            const std::string ours = TestPathHelper::GetUniqueTestScratchPath("setup-menu.png");
            lodepng::encode(ours, rgba, fb.width, fb.height);
            ADD_FAILURE() << "SETUP screen differs from " << golden << "; ours: " << ours;
        }
    }
    _emulator->EnableTurboMode();

    const uint8_t before = rtc.PeekRegister(0x0E);
    key({0xE0, 0x72}, {0xE0, 0xF0, 0x72});  // Down: item 1, Memory Test
    key({0xE0, 0x7A}, {0xE0, 0xF0, 0x7A});  // PgDn: the next value
    const uint8_t edited = rtc.PeekRegister(0x0E);
    EXPECT_EQ(edited, before) << "SETUP edits its copy; the CMOS changes at the save";
    key({0x09}, {0xF0, 0x09});              // F10: save and exit

    const uint8_t after = rtc.PeekRegister(0x0E);
    EXPECT_NE(after, before) << "register #0E (options: memory test, ...) saved";
    EXPECT_EQ(rtc.PeekRegister(0x3F), SetupChecksum(rtc)) << "checksum #3F";

    // SETUP starts over with the saved values: the boot screen without the checksum warning
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Memory    : 4096K"); }, 600, 5);
    ASSERT_TRUE(ScreenHas("Memory    : 4096K")) << ScreenText();
    EXPECT_FALSE(ScreenHas("CMOS CHECKSUM ERROR")) << ScreenText();

    // The machine goes away: the decoder writes the CMOS file
    _emulator.reset();
    for (const auto& id : _manager->GetEmulatorIds())
        _manager->RemoveEmulator(id);
    std::vector<uint8_t> image(128);
    FILE* f = std::fopen(cmosPath.c_str(), "rb");
    ASSERT_NE(f, nullptr) << cmosPath;
    const size_t got = std::fread(image.data(), 1, image.size(), f);
    std::fclose(f);
    std::remove(cmosPath.c_str());
    ASSERT_EQ(got, 128u);
    EXPECT_EQ(image[0x0E], after);
    Ds12887 fromFile{128};
    for (uint8_t reg = 0x0E; reg < 0x40; reg++)
        fromFile.WriteRegister(reg, image[reg]);
    EXPECT_EQ(image[0x3F], SetupChecksum(fromFile)) << "the file's checksum is valid";
}
