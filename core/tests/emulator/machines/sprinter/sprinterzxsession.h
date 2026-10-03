#pragma once

/// @file sprinterzxsession.h
/// @brief A Sprinter running the owner's DSS 1.71 system disk (the MAME pack's sp_hdd_sys.img, path in
/// UNREAL_SPRINTER_HDD; not in the repo) on BIOS 3.06 HF2 (or the fixture's BiosFile), for the ZX-mode tests (tdd-zx-mode.md §8): DSS stops
/// at its prompt (the session copy of SYSTEM.BAT does not start Flex Navigator), commands are typed through the
/// PC keyboard as a user types them, and the Spectrum screen is read back by OCR. Guest writes stay in memory
/// (Session): the image file is never written.

#include <emulator/cpu/core.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/keyboard/keyboard.h>
#include <emulator/media/mediamanager.h>
#include <emulator/media/medium.h>
#include <emulator/memory/memory.h>
#include <emulator/memory/rom.h>
#include <emulator/ports/models/portdecoder_sprinter.h>
#include <emulator/video/screen.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "3rdparty/lodepng/lodepng.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "emulator/io/keyboard/pckey.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/iblockdevice.h"

/// Files inside a FAT16 volume on a block device, rewritten in place (same size, the cluster chain kept): the
/// session layer takes the writes. Enough to change SYSTEM.BAT or swap a TRD for another of the same size
class FatInPlace
{
public:
    explicit FatInPlace(IBlockDevice& device) : _device(device) {}

    bool Open()
    {
        if (!_reader.Open(_device))
            return false;
        uint8_t bs[512];
        if (!_device.ReadSector(_reader.VolumeStart(), bs))
            return false;
        _spc = bs[13];
        const uint32_t reserved = bs[14] | bs[15] << 8;
        const uint32_t fats = bs[16];
        const uint32_t rootEntries = bs[17] | bs[18] << 8;
        const uint32_t fatSize = bs[22] | bs[23] << 8;
        _fatLba = _reader.VolumeStart() + reserved;
        _dataLba = _fatLba + fats * fatSize + (rootEntries * 32 + 511) / 512;
        return _spc != 0;
    }

    /// The entry of `path` ("/DIR/NAME.EXT")
    bool Entry(const std::string& path, FatDirEntryInfo& out)
    {
        const size_t slash = path.find_last_of('/');
        const std::string dir = slash == 0 ? "/" : path.substr(0, slash);
        const std::string name = path.substr(slash + 1);
        std::vector<FatDirEntryInfo> entries;
        if (!_reader.List(dir, entries))
            return false;
        for (const FatDirEntryInfo& e : entries)
        {
            if (EqualNoCase(e.shortName, name) || EqualNoCase(e.name, name))
            {
                out = e;
                return true;
            }
        }
        return false;
    }

    bool Read(const std::string& path, std::vector<uint8_t>& data) { return _reader.ReadFile(path, data); }

    /// `data` over the file's bytes (data.size() <= the file's size; the size in the directory is kept)
    bool Overwrite(const std::string& path, const std::vector<uint8_t>& data)
    {
        FatDirEntryInfo e;
        if (!Entry(path, e) || data.size() > e.size)
            return false;
        uint32_t cluster = e.firstCluster;
        const size_t clusterBytes = static_cast<size_t>(_spc) * 512;
        for (size_t at = 0; at < data.size(); at += clusterBytes)
        {
            if (cluster < 2 || cluster >= 0xFFF8)
                return false;
            for (uint32_t s = 0; s < _spc && at + s * 512 < data.size(); s++)
            {
                uint8_t sector[512];
                const uint64_t lba = _dataLba + static_cast<uint64_t>(cluster - 2) * _spc + s;
                if (!_device.ReadSector(lba, sector))
                    return false;
                const size_t n = std::min<size_t>(512, data.size() - (at + s * 512));
                std::memcpy(sector, data.data() + at + s * 512, n);
                if (!_device.WriteSector(lba, sector))
                    return false;
            }
            cluster = NextCluster(cluster);
        }
        return true;
    }

private:
    static bool EqualNoCase(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); i++)
        {
            if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    }

    uint32_t NextCluster(uint32_t cluster)
    {
        uint8_t sector[512];
        if (!_device.ReadSector(_fatLba + cluster * 2 / 512, sector))
            return 0xFFFF;
        const size_t at = cluster * 2 % 512;
        return static_cast<uint32_t>(sector[at] | sector[at + 1] << 8);
    }

    IBlockDevice& _device;
    FatVolumeReader _reader;
    uint32_t _spc = 0;
    uint64_t _fatLba = 0;
    uint64_t _dataLba = 0;
};

