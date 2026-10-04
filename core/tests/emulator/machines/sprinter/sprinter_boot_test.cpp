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
#include <emulator/io/fdc/fdd.h>
#include <emulator/io/fdc/wd1793.h>
#include <emulator/io/keyboard/keyboard.h>
#include <emulator/media/mediamanager.h>
#include <emulator/memory/memory.h>
#include <emulator/memory/sprinter/sprinteraccelerator.h>
#include <emulator/sound/sprinter/covoxblaster.h>
#include <emulator/ports/models/portdecoder_sprinter.h>
#include <emulator/video/screen.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "3rdparty/lodepng/lodepng.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "pch.h"
#include "stdafx.h"
#include "sprinterdssmedia.h"
#include "sprinterfixture.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/mouse/debugmousemanager.h"
#include "emulator/state/devicestate.h"
#include "emulator/io/storage/chd/chdfile.h"
#include "emulator/io/storage/chd/chdimage.h"
#include "emulator/io/storage/chd/chdwriter.h"
#include "emulator/io/storage/rawimage.h"

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
        // These tests pin BIOS 3.04 (its screens, SETUP 1.58, IDE waits); the shipped default is 3.07 BETA 1
        ASSERT_TRUE(SprinterFixture::SelectBios(_context, "sp2k-3.04.rom"));

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

    /// SETUP's IDE detection with no drive: an empty channel reads #7F (the DD7 pull-down, BSY = 0), so BIOS 3.04
    /// reports both primary units "None" at once, without the F4 a user pressed while the bus floated at #FF
    void SkipIdeDetection()
    {
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return DetectResult("Primary Slave") == "None"; }, 400, 1);
        ASSERT_EQ(DetectResult("Primary Master"), "None") << ScreenText();
        ASSERT_EQ(DetectResult("Primary Slave"), "None") << ScreenText();
    }

    /// What SETUP printed after "Detecting IDE <unit> ... ": "None", "Skipped" or the drive's model; empty while the
    /// unit is still probed ("[Press F4 to skip]") or not reached yet
    std::string DetectResult(const std::string& unit)
    {
        const std::string text = ScreenText();
        const size_t at = text.find("Detecting IDE " + unit);
        if (at == std::string::npos)
            return {};
        const size_t eol = text.find('\n', at);
        const size_t dots = text.find("... ", at);
        if (dots == std::string::npos || dots > eol)
            return {};
        std::string result = text.substr(dots + 4, eol - dots - 4);
        result.erase(result.find_last_not_of(' ') + 1);
        return result.rfind("[Press F4", 0) == 0 ? std::string() : result;
    }

    /// A blank hard disk image (1 MiB) in the scratch folder: enough for IDENTIFY
    static std::string BlankHddFile(const std::string& leaf)
    {
        std::vector<uint8_t> disk(1024 * 1024, 0);
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(leaf);
        return FileHelper::SaveBufferToFile(path, disk.data(), disk.size()) ? path : std::string();
    }

    struct UnitProbe
    {
        std::string unit;
        std::string result;
        uint64_t frames = 0;  ///< from the previous unit's result (the first: from "Detecting IDE" on the screen)
    };

    /// SETUP's IDE scan, unit by unit: each unit's result and the frames its probe took
    std::vector<UnitProbe> RunIdeDetection(const std::vector<std::string>& units, int maxFramesPerUnit)
    {
        std::vector<UnitProbe> probes;
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Detecting IDE " + units[0]); }, 800, 1);
        uint64_t last = Frame();
        for (const std::string& unit : units)
        {
            EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return !DetectResult(unit).empty(); }, maxFramesPerUnit, 1);
            probes.push_back({unit, DetectResult(unit), Frame() - last});
            last = Frame();
        }
        return probes;
    }

    /// The kept BIOS images and the units their SETUP scans (3.04: the primary channel; 3.06 / 3.07: both)
    static const std::vector<std::pair<std::string, std::vector<std::string>>>& DetectingBioses()
    {
        static const std::vector<std::string> two = {"Primary Master", "Primary Slave"};
        static const std::vector<std::string> four = {"Primary Master", "Primary Slave", "Secondary Master", "Secondary Slave"};
        static const std::vector<std::pair<std::string, std::vector<std::string>>> bioses = {
            {"sp2k-3.04.rom", two}, {"sp2k-3.06-hf2.rom", four}, {"sp2k-3.07-beta1.rom", four}};
        return bioses;
    }

    /// Each kept BIOS on a fresh machine (a blank CMOS and RAM: what one image leaves there changes the next one's
    /// scan) with `insertMedia` through its IDE scan: every unit's result is `expected(unit)`; a unit on a channel
    /// without a drive takes at most `kEmptyProbeFrames`
    void ExpectIdeDetection(const std::function<void()>& insertMedia, const std::function<std::string(const std::string&)>& expected,
                            const std::function<bool(const std::string&)>& channelEmpty)
    {
        constexpr uint64_t kEmptyProbeFrames = 10;  // ~0.2 s; a floating #FF bus kept BSY set for ~31 s
        for (const auto& [bios, units] : DetectingBioses())
        {
            if (bios != DetectingBioses().front().first)
            {
                TearDown();
                SetUp();
            }
            insertMedia();
            if (!UseBios(bios))
            {
                ADD_FAILURE() << "data/rom/sprinter/" << bios << " not loaded";
                continue;
            }
            for (const UnitProbe& probe : RunIdeDetection(units, 2000))
            {
                EXPECT_EQ(probe.result, expected(probe.unit)) << bios << " " << probe.unit << "\n" << ScreenText();
                if (channelEmpty(probe.unit))
                    EXPECT_LE(probe.frames, kEmptyProbeFrames) << bios << " " << probe.unit;
                std::string key = bios + "-" + probe.unit;
                std::replace(key.begin(), key.end(), ' ', '-');
                RecordProperty(key, std::to_string(probe.frames) + " frames, " + probe.result);
                std::printf("[ IDE probe ] %-20s %-17s %5llu frames  %s\n", bios.c_str(), probe.unit.c_str(),
                            static_cast<unsigned long long>(probe.frames), probe.result.c_str());
            }
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

    /// The current picture as a PNG (ScreenSprinter's render after two full frames)
    void SaveScreen(const std::string& path)
    {
        _emulator->DisableTurboMode();
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 2);
        const FramebufferDescriptor& fb = _context->pScreen->GetFramebufferDescriptor();
        const uint32_t* pixels = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
        std::vector<unsigned char> rgba(static_cast<size_t>(fb.width) * fb.height * 4);
        for (size_t i = 0; i < static_cast<size_t>(fb.width) * fb.height; i++)
        {
            for (unsigned c = 0; c < 3; c++)
                rgba[i * 4 + c] = static_cast<unsigned char>(pixels[i] >> (8 * c));
            rgba[i * 4 + 3] = 0xFF;
        }
        lodepng::encode(path, rgba, fb.width, fb.height);
        _emulator->EnableTurboMode();
    }

    /// The picture against a golden image in testdata/machines/sprinter/golden (ScreenSprinter's own render,
    /// reviewed by eye): every pixel equal. Two frames without the turbo mode render the screen in full; on a
    /// difference our frame is saved to the scratch folder for review
    void ExpectScreenMatchesGolden(const std::string& name)
    {
        _emulator->DisableTurboMode();
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 2);
        const std::string golden =
            (TestPathHelper::FindProjectRoot() / "testdata" / "machines" / "sprinter" / "golden" / name).string();
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
            const std::string ours = TestPathHelper::GetUniqueTestScratchPath(name);
            lodepng::encode(ours, rgba, fb.width, fb.height);
            ADD_FAILURE() << "the screen differs from " << golden << "; ours: " << ours;
        }
        _emulator->EnableTurboMode();
    }

    bool SpectrumScreenHas(const std::string& text) { return ScreenOCR::containsText(_emulator->GetId(), text); }

    /// A hard disk image into an IDE slot through the media manager, guest writes kept in memory
    /// (Session): the image file is never written
    void InsertHdd(const std::string& path, const char* slot = "ide0.master")
    {
        ASSERT_NE(_context->pMediaManager, nullptr);
        MediaSource source;
        source.path = path;
        InsertOptions options;
        options.immediate = true;
        options.access = AccessMode::Session;
        const auto result = _context->pMediaManager->Insert(slot, source, options);
        ASSERT_TRUE(result.Ok()) << slot << ": " << result.message;
    }

    uint64_t Frame() const { return _context->emulatorState.frame_counter; }

    size_t ScreenCount(const std::string& needle)
    {
        const std::string text = ScreenText();
        size_t count = 0;
        for (size_t pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + 1))
            count++;
        return count;
    }

    /// Another BIOS image from data/rom/sprinter, then the reset (false when the image is not there)
    bool UseBios(const std::string& file)
    {
        if (!SprinterFixture::SelectBios(_context, file))
            return false;
        _emulator->Reset();
        return true;
    }

    /// The DSS 1.62.92 system on a built hard disk (BuildDssHdd): the floppy's loader (LBA 1-3) and its SYSTEM.DOS /
    /// SYSTEM.EXE, with `bat` as SYSTEM.BAT; saved as a unique scratch file. Empty when the floppy is missing
    std::string DssHddFile(const std::string& leaf, const std::string& bat)
    {
        const std::vector<uint8_t> floppy = ReadAll(TestPathHelper::GetTestDataPath("machines/sprinter/dss_1_62_92.img"));
        if (floppy.size() != 1474560u)
            return {};
        const std::vector<uint8_t> loader(floppy.begin() + 512, floppy.begin() + 4 * 512);
        std::vector<uint8_t> disk = BuildDssHdd(loader, {{"SYSTEM  DOS", FloppyRootFile(floppy, "SYSTEM  DOS")},
                                                         {"SYSTEM  EXE", FloppyRootFile(floppy, "SYSTEM  EXE")},
                                                         {"SYSTEM  BAT", std::vector<uint8_t>(bat.begin(), bat.end())}});
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(leaf);
        return FileHelper::SaveBufferToFile(path, disk.data(), disk.size()) ? path : std::string();
    }

    /// A hard disk image with guest writes going to the file (the default for an image: WriteThrough)
    void InsertHddWriteThrough(const std::string& path, const char* slot)
    {
        MediaSource source;
        source.path = path;
        InsertOptions options;
        options.immediate = true;
        options.access = AccessMode::WriteThrough;
        const auto result = _context->pMediaManager->Insert(slot, source, options);
        ASSERT_TRUE(result.Ok()) << slot << ": " << result.message;
    }

    /// The machine goes away (media written back and closed)
    void DestroyEmulator()
    {
        _emulator.reset();
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);
        _context = nullptr;
        _decoder = nullptr;
    }

    /// A directory entry `name` (8.3, 11 characters) in the root of a BuildDssHdd image
    static bool RootHasDirectory(const std::vector<uint8_t>& disk, const char name[11])
    {
        const size_t root = static_cast<size_t>(kDssHddRootLba) * 512;
        for (size_t e = root; e + 32 <= root + 512 * 32 && e + 32 <= disk.size(); e += 32)
        {
            if (std::memcmp(&disk[e], name, 11) == 0 && (disk[e + 11] & 0x10))
                return true;
        }
        return false;
    }

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

    // IDE auto-detect with no drive: the empty channel reads #7F (DD7 pulled down, BSY = 0) and the sector count
    // does not echo, so both units are "None" at once (a floating #FF kept BSY set: ~31 s per unit until F4)
    SkipIdeDetection();

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

    // The prompt screen through the S2 renderer (BIOS 80-column text, DSS output) against its golden image,
    // tolerance 0: the screen is static from the prompt until Flex Navigator prints its banner (~60 frames
    // later), so two frames after the prompt is found (5-frame polling) give the same picture every run
    ExpectScreenMatchesGolden("dss-prompt.png");
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
    // image (no MAME capture: MAME's Sprinter runs without a keyboard here)
    ExpectScreenMatchesGolden("setup-menu.png");

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


