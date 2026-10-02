// Sprinter ZX (Spectrum-compatible) mode on the owner's DSS 1.71 system disk (tdd-zx-mode.md §8, phase Z1;
// UNREAL_SPRINTER_HDD, the MAME pack's sp_hdd_sys.img, not in the repo): the community launcher v2.03
// (C:\ZX\SPECTRUM.EXE) run as a user runs it, the RAM disk TR-DOS 7.03 reads, Ctrl+Alt+Del back to DSS (/ret-fn).

#include "stdafx.h"
#include "pch.h"

#include "sprinterzxsession.h"

#include <cctype>
#include <cstring>

#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/wd1793.h"

class SprinterZxMode_Test : public SprinterZxSession_Test
{
protected:
    /// ENTER on the menu's first entry (TR-DOS 7.03), then a keyword key and ENTER: K = LIST (the catalog),
    /// R = RUN (boot)
    void TrDos(const char* keywordKey, int frames)
    {
        Zx("enter", 100);
        ASSERT_TRUE(SpectrumHas("TR-DOS")) << SpectrumText();
        Zx(keywordKey, 6);
        Zx("enter", frames);
    }
};

namespace
{
/// The "boot" BASIC program's bytes in a TRD or an SCL image (empty when there is none)
std::vector<uint8_t> BootOf(const std::vector<uint8_t>& image, bool scl)
{
    if (scl)
    {
        // "SINCLAIR", file count, 14-byte headers (name 8, type, start 2, length 2, sectors), then the data
        if (image.size() < 9 || std::memcmp(image.data(), "SINCLAIR", 8) != 0)
            return {};
        const uint8_t files = image[8];
        size_t data = 9 + files * 14u;
        for (uint8_t f = 0; f < files; f++)
        {
            const uint8_t* h = &image[9 + f * 14u];
            if (std::memcmp(h, "boot    B", 9) == 0 && data + 256 <= image.size())
                return std::vector<uint8_t>(image.begin() + data, image.begin() + data + (h[11] | h[12] << 8));
            data += h[13] * 256u;
        }
        return {};
    }
    for (size_t e = 0; e + 16 <= 2048 && e + 16 <= image.size(); e += 16)
    {
        const uint8_t* h = &image[e];
        if (std::memcmp(h, "boot    B", 9) == 0)
        {
            const size_t offset = (h[15] * 16u + h[14]) * 256u;
            const size_t length = h[11] | h[12] << 8;
            if (offset + length <= image.size())
                return std::vector<uint8_t>(image.begin() + offset, image.begin() + offset + length);
        }
    }
    return {};
}
}  // namespace

class SprinterZxModeFn_Test : public SprinterZxMode_Test
{
protected:
    bool KeepFlexNavigator() const override { return true; }

    void Tap(const char* key, int frames)
    {
        Keys()->TapKey(key);
        EmulatorTestHelper::RunFramesFast(_emulator.get(), frames);
    }
};

// T-ZX-5 and the owner's report of 2026-10-02 ("on the second try Ctrl+Alt+Del reset into the 128 menu like
// /ret-zx"): /ret-fn brings DSS back every time - from the default mode (21 MHz), from P128.ZX (3.5 MHz) and once
// more. The PLD presets its turbo bit on every CPU reset (DCP.TDF TB_SW.prn = /RESET): DSS comes back at 21 MHz
// after a 3.5 MHz mode too (it used to come back at 3.5 MHz and lose the first key typed). Keys held for 20 frames,
// as fingers do.
// Boot-bound (BIOS, DSS 1.71, three launcher runs and returns): ~3 s host time
TEST_F(SprinterZxMode_Test, RetFn_CtrlAltDelReturnsToDssEveryTime)
{
    ASSERT_NO_FATAL_FAILURE(BootToPrompt());
    Dss("cd \\trd", 20);
    for (const char* mode : {"sp.zx", "p128.zx", "sp.zx"})
    {
        SCOPED_TRACE(mode);
        ASSERT_NO_FATAL_FAILURE(Launch(std::string(mode) + " atarin.trd"));
        EXPECT_EQ(_decoder->GetPldState().turbo, std::string(mode) == "sp.zx" ? 1 : 0);
        CtrlAltDel(20);
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return !InZxMode(); }, 300, 10);
        ASSERT_FALSE(InZxMode()) << "Ctrl+Alt+Del did not leave the Spectrum mode";
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 200);
        EXPECT_FALSE(InZxMode()) << "back in the Spectrum mode (/ret-zx behavior)";
        EXPECT_EQ(_decoder->GetPldState().allMode, 0xFF) << "DSS's screen and keyboard";
        EXPECT_EQ(_decoder->GetPldState().turbo, 1) << "the reset presets the turbo bit";
        EXPECT_TRUE(ScreenHas("C:\\TRD>")) << ScreenText();
    }
}