class SprinterZxSession_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;

    void SetUp() override
    {
        const char* path = std::getenv("UNREAL_SPRINTER_HDD");
        if (!path || !FileHelper::FileExists(path))
            GTEST_SKIP() << "UNREAL_SPRINTER_HDD (the raw sp_hdd_sys.img) not set";
        const char* biosOverride = std::getenv("UNREAL_SPRINTER_ZX_BIOS");  // another BIOS 3.06 image (exploration)
        const std::string bios = biosOverride ? std::string(biosOverride)
                                              : (TestPathHelper::FindProjectRoot() / "data" / "rom" / "sprinter" / BiosFile()).string();
        if (!FileHelper::FileExists(bios))
            GTEST_SKIP() << "data/rom/sprinter/" << BiosFile() << " not found";

        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);
        _emulator = _manager->CreateEmulatorWithModelAndRAM("sprinter-zx", "SPRINTER", 4096, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
        _decoder->GetRtc().SetFixedTime(1767268830);  // 2026-01-01 12:00:30 UTC
        _context->config.sprinter.fast_start = std::getenv("UNREAL_SPRINTER_ZX_FULLSTART") ? 0 : 1;

        CONFIG& config = _context->config;
        std::memset(config.sprinter_rom_path, 0, sizeof(config.sprinter_rom_path));
        std::strncpy(config.sprinter_rom_path, bios.c_str(), sizeof(config.sprinter_rom_path) - 1);
        ASSERT_TRUE(_context->pCore->GetROM()->LoadROM());

        MediaSource source;
        source.path = path;
        InsertOptions options;
        options.immediate = true;
        options.access = AccessMode::Session;
        const auto result = _context->pMediaManager->Insert("ide0.master", source, options);
        ASSERT_TRUE(result.Ok()) << result.message;

        // DSS stops at its prompt: the last line of SYSTEM.BAT ("fn", Flex Navigator) becomes blank
        if (KeepFlexNavigator())
        {
            _emulator->Reset();
            _emulator->EnableTurboMode();
            return;
        }
        std::vector<uint8_t> bat;
        ASSERT_TRUE(Disk().Read("/SYSTEM.BAT", bat));
        const std::string text(bat.begin(), bat.end());
        const size_t fn = text.rfind("fn");
        ASSERT_NE(fn, std::string::npos) << text;
        bat[fn] = bat[fn + 1] = ' ';
        ASSERT_TRUE(Disk().Overwrite("/SYSTEM.BAT", bat));

        _emulator->Reset();
        _emulator->EnableTurboMode();
    }

    /// The disk's own SYSTEM.BAT, which starts Flex Navigator (the owner's setup)
    virtual bool KeepFlexNavigator() const { return false; }
    /// The BIOS image in data/rom/sprinter (UNREAL_SPRINTER_ZX_BIOS overrides it with a path)
    virtual const char* BiosFile() const { return "sp2k-3.06-hf2.rom"; }

    void TearDown() override
    {
        _fat.reset();
        _emulator.reset();
        if (_manager)
        {
            for (const auto& id : _manager->GetEmulatorIds())
                _manager->RemoveEmulator(id);
        }
    }

    FatInPlace& Disk()
    {
        if (!_fat)
        {
            Medium* medium = _context->pMediaManager->GetMedium("ide0.master");
            EXPECT_NE(medium, nullptr);
            _fat = std::make_unique<FatInPlace>(*medium->Block());
            EXPECT_TRUE(_fat->Open());
        }
        return *_fat;
    }

    /// The DSS text screen (mode-table rows, both mode pages)
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
    size_t ScreenCount(const std::string& needle)
    {
        const std::string text = ScreenText();
        size_t count = 0;
        for (size_t pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + 1))
            count++;
        return count;
    }
    std::string SpectrumText() { return ScreenOCR::ocrScreen(_emulator->GetId()); }
    bool SpectrumHas(const std::string& text) { return ScreenOCR::containsText(_emulator->GetId(), text); }

    /// The picture as a PNG in the scratch folder (two full frames rendered first); returns the path
    std::string SaveScreen(const std::string& leaf)
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
        const char* keep = std::getenv("UNREAL_SPRINTER_ZX_SHOTS");  // a folder that outlives the test
        const std::string path = keep ? std::string(keep) + "/" + leaf : TestPathHelper::GetUniqueTestScratchPath(leaf);
        lodepng::encode(path, rgba, fb.width, fb.height);
        _emulator->EnableTurboMode();
        return path;
    }

    std::string PldLine()
    {
        const SprinterPldState& p = _decoder->GetPldState();
        const Z80* z80 = _context->pCore->GetZ80();
        char line[200];
        std::snprintf(line, sizeof line, "PC=%04X SP=%04X IM=%u cnf=%02X allMode=%02X pn=%02X sc=%02X dos=%u ramSys=%u romOff=%u turbo=%u frame=%llu",
                      z80->pc, z80->sp, z80->im, p.cnf, p.allMode, p.pn, p.sc, p.dos, p.ramSys, p.romOff, p.turbo,
                      static_cast<unsigned long long>(Frame()));
        return line;
    }

    uint64_t Frame() const { return _context->emulatorState.frame_counter; }
    DebugKeyboardManager* Keys() { return _emulator->GetDebugManager()->GetKeyboardManager(); }

    /// DSS 1.71 at its prompt (BIOS POST, SETUP, the IDE scan, DSS from the hard disk)
    void BootToPrompt()
    {
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Shell version"); }, 2000, 5);
        ASSERT_TRUE(ScreenHas("Shell version")) << ScreenText();
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("C:\\>"); }, 400, 5);
        ASSERT_TRUE(ScreenHas("C:\\>")) << ScreenText();
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 20);
    }

    /// A command line at the DSS prompt, typed through the PC keyboard, then ENTER
    void Dss(const std::string& command, int frames = 0)
    {
        Keys()->TypeText(command + "\n", 2);
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return !Keys()->IsSequenceRunning(); }, 2000, 1);
        if (frames)
            EmulatorTestHelper::RunFramesFast(_emulator.get(), frames);
    }

    /// One key on the Spectrum keyboard (through the same automation keyboard)
    void Zx(const std::string& key, int frames = 6)
    {
        Keys()->TapKey(key, 3);
        EmulatorTestHelper::RunFramesFast(_emulator.get(), frames);
    }

    /// Ctrl + Alt + Del on the PC keyboard (the PLD's CPU reset), as fingers press it
    /// `holdFrames`: how long Delete stays down; Ctrl and Alt are let go after it (a person holds the three keys
    /// for a quarter to half a second: 12-25 frames)
    void CtrlAltDel(int holdFrames = 3)
    {
        Keys()->PressKey("pc.lctrl");
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 2);
        Keys()->PressKey("pc.lalt");
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 2);
        Keys()->PressKey("pc.delete");
        EmulatorTestHelper::RunFramesFast(_emulator.get(), holdFrames);
        Keys()->ReleaseKey("pc.delete");
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 2);
        Keys()->ReleaseKey("pc.lalt");
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 2);
        Keys()->ReleaseKey("pc.lctrl");
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 2);
    }

    /// `spectrum <args>` at the prompt, then the 128 menu is on the Spectrum screen
    /// The PLD shows the Spectrum screen and reads the ZX keyboard (ALL_MODE bit 0 = 0): the launcher's ZX mode
    bool InZxMode() const { return (_decoder->GetPldState().allMode & 0x01) == 0; }

    void Launch(const std::string& args, const std::string& menuText = "128 BASIC")
    {
        Dss("spectrum " + args);
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return InZxMode() && SpectrumHas(menuText); }, 1500, 10);
        ASSERT_TRUE(InZxMode()) << ScreenText();
        ASSERT_TRUE(SpectrumHas(menuText)) << SpectrumText() << ScreenText();
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 20);
    }

private:
    std::unique_ptr<FatInPlace> _fat;
};