/// region <S3b: IDE (tdd-storage §3, test-plan R-5, T-IDE-8)>

// ACC-4: DSS 1.62.92 boots from a hard disk image on ide0.master. The image is built here (BuildDssHdd) from the
// DSS floppy's files - the 1 GB real disks are not in the repo - with a SYSTEM.BAT of "ver" and "mkdir c:\s3b".
// BIOS 3.04 with a blank CMOS (#10 = #12: the IDE master, then floppy B) finds the disk at once (IDENTIFY), probes
// the empty slave (the master answers for it with status #00, the BIOS's NOP check times out after #118 HALTs,
// ~5.7 s), reads LBA 1 ("Starting...") and jumps to the DSS loader, which reads LBA 2-3, the MBR, the partition
// boot sector, the root and SYSTEM.DOS; SYSTEM.EXE runs SYSTEM.BAT. The directory DSS creates is in the image file.
// Boot-bound (BIOS POST, SETUP, the slave probe, DSS from the hard disk): ~500 frames of real ROM, the turbo mode on
TEST_F(SprinterBoot_Test, Dss162_BootsFromAHardDiskImage)
{
    const std::string image = DssHddFile("dss-hdd.img", "ver\r\nmkdir c:\\s3b\r\n");
    if (image.empty())
        GTEST_SKIP() << "testdata/machines/sprinter/dss_1_62_92.img is missing";
    InsertHddWriteThrough(image, "ide0.master");

    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Primary Slave    ... None"); }, 800, 5);
    EXPECT_TRUE(ScreenHas("Detecting IDE Primary Master   ... UNREAL-NG HDD")) << ScreenText();
    EXPECT_TRUE(ScreenHas("Detecting IDE Primary Slave    ... None")) << ScreenText();
    RecordProperty("detect_done_frame", std::to_string(Frame()));

    // The batch's last line, then the prompt again: three prompts on the screen
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenCount("C:\\>") >= 3; }, 800, 5);
    EXPECT_TRUE(ScreenHas("Start from Hard disk...Ok")) << ScreenText();
    EXPECT_TRUE(ScreenHas("Starting DOS...")) << ScreenText();
    EXPECT_TRUE(ScreenHas("Estex DSS Version 1.62.92")) << ScreenText();
    EXPECT_TRUE(ScreenHas("C:\\>mkdir c:\\s3b")) << ScreenText();
    ASSERT_EQ(ScreenCount("C:\\>"), 3u)
        << StringHelper::Format("PC=%04X frame=%llu\n", _context->pCore->GetZ80()->pc, static_cast<unsigned long long>(Frame()))
        << ScreenText();
    EXPECT_LT(Frame(), 600u) << "the prompt at ~10 s of emulated time";
    RecordProperty("prompt_frame", std::to_string(Frame()));  // 20.48 ms frames

    // The prompt screen through the S2 renderer against its golden image (static: no program runs)
    ExpectScreenMatchesGolden("dss-hdd-prompt.png");

    // The guest's MKDIR reached the image file (WriteThrough)
    DestroyEmulator();
    EXPECT_TRUE(RootHasDirectory(ReadAll(image), "S3B        ")) << "C:\\S3B in the root of " << image;
    std::remove(image.c_str());
}

// T-IDE-8: BIOS 3.04 probes ide0.slave whatever it holds: with an empty CD unit there (CD1=1, the configuration
// MAME ships, sprinter.cpp:1967) it reports the drive by its IDENTIFY PACKET DEVICE model at once, and the boot from
// the master is unaffected. With no device there it reports "None" (Dss162_BootsFromAHardDiskImage).
// Boot-bound (BIOS POST, SETUP, DSS from the hard disk), the turbo mode on
TEST_F(SprinterBoot_Test, Bios304_FindsAnEmptyCdUnitOnTheSlave)
{
    const std::string image = DssHddFile("dss-hdd-cd.img", "ver\r\n");
    if (image.empty())
        GTEST_SKIP() << "testdata/machines/sprinter/dss_1_62_92.img is missing";
    _context->config.ide[1].cd = 1;
    _context->pCore->RefitIde();
    _emulator->Reset();
    ASSERT_EQ(_context->pMediaManager->Info("ide0.slave")->descriptor.kind, MediaKind::Optical);
    InsertHdd(image);

    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Primary Slave    ... UNREAL-NG CD-ROM"); }, 400, 5);
    EXPECT_TRUE(ScreenHas("Detecting IDE Primary Master   ... UNREAL-NG HDD")) << ScreenText();
    ASSERT_TRUE(ScreenHas("Detecting IDE Primary Slave    ... UNREAL-NG CD-ROM")) << ScreenText();
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenCount("C:\\>") >= 2; }, 600, 5);
    EXPECT_TRUE(ScreenHas("Estex DSS Version 1.62.92")) << ScreenText();
    EXPECT_EQ(ScreenCount("C:\\>"), 2u) << ScreenText();
    std::remove(image.c_str());
}

// Both channels through the firmware: BIOS 3.06 (community build, data/rom/sprinter/sp2k-3.06-hf2.rom) scans four
// units, so it finds a disk on the secondary master after OUT (#BC),#01; DSS 1.62 boots from the primary master and,
// scanning #80-#83 itself (ide_drv0.asm), mounts the secondary disk as D: and writes a directory there. No F4: each
// slave probe gets status #00 from the master of its channel.
// Boot-bound (BIOS POST, SETUP, DSS from the hard disk), the turbo mode on
TEST_F(SprinterBoot_Test, Bios306_DssUsesBothChannels)
{
    const std::string system = DssHddFile("dss-hdd-c.img", "ver\r\nmkdir d:\\ide1\r\n");
    const std::string data = DssHddFile("dss-hdd-d.img", "");
    if (system.empty() || data.empty())
        GTEST_SKIP() << "testdata/machines/sprinter/dss_1_62_92.img is missing";
    if (!UseBios("sp2k-3.06-hf2.rom"))
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.06-hf2.rom not found";
    InsertHddWriteThrough(system, "ide0.master");
    InsertHddWriteThrough(data, "ide1.master");

    // Both channels have a master: each slave probe gets status #00 from its master and ends without F4
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Secondary Slave   ... None"); }, 800, 5);
    EXPECT_TRUE(ScreenHas("Detecting IDE Primary Master    ... UNREAL-NG HDD")) << ScreenText();
    EXPECT_TRUE(ScreenHas("Detecting IDE Primary Slave     ... None")) << ScreenText();
    EXPECT_TRUE(ScreenHas("Detecting IDE Secondary Master  ... UNREAL-NG HDD")) << ScreenText();
    ASSERT_TRUE(ScreenHas("Detecting IDE Secondary Slave   ... None")) << ScreenText();

    // DSS 1.62's MKDIR with a drive letter leaves that drive current: the next prompt is D:\>
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("D:\\>"); }, 800, 5);
    EXPECT_TRUE(ScreenHas("Boot from HDD Primary IDE Master OK")) << ScreenText();
    EXPECT_TRUE(ScreenHas("Estex DSS Version 1.62.92")) << ScreenText();
    EXPECT_TRUE(ScreenHas("C:\\>mkdir d:\\ide1")) << ScreenText();
    ASSERT_TRUE(ScreenHas("D:\\>")) << ScreenText();
    EXPECT_FALSE(ScreenHas("rror")) << ScreenText();

    DestroyEmulator();
    EXPECT_TRUE(RootHasDirectory(ReadAll(data), "IDE1       ")) << "D:\\IDE1 on the secondary master's image";
    EXPECT_FALSE(RootHasDirectory(ReadAll(system), "IDE1       "));
    std::remove(system.c_str());
    std::remove(data.c_str());
}

