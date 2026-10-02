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
#include <emulator/ports/models/portdecoder_sprinter.h>
#include <emulator/video/screen.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
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
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "emulator/state/devicestate.h"
#include "emulator/io/storage/chd/chdfile.h"
#include "emulator/io/storage/chd/chdimage.h"
#include "emulator/io/storage/chd/chdwriter.h"
#include "emulator/io/storage/rawimage.h"

namespace
{
/// A file from the DSS 1.62.92 floppy's root (FAT12: one FAT at LBA 10, the root at LBA 19, cluster 2 at
/// LBA 33, one sector per cluster; testdata/machines/sprinter/README.md "Layout"); empty when not found
std::vector<uint8_t> FloppyRootFile(const std::vector<uint8_t>& floppy, const char name[11])
{
    const uint8_t* root = floppy.data() + 19 * 512;
    for (int i = 0; i < 224; i++)
    {
        const uint8_t* e = root + i * 32;
        if (std::memcmp(e, name, 11) != 0)
            continue;
        const uint32_t size = e[28] | e[29] << 8 | e[30] << 16 | static_cast<uint32_t>(e[31]) << 24;
        std::vector<uint8_t> data;
        uint16_t cluster = static_cast<uint16_t>(e[26] | e[27] << 8);
        const uint8_t* fat = floppy.data() + 10 * 512;
        while (cluster >= 2 && cluster < 0xFF8 && data.size() < size)
        {
            const uint8_t* sector = floppy.data() + (33 + cluster - 2) * 512;
            data.insert(data.end(), sector, sector + 512);
            const uint16_t pair = static_cast<uint16_t>(fat[cluster * 3 / 2] | fat[cluster * 3 / 2 + 1] << 8);
            cluster = (cluster & 1) ? static_cast<uint16_t>(pair >> 4) : static_cast<uint16_t>(pair & 0xFFF);
        }
        data.resize(size);
        return data;
    }
    return {};
}

/// A bootable DSS hard disk, built the way DSS's BOOT.EXE leaves one (hardware-reference §9.3,
/// materials.md "A reference HDD image"): 16 MiB, an MBR whose entry 0 is a FAT16 partition (type #06) at
/// LBA 63, the 3-sector DSS loader at LBA 1-3, and a FAT16 volume (4 sectors per cluster, 2 FATs, 512 root
/// entries) holding `files` in its root, in order
struct DssHddFile
{
    const char* name;  ///< 8.3 directory form, 11 characters
    std::vector<uint8_t> data;
};

constexpr uint32_t kDssHddStart = 63, kDssHddFatSize = 32;
constexpr uint32_t kDssHddRootLba = kDssHddStart + 1 + 2 * kDssHddFatSize;  ///< 128

std::vector<uint8_t> BuildDssHdd(const std::vector<uint8_t>& loader, const std::vector<DssHddFile>& files)
{
    constexpr uint32_t kTotal = 32768, kStart = kDssHddStart, kSectors = kTotal - kStart;
    constexpr uint32_t kSpc = 4, kReserved = 1, kFatSize = kDssHddFatSize, kRootEntries = 512;
    constexpr uint32_t kRootLba = kDssHddRootLba, kDataLba = kRootLba + kRootEntries * 32 / 512;
    std::vector<uint8_t> disk(static_cast<size_t>(kTotal) * 512);
    auto put16 = [&](size_t at, uint32_t v) { disk[at] = static_cast<uint8_t>(v); disk[at + 1] = static_cast<uint8_t>(v >> 8); };
    auto put32 = [&](size_t at, uint32_t v) { put16(at, v & 0xFFFF); put16(at + 2, v >> 16); };

    // MBR: entry 0 = active FAT16 (#06) from LBA 63; the DSS loader checks entry 0 only
    disk[446] = 0x80;
    disk[446 + 4] = 0x06;
    put32(446 + 8, kStart);
    put32(446 + 12, kSectors);
    put16(510, 0xAA55);
    std::memcpy(disk.data() + 512, loader.data(), std::min<size_t>(loader.size(), 3 * 512));

    // Partition boot sector with the BPB (DOSBOOT4: "FAT16   " at +#36, media #F8)
    const size_t bs = static_cast<size_t>(kStart) * 512;
    const uint8_t jump[3] = {0xEB, 0x3C, 0x90};
    std::memcpy(&disk[bs], jump, 3);
    std::memcpy(&disk[bs + 3], "DSS 1.62", 8);
    put16(bs + 11, 512);
    disk[bs + 13] = kSpc;
    put16(bs + 14, kReserved);
    disk[bs + 16] = 2;
    put16(bs + 17, kRootEntries);
    put16(bs + 19, kSectors);
    disk[bs + 21] = 0xF8;
    put16(bs + 22, kFatSize);
    put16(bs + 24, 32);  // sectors per track
    put16(bs + 26, 16);  // heads
    put32(bs + 28, kStart);
    disk[bs + 36] = 0x80;
    disk[bs + 38] = 0x29;
    put32(bs + 39, 0x53334200);
    std::memcpy(&disk[bs + 43], "NO NAME    ", 11);
    std::memcpy(&disk[bs + 0x36], "FAT16   ", 8);
    put16(bs + 510, 0xAA55);

    // Files: consecutive clusters from 2, a chain in both FATs, an entry in the root (2026-10-02 12:00)
    std::vector<uint16_t> fat(kFatSize * 256);
    fat[0] = 0xFFF8;
    fat[1] = 0xFFFF;
    uint16_t next = 2;
    for (size_t i = 0; i < files.size(); i++)
    {
        const DssHddFile& file = files[i];
        const uint16_t first = file.data.empty() ? 0 : next;
        const uint32_t clusters = static_cast<uint32_t>((file.data.size() + kSpc * 512 - 1) / (kSpc * 512));
        for (uint32_t c = 0; c < clusters; c++, next++)
            fat[next] = c + 1 < clusters ? static_cast<uint16_t>(next + 1) : 0xFFFF;
        if (!file.data.empty())
            std::memcpy(&disk[(kDataLba + (first - 2) * kSpc) * 512], file.data.data(), file.data.size());
        const size_t e = kRootLba * 512 + i * 32;
        std::memcpy(&disk[e], file.name, 11);
        disk[e + 11] = 0x20;
        put16(e + 22, 12 << 11);
        put16(e + 24, (2026 - 1980) << 9 | 10 << 5 | 2);
        put16(e + 26, first);
        put32(e + 28, static_cast<uint32_t>(file.data.size()));
    }
    for (uint32_t copy = 0; copy < 2; copy++)
    {
        for (size_t i = 0; i < fat.size(); i++)
            put16((kStart + kReserved + copy * kFatSize) * 512 + i * 2, fat[i]);
    }
    return disk;
}
}  // namespace

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
        const std::string path = (TestPathHelper::FindProjectRoot() / "data" / "rom" / "sprinter" / file).string();
        if (!FileHelper::FileExists(path))
            return false;
        CONFIG& config = _context->config;
        std::memset(config.sprinter_rom_path, 0, sizeof(config.sprinter_rom_path));
        std::strncpy(config.sprinter_rom_path, path.c_str(), sizeof(config.sprinter_rom_path) - 1);
        if (!_context->pCore->GetROM()->LoadROM())
            return false;
        _emulator->Reset();
        return true;
    }

    /// F4 at SETUP's "Detecting IDE <unit> ... [Press F4 to skip]" (an empty channel floats: BSY never drops)
    void PressF4At(const std::string& unit, int maxFrames = 2000)
    {
        const std::string waiting = unit + " ... [Press F4";
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas(waiting); }, maxFrames, 1);
        ASSERT_TRUE(ScreenHas(waiting)) << ScreenText();
        for (uint8_t code : {0x0C, 0xF0, 0x0C})
            _decoder->GetZ84().sio.Receive(0, code);
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return !ScreenHas(waiting); }, 300, 1);
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

// The owner's real system disk (the MAME pack's sp_hdd_sys.chd as a raw 1 GiB image, not in the repo; path in
// UNREAL_SPRINTER_HDD): DSS 1.71.57 on BIOS 3.06, the firmware the pack runs it with (BIOS 3.04 loads the DSS loader
// and SYSTEM.DOS, then DSS 1.71 stops in its own start-up with "Fatal error", as it does from the DSS 1.71 floppy).
// Guest writes stay in memory (Session). The empty secondary units float and are skipped with F4; MAME reaches the
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

    PressF4At("Secondary Master ");
    PressF4At("Secondary Slave  ");
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

    PressF4At("Secondary Master ");
    PressF4At("Secondary Slave  ");
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
