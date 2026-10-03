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
#include "emulator/memory/sprinter/sprinteraccelerator.h"
#include "emulator/state/devicestate.h"

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

/// Flex Navigator, the Spectrum mode from its Enter on a TRD, and each way back (tdd-zx-mode §11 finding 5)
class SprinterZxResetFn_Test : public SprinterZxModeFn_Test
{
protected:
    /// What decides Flex Navigator's picture besides its own drawing: the PLD's mode registers and the accelerator
    struct FnState
    {
        uint8_t allMode = 0;
        uint8_t rgMod = 0;
        uint8_t modePage = 0;
        uint8_t hold = 0;
        uint8_t frameLines = 0;
        uint8_t turbo = 0;
        bool accelerator = false;

        bool operator==(const FnState& o) const
        {
            return allMode == o.allMode && rgMod == o.rgMod && modePage == o.modePage && hold == o.hold &&
                   frameLines == o.frameLines && turbo == o.turbo && accelerator == o.accelerator;
        }
    };

    FnState Capture()
    {
        const SprinterPldState& p = _decoder->GetPldState();
        const SprinterAccelerator* acc = _decoder->GetAccelerator();
        FnState s;
        s.allMode = p.allMode;
        s.rgMod = p.rgMod;
        s.modePage = _decoder->GetIntSource().ModePage();
        s.hold = p.hold;
        s.frameLines = p.frameLines;
        s.turbo = p.turbo;
        s.accelerator = acc && acc->IsEnabled();
        return s;
    }

    static std::string Describe(const FnState& s)
    {
        char text[160];
        std::snprintf(text, sizeof text, "ALL_MODE=%02X RGMOD=%02X modePage=%u HOLD=%02X lines=%u turbo=%u accelerator=%d",
                      s.allMode, s.rgMod, s.modePage, s.hold, s.frameLines ? 312u : 320u, s.turbo, s.accelerator);
        return text;
    }

    /// The picture (two full frames rendered at the normal speed)
    std::vector<uint32_t> Picture()
    {
        _emulator->DisableTurboMode();
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 2);
        const FramebufferDescriptor& fb = _context->pScreen->GetFramebufferDescriptor();
        const uint32_t* pixels = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
        std::vector<uint32_t> picture(pixels, pixels + static_cast<size_t>(fb.width) * fb.height);
        _emulator->EnableTurboMode();
        return picture;
    }

    /// Flex Navigator starts in the root of C: (the disk's SYSTEM.BAT) - cold and after every return
    void WaitForFlexNavigator(int frames) { EmulatorTestHelper::RunFramesFast(_emulator.get(), frames); }

    /// Enter on C:\UTILS\CDPLAYER\CD_PLAY.TRD: the launcher's Spectrum mode, TR-DOS. `fromRoot`: Flex Navigator
    /// shows the root of C: (a cold start); otherwise it came back in CDPLAYER with the cursor on the TRD (the
    /// launcher's /ret-fn, after Ctrl+Alt+Del and after the RESET button alike)
    void EnterZxMode(bool fromRoot)
    {
        if (fromRoot)
        {
            for (int i = 0; i < 12; i++)
                Tap("pc.down", 10);
            Tap("pc.enter", 150);
            for (int i = 0; i < 3; i++)
                Tap("pc.down", 10);
            Tap("pc.enter", 150);
            Tap("pc.down", 10);
        }
        Tap("pc.enter", 10);
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return InZxMode() && SpectrumHas("TR-DOS"); }, 1500, 10);
        ASSERT_TRUE(InZxMode()) << PldLine();
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 20);
    }

    enum class Way
    {
        CtrlAltDel,  ///< the PLD's /RESET from the keyboard (/ret-fn: the BIOS reset intercept back to DSS)
        Reset,       ///< Emulator::Reset (WebAPI / CLI reset, the Qt reset button): the RESET button
        PowerCycle,  ///< Emulator::Reset(true): power off and on
    };

    static const char* Name(Way way)
    {
        switch (way)
        {
            case Way::CtrlAltDel: return "Ctrl+Alt+Del";
            case Way::Reset: return "reset";
            default: return "power cycle";
        }
    }

    /// Cold start to Flex Navigator, then ZX mode -> each way back -> Flex Navigator, twice over: the PLD's mode
    /// registers, the accelerator and the picture equal the cold start's (the picture up to the panels' contents:
    /// /ret-fn brings Flex Navigator back in the TRD's folder; the wrong mode turns most of the picture black)
    void RunResetWays()
    {
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Shell version"); }, 2000, 5);
        ASSERT_TRUE(ScreenHas("Shell version")) << ScreenText();
        WaitForFlexNavigator(1500);
        const FnState cold = Capture();
        ASSERT_TRUE(cold.accelerator) << Describe(cold);
        const std::vector<uint32_t> coldPicture = Picture();

        // The power cycle last: Flex Navigator then starts in the root again
        bool fromRoot = true;
        for (Way way : {Way::CtrlAltDel, Way::Reset, Way::CtrlAltDel, Way::Reset, Way::PowerCycle})
        {
            SCOPED_TRACE(Name(way));
            ASSERT_NO_FATAL_FAILURE(EnterZxMode(fromRoot));
            fromRoot = false;
            EXPECT_EQ(_decoder->GetPldState().allMode & 0x01, 0) << "the launcher's Spectrum mode";
            switch (way)
            {
                case Way::CtrlAltDel:
                    CtrlAltDel(20);
                    break;
                case Way::Reset:
                    _emulator->Reset();
                    break;
                case Way::PowerCycle:
                    _emulator->Reset(true);
                    break;
            }
            WaitForFlexNavigator(1500);  // a kept ZX mode shows as ALL_MODE bit 0 = 0 below

            const FnState after = Capture();
            EXPECT_TRUE(after == cold) << "cold: " << Describe(cold) << "\nafter: " << Describe(after);
            const std::vector<uint32_t> picture = Picture();
            ASSERT_EQ(picture.size(), coldPicture.size());
            size_t differ = 0;
            for (size_t i = 0; i < picture.size(); i++)
                differ += picture[i] != coldPicture[i];
            // The clock in the menu bar and the mouse pointer may differ; a wrong video mode changes most of it
            EXPECT_LT(differ, picture.size() / 50) << differ << " of " << picture.size() << " pixels differ from the cold start; "
                                                   << SaveScreen("sprinter-fn-after-reset.png");
        }
    }
};