// Empty channels (tdd-storage §3.4): the Sprinter's AT board pulls DD7 down as the ATA standard asks, so a channel
// with no drive reads #7F - BSY = 0 - and every BIOS rejects its units at once:
//   3.04 (two units, AUTOIDE MASTER): status without BSY, then the sector count written with 5 reads back #7F;
//   3.06 / 3.07 (four units, AUTOIDE.asm AUTODETECTING): CheckChanel's floating-bus signature (#78 #68 #ED, the
//   opcode bytes a bus-holding board returns) does not match, Bug31SecCheck / Clear_BUSY see BSY = 0 at once, and
//   DETECTORS.Counter reads #7F instead of 5.
// A floating #FF (the S3b model) kept BSY set: ~1650 frames on a master (the 2 s Bug31 wait, then 31 s) and ~1550 on
// a slave, until F4. An absent slave next to a master is unchanged: the master answers with status #00 (3.04's NOP
// check waits #118 HALTs for DRDY, 3.06 / 3.07 see status 0 at once).
// Boot-bound (BIOS POST, SETUP and the IDE scan of three BIOS images per test, ~300-500 frames each), turbo mode on
TEST_F(SprinterBoot_Test, EmptyChannels_DiskOnThePrimaryMasterOnly)
{
    const std::string image = BlankHddFile("ide-empty-pm.img");
    ASSERT_FALSE(image.empty());
    ExpectIdeDetection([&] { InsertHdd(image, "ide0.master"); }, [](const std::string& unit) { return unit == "Primary Master" ? "UNREAL-NG HDD" : "None"; },
                       [](const std::string& unit) { return unit.rfind("Secondary", 0) == 0; });
    std::remove(image.c_str());
}

TEST_F(SprinterBoot_Test, EmptyChannels_NoDrives)
{
    ExpectIdeDetection([] {}, [](const std::string&) { return "None"; }, [](const std::string&) { return true; });
}

TEST_F(SprinterBoot_Test, EmptyChannels_DiskOnTheSecondaryMasterOnly)
{
    const std::string image = BlankHddFile("ide-empty-sm.img");
    ASSERT_FALSE(image.empty());
    ExpectIdeDetection([&] { InsertHdd(image, "ide1.master"); }, [](const std::string& unit) { return unit == "Secondary Master" ? "UNREAL-NG HDD" : "None"; },
                       [](const std::string& unit) { return unit.rfind("Primary", 0) == 0; });
    std::remove(image.c_str());
}

// The owner's real system disk (the MAME pack's sp_hdd_sys.chd as a raw 1 GiB image, not in the repo; path in
// UNREAL_SPRINTER_HDD): DSS 1.71.57 on BIOS 3.06, the firmware the pack runs it with (BIOS 3.04 loads the DSS loader
// and SYSTEM.DOS, then DSS 1.71 stops in its own start-up with "Fatal error", as it does from the DSS 1.71 floppy).
// Guest writes stay in memory (Session). The empty secondary channel reads #7F (DD7 pull-down): "None" at once; MAME reaches the
// banner at frame 385 with its CD unit on the primary slave (testdata/machines/sprinter/reference/README.md).
// Boot-bound (BIOS POST, SETUP, DSS 1.71 from the hard disk), the turbo mode on
TEST_F(SprinterBoot_Test, RealHdd_Dss171BootsFromTheMamePackImage)
{
    const char* path = std::getenv("UNREAL_SPRINTER_HDD");
    if (!path || !FileHelper::FileExists(path))
        GTEST_SKIP() << "UNREAL_SPRINTER_HDD (the raw sp_hdd_sys.img) not set";
    if (!UseBios("sp2k-3.06-hf2.rom"))
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.06-hf2.rom not found";
    InsertHdd(path);

    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return DetectResult("Secondary Slave") == "None"; }, 800, 1);
    EXPECT_EQ(DetectResult("Secondary Master"), "None") << ScreenText();
    EXPECT_EQ(DetectResult("Secondary Slave"), "None") << ScreenText();
    EXPECT_TRUE(ScreenHas("Detecting IDE Primary Master    ... UNREAL-NG HDD")) << ScreenText();
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Shell version"); }, 400, 1);
    EXPECT_TRUE(ScreenHas("Boot from HDD Primary IDE Master OK")) << ScreenText();
    ASSERT_TRUE(ScreenHas("Estex DSS version 1.71.57. Shell version 1.2.522.")) << ScreenText();
    RecordProperty("banner_frame", std::to_string(Frame()));
}

// The ZXMAK2 bundle's disk (sp_disk1.vhd, a fixed VHD of 2 GiB, not in the repo; path in UNREAL_SPRINTER_HDD_VHD):
// DSS 1.62.93 on BIOS 3.04 to the prompt before its SYSTEM.BAT starts Flex Navigator. Guest writes stay in memory.
// Boot-bound (BIOS POST, SETUP, DSS from the hard disk), the turbo mode on
TEST_F(SprinterBoot_Test, RealHdd_Dss16293BootsFromTheZxmak2Vhd)
{
    const char* path = std::getenv("UNREAL_SPRINTER_HDD_VHD");
    if (!path || !FileHelper::FileExists(path))
        GTEST_SKIP() << "UNREAL_SPRINTER_HDD_VHD (sp_disk1.vhd) not set";
    InsertHdd(path);
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("C:\\>fn"); }, 800, 1);
    EXPECT_TRUE(ScreenHas("Start from Hard disk...Ok")) << ScreenText();
    EXPECT_TRUE(ScreenHas("Estex DSS Version 1.62.93")) << ScreenText();
    ASSERT_TRUE(ScreenHas("C:\\>fn")) << ScreenText();
    RecordProperty("prompt_frame", std::to_string(Frame()));
}

// A MAME CHD on the IDE (docs/inprogress/2026-10-02-media-chd/): the ACC-4 disk compressed with chdman's default
// codecs (lzma, zlib, huff, flac) boots DSS 1.62 like the raw image. The IDE default (WriteThrough) turns into session
// access for a CHD: the guest's MKDIR stays in the change layer and the file is not touched until `save`, which writes
// the CHD again with the directory in it.
// Boot-bound (BIOS POST, SETUP, the slave probe, DSS from the hard disk), the turbo mode on
TEST_F(SprinterBoot_Test, Dss162_BootsFromAChdAndSavesTheGuestWrites)
{
    const std::string image = DssHddFile("dss-hdd-for-chd.img", "ver\r\nmkdir c:\\chd\r\n");
    if (image.empty())
        GTEST_SKIP() << "testdata/machines/sprinter/dss_1_62_92.img is missing";
    const std::string chdPath = TestPathHelper::GetUniqueTestScratchPath("dss-hdd.chd");
    {
        auto raw = RawImage::Open(image, RawImage::Access::ReadOnly);
        ASSERT_NE(raw, nullptr);
        chd::WriteOptions options;
        options.codecs = chd::kDefaultHardDiskCodecs;
        options.metadata = {chd::HardDiskMetadata(*chd::GuessGeometry(raw->SectorCount()))};
        std::string error;
        ASSERT_TRUE(chd::WriteChd(chdPath, *raw, options, &error)) << error;
    }
    std::remove(image.c_str());
    const std::vector<uint8_t> before = ReadAll(chdPath);

    InsertHddWriteThrough(chdPath, "ide0.master");
    EXPECT_EQ(_context->pMediaManager->Info("ide0.master")->access, AccessMode::Session);
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenCount("C:\\>") >= 3; }, 1200, 5);
    EXPECT_TRUE(ScreenHas("Detecting IDE Primary Master   ... UNREAL-NG HDD")) << ScreenText();
    EXPECT_TRUE(ScreenHas("Estex DSS Version 1.62.92")) << ScreenText();
    ASSERT_TRUE(ScreenHas("C:\\>mkdir c:\\chd")) << ScreenText();
    ASSERT_EQ(ScreenCount("C:\\>"), 3u) << ScreenText();

    EXPECT_TRUE(_context->pMediaManager->Info("ide0.master")->dirty);
    EXPECT_TRUE(ReadAll(chdPath) == before) << "the CHD is untouched until a save";


    SaveOutcome outcome;
    const MediaResult saved = _context->pMediaManager->Save("ide0.master", {}, &outcome);
    ASSERT_TRUE(saved.Ok()) << saved.message;
    EXPECT_FALSE(_context->pMediaManager->Info("ide0.master")->dirty);

    // The saved CHD, read on its own, has the directory in the root
    std::string error;
    auto chd = ChdImage::Open(chdPath, &error);
    ASSERT_NE(chd, nullptr) << error;
    EXPECT_TRUE(chd->File().Verify(&error)) << error;
    std::vector<uint8_t> disk(static_cast<size_t>(chd->SectorCount()) * 512);
    for (uint64_t lba = 0; lba < chd->SectorCount(); lba++)
        ASSERT_TRUE(chd->ReadSector(lba, disk.data() + lba * 512));
    EXPECT_TRUE(RootHasDirectory(disk, "CHD        ")) << "C:\\CHD in the saved CHD";
    chd.reset();
    DestroyEmulator();
    std::remove(chdPath.c_str());
}