// T-ZX-3: the launcher reads an image into a RAM disk and TR-DOS 7.03 boots it from drive A (no floppy inserted):
// an SCL (launcher v2.03 unpacks it; VIBRATE!, its "boot" is the third file) and a TRD (KOL0BOK2). RUN loads the
// disk's "boot": the BASIC program in memory (PROG) is the one in the image
// Boot-bound (BIOS, DSS 1.71, two launcher runs, TR-DOS loading): ~3 s host time
TEST_F(SprinterZxMode_Test, RamDisk_TrdAndSclBootToTheirPrograms)
{
    ASSERT_NO_FATAL_FAILURE(BootToPrompt());
    Dss("cd \\trd", 20);
    for (const char* name : {"VIBRATE!.SCL", "KOL0BOK2.TRD"})
    {
        SCOPED_TRACE(name);
        std::vector<uint8_t> image;
        ASSERT_TRUE(Disk().Read(std::string("/TRD/") + name, image));
        const std::vector<uint8_t> boot = BootOf(image, std::strstr(name, ".SCL") != nullptr);
        ASSERT_GT(boot.size(), 16u);

        std::string lower = name;
        for (char& c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        ASSERT_NO_FATAL_FAILURE(Launch(lower));
        ASSERT_NO_FATAL_FAILURE(TrDos("r", 400));
        const uint16_t prog = static_cast<uint16_t>(_context->pMemory->DirectReadFromZ80Memory(23635) |
                                                    _context->pMemory->DirectReadFromZ80Memory(23636) << 8);
        size_t same = 0;
        for (size_t i = 0; i < boot.size(); i++)
            same += _context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(prog + i)) == boot[i];
        EXPECT_EQ(same, boot.size()) << "PROG = " << prog << "; " << SpectrumText();
        EXPECT_FALSE(_context->pBetaDisk && _context->pBetaDisk->getDrive() && _context->pBetaDisk->getDrive()->isDiskInserted())
            << "no floppy: the RAM disk served TR-DOS";

        CtrlAltDel(20);
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return !InZxMode(); }, 300, 10);
        ASSERT_FALSE(InZxMode());
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 200);
    }
}

// The owner's report of 2026-10-02 ("Enter on C:\UTILS\CDPLAYER\cd_play.trd showed the catalog of a disk titled
// comdos"): Flex Navigator's Enter on the TRD (FN.EXT: spectrum.exe sp.zx) puts CD_PLAY.TRD into the RAM disk and
// TR-DOS 7.03's LIST shows its own catalog. comdos.trd (C:\UTILS\COMDOS) is the TWIX kit's Commander DOS: the disk
// in the drive then was another one (TWIX's own RAM disks, a floppy), not the launcher's image.
// Boot-bound (BIOS, DSS 1.71, Flex Navigator, the launcher, TR-DOS): ~5 s host time
TEST_F(SprinterZxModeFn_Test, FlexNavigator_EnterOnCdPlayTrd_ShowsItsCatalog)
{
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Shell version"); }, 2000, 5);
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 1500);
    // The root of C: in the left panel: .. BIN C DEMOS DEV DOCS DSS FM FN GAMES MODEM TESTS TRD UTILS ...
    for (int i = 0; i < 12; i++)
        Tap("pc.down", 10);
    Tap("pc.enter", 150);  // UTILS: .. 2DSTUDIO CD CDPLAYER ...
    for (int i = 0; i < 3; i++)
        Tap("pc.down", 10);
    Tap("pc.enter", 150);  // CDPLAYER: .. CD_PLAY.TRD ...
    Tap("pc.down", 10);
    Tap("pc.enter", 10);
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return InZxMode() && SpectrumHas("TR-DOS"); }, 1500, 10);
    ASSERT_TRUE(InZxMode()) << ScreenText();
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 50);
    ASSERT_NO_FATAL_FAILURE(TrDos("k", 120));
    const std::string catalog = SpectrumText();
    EXPECT_NE(catalog.find("Title: pp"), std::string::npos) << catalog;
    EXPECT_NE(catalog.find("3 File(s)"), std::string::npos) << catalog;
    EXPECT_NE(catalog.find("CD_PLAY <C>"), std::string::npos) << catalog;
    EXPECT_EQ(catalog.find("comdos"), std::string::npos) << catalog;
}