class SprinterZxResetFn307_Test : public SprinterZxResetFn_Test
{
protected:
    const char* BiosFile() const override { return "sp2k-3.07-beta1.rom"; }
};

// The owner's report of 2026-10-02: after the ZX mode and a reset Flex Navigator came up without its video mode
// (the accelerator off, the Spectrum screen addressing on). The board's /RESET presets ALL_MODE to #FF and clears
// RGMOD and PORT_Y (PLD SP2_ACEX.TDF:1041, :958, ACCELER.TDF:204); the reset kept the ZX mode's #FE, and BIOS
// 3.07 BETA 1 reads ALL_MODE back at its reset intercept and writes what it read. BIOS 3.06 HF2 writes #FF there.
// Boot-bound (BIOS, DSS 1.71, Flex Navigator, five launcher runs and returns): ~21 s host time each
TEST_F(SprinterZxResetFn307_Test, EveryResetFromZxMode_FlexNavigatorAsAfterColdStart)
{
    RunResetWays();
}

TEST_F(SprinterZxResetFn_Test, EveryResetFromZxMode_FlexNavigatorAsAfterColdStart)
{
    RunResetWays();
}

// The owner's report of 2026-10-02: in the Spectrum mode the status bar said "text 40 (mixed)". The hardware has
// no Spectrum mode of its own: the launcher writes ZX-40 squares (Mode1 = Mode2 = the cell's address low byte)
// inside border squares, and the renderer draws them as symbol squares whose font is the screen bitmap. The
// classifier reads all three mode bytes as the renderer does: the status bar, the machine report, the per-square
// map and the screen text all say Spectrum in the 128 menu and at the TR-DOS prompt, and the native mode in
// Flex Navigator before.
// Boot-bound (BIOS, DSS 1.71, Flex Navigator, the launcher, TR-DOS): ~5 s host time
TEST_F(SprinterZxModeFn_Test, PictureMode_FlexNavigatorThenSpectrum)
{
    auto video = [&] {
        DeviceState::SprinterVideoQuery query;
        query.squares = false;
        return DeviceState::SprinterVideo(_context, query);
    };
    auto field = [](const StateNode& node, const char* key) {
        const StateNode* member = node.find(key);
        return member ? *member : StateNode();
    };

    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Shell version"); }, 2000, 5);
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 1500);
    EXPECT_EQ(_context->pScreen->DescribeScreenState().videoModeBrief, "640x256 16c");
    EXPECT_EQ(field(video(), "picture_mode").s, "graphics_640");

    for (int i = 0; i < 12; i++)
        Tap("pc.down", 10);
    Tap("pc.enter", 150);
    for (int i = 0; i < 3; i++)
        Tap("pc.down", 10);
    Tap("pc.enter", 150);
    Tap("pc.down", 10);
    Tap("pc.enter", 10);
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return InZxMode() && SpectrumHas("TR-DOS"); }, 1500, 10);
    ASSERT_TRUE(InZxMode()) << PldLine();
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 50);

    for (const char* where : {"128 menu", "TR-DOS prompt"})
    {
        SCOPED_TRACE(where);
        if (std::strcmp(where, "TR-DOS prompt") == 0)
        {
            Zx("enter", 100);
            ASSERT_TRUE(SpectrumHas("TR-DOS")) << SpectrumText();
        }
        const ScreenState screen = _context->pScreen->DescribeScreenState();
        EXPECT_EQ(screen.videoModeBrief, "Spectrum 256x192, screen 5") << screen.videoMode;
        EXPECT_NE(screen.videoMode.find("spectrum 768, border 512"), std::string::npos) << screen.videoMode;
        const StateNode map = video();
        EXPECT_EQ(field(map, "picture_mode").s, "spectrum");
        EXPECT_FALSE(field(map, "picture_mixed").b);
        const StateNode machine = DeviceState::Sprinter(_context);
        EXPECT_EQ(field(field(machine, "video"), "picture_mode_key").s, "spectrum");
        const StateNode text = DeviceState::SprinterText(_context);
        EXPECT_TRUE(field(text, "spectrum_screen").b);
        EXPECT_EQ(field(text, "text_squares").i, 0) << "Spectrum cells are bitmaps, not characters";
    }
    EXPECT_NE(SpectrumText().find("TR-DOS"), std::string::npos) << "the screen OCR reads the ZX screen";
}