// The owner's real system disk as MAME ships it (the pack's sp_hdd_sys.chd, an uncompressed 1 GiB CHD of 96 MB,
// not in the repo; path in UNREAL_SPRINTER_HDD_CHD): inserted as is, no extraction. DSS 1.71.57 on BIOS 3.06, as
// RealHdd_Dss171BootsFromTheMamePackImage does with the extracted raw image. The file is not written.
// Boot-bound (BIOS POST, SETUP, DSS 1.71 from the hard disk), the turbo mode on
TEST_F(SprinterBoot_Test, RealHdd_Dss171BootsFromTheMamePackChd)
{
    const char* path = std::getenv("UNREAL_SPRINTER_HDD_CHD");
    if (!path || !FileHelper::FileExists(path))
        GTEST_SKIP() << "UNREAL_SPRINTER_HDD_CHD (the MAME pack's sp_hdd_sys.chd) not set";
    if (!UseBios("sp2k-3.06-hf2.rom"))
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.06-hf2.rom not found";
    const uint64_t size = FileHelper::GetFileSize(path);
    std::error_code ec;
    const auto modified = std::filesystem::last_write_time(FileHelper::ToFsPath(path), ec);
    InsertHdd(path);
    EXPECT_EQ(_context->pMediaManager->Info("ide0.master")->format, "chd");

    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return DetectResult("Secondary Slave") == "None"; }, 800, 1);
    EXPECT_EQ(DetectResult("Secondary Master"), "None") << ScreenText();
    EXPECT_EQ(DetectResult("Secondary Slave"), "None") << ScreenText();
    EXPECT_TRUE(ScreenHas("Detecting IDE Primary Master    ... UNREAL-NG HDD")) << ScreenText();
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Shell version"); }, 400, 1);
    EXPECT_TRUE(ScreenHas("Boot from HDD Primary IDE Master OK")) << ScreenText();
    ASSERT_TRUE(ScreenHas("Estex DSS version 1.71.57. Shell version 1.2.522.")) << ScreenText();
    RecordProperty("banner_frame", std::to_string(Frame()));
    DestroyEmulator();
    EXPECT_EQ(FileHelper::GetFileSize(path), size);
    EXPECT_TRUE(std::filesystem::last_write_time(FileHelper::ToFsPath(path), ec) == modified) << "the CHD was written";
}

/// endregion </S3b>


// Instance lifecycle (found with AddressSanitizer while chasing a GUI crash): Core::Release deletes the memory before
// the port decoder, and the decoder's destructor detached itself from the freed SprinterMemory - a heap write after
// free on every Sprinter teardown, which corrupts whatever the allocator hands out next. The decoder now detaches only
// from the context's live memory. Under ASan this test fails on the old code; in a normal build it checks that a
// machine created after removed ones still boots.
// Boot-bound (BIOS POST to the boot screen on the last instance), the turbo mode on
TEST_F(SprinterBoot_Test, InstancesCanBeRemovedAndCreatedAgain)
{
    for (int i = 0; i < 3; i++)
    {
        auto extra = _manager->CreateEmulatorWithModelAndRAM("sprinter-extra", "SPRINTER", 4096, LoggerLevel::LogError);
        ASSERT_NE(extra, nullptr);
        extra->EnableTurboMode();
        EmulatorTestHelper::RunFramesFast(extra.get(), 10);
        const std::string id = extra->GetId();
        extra.reset();
        _manager->RemoveEmulator(id);
    }
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Memory    : 4096K"); }, 300, 5);
    EXPECT_TRUE(ScreenHas("Sprinter BIOS: ver 3.04")) << ScreenText();
}

/// region <The accelerator in real software (phase S5, tdd-accel-sound-input §1)>

namespace
{
/// The DSS 1.62 floppy's FAT12 (one FAT at LBA 10, the root at LBA 19, cluster n at LBA 31 + n, one sector per
/// cluster; testdata/machines/sprinter/README.md): put `data` into the root as `name11` ("NAME    EXT"),
/// replacing the file of that name
bool PutRootFile(std::vector<uint8_t>& disk, const std::string& name11, const std::vector<uint8_t>& data)
{
    uint8_t* fat = disk.data() + 10 * 512;
    const auto get = [fat](uint32_t n) -> uint32_t {
        const uint32_t pair = fat[n * 3 / 2] | fat[n * 3 / 2 + 1] << 8;
        return (n & 1) ? pair >> 4 : pair & 0xFFF;
    };
    const auto set = [fat](uint32_t n, uint32_t v) {
        uint8_t* p = fat + n * 3 / 2;
        if (n & 1)
        {
            p[0] = static_cast<uint8_t>((p[0] & 0x0F) | (v << 4));
            p[1] = static_cast<uint8_t>(v >> 4);
        }
        else
        {
            p[0] = static_cast<uint8_t>(v);
            p[1] = static_cast<uint8_t>((p[1] & 0xF0) | (v >> 8));
        }
    };
    constexpr uint32_t kClusters = 2880 - 33 + 2;

    uint8_t* entry = nullptr;
    for (uint32_t i = 0; i < 224 && !entry; i++)
    {
        uint8_t* e = disk.data() + 19 * 512 + i * 32;
        if (std::memcmp(e, name11.data(), 11) == 0)
        {
            for (uint32_t c = e[26] | e[27] << 8; c >= 2 && c < 0xFF8;)
            {
                const uint32_t next = get(c);
                set(c, 0);
                c = next;
            }
            entry = e;
        }
    }
    for (uint32_t i = 0; i < 224 && !entry; i++)
    {
        uint8_t* e = disk.data() + 19 * 512 + i * 32;
        if (e[0] == 0x00 || e[0] == 0xE5)
            entry = e;
    }
    if (!entry)
        return false;

    uint32_t first = 0, previous = 0;
    for (size_t at = 0; at < data.size(); at += 512)
    {
        uint32_t c = 2;
        while (c < kClusters && get(c) != 0)
            c++;
        if (c >= kClusters)
            return false;
        set(c, 0xFFF);
        if (previous)
            set(previous, c);
        else
            first = c;
        previous = c;
        std::memcpy(disk.data() + (31 + c) * 512, data.data() + at, std::min<size_t>(512, data.size() - at));
    }
    std::memset(entry, 0, 32);
    std::memcpy(entry, name11.data(), 11);
    entry[11] = 0x20;  // archive
    entry[26] = static_cast<uint8_t>(first);
    entry[27] = static_cast<uint8_t>(first >> 8);
    for (int b = 0; b < 4; b++)
        entry[28 + b] = static_cast<uint8_t>(data.size() >> (8 * b));
    return true;
}
}  // namespace

// ACC-9 (part): the accelerator test program of the HDD system disk (TESTS\ACCTEST.EXE, testdata/machines/sprinter/
// software) copies a 64 x 64 picture into the graphics screen one row at a time: LD D,D : LD A,#40 (length 64)
// : LD L,L : LD A,(HL) (64 bytes into the buffer) : LD (DE),A (64 bytes into row PORT_Y) : LD B,B. The video RAM
// rows 0-63, columns 0-63 then hold the file's picture (load address #8100, picture at #8300).
// Boot-bound (BIOS, DSS from the floppy, the program): ~3 s host time with the turbo mode
TEST_F(SprinterBoot_Test, Dss162_AccTestCopiesItsPictureWithTheAccelerator)
{
    const std::string image = TestPathHelper::GetTestDataPath("machines/sprinter/dss_1_62_92.img");
    const std::string program = TestPathHelper::GetTestDataPath("machines/sprinter/software/acctest.exe");
    if (!FileHelper::FileExists(image) || !FileHelper::FileExists(program))
        GTEST_SKIP() << "the DSS floppy or ACCTEST.EXE is missing";

    std::vector<uint8_t> disk = ReadAll(image);
    const std::vector<uint8_t> exe = ReadAll(program);
    ASSERT_EQ(disk.size(), 1474560u);
    ASSERT_EQ(exe.size(), 4632u);
    const std::string bat = "@echo off\r\nacctest\r\n";
    ASSERT_TRUE(PutRootFile(disk, "ACCTEST EXE", exe));
    ASSERT_TRUE(PutRootFile(disk, "SYSTEM  BAT", std::vector<uint8_t>(bat.begin(), bat.end())));
    const std::string copy = TestPathHelper::GetUniqueTestScratchPath("dss-acctest.img");
    ASSERT_TRUE(FileHelper::SaveBufferToFile(copy, disk.data(), disk.size()));
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(copy, 1, &error)) << error;

    // The picture: file offset #16 is load address #8100, so #8300 is offset #216
    constexpr size_t kPicture = 0x216;
    const SprinterVideoRam& vram = _decoder->GetVideoRam();
    const auto mismatches = [&]() {
        int count = 0;
        for (uint32_t y = 0; y < 64; y++)
            for (uint32_t x = 0; x < 64; x++)
                count += vram.Read(y * 1024 + x) != exe[kPicture + y * 64 + x];
        return count;
    };

    SkipIdeDetection();
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return mismatches() == 0; }, 3000, 5);
    EXPECT_EQ(mismatches(), 0) << StringHelper::Format("PC=%04X frame=%llu", _context->pCore->GetZ80()->pc,
                                                       static_cast<unsigned long long>(_context->emulatorState.frame_counter));
    const SprinterAccelerator* accelerator = _decoder->GetAccelerator();
    ASSERT_NE(accelerator, nullptr);
    EXPECT_GE(accelerator->State().operations, 128u) << "64 block reads and 64 block writes";
    EXPECT_EQ(accelerator->State().length, 0x40);
    EXPECT_EQ(vram.Read(64 * 1024), 0x00) << "row 64 is not drawn";

    // The screen against MAME's (ScreenSprinter's render; reviewed against MAME's capture, s5-accelerator-outcome.md)
    ExpectScreenMatchesGolden("acctest.png");
}

// Flex Navigator (SYSTEM.BAT's "fn") clears its graphics screen with the accelerator (#86CB: OUT (#A2),#50 puts the
// video RAM into window 1, then LD D,D : LD A,0 sets the length 256 and every column of 320 runs LD E,E : LD (HL),E
// : LD B,B: a vertical fill of 256 rows), then draws its panels: the text glyphs through the accelerator's copies.
// The floppy RESTORE of the BIOS RESETD is too slow for its poll loop at 21 MHz (roadmap §8, the open wait-state
// question): until that is fixed the test puts the head on track 20 when RESETD starts, so the RESTORE ends in time.
// Boot-bound (BIOS, DSS, Flex Navigator loading from the floppy): ~6 s host time with the turbo mode
TEST_F(SprinterBoot_Test, Dss162_FlexNavigatorDrawsWithTheAccelerator)
{
    const std::string image = TestPathHelper::GetTestDataPath("machines/sprinter/dss_1_62_92.img");
    if (!FileHelper::FileExists(image))
        GTEST_SKIP() << "testdata/machines/sprinter/dss_1_62_92.img is missing";
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(image, 1, &error)) << error;

    Z80* z80 = _context->pCore->GetZ80();
    z80->busTraceHook = [this, z80](char type, uint16_t addr, uint8_t) {
        // ROM page 0 RESETD (#0609) fetched: the head from FN's last read (track 71) to track 20
        FDD* drive = _context->pBetaDisk->getDrive();
        if (type == 'R' && addr == 0x0609 && (z80->pc == 0x0609 || z80->pc == 0x060A) && drive && drive->getTrack() > 20)
            drive->setTrack(20);
    };

    const SprinterVideoRam& vram = _decoder->GetVideoRam();
    const auto cleared = [&]() {
        const uint8_t v = vram.Read(0);
        for (uint32_t y = 0; y < 256; y++)
            for (uint32_t x = 0; x < 320; x++)
                if (vram.Read(y * 1024 + x) != v)
                    return false;
        return true;
    };

    SkipIdeDetection();
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("B:\\>"); }, 3000, 5);
    EmulatorTestHelper::RunUntil(_emulator.get(), cleared, 1000, 1);
    ASSERT_TRUE(cleared()) << "320 columns x 256 rows of one value";
    EXPECT_EQ(_decoder->GetAccelerator()->State().operations, 320u) << "one vertical fill per column";

    // The panels with the file list of B:\FN (the RTC is fixed: the clock shows the same time every run)
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 600);
    z80->busTraceHook = nullptr;
    ExpectScreenMatchesGolden("fn-panels.png");
}

/// endregion </The accelerator in real software>

/// region <Flex Navigator input>

// Flex Navigator reads its keys through DSS (WAITKEY / SCANKEY: DSS's KEYSCAN reads the set 2 codes from SIO A in the
// frame INT; Tab = #0D -> position #0F) and its mouse through the DSS mouse driver (INTMOUSE: DSS 1.62.9x reads the
// PLD's Kempston view #FADF / #FBDF / #FFDF, DSS 1.71 the Microsoft serial packets on SIO B). These tests drive both
// through the automation (the same journaled path the GUI posts to) and read the active panel from the picture: the
// file cursor bar on the first row of the active panel.
class SprinterFlexNavigator_Test : public SprinterBoot_Test
{
protected:
    /// The picture rendered in full (two frames without the turbo mode), one pixel of it (0x00RRGGBB)
    uint32_t Pixel(uint32_t x, uint32_t y)
    {
        _emulator->DisableTurboMode();
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 2);
        _emulator->EnableTurboMode();
        const FramebufferDescriptor& fb = _context->pScreen->GetFramebufferDescriptor();
        const uint32_t* pixels = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
        const uint32_t p = pixels[y * fb.width + x];
        return (p & 0xFF) << 16 | (p & 0xFF00) | (p >> 16 & 0xFF);
    }

    /// Which panel shows the cursor bar on row `y` (the left list at x = 100, the right one at x = 420): 'L', 'R', '?'
    char ActivePanel(uint32_t barColor, uint32_t y = 82)
    {
        const bool left = Pixel(100, y) == barColor;
        const bool right = Pixel(420, y) == barColor;
        return left == right ? '?' : (left ? 'L' : 'R');
    }

    /// A key through the automation keyboard, then `frames` for Flex Navigator to act on it
    void Tap(const std::string& key, uint16_t frames = 20)
    {
        _emulator->GetDebugManager()->GetKeyboardManager()->TapKey(key);
        EmulatorTestHelper::RunFramesFast(_emulator.get(), frames);
    }

    /// The automation mouse (what the WebAPI / MCP / CLI / Lua / Python mouse surfaces call): its input goes
    /// through the emulator's MouseManager, journaled, to the board mouse
    DebugMouseManager* Mouse() { return _emulator->GetDebugManager()->GetMouseManager(); }

    /// `steps` moves of (dx, dy) (Kempston units: + right, + up), `frames` after each for the DSS driver to follow
    void MoveMouse(int dx, int dy, int steps, uint16_t frames = 3)
    {
        for (int i = 0; i < steps && (dx || dy); i++)
        {
            ASSERT_TRUE(Mouse()->Move(dx, dy).ok());
            EmulatorTestHelper::RunFramesFast(_emulator.get(), frames);
        }
    }

    /// `button` held for `frames`, released, then `settle` frames
    void ClickMouse(MouseButton button, uint16_t frames, uint16_t settle)
    {
        ASSERT_TRUE(Mouse()->PressButton(button).ok());
        EmulatorTestHelper::RunFramesFast(_emulator.get(), frames);
        ASSERT_TRUE(Mouse()->ReleaseButton(button).ok());
        EmulatorTestHelper::RunFramesFast(_emulator.get(), settle);
    }

    /// The whole picture rendered in full (two frames without the turbo mode)
    std::vector<uint32_t> Picture()
    {
        _emulator->DisableTurboMode();
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 2);
        _emulator->EnableTurboMode();
        const FramebufferDescriptor& fb = _context->pScreen->GetFramebufferDescriptor();
        const uint32_t* pixels = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
        return std::vector<uint32_t>(pixels, pixels + static_cast<size_t>(fb.width) * fb.height);
    }

    /// The box around the pixels two pictures disagree on (empty: right < left)
    struct Box
    {
        int left = 1 << 30;
        int top = 1 << 30;
        int right = -1;
        int bottom = -1;
        bool Empty() const { return right < left; }
    };
    Box Changed(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, int fromX = 0, int toX = 1 << 30)
    {
        const int width = static_cast<int>(_context->pScreen->GetFramebufferDescriptor().width);
        Box box;
        for (size_t i = 0; i < a.size() && i < b.size(); i++)
        {
            const int x = static_cast<int>(i % width);
            const int y = static_cast<int>(i / width);
            if (a[i] == b[i] || x < fromX || x >= toX)
                continue;
            box.left = std::min(box.left, x);
            box.right = std::max(box.right, x);
            box.top = std::min(box.top, y);
            box.bottom = std::max(box.bottom, y);
        }
        return box;
    }

    static constexpr uint32_t kFn115Bar = 0x00FFFF;  // FN 1.15: a cyan bar on blue panels
    struct Point
    {
        int x;
        int y;
    };
    /// FN 1.15's drive icons in the 640x256 picture (the drive bar above the left panel)
    static constexpr Point kDriveIconC = {74, 35};
    static constexpr Point kDriveIconD = {98, 35};

    /// The picture as a PNG in $UNREAL_MOUSE_DUMP_DIR (diagnostics; nothing without the variable)
    void DumpPicture(const std::string& name)
    {
        const char* dir = std::getenv("UNREAL_MOUSE_DUMP_DIR");
        if (!dir)
            return;
        const FramebufferDescriptor& fb = _context->pScreen->GetFramebufferDescriptor();
        const std::string file = std::string(dir) + "/" + name + ".png";
        lodepng::encode(file, fb.memoryBuffer, fb.width, fb.height);
    }

    /// Home the pointer (a glide far up and left: FN clamps it at the corner), glide to (x, y) of the picture
    /// (FN 1.15 in the 640-pixel mode: one count per pixel) and click there with the left button
    void ClickAt(int x, int y, bool click = true)
    {
        ASSERT_TRUE(Mouse()->Glide(-1000, 1000).ok());
        ASSERT_TRUE(Mouse()->Glide(x, -y).ok());
        if (click)
            ASSERT_TRUE(Mouse()->Click(MouseButton::Left, 5).ok());
        for (int i = 0; i < 100 && (Mouse()->IsBusy() || Mouse()->IsClickPending()); i++)
            EmulatorTestHelper::RunFramesFast(_emulator.get(), 1);
        ASSERT_FALSE(Mouse()->IsBusy()) << "the glide did not end";
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 60);
    }

    /// The panel list's row `row` (8 pixels each from y = 79), its middle line
    static constexpr int RowY(int row) { return 79 + 8 * row + 3; }

    /// The mouse in Flex Navigator, through the automation (WebAPI / MCP mouse surfaces), the panels as in
    /// the tests below (the left one active, the pointer where FN put it). `bar`: the cursor bar's color,
    /// `fileRow`: a row of the right panel's list that holds a file (FN marks files, not directories).
    /// 1. The pointer follows the mouse: 40 units right moves it 40 pixels right, 40 units down 40 pixels down
    ///    (Kempston Y grows upward; both DSS drivers scale 1:1 in the 640-pixel mode);
    /// 2. into the right panel's list, where the right button (D1) and the middle one (D2) do not activate the
    ///    panel - only the left one (D0) does;
    /// 3. a left click on a row puts the cursor bar there; a right click marks the file and moves the bar down
    void ExerciseMouse(uint32_t bar, int fileRow, uint16_t settle)
    {
        // 1. The pointer
        std::vector<uint32_t> before = Picture();
        ASSERT_NO_FATAL_FAILURE(MoveMouse(8, 0, 5));
        std::vector<uint32_t> after = Picture();
        Box box = Changed(before, after);
        ASSERT_FALSE(box.Empty()) << "the pointer did not move";
        const int pointerWidth = box.right - box.left + 1 - 40;
        const int pointerHeight = box.bottom - box.top + 1;
        EXPECT_GE(pointerWidth, 6) << "40 pixels right: the old and the new pointer side by side";
        EXPECT_LE(pointerWidth, 24);
        EXPECT_LE(pointerHeight, 20) << "a move to the right does not move the pointer vertically";

        before = std::move(after);
        ASSERT_NO_FATAL_FAILURE(MoveMouse(0, -8, 5));
        after = Picture();
        box = Changed(before, after);
        ASSERT_FALSE(box.Empty()) << "the pointer did not move down";
        EXPECT_NEAR(box.bottom - box.top + 1, pointerHeight + 40, 2) << "40 pixels down";
        EXPECT_LE(box.right - box.left + 1, pointerWidth + 2) << "a move down does not move the pointer sideways";
        int pointerX = box.right - pointerWidth + 1;
        int pointerY = box.bottom - pointerHeight + 1;  // the hot spot: the arrow's tip, its top left

        // 2. Into the right panel's list, two rows below the file, on the name column (FN 1.15 selects a row only
        //    there; the pointer's tip at x = 400 keeps the arrow off the pixels ActivePanel reads at x = 420);
        //    D1 and D2 do not activate the panel, D0 does
        const int dx = 400 - pointerX;
        const int dy = RowY(fileRow + 2) - pointerY;
        ASSERT_NO_FATAL_FAILURE(MoveMouse(dx / 20, -dy / 20, 20));
        ASSERT_NO_FATAL_FAILURE(MoveMouse(dx % 20, -(dy % 20), 1));
        pointerX += dx;
        pointerY += dy;
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 10);
        ASSERT_EQ(ActivePanel(bar), 'L') << "moving does not click";
        ASSERT_NO_FATAL_FAILURE(ClickMouse(MouseButton::Right, 10, settle));
        EXPECT_EQ(ActivePanel(bar), 'L') << "the right button (D1) is not the left one";
        before = Picture();
        ASSERT_NO_FATAL_FAILURE(ClickMouse(MouseButton::Middle, 10, settle));
        EXPECT_TRUE(Changed(before, Picture()).Empty()) << "FN does nothing with the middle button (D2)";
        ASSERT_NO_FATAL_FAILURE(ClickMouse(MouseButton::Left, 10, settle));
        EXPECT_EQ(ActivePanel(bar), 'R') << "the left button (D0) activates the right panel";

        // 3. A left click on the file row selects it; a right click marks it and moves the bar down
        ASSERT_NO_FATAL_FAILURE(MoveMouse(0, 16, 1));
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 5);
        ASSERT_NO_FATAL_FAILURE(ClickMouse(MouseButton::Left, 10, settle));
        // Read at x = 432, the 8th character of the name (a space for these names): the right panel only, the left
        // panel's names are in the bar's color in FN 1.15, and FN 1.15's bar covers the name and extension only
        EXPECT_EQ(Pixel(432, RowY(fileRow)), bar) << "the left click put the bar on the row under the pointer";
        ASSERT_NO_FATAL_FAILURE(ClickMouse(MouseButton::Right, 3, settle));
        EXPECT_NE(Pixel(432, RowY(fileRow)), bar) << "the right click moved the bar off the file it marked";
        bool below = false;
        for (int row = fileRow + 1; row <= fileRow + 3 && !below; row++)
            below = Pixel(432, RowY(row)) == bar;
        EXPECT_TRUE(below) << "the bar is on a row below";
    }
};

// FN 1.10 from the DSS 1.62.92 floppy (the S5 RESTORE workaround of Dss162_FlexNavigatorDrawsWithTheAccelerator):
// Tab switches the panels and back, Down moves the cursor; the mouse through the automation (with no Kempston
// interface configured: the board's mouse) moves the pointer, the buttons map D0 left / D1 right / D2 middle, a
// left click activates the right panel and selects a row, a right click marks a file (ExerciseMouse).
// Boot-bound (BIOS, DSS, Flex Navigator loading from the floppy, its disk reads per panel switch): ~9 s host time
// with the turbo mode
TEST_F(SprinterFlexNavigator_Test, Dss162_Fn110KeysAndMouse)
{
    const std::string image = TestPathHelper::GetTestDataPath("machines/sprinter/dss_1_62_92.img");
    if (!FileHelper::FileExists(image))
        GTEST_SKIP() << "testdata/machines/sprinter/dss_1_62_92.img is missing";
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(image, 1, &error)) << error;
    _context->pMouse->SetPresent(false);  // [INPUT] Mouse=NONE: the Sprinter's mouse is still there

    Z80* z80 = _context->pCore->GetZ80();
    z80->busTraceHook = [this, z80](char type, uint16_t addr, uint8_t) {
        FDD* drive = _context->pBetaDisk->getDrive();
        if (type == 'R' && addr == 0x0609 && (z80->pc == 0x0609 || z80->pc == 0x060A) && drive && drive->getTrack() > 20)
            drive->setTrack(20);
    };
    SkipIdeDetection();
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("B:\\>"); }, 3000, 5);
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 600);

    // A panel switch reads the floppy (~30 frames with interrupts off, SIO A unread): the next key waits for it
    constexpr uint16_t kSettle = 60;
    constexpr uint32_t kBar = 0x000000;  // FN 1.10: a black bar on grey panels
    ASSERT_EQ(ActivePanel(kBar), 'L') << "FN starts on the left panel";
    Tap("tab", kSettle);
    EXPECT_EQ(ActivePanel(kBar), 'R') << "Tab: the right panel";
    Tap("tab", kSettle);
    EXPECT_EQ(ActivePanel(kBar), 'L') << "Tab again: the left panel";
    Tap("down", kSettle);
    EXPECT_EQ(ActivePanel(kBar, 90), 'L') << "Down: the bar on the second row";
    EXPECT_NE(Pixel(100, 82), kBar) << "Down: the first row is no longer marked";
    Tap("up", kSettle);
    EXPECT_EQ(_decoder->GetInput().KeyboardOverruns(), 0u);

    // The mouse (no Kempston interface: the board mouse, the PLD's Kempston view DSS 1.62.92 reads); fn ini is row 4
    ExerciseMouse(kBar, 4, kSettle);
    z80->busTraceHook = nullptr;
    _context->pMouse->SetPresent(true);
}

// FN 1.15 on the owner's DSS 1.71 system disk (UNREAL_SPRINTER_HDD, the raw sp_hdd_sys.img; not in the repo): Tab
// switches the panels, and the serial mouse (DSS 1.71 reads SIO B) moves the pointer, activates the right panel,
// selects and marks a file (ExerciseMouse). Boot-bound (BIOS 3.06, DSS 1.71 and FN from the hard disk): ~8 s host time with the turbo mode
TEST_F(SprinterFlexNavigator_Test, RealHdd_Fn115KeysAndMouse)
{
    const char* path = std::getenv("UNREAL_SPRINTER_HDD");
    if (!path || !FileHelper::FileExists(path))
        GTEST_SKIP() << "UNREAL_SPRINTER_HDD (the raw sp_hdd_sys.img) not set";
    if (!UseBios("sp2k-3.06-hf2.rom"))
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.06-hf2.rom not found";
    InsertHdd(path);
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Shell version"); }, 1200, 1);
    ASSERT_TRUE(ScreenHas("Shell version")) << ScreenText();
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 1500);

    constexpr uint32_t kBar = 0x00FFFF;  // FN 1.15: a cyan bar on blue panels
    ASSERT_EQ(ActivePanel(kBar), 'L') << "FN starts on the left panel";
    Tap("tab");
    EXPECT_EQ(ActivePanel(kBar), 'R') << "Tab: the right panel";
    Tap("tab");
    EXPECT_EQ(ActivePanel(kBar), 'L') << "Tab again: the left panel";

    // The mouse: the serial packets on SIO B DSS 1.71 reads; "system bat" is row 14 of the root of drive C
    ExerciseMouse(kBar, 14, 30);
}

// FN 1.15 on the owner's DSS 1.71 system disk (UNREAL_SPRINTER_HDD; not in the repo): the drive icon "D" clicked
// through the automation mouse (DebugMouseManager: what the WebAPI / MCP / CLI / Lua / Python mouse surfaces call)
// switches the left panel to drive D; the "C" icon brings drive C back, the same picture as before. The pointer is
// homed first: a glide far up and left, which FN clamps at the screen's corner, then a glide to the icon - the
// pattern of the mouse recipe (.recipe/input/mouse.md). Boot-bound (BIOS 3.06, DSS 1.71, FN from the hard disk):
// ~8 s host time with the turbo mode
TEST_F(SprinterFlexNavigator_Test, RealHdd_Fn115ClickTheDriveIcon)
{
    const char* path = std::getenv("UNREAL_SPRINTER_HDD");
    if (!path || !FileHelper::FileExists(path))
        GTEST_SKIP() << "UNREAL_SPRINTER_HDD (the raw sp_hdd_sys.img) not set";
    // Drive D: the pack's data disk on the slave ($UNREAL_SPRINTER_HDD_MEDIA, or sp_hdd_media.img next to the system disk)
    const char* mediaEnv = std::getenv("UNREAL_SPRINTER_HDD_MEDIA");
    const std::string media =
        mediaEnv ? std::string(mediaEnv) : (std::filesystem::path(path).parent_path() / "sp_hdd_media.img").string();
    if (!FileHelper::FileExists(media))
        GTEST_SKIP() << "the data disk for drive D (UNREAL_SPRINTER_HDD_MEDIA or sp_hdd_media.img) not found";
    if (!UseBios("sp2k-3.06-hf2.rom"))
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.06-hf2.rom not found";
    InsertHdd(path);
    InsertHdd(media, "ide0.slave");
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Shell version"); }, 1200, 1);
    ASSERT_TRUE(ScreenHas("Shell version")) << ScreenText();
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 1500);
    ASSERT_EQ(ActivePanel(kFn115Bar), 'L') << "FN starts on the left panel";

    const MouseStateSnapshot status = Mouse()->GetState();
    ASSERT_TRUE(status.device.has_value());
    EXPECT_EQ(status.device->id, "sprinter");
    EXPECT_TRUE(status.device->inUse) << "DSS 1.71 polls SIO B";
    EXPECT_TRUE(status.device->serial.receiverInTune) << "DSS 1.71 clocks SIO B at ~1 200 baud";

    // The pointer onto the drive bar first (no click), so the reference picture has it off the list
    ClickAt(kDriveIconC.x, kDriveIconC.y, false);
    const std::vector<uint32_t> driveC = Picture();
    DumpPicture("fn115-drive-c");
    ClickAt(kDriveIconD.x, kDriveIconD.y);
    const std::vector<uint32_t> driveD = Picture();
    DumpPicture("fn115-drive-d");
    const Box changed = Changed(driveC, driveD, 0, 360);
    EXPECT_FALSE(changed.Empty()) << "the left panel did not change";
    EXPECT_GT(changed.bottom - changed.top, 100) << "the whole list changed, not just the pointer";
    EXPECT_EQ(ActivePanel(kFn115Bar), 'L') << "still the left panel";

    ClickAt(kDriveIconC.x, kDriveIconC.y);
    const std::vector<uint32_t> driveCAgain = Picture();
    DumpPicture("fn115-drive-c-again");
    // The left panel's path line and list (picture rows 64-230; the pointer stays on the drive bar above)
    const uint32_t width = _context->pScreen->GetFramebufferDescriptor().width;
    const auto listRows = [width](const std::vector<uint32_t>& picture) {
        return std::vector<uint32_t>(picture.begin() + 64 * width, picture.begin() + 230 * width);
    };
    EXPECT_TRUE(Changed(listRows(driveC), listRows(driveCAgain), 0, 360).Empty()) << "drive C back: the list as before";
    EXPECT_FALSE(Changed(listRows(driveD), listRows(driveCAgain), 0, 360).Empty());
}

/// endregion </Flex Navigator input>

/// region <CTC playback tick (Bad Apple, dontBlink)>

// Demos on the owner's DSS 1.71 system disk (UNREAL_SPRINTER_HDD, the raw sp_hdd_sys.img of the MAME pack; not in the
// repo) that pace themselves with the Z84C15's CTC: channel 2 counts the 875 kHz TRG2 by 112, its ZC/TO2 drives TRG3,
// channel 3 counts by 160 and interrupts at 48.83 Hz with vector #06 (IM2, a table with only that entry). The main
// loop waits at EI / HALT / LD A,#00 / AND A / JR Z for the handler to patch the LD's operand: before the counter
// mode had its inputs the tick never came and the demos hung on a black screen after their logo.
class SprinterCtcDemo_Test : public SprinterFlexNavigator_Test
{
protected:
    /// A command typed into Flex Navigator's command line, then Enter
    void Command(const std::string& text)
    {
        for (char c : text)
        {
            std::string key;
            if (c == ' ')
                key = "space";
            else if (c == '\\')
                key = "backslash";
            else if (c == '.')
                key = "period";
            else
                key = std::string(1, c);
            Tap(key, 3);
        }
        Tap("enter", 20);
    }

    /// The left panel: the directory on row `first` of the root (BIN, C, DEMOS ...), then the one on row `second`
    /// of it (".." is row 0); Flex Navigator's current directory is the program's
    void OpenDirectory(int first, int second)
    {
        for (int i = 0; i < first; i++)
            Tap("down", 3);
        Tap("enter", 60);
        for (int i = 0; i < second; i++)
            Tap("down", 3);
        Tap("enter", 60);
    }

    struct Playback
    {
        uint64_t ticks = 0;       ///< CTC channel 3 zero counts turned into interrupt requests
        uint64_t frames = 0;
        int pictures = 0;         ///< distinct pictures among the samples
        uint32_t ringWrites = 0;  ///< Covox-Blaster ring words written
        uint32_t cblTicks = 0;    ///< Covox-Blaster play ticks
    };

    /// `seconds` of the demo, a picture every half second
    Playback Watch(int seconds)
    {
        Playback p;
        const Z84Lib::Z84Ctc& ctc = _decoder->GetZ84().ctc;
        const CovoxBlasterState& cbl = _decoder->GetCovoxBlaster().State();
        const uint64_t seen = ctc.GetChannel(3).zeroSeen;
        const uint64_t frame = Frame();
        const uint32_t writes = cbl.ringWrites;
        const uint32_t ticks = cbl.ticks;
        std::vector<std::vector<uint32_t>> pictures;
        for (int i = 0; i < seconds * 2; i++)
        {
            EmulatorTestHelper::RunFramesFast(_emulator.get(), 24);
            std::vector<uint32_t> picture = Picture();
            if (std::find(pictures.begin(), pictures.end(), picture) == pictures.end())
                pictures.push_back(std::move(picture));
        }
        p.ticks = ctc.GetChannel(3).zeroSeen - seen;
        p.frames = Frame() - frame;
        p.pictures = static_cast<int>(pictures.size());
        p.ringWrites = cbl.ringWrites - writes;
        p.cblTicks = cbl.ticks - ticks;
        return p;
    }

    bool BootDss171()
    {
        const char* path = std::getenv("UNREAL_SPRINTER_HDD");
        if (!path || !FileHelper::FileExists(path))
            return false;
        if (!UseBios("sp2k-3.06-hf2.rom"))
            return false;
        InsertHdd(path);
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Shell version"); }, 1200, 1);
        EXPECT_TRUE(ScreenHas("Shell version")) << ScreenText();
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 1500);  // Flex Navigator up
        return true;
    }

    void Report(const char* name, const Playback& p)
    {
        std::printf("[ CTC demo ] %-10s %4llu ticks in %4llu frames, %3d pictures, CBL %u ring writes, %u play ticks\n",
                    name, static_cast<unsigned long long>(p.ticks), static_cast<unsigned long long>(p.frames), p.pictures,
                    p.ringWrites, p.cblTicks);
        RecordProperty(std::string(name) + "_ticks", std::to_string(p.ticks));
        RecordProperty(std::string(name) + "_pictures", std::to_string(p.pictures));
    }
};

// Bad Apple (C:\DEMOS\BADAPPLE): its logo, then the video in 1-bit frames with the Covox-Blaster. One CTC tick per
// 20.48 ms frame. Boot-bound (BIOS 3.06, DSS 1.71, Flex Navigator, the demo from the hard disk): ~6 s host time
TEST_F(SprinterCtcDemo_Test, RealHdd_BadApplePlays)
{
    if (!BootDss171())
        GTEST_SKIP() << "UNREAL_SPRINTER_HDD (the raw sp_hdd_sys.img) or BIOS 3.06 not available";
    OpenDirectory(2, 1);  // C:\DEMOS, then BADAPPLE
    Command("badapple.exe");
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 500);  // the logo, then the playback
    SaveScreen(TestPathHelper::GetUniqueTestScratchPath("badapple.png"));
    const Playback p = Watch(10);
    Report("badapple", p);
    const Z84Lib::Z84Ctc& ctc = _decoder->GetZ84().ctc;
    EXPECT_EQ(ctc.GetChannel(3).control & 0xC0, 0xC0) << "channel 3: interrupt, counter";
    EXPECT_NEAR(ctc.OutputHz(3), 48.828125, 1e-6);
    EXPECT_NEAR(static_cast<double>(p.ticks), static_cast<double>(p.frames), 2.0) << "48.83 Hz: one tick per 20.48 ms frame";
    EXPECT_GE(p.pictures, 10) << "video frames";
    EXPECT_GT(p.ringWrites, 0u) << "the Covox-Blaster is fed";
    EXPECT_GT(p.cblTicks, 0u) << "and plays";
    EXPECT_GE(p.ringWrites, p.cblTicks) << "fed as fast as it plays (stereo: two words a tick): no gap";
}

// deMarche's dontBlink (C:\DEMOS\DNTBLINK, DSS 1.70.998+): a progress bar, then the demo; it streams its music from
// the disk into the Covox-Blaster in the CTC handler (I = #3E, table #3E00, vector #06), so the sound plays only
// while the tick comes. Boot-bound like Bad Apple: ~9 s host time
TEST_F(SprinterCtcDemo_Test, RealHdd_DontBlinkPlays)
{
    if (!BootDss171())
        GTEST_SKIP() << "UNREAL_SPRINTER_HDD (the raw sp_hdd_sys.img) or BIOS 3.06 not available";
    OpenDirectory(2, 4);  // C:\DEMOS, then DNTBLINK
    Command("dntblink.exe");
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 1000);  // the progress bar, then the demo
    SaveScreen(TestPathHelper::GetUniqueTestScratchPath("dntblink.png"));
    const Playback p = Watch(10);
    Report("dntblink", p);
    EXPECT_NEAR(static_cast<double>(p.ticks), static_cast<double>(p.frames), 2.0);
    EXPECT_GE(p.pictures, 10);
    EXPECT_GT(p.ringWrites, 0u) << "the music streams from the disk in the CTC handler";
    EXPECT_GT(p.cblTicks, 0u);
    EXPECT_GE(p.ringWrites, p.cblTicks) << "fed as fast as it plays: continuous";
}

/// endregion </CTC playback tick>

/// region <The Game PLD configuration (GAME_00, LDConf)>

// Programs on the MAME pack's system disk (UNREAL_SPRINTER_HDD, the raw sp_hdd_sys.img; not in the repo) that load
// the "Game" PLD bitstream (GAME_00.ACX = LDConf's GC.BIN, full hash #C0FA3055): they copy it into fast RAM with
// "ACEX_30K_LOADING", reload the PLD (code #2E), the ROM loader streams the 473 720 writes from fast RAM, and the
// Game module runs (sprinterpldgame.h). Its cell #EE = #41 makes the BIOS return to the program instead of
// cold-booting (the old symptom: back in Flex Navigator). Boot-bound (BIOS 3.06, DSS 1.71, Flex Navigator, a full
// PLD load): ~10 s host time each
class SprinterGameConfig_Test : public SprinterCtcDemo_Test
{
protected:
    SprinterPldState& Pld() { return _decoder->GetPldState(); }
    std::string Module() const { return _decoder->ActiveModule().Descriptor().name; }
    std::string Selection() const
    {
        std::string key, why;
        _decoder->ModuleSelection(key, why);
        return key;
    }

    /// Frames until the module `name` runs on a configured PLD (false after `maxFrames`)
    bool WaitForModule(const std::string& name, int maxFrames)
    {
        for (int i = 0; i < maxFrames; i += 10)
        {
            if (Pld().configState == SprinterConfigState::Configured && Module() == name)
                return true;
            EmulatorTestHelper::RunFramesFast(_emulator.get(), 10);
        }
        return Pld().configState == SprinterConfigState::Configured && Module() == name;
    }

    /// The panel's file on row `row` of the current folder (".." is row 0), then Enter
    void RunFile(int row)
    {
        for (int i = 0; i < row; i++)
            Tap("down", 3);
        Tap("enter", 20);
    }

    /// The share of pixels (0..1) in which two 736 x 288 pictures differ (colors as 0x00BBGGRR, alpha ignored)
    static double Difference(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b)
    {
        if (a.size() != b.size() || a.empty())
            return 1.0;
        size_t differ = 0;
        for (size_t i = 0; i < a.size(); i++)
            differ += ((a[i] ^ b[i]) & 0x00FFFFFFu) ? 1 : 0;
        return static_cast<double>(differ) / static_cast<double>(a.size());
    }

    /// A MAME capture of testdata/machines/sprinter/reference/game/ as framebuffer colors; empty when missing
    static std::vector<uint32_t> MameCapture(const std::string& name)
    {
        const std::string path =
            (TestPathHelper::FindProjectRoot() / "testdata" / "machines" / "sprinter" / "reference" / "game" / name).string();
        std::vector<unsigned char> rgba;
        unsigned width = 0, height = 0;
        if (lodepng::decode(rgba, width, height, path) != 0 || width != 736 || height != 288)
            return {};
        std::vector<uint32_t> colors(static_cast<size_t>(width) * height);
        for (size_t i = 0; i < colors.size(); i++)
            colors[i] = rgba[i * 4] | (rgba[i * 4 + 1] << 8) | (static_cast<uint32_t>(rgba[i * 4 + 2]) << 16);
        return colors;
    }

    /// Of `frames` consecutive frames (each rendered in full), the smallest difference to `reference`
    double ClosestFrame(const std::vector<uint32_t>& reference, int frames)
    {
        double best = 1.0;
        _emulator->DisableTurboMode();
        for (int i = 0; i < frames; i++)
        {
            EmulatorTestHelper::RunFramesFast(_emulator.get(), 1);
            const FramebufferDescriptor& fb = _context->pScreen->GetFramebufferDescriptor();
            const uint32_t* pixels = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
            best = std::min(best, Difference(reference, std::vector<uint32_t>(pixels, pixels + static_cast<size_t>(fb.width) * fb.height)));
        }
        _emulator->EnableTurboMode();
        return best;
    }
};

// GAME_00.EXE (C:\DEMOS\GAME_00): the Game module by the full hash, the BIOS's return into RELOAD_RET (cell #EE read
// and cleared, the program runs on), the scrolling grid it draws - compared with MAME's picture of the same program
TEST_F(SprinterGameConfig_Test, RealHdd_Game00ReloadsThePld)
{
    if (!BootDss171())
        GTEST_SKIP() << "UNREAL_SPRINTER_HDD (the raw sp_hdd_sys.img) or BIOS 3.06 not available";
    OpenDirectory(2, 9);  // C:\DEMOS, then GAME_00
    RunFile(2);           // .., GAME_00.ACX, GAME_00.EXE
    ASSERT_TRUE(WaitForModule("Game", 600)) << "module " << Module() << ", " << ScreenText();
    EXPECT_EQ(Selection(), "full_hash");
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 150);  // the BIOS returns, the program draws its grid
    EXPECT_EQ(Module(), "Game") << "the program runs on: no cold boot";
    EXPECT_EQ(Pld().Cell(0xEE), 0x00) << "the BIOS read RET_PORT (#41) and cleared it";
    const uint16_t pc = _context->pCore->GetZ80()->pc;
    EXPECT_TRUE(pc >= 0x8100 && pc < 0x8700) << "PC #" << std::hex << pc << ": the program (RELOAD_RET.. #8700)";
    SaveScreen(TestPathHelper::GetUniqueTestScratchPath("game00.png"));
    const std::vector<uint32_t> a = Picture();
    const std::vector<uint32_t> b = Picture();
    EXPECT_GT(Difference(a, b), 0.01) << "the grid scrolls every frame";

    const std::vector<uint32_t> mame = MameCapture("mame-game00-306.png");
    if (mame.empty())
        GTEST_SKIP() << "the MAME capture is missing";
    const double closest = ClosestFrame(mame, 120);
    RecordProperty("game00_closest_to_mame", std::to_string(closest));
    std::printf("[ Game ] GAME_00: closest frame differs from MAME's capture in %.2f%% of the pixels\n", closest * 100);
    EXPECT_LT(closest, 0.05) << "a frame of the scroll equals MAME's capture but for the grid-offset rule differences";
}

// TEST_005.EXE and TEST_010.EXE (the same folder): the same reload, then a landscape scrolled with the per-square
// grid offset. Both reach the Game module by the full hash and run on it (the picture moves, the PC stays in the
// program)
TEST_F(SprinterGameConfig_Test, RealHdd_Test005AndTest010RunOnGame)
{
    if (!BootDss171())
        GTEST_SKIP() << "UNREAL_SPRINTER_HDD (the raw sp_hdd_sys.img) or BIOS 3.06 not available";
    for (int row : {6, 7})  // .., GAME_00.ACX, GAME_00.EXE, PAGE_0.BIN, RELOAD.ASZ, SPRINT00.ASZ, TEST_005, TEST_010
    {
        if (row == 7)
        {
            // Back to Flex Navigator through the RESET button: the PLD loads the ROM's bitstream, Standard again
            _emulator->Reset();
            EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Shell version"); }, 1200, 1);
            EXPECT_EQ(Module(), "Standard");
            EXPECT_EQ(Selection(), "full_hash") << "BIOS 3.06's own Standard build";
            EmulatorTestHelper::RunFramesFast(_emulator.get(), 1500);
            OpenDirectory(2, 9);
        }
        else
            OpenDirectory(2, 9);
        RunFile(row);
        ASSERT_TRUE(WaitForModule("Game", 600)) << "row " << row << ": module " << Module();
        EXPECT_EQ(Selection(), "full_hash");
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 150);
        EXPECT_EQ(Pld().Cell(0xEE), 0x00) << "the BIOS returned into the program";
        const uint16_t pc = _context->pCore->GetZ80()->pc;
        EXPECT_TRUE(pc >= 0x8100 && pc < 0xC000) << "row " << row << ": PC #" << std::hex << pc;
        SaveScreen(TestPathHelper::GetUniqueTestScratchPath(row == 6 ? "test005.png" : "test010.png"));
        const std::vector<uint32_t> a = Picture();
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 20);
        EXPECT_GT(Difference(a, Picture()), 0.001) << "row " << row << ": the picture scrolls";
    }
}

// LDConf's START.BAT (C:\DEMOS\LDCONF, `ldconf c gc.bin e scroll.exe`): LDConf loads GC.BIN (the Game bitstream)
// through the BIOS, runs SCROLL.EXE on it, and after a key loads the ROM's bitstream again - the way back to Standard
// and to DSS
TEST_F(SprinterGameConfig_Test, RealHdd_LdconfStartBatGoesToGameAndBack)
{
    if (!BootDss171())
        GTEST_SKIP() << "UNREAL_SPRINTER_HDD (the raw sp_hdd_sys.img) or BIOS 3.06 not available";
    OpenDirectory(2, 11);  // C:\DEMOS, then LDCONF
    RunFile(8);            // .., 300.BAT, 303.BAT, 304.BAT, 305.BAT, GC.BIN, LDCONF.EXE, SCROLL.EXE, START.BAT
    ASSERT_TRUE(WaitForModule("Game", 800)) << "module " << Module() << ", " << ScreenText();
    EXPECT_EQ(Selection(), "full_hash");
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 150);
    const uint16_t pc = _context->pCore->GetZ80()->pc;
    EXPECT_TRUE(pc >= 0x8100 && pc < 0x8300) << "SCROLL.EXE's frame loop, PC #" << std::hex << pc;
    const std::vector<uint32_t> a = Picture();
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 3);
    EXPECT_GT(Difference(a, Picture()), 0.01) << "SCROLL.EXE scrolls the grid";

    Tap("space", 10);  // SCROLL.EXE checks a key every frame and exits to LDConf, which reloads the ROM's bitstream
    ASSERT_TRUE(WaitForModule("Standard", 800)) << "module " << Module();
    EXPECT_EQ(Selection(), "full_hash") << "BIOS 3.06's own Standard build";
    EXPECT_EQ(_decoder->BeamVideo(), nullptr);
    // Flex Navigator 1.15 idles in a HALT at #A441 (the demo runner's FN_IDLE_PC)
    constexpr uint16_t kFlexNavigatorIdle = 0xA441;
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return _context->pCore->GetZ80()->pc == kFlexNavigatorIdle; }, 1500, 10);
    SaveScreen(TestPathHelper::GetUniqueTestScratchPath("ldconf-back.png"));
    EXPECT_EQ(_context->pCore->GetZ80()->pc, kFlexNavigatorIdle) << "back in Flex Navigator after START.BAT";
    EXPECT_EQ(Module(), "Standard");
}

/// endregion </The Game PLD configuration>
