// ZX Profi boot tests with the real 64 KB Profi ROM (data/rom/profi.rom).
//
// Guards the whole chain: the PROFI model is creatable (configs/profi), the decoder resets into the
// SYS (service / menu) ROM with the DOS latch on, and the machine runs the ROM without derailing.
// Skipped when the ROM image is not deployed.
//
// Design: docs/inprogress/2026-09-21-profi/technical-design.md sections 4, 5, 10.4

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/keyboard/keyboard.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <emulator/ports/portdecoder.h>
#include <gtest/gtest.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <vector>

#include "3rdparty/lodepng/lodepng.h"
#include "emulator/video/profi/profigeometry.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/media/mediamanager.h"
#include "emulator/ports/portdiagrecorder.h"
#include "emulator/video/screen.h"

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/serial/serialpeer.h"
#include "emulator/ports/models/portdecoder_profi.h"
#include "pch.h"
#include "stdafx.h"

class ProfiBoot_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;

    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);

        _emulator = _manager->CreateEmulatorWithModelAndRAM("profi-boot", "PROFI", 1024, LoggerLevel::LogError);
        if (!_emulator)
            GTEST_SKIP() << "PROFI is not creatable (missing configs/profi or data/rom/profi.rom)";
    }

    void TearDown() override
    {
        // Only our own instance: the manager is process-wide, and wiping it
        // also hid instances other tests had leaked
        if (_emulator)
        {
            const std::string id = _emulator->GetId();
            _emulator.reset();
            _manager->RemoveEmulator(id);
        }
    }

    /// Tap keys on the ZX matrix: a comma list where one character is that key, ENT is Enter, Cn is Caps Shift + n
    /// (Cn with 6 / 7 = cursor down / up) and Sn is Symbol Shift + n. Each key is held 6 frames and released for 12
    void TapKeys(const std::string& keys)
    {
        Keyboard* keyboard = _emulator->GetContext()->pKeyboard;
        std::stringstream list(keys);
        std::string token;
        while (std::getline(list, token, ','))
        {
            std::vector<ZXKeysEnum> held;
            if (token == "ENT")
                held.push_back(ZXKEY_ENTER);
            else if (token.size() == 2 && token[0] == 'C')
                held = {ZXKEY_CAPS_SHIFT, static_cast<ZXKeysEnum>(token[1])};
            else if (token.size() == 2 && token[0] == 'S')
                held = {ZXKEY_SYM_SHIFT, static_cast<ZXKeysEnum>(token[1])};
            else if (token.size() == 1)
                held.push_back(static_cast<ZXKeysEnum>(token[0]));
            for (ZXKeysEnum k : held)
                keyboard->PressKey(k);
            _emulator->RunNFrames(6, true);
            for (ZXKeysEnum k : held)
                keyboard->ReleaseKey(k);
            _emulator->RunNFrames(12, true);
        }
    }

    /// Decode rows of the standard ZX bitmap screen (RAM page 5/7) with the font found at `fontOffset` of ROM page `fontPage`
    static std::string DecodeRows(EmulatorContext* context, uint8_t screenPage, uint8_t fontPage, uint32_t fontOffset,
                                  uint8_t rowFrom, uint8_t rowTo)
    {
        Memory* memory = context->pMemory;
        const uint8_t* vram = memory->RAMPageAddress(screenPage);
        const uint8_t* font = memory->ROMPageHostAddress(fontPage) + fontOffset;
        std::string result;
        for (uint8_t row = rowFrom; row < rowTo; row++)
        {
            for (uint8_t col = 0; col < 32; col++)
            {
                uint8_t glyph[8];
                for (int k = 0; k < 8; k++)
                {
                    uint16_t y = row * 8 + k;
                    uint16_t addr = ((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2);
                    glyph[k] = vram[addr + col];
                }
                char best = '?';
                for (int c = 0x20; c < 0x80; c++)
                {
                    if (memcmp(glyph, font + static_cast<uint32_t>(c - 0x20) * 8, 8) == 0)
                    {
                        best = static_cast<char>(c);
                        break;
                    }
                }
                result += best;
            }
            result += '\n';
        }
        return result;
    }
};

/// @brief Power-on state: SYS ROM paged in, DOS latch on, latches clear
TEST_F(ProfiBoot_Test, ResetStateIsSysRom)
{
    EmulatorContext* context = _emulator->GetContext();
    ASSERT_EQ(context->config.mem_model, MM_PROFI);

    EXPECT_EQ(context->emulatorState.p7FFD, 0x00);
    EXPECT_EQ(context->emulatorState.pDFFD, 0x00);
    EXPECT_NE(context->emulatorState.flags & CF_TRDOS, 0) << "DOS latch is on after reset";
    EXPECT_EQ(context->pMemory->GetROMPage(), 0) << "SYS ROM (page 0) at #0000";
}

/// @brief The BIOS enables hi-res (DFFD.7) for its splash screen: the renderer switches to the 512x240
///        mode and draws the boot screen (blue "ROM Bios" box, red copyright bar, logo)
TEST_F(ProfiBoot_Test, BiosSplashRendersInHiRes)
{
    EmulatorContext* context = _emulator->GetContext();
    _emulator->EnableTurboMode();  // asserts on emulated state and a coarse pixel census only

    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return (context->emulatorState.pDFFD & 0x80) != 0; }, 300);
    _emulator->RunNFrames(120, true);  // the BIOS memory check runs before the splash is drawn (~1 s of emulated time)

    ASSERT_NE(context->emulatorState.pDFFD & 0x80, 0) << "the BIOS never switched DS80 on";
    Screen* screen = context->pScreen;
    EXPECT_EQ(screen->GetVideoMode(), M_PROFIHR);

    FramebufferDescriptor& fb = screen->GetFramebufferDescriptor();
    ASSERT_EQ(fb.width, 608u);
    ASSERT_EQ(fb.height, 288u);

    // Census: the splash uses black background, a blue box, a red bar and white text
    const uint32_t* pixels = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
    size_t black = 0, blue = 0, red = 0, white = 0;
    for (size_t i = 0; i < static_cast<size_t>(fb.width) * fb.height; i++)
    {
        const uint32_t p = pixels[i] & 0x00FFFFFF;  // ABGR: 0xBBGGRR
        const uint32_t b = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, r = p & 0xFF;
        if (p == 0)                          black++;
        else if (b > 150 && r < 60 && g < 60) blue++;
        else if (r > 150 && g < 60 && b < 60) red++;
        else if (r > 200 && g > 200 && b > 200) white++;
    }
    EXPECT_GT(black, 60000u) << "black background";
    EXPECT_GT(blue, 5000u) << "blue ROM Bios box";
    EXPECT_GT(red, 500u) << "red copyright bar / logo stripe";
    EXPECT_GT(white, 500u) << "white text and logo";
}

/// @brief The BIOS memory check runs against the real 1 MB RAM and 64 KB ROM: it must reach the splash
///        without leaving the SYS ROM or corrupting paging (last latch state is the BIOS's own)
TEST_F(ProfiBoot_Test, BiosStaysInSysRomDuringMemoryCheck)
{
    // Slower than the 50 ms guideline on purpose: booting the real BIOS to a machine state is the point
    EmulatorContext* context = _emulator->GetContext();
    _emulator->EnableTurboMode();
    _emulator->RunNFrames(150, true);

    EXPECT_EQ(context->pMemory->GetROMPage(), 0) << "SYS ROM still paged in";
    EXPECT_NE(context->emulatorState.flags & CF_TRDOS, 0) << "DOS latch still on";
    EXPECT_LT(context->pCore->GetZ80()->pc, 0x4000) << "executing inside the ROM";
}

/// @brief The BIOS must get past "Please wait ..." and show its main menu, with NO disk in the drive.
///        Two FDC behaviours had to be authentic for that: (1) fast disk loading must stay disarmed in the
///        SYS ROM (it collapsed the Restore verify delay to 1 T-state, hiding BUSY), and (2) a Type II command
///        on a not-ready drive must keep BUSY visible briefly before ending (the drive probe polls for BUSY).
TEST_F(ProfiBoot_Test, BiosReachesMainMenuWithoutDisk)
{
    EmulatorContext* context = _emulator->GetContext();
    _emulator->EnableTurboMode();  // asserts on emulated state and a coarse pixel census only

    _emulator->RunNFrames(600, true);

    // Still parked in the BUSY poll loop ($0797-$079A) would mean the hang is back
    const uint16_t pc = context->pCore->GetZ80()->pc;
    EXPECT_FALSE(pc >= 0x0797 && pc <= 0x079A) << "BIOS is stuck waiting for FDC BUSY, pc=" << std::hex << pc;

    // The menu panel is cyan and covers a large part of the 608x288 screen
    FramebufferDescriptor& fb = context->pScreen->GetFramebufferDescriptor();
    const uint32_t* pixels = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
    size_t cyan = 0;
    for (size_t i = 0; i < static_cast<size_t>(fb.width) * fb.height; i++)
    {
        const uint32_t b = (pixels[i] >> 16) & 0xFF, g = (pixels[i] >> 8) & 0xFF, r = pixels[i] & 0xFF;
        if (b > 150 && g > 150 && r < 60)
            cyan++;
    }
    EXPECT_GT(cyan, 20000u) << "main menu panel not on screen";
}

/// @brief Same with a disk inserted (the BIOS also probes the drive and reads the boot sector)
TEST_F(ProfiBoot_Test, BiosLeavesPleaseWaitWithDisk)
{
    EmulatorContext* context = _emulator->GetContext();
    _emulator->EnableTurboMode();
    ASSERT_TRUE(_emulator->LoadDisk(TestPathHelper::GetTestDataPath("loaders/trd/zx-format8.trd")));

    _emulator->RunNFrames(400, true);

    const uint16_t pc = context->pCore->GetZ80()->pc;
    EXPECT_FALSE(pc >= 0x0797 && pc <= 0x079A) << "BIOS is stuck waiting for FDC BUSY, pc=" << std::hex << pc;
}

/// @brief Development probe (disabled): boot with a disk, dump the framebuffer to scratch/profi/boot.png
TEST_F(ProfiBoot_Test, DISABLED_ProbeBootScreen)
{
    EmulatorContext* context = _emulator->GetContext();
    _emulator->EnableTurboMode();
    if (const char* disk = std::getenv("PROFI_PROBE_DISK"))
        ASSERT_TRUE(_emulator->LoadDisk(disk));
    _emulator->RunNFrames(900, true);

    for (int i = 0; i < 6; i++)
    {
        _emulator->RunNFrames(7, true);
        std::cout << "pc=" << std::hex << context->pCore->GetZ80()->pc << " status1F=" << int(context->pPortDecoder->DecodePortIn(0x001F, 0)) << std::dec << "\n";
    }
    Screen* screen = context->pScreen;
    FramebufferDescriptor& fb = screen->GetFramebufferDescriptor();
    std::cout << "mode=" << Screen::GetVideoModeName(screen->GetVideoMode()) << " fb " << fb.width << "x" << fb.height
              << " p7FFD=" << std::hex << int(context->emulatorState.p7FFD) << " pDFFD=" << int(context->emulatorState.pDFFD)
              << std::dec << "\n";
    lodepng_encode32_file("scratch/profi/boot.png", fb.memoryBuffer, fb.width, fb.height);
}

/// @brief The BIOS menu entries start what they name (cursor down = Caps Shift + 6, Enter). Slower than 50 ms on
///        purpose: each one boots the real BIOS to its menu first
TEST_F(ProfiBoot_Test, MenuEntriesStartWhatTheyName)
{
    // text = nullptr: the screen uses its own font, so the check is the ROM page instead
    struct Entry { const char* keys; const char* text; uint8_t romPage; const char* what; };
    const Entry entries[] = {
        {"C6,ENT",          "TR-DOS Ver 6.08", 3, "TR-DOS 48K: the profi.rom TR-DOS at its prompt"},
        {"C6,C6,C6,ENT",    "1982 Sinclair",   3, "Sinclair 48: 48 BASIC"},
        {"C6,C6,C6,C6,ENT", nullptr,           2, "Sinclair 128: the 128 page of profi.rom (STS / Power of Sound menu)"},
    };
    for (const Entry& e : entries)
    {
        SCOPED_TRACE(e.what);
        _emulator->Reset(true);
        EmulatorContext* context = _emulator->GetContext();
        _emulator->EnableTurboMode();
        _emulator->RunNFrames(600, true);   // the BIOS menu (BiosReachesMainMenuWithoutDisk)
        TapKeys(e.keys);
        auto shown = [&] {
            return DecodeRows(context, (context->emulatorState.p7FFD & 0x08) ? 7 : 5, 3, 0x3D00, 0, 24);
        };
        if (e.text)
        {
            EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return shown().find(e.text) != std::string::npos; }, 400);
            EXPECT_NE(shown().find(e.text), std::string::npos) << shown();
        }
        else
        {
            _emulator->RunNFrames(300, true);
            EXPECT_EQ(context->pMemory->GetROMPage(), e.romPage);
            EXPECT_EQ(context->emulatorState.flags & CF_TRDOS, 0) << "the SYS session is over";
        }
        EXPECT_EQ(context->emulatorState.pDFFD & 0x80, 0) << "hi-res is off once the BIOS hands over";
    }
}

/// @brief The BIOS 2.0 "Тест быстродействия" in hi-res reads 1.50 and, with TURBO, 2.45 - the figures a real 5.06
///        with its 20 MHz ZQ3 showed (termik, zx-pk 21644 p.11, 2014; solegstar: the test is scaled to the 12 MHz
///        crystal, "1.5 + the waits"). Without the hi-res waits it reads 1.65 / 3.35, so this checks the CPU clock
///        and the wait model together (design-hires.md H3). The result bar ends at x = 63 + 80 x the figure
static uint32_t SpeedTestBarEnd(EmulatorContext* context)
{
    FramebufferDescriptor& fb = context->pScreen->GetFramebufferDescriptor();
    const uint32_t* pixels = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
    const uint32_t y = 164;   // the result bar's row; the 1.00 reference bar is at y 153
    const uint32_t background = pixels[140 * fb.width + 300] & 0x00FFFFFF;   // the panel's grey, inside the frame
    uint32_t end = 0;
    for (uint32_t x = 60; x < 548; x++)
        if ((pixels[y * fb.width + x] & 0x00FFFFFF) != background)
            end = x;
    return end;
}

TEST_F(ProfiBoot_Test, BiosSpeedTestReadsWhatARealBoardReads)
{
    // Slower than the 50 ms guideline on purpose: the real BIOS boots, then runs its own timing loop
    for (const bool turbo : {false, true})
    {
        SCOPED_TRACE(turbo ? "TURBO" : "normal");
        _emulator->Reset(true);
        _emulator->SetFrontPanelSwitch(FrontPanelSwitch::Turbo, turbo);
        _emulator->RunNFrames(600, true);                  // the BIOS menu
        TapKeys("C6,C6,C6,C6,C6,ENT");                     // the test menu
        TapKeys("C6,C6,C6,C6,C6,C6,ENT");                  // Тест быстродействия
        _emulator->RunNFrames(400, true);
        const uint32_t end = SpeedTestBarEnd(_emulator->GetContext());
        EXPECT_EQ(end, turbo ? 63u + 196u : 63u + 120u) << "bar end " << end << " = speed " << (end - 63) / 80.0;
    }
}

/// @brief Pixel lines of the hi-res screen that hold anything (ProfiGeometry: 240 lines of 64 bytes)
static uint32_t HiresLinesWithInk(EmulatorContext* context)
{
    const uint8_t* page = context->pMemory->RAMPageAddress(ProfiGeometry::PixelPage(context->emulatorState.p7FFD));
    uint32_t lines = 0;
    for (uint32_t v = 0; v < ProfiGeometry::kScreenLines; v++)
    {
        bool any = false;
        for (uint32_t b = 0; b < 64 && !any; b++)
            any = page[ProfiGeometry::ByteOffset(v, b)] != 0;
        lines += any ? 1u : 0u;
    }
    return lines;
}

/// @brief The SP-DOS system disk (testdata/machines/profi/cpm/sp-dos) boots on the v5 from BIOS 2.0's CP/M entry to
///        "SP-DOS Shell by Michael Markowsky". Its loader runs in hi-res (CPU at 5 MHz) and polls the VG93 through
///        #BF with INI: the VG93 has to keep its own 3.5 MHz clock, or its time steps back at every frame boundary
///        and every read ends in Lost Data (WD1793::SetBaseClockTimeBase, set by PortDecoder_Profi)
TEST_F(ProfiBoot_Test, SpDosBootsToItsShell)
{
    // Slower than the 50 ms guideline on purpose: the BIOS and SP-DOS boot from a floppy
    const std::string disk = TestPathHelper::GetTestDataPath("machines/profi/cpm/sp-dos/unicopy-sp-dos.td0");
    EmulatorContext* context = _emulator->GetContext();
    _emulator->EnableTurboMode();
    _emulator->RunNFrames(600, true);   // the BIOS menu
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(disk, 0, &error)) << error;
    TapKeys("ENT");                      // the first entry: CP/M
    _emulator->RunNFrames(1500, true);

    std::string ram;
    for (uint32_t a = 0; a < 0x10000; a++)
        ram.push_back(static_cast<char>(context->pCore->GetZ80()->DirectRead(static_cast<uint16_t>(a))));
    EXPECT_NE(ram.find("SP-DOS Shell by Michael Markowsky"), std::string::npos)
        << "the shell did not load, pc=" << std::hex << context->pCore->GetZ80()->pc;
    EXPECT_NE(context->emulatorState.pDFFD & 0x80, 0) << "hi-res";
}

/// @brief CP/M boots on the v5 from the BIOS menu's "Загрузка системы CP/M" entry: the Kondor "Copy K" system disk
///        (testdata/machines/profi/cpm/v5, README there) loads its drivers from CONFIG.SYS, signs on in hi-res and
///        runs its AUTOEXEC.BAT (KEYHELP, PRSCR, then PAUSE, which waits for Space)
TEST_F(ProfiBoot_Test, CpmBootsFromTheKondorSystemDisk)
{
    // Slower than the 50 ms guideline on purpose: the BIOS and CP/M boot from a floppy
    const std::string disk = TestPathHelper::GetTestDataPath("machines/profi/cpm/v5/kondor-system-copyk.fdi");
    EmulatorContext* context = _emulator->GetContext();
    _emulator->EnableTurboMode();
    _emulator->RunNFrames(600, true);   // the BIOS menu
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(disk, 0, &error)) << error;
    TapKeys("ENT");                      // the first entry: CP/M
    _emulator->RunNFrames(2500, true);

    EXPECT_NE(context->emulatorState.pDFFD & 0x20, 0) << "CP/M mode";
    EXPECT_NE(context->emulatorState.pDFFD & 0x80, 0) << "hi-res";
    const uint16_t pc = context->pCore->GetZ80()->pc;
    EXPECT_FALSE(pc >= 0x8000 && pc < 0x8300) << "stuck in the boot loader, pc=" << std::hex << pc;
    const uint32_t lines = HiresLinesWithInk(context);
    std::cout << "hi-res lines with ink: " << lines << "\n";
    EXPECT_GE(lines, 80u) << "the sign-on and the AUTOEXEC output fill the screen";
}

/// @brief The CP/M switch at power-on: the board holds #DFFD at #00 (research-profi-v5-open-items.md Q6), so the
///        BIOS cannot raise its hi-res menu and goes straight to Spectrum 128 - what the v5.0 manual says the switch
///        does ("pressed = Spectrum 128"). No other emulator models the switch; the behavior comes from the BIOS alone
TEST_F(ProfiBoot_Test, CpmSwitchAtPowerOnStartsSpectrum128)
{
    // Slower than the 50 ms guideline on purpose: the real BIOS has to run to its decision
    EmulatorContext* context = _emulator->GetContext();
    ASSERT_TRUE(_emulator->SetFrontPanelSwitch(FrontPanelSwitch::Cpm, true));
    _emulator->EnableTurboMode();
    _emulator->RunNFrames(300, true);

    EXPECT_EQ(context->emulatorState.pDFFD, 0) << "the switch holds #DFFD cleared";
    EXPECT_NE(context->pScreen->GetVideoMode(), M_PROFIHR) << "no hi-res BIOS menu";
    EXPECT_EQ(context->pMemory->GetROMPage(), 2) << "the 128 ROM page (the STS / Power of Sound menu)";
    EXPECT_EQ(context->emulatorState.flags & CF_TRDOS, 0) << "the SYS session is over";
}

/// @brief Development probe (disabled): reach the BIOS menu, tap PROFI_PROBE_KEYS (see TapKeys), print the standard
///        screen and dump the framebuffer to scratch/profi/keys.png
TEST_F(ProfiBoot_Test, DISABLED_ProbeMenuKeys)
{
    EmulatorContext* context = _emulator->GetContext();
    _emulator->EnableTurboMode();
    _emulator->RunNFrames(600, true);
    const char* keys = std::getenv("PROFI_PROBE_KEYS");
    TapKeys(keys ? keys : "");
    _emulator->RunNFrames(300, true);
    std::cout << "pc=" << std::hex << context->pCore->GetZ80()->pc << " p7FFD=" << int(context->emulatorState.p7FFD)
              << " pDFFD=" << int(context->emulatorState.pDFFD) << " rom=" << int(context->pMemory->GetROMPage())
              << " flags=" << int(context->emulatorState.flags) << std::dec << "\n";
    std::cout << DecodeRows(context, (context->emulatorState.p7FFD & 0x08) ? 7 : 5, 3, 0x3D00, 0, 24);
    FramebufferDescriptor& fb = context->pScreen->GetFramebufferDescriptor();
    lodepng_encode32_file("scratch/profi/keys.png", fb.memoryBuffer, fb.width, fb.height);
}

/// @brief Development probe (disabled): run a program as a user would and save screenshots.
///        PROFI_PROGRAM = a .tap / .tzx (Sinclair 48 + LOAD "") or a .trd (TR-DOS + RUN), a .udi / .fdi / .td0 boot disk
///        (drive A, then PROFI_BOOT_KEYS in the BIOS menu, default Enter: CP/M); PROFI_MODEL = PROFI / PROFI3;
///        PROFI_TURBO=1 / PROFI_CPM=1 press the TURBO / CP/M switch (PROFI_PROGRAM=none: the BIOS only); PROFI_CONTENTION=0 turns the waits off; PROFI_FRAMES = frames to run after the load starts (default 1500);
///        PROFI_SHOTS = comma list of frame numbers to save as scratch/profi/<PROFI_NAME>-<frame>.png; PROFI_KEYS =
///        keys to tap after the load (TapKeys format), at frame PROFI_KEYS_AT (default: at once) and again every
///        PROFI_KEYS_EVERY frames. The emulated test programs of
///        docs/inprogress/2026-10-01-profi-v3-v5 (README of the materials' test-programs folder) run through this
TEST_F(ProfiBoot_Test, DISABLED_RunProgram)
{
    const char* program = std::getenv("PROFI_PROGRAM");
    if (!program)
        GTEST_SKIP() << "PROFI_PROGRAM not set";
    const bool bootOnly = std::string(program) == "none";   // PROFI_PROGRAM=none: only the BIOS, with the switches
    const char* model = std::getenv("PROFI_MODEL");
    {
        // The test build fits no sound card unless a test asks (SoundCardScope): programs and BIOS tests probe the
        // AY, so the machine is made again with its configured TurboSound slot
        SoundCardScope sound(TestSound::TurboSound);
        const std::string id = _emulator->GetId();
        _emulator.reset();
        _manager->RemoveEmulator(id);
        if (model && std::string(model) != "PROFI")
            _emulator = _manager->CreateEmulatorWithModel("profi-program", model, LoggerLevel::LogError);
        else
            _emulator = _manager->CreateEmulatorWithModelAndRAM("profi-program", "PROFI", 1024, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << (model ? model : "PROFI");
    }
    EmulatorContext* context = _emulator->GetContext();
    if (const char* contention = std::getenv("PROFI_CONTENTION"); contention && contention[0] == '0')
        context->pFeatureManager->setFeature(Features::kContention, false);
    if (const char* cpm = std::getenv("PROFI_CPM"); cpm && cpm[0] == '1')
        _emulator->SetFrontPanelSwitch(FrontPanelSwitch::Cpm, true);
    if (const char* turbo = std::getenv("PROFI_TURBO"); turbo && turbo[0] == '1')
        _emulator->SetFrontPanelSwitch(FrontPanelSwitch::Turbo, true);
    if (const char* rom = std::getenv("PROFI_ROM"))
    {
        // Another system ROM image (a path, absolute or relative to the binary)
        ASSERT_TRUE(_emulator->LoadROM(rom)) << rom;
        _emulator->Reset(true);
    }
    if (const char* hdd = std::getenv("PROFI_HDD"))
    {
        // A hard-disk image on ide0.master (the image is written to: pass a copy)
        MediaSource source;
        source.path = hdd;
        InsertOptions options;
        options.immediate = true;
        ASSERT_TRUE(context->pMediaManager->Insert("ide0.master", source, options).Ok()) << hdd;
        _emulator->Reset(true);
    }
    // PROFI_PORTTRACE=<file>: every IN / OUT from here on, written at the end (development aid)
    const char* portTraceFile = std::getenv("PROFI_PORTTRACE");
    PortDiagnosticRecorder* portTrace = nullptr;
    if (portTraceFile)
    {
        ASSERT_TRUE(_emulator->GetFeatureManager()->setFeature(Features::kPortTrace, true));
        portTrace = context->pPortDecoder->getPortTraceRecorder();
        ASSERT_NE(portTrace, nullptr);
        portTrace->start();
    }
    const std::string path(program);
    const bool v3 = model && std::string(model) == "PROFI3";   // the Kramis menu: Sinclair second, TR-DOS fifth
    const bool profi = !model || std::string(model).rfind("PROFI", 0) == 0;
    const auto endsWith = [&](const char* ext) {
        std::string lower = path;
        for (char& c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return lower.size() > 4 && lower.substr(lower.size() - 4) == ext;
    };
    const bool anyDisk = endsWith(".trd") || endsWith(".scl") || endsWith(".fdi") || endsWith(".td0") || endsWith(".udi");
    if (!profi && anyDisk)
    {
        // A reference machine with a disk in drive A: PROFI_REF_KEYS reach TR-DOS from its ROM (default: the
        // Pentagon 128 menu's last entry), then RUN boots the disk
        _emulator->RunNFrames(150, true);
        std::string error;
        ASSERT_TRUE(_emulator->LoadDisk(path, 0, &error)) << error;
        const char* refKeys = std::getenv("PROFI_REF_KEYS");
        TapKeys(refKeys ? refKeys : "C6,C6,C6,C6,ENT");
        _emulator->RunNFrames(100, true);
        TapKeys("R,ENT");
    }
    else if (!profi)
    {
        // A reference machine (48K, 128K, ...): its ROM boots to BASIC; a .tap only
        _emulator->RunNFrames(150, true);
        std::string error;
        ASSERT_TRUE(_emulator->LoadTape(path, &error)) << error;
        TapKeys("ENT,J,SP,SP,ENT");   // leave the copyright screen / the 128 menu (Tape Loader), then LOAD ""
    }
    else
    {
        // The BIOS menu (PROFI_BOOT_FRAMES: another count, e.g. to stop a hard-disk boot before its program starts)
        const char* bootFramesEnv = std::getenv("PROFI_BOOT_FRAMES");
        _emulator->RunNFrames(bootFramesEnv ? std::atoi(bootFramesEnv) : 600, true);
    }
    const bool disk = path.size() > 4 && (path.substr(path.size() - 4) == ".trd" || path.substr(path.size() - 4) == ".TRD");
    const bool bootDisk = path.size() > 4 && (path.substr(path.size() - 4) == ".udi" || path.substr(path.size() - 4) == ".UDI" ||
                                              path.substr(path.size() - 4) == ".fdi" || path.substr(path.size() - 4) == ".FDI" ||
                                              path.substr(path.size() - 4) == ".td0" || path.substr(path.size() - 4) == ".TD0");
    if (!profi || bootOnly)
    {
    }
    else if (bootDisk)
    {
        // A boot disk (CP/M): in drive A before the BIOS menu entry is chosen (PROFI_BOOT_KEYS, default the first
        // entry: Enter)
        std::string error;
        ASSERT_TRUE(_emulator->LoadDisk(path, 0, &error)) << error;
        const char* bootKeys = std::getenv("PROFI_BOOT_KEYS");
        TapKeys(bootKeys ? bootKeys : "ENT");
    }
    else if (disk)
    {
        TapKeys(v3 ? "C6,C6,C6,C6,ENT" : "C6,ENT");   // TR-DOS
        _emulator->RunNFrames(200, true);
        std::string error;
        ASSERT_TRUE(_emulator->LoadDisk(path, 0, &error)) << error;
        TapKeys("R,ENT");   // RUN: boot
    }
    else
    {
        TapKeys(v3 ? "C6,ENT" : "C6,C6,C6,ENT");   // v3: Sinclair (the 128 menu); v5: Sinclair 48
        _emulator->RunNFrames(150, true);
        std::string error;
        ASSERT_TRUE(_emulator->LoadTape(path, &error)) << error;
        TapKeys(v3 ? "ENT" : "J,SP,SP,ENT");   // v3: Tape Loader; v5: LOAD ""
    }
    const char* keys = std::getenv("PROFI_KEYS");
    const char* keysAtEnv = std::getenv("PROFI_KEYS_AT");
    const int keysAt = keysAtEnv ? std::atoi(keysAtEnv) : 0;
    const char* everyEnv = std::getenv("PROFI_KEYS_EVERY");
    const int every = everyEnv ? std::atoi(everyEnv) : 0;
    if (keys && keysAt == 0)
        TapKeys(keys);

    const char* framesEnv = std::getenv("PROFI_FRAMES");
    const int frames = framesEnv ? std::atoi(framesEnv) : 1500;
    std::vector<int> shots;
    if (const char* list = std::getenv("PROFI_SHOTS"))
    {
        std::stringstream ss(list);
        std::string token;
        while (std::getline(ss, token, ','))
            shots.push_back(std::atoi(token.c_str()));
    }
    const char* nameEnv = std::getenv("PROFI_NAME");
    const std::string name = nameEnv ? nameEnv : "program";
    const bool fdcTrace = std::getenv("PROFI_FDCTRACE") != nullptr;
    uint32_t lastFdc = 0xFFFFFFFF;
    for (int f = 1; f <= frames; f++)
    {
        if (!fdcTrace)
            _emulator->RunNFrames(1, true);
        else
        {
            // Every change of the FDC's command / track / sector registers and the head position (development aid)
            const uint64_t startFrame = context->emulatorState.frame_counter;
            while (context->emulatorState.frame_counter == startFrame)
            {
                // The OUT about to execute, when it goes to the FDC (#1F/#3F/#5F/#7F) or the system port (#FF)
                Z80* z = context->pCore->GetZ80();
                const uint8_t op = z->DirectRead(z->pc);
                int port = -1, value = 0;
                if (op == 0xD3)
                {
                    port = (z->a << 8) | z->DirectRead(static_cast<uint16_t>(z->pc + 1));
                    value = z->a;
                }
                else if (op == 0xED)
                {
                    const uint8_t op2 = z->DirectRead(static_cast<uint16_t>(z->pc + 1));
                    const uint8_t regs[8] = {z->b, z->c, z->d, z->e, z->h, z->l, 0, z->a};
                    if ((op2 & 0xC7) == 0x41)
                    {
                        port = z->bc;
                        value = regs[(op2 >> 3) & 7];
                    }
                }
                const int low = port & 0xFF;
                if (port >= 0 && (low == 0x1F || low == 0x3F || low == 0x5F || low == 0x7F || low == 0xFF))
                    std::cout << "F" << f << std::hex << " pc=" << z->pc << " OUT " << low << "," << value << std::dec << "\n";
                _emulator->RunSingleCPUCycle(true);
                WD1793* fdc = context->pBetaDisk;
                if (!fdc || !fdc->getDrive())
                    continue;
                static uint8_t lastStatusBits = 0;
                const uint8_t statusBits = static_cast<uint8_t>(static_cast<const WD1793*>(fdc)->getStatusRegister() & 0x1C);
                const bool statusChanged = statusBits != lastStatusBits;
                lastStatusBits = statusBits;
                const uint32_t now = (uint32_t(fdc->getCommandRegister()) << 24) | (uint32_t(fdc->getTrackRegister()) << 16) |
                                     (uint32_t(fdc->getSectorRegister()) << 8) | uint32_t(fdc->getDrive()->getTrack()) |
                                     (fdc->getSideUp() ? 0x80u : 0u);
                if (now == lastFdc && !statusChanged)
                    continue;
                lastFdc = now;
                std::cout << "F" << f << std::hex << " pc=" << context->pCore->GetZ80()->pc << " cmd=" << (now >> 24)
                          << " trk=" << ((now >> 16) & 0xFF) << " sec=" << ((now >> 8) & 0xFF) << " head=" << (now & 0x7F)
                          << " side=" << ((now >> 7) & 1) << " st=" << int(static_cast<const WD1793*>(fdc)->getStatusRegister()) << std::dec << "\n";
            }
        }
        if (keys && keysAt > 0 && (f == keysAt || (every > 0 && f > keysAt && (f - keysAt) % every == 0)))
            TapKeys(keys);
        if (std::find(shots.begin(), shots.end(), f) == shots.end() && f != frames)
            continue;
        FramebufferDescriptor& fb = context->pScreen->GetFramebufferDescriptor();
        const std::string file = "scratch/profi/" + name + "-" + std::to_string(f) + ".png";
        lodepng_encode32_file(file.c_str(), fb.memoryBuffer, fb.width, fb.height);
    }
    if (const char* bdosEnv = std::getenv("PROFI_BDOSLOG"))
    {
        // Steps after the frames: every CP/M BDOS entry (PC = 5): function, DE, the caller and the A / HL / DE the
        // call returns with, one line per call, at most 400 lines (development aid)
        Z80* z = context->pCore->GetZ80();
        const long steps = std::atol(bdosEnv);
        int lines = 0;
        // The log starts at the PROFI_BDOSLOG_ENTRY-th entry (default 1) of a .COM program (PC = #100; a DOS shell
        // enters #100 for itself first)
        const char* entryEnv = std::getenv("PROFI_BDOSLOG_ENTRY");
        int entries = entryEnv ? std::atoi(entryEnv) : 1;
        bool armed = false;
        for (long i = 0; i < steps && lines < 400; i++)
        {
            if (!armed && z->pc == 0x100)
            {
                // PROFI_BDOSLOG_MATCH=<hex bytes>: only the entry whose #100 holds these bytes (the program's first
                // bytes, as xxd prints them); else the PROFI_BDOSLOG_ENTRY-th entry
                bool match = true;
                if (const char* hex = std::getenv("PROFI_BDOSLOG_MATCH"))
                {
                    for (size_t k = 0; hex[2 * k] && hex[2 * k + 1] && match; k++)
                    {
                        const char pair[3] = {hex[2 * k], hex[2 * k + 1], 0};
                        match = z->DirectRead(static_cast<uint16_t>(0x100 + k)) == std::strtoul(pair, nullptr, 16);
                    }
                    if (match)
                        armed = true;
                }
                else if (--entries <= 0)
                    armed = true;
            }
            if (armed && z->pc == 5)
            {
                const uint16_t sp = z->sp;
                const uint16_t ret = static_cast<uint16_t>(z->DirectRead(sp) | (z->DirectRead(static_cast<uint16_t>(sp + 1)) << 8));
                const uint8_t function = z->c;
                const uint16_t de = z->de;
                for (long k = 0; k < 2000000 && z->pc != ret; k++)
                    _emulator->RunSingleCPUCycle(true);
                if (function == 0x48 && ret == 0x89B1)
                    continue;   // the PQ-DOS shell's idle poll
                std::cout << std::hex << "BDOS c=" << int(function) << " de=" << de << " from=" << ret << " -> a=" << int(z->a)
                          << " hl=" << z->hl << " de=" << z->de << std::dec << "\n";
                lines++;
                continue;
            }
            _emulator->RunSingleCPUCycle(true);
        }
    }
    if (const char* untilEnv = std::getenv("PROFI_UNTIL_PC"))
    {
        // Steps after the frames until PC = PROFI_UNTIL_PC (hex) for the PROFI_UNTIL_HIT-th time (default 1), at most
        // 50 million steps; the dumps below then show that moment (development aid)
        Z80* z = context->pCore->GetZ80();
        const uint16_t target = static_cast<uint16_t>(std::strtoul(untilEnv, nullptr, 16));
        const char* hitEnv = std::getenv("PROFI_UNTIL_HIT");
        int hits = hitEnv ? std::atoi(hitEnv) : 1;
        long steps = 0;
        for (; steps < 50000000L; steps++)
        {
            if (z->pc == target && --hits <= 0)
                break;
            _emulator->RunSingleCPUCycle(true);
        }
        std::cout << std::hex << "UNTIL pc=" << z->pc << " af=" << z->af << " bc=" << z->bc << " de=" << z->de
                  << " hl=" << z->hl << " 7ffd=" << int(context->emulatorState.p7FFD)
                  << " dffd=" << int(context->emulatorState.pDFFD) << std::dec << " steps=" << steps << "\n";
    }
    if (const char* traceEnv = std::getenv("PROFI_TRACE"))
    {
        // Instruction trace after the frames (development aid): PC, the two paging latches and SP per step, until
        // PROFI_TRACE steps or a HALT with interrupts off
        Z80* z = context->pCore->GetZ80();
        const int steps = std::atoi(traceEnv);
        for (int i = 0; i < steps; i++)
        {
            std::cout << std::hex << "T " << z->pc << " " << int(context->emulatorState.p7FFD) << " "
                      << int(context->emulatorState.pDFFD) << " sp=" << z->sp << " a=" << int(z->a) << std::dec << "\n";
            if (z->halted && !z->iff1)
                break;
            _emulator->RunSingleCPUCycle(true);
        }
    }
    if (const char* watchEnv = std::getenv("PROFI_WATCH"))
    {
        // Steps after the frames until a byte of the hi-res cell rectangle PROFI_WATCH_RECT ("col,row,w,h" in 8x8
        // cells) changes in the bitmap or attribute page; prints PC and registers per change (development aid)
        int col = 21, row = 3, w = 22, h = 4;
        if (const char* rect = std::getenv("PROFI_WATCH_RECT"))
            std::sscanf(rect, "%d,%d,%d,%d", &col, &row, &w, &h);
        Z80* z = context->pCore->GetZ80();
        Memory* memory = context->pMemory;
        std::vector<uint16_t> watchPages = {6, 0x3A};   // PROFI_WATCH_PAGES: another hex list, e.g. "4,38"
        if (const char* pagesEnv = std::getenv("PROFI_WATCH_PAGES"))
        {
            watchPages.clear();
            std::stringstream list(pagesEnv);
            std::string token;
            while (std::getline(list, token, ','))
                watchPages.push_back(static_cast<uint16_t>(std::strtoul(token.c_str(), nullptr, 16)));
        }
        std::vector<uint16_t> offsets;
        for (int v = row * 8; v < (row + h) * 8; v++)
            for (int c = col; c < col + w; c++)
                offsets.push_back(ProfiGeometry::ByteOffset(static_cast<uint32_t>(v), static_cast<uint32_t>(c)));
        auto snapshot = [&](std::vector<uint8_t>& out) {
            out.clear();
            for (uint16_t page : watchPages)
                for (uint16_t o : offsets)
                    out.push_back(memory->RAMPageAddress(page)[o]);
        };
        std::vector<uint8_t> before;
        std::vector<uint8_t> now;
        snapshot(before);
        const long steps = std::atol(watchEnv);
        int reported = 0;
        for (long i = 0; i < steps && reported < 40; i++)
        {
            const uint16_t pc = z->pc;
            _emulator->RunSingleCPUCycle(true);
            snapshot(now);
            if (now == before)
                continue;
            for (size_t k = 0; k < now.size(); k++)
                if (now[k] != before[k])
                {
                    std::cout << std::hex << "W step=" << std::dec << i << std::hex << " pc=" << pc
                              << " page=" << watchPages[k / offsets.size()] << " off=" << offsets[k % offsets.size()]
                              << " " << int(before[k]) << "->" << int(now[k]) << " hl=" << z->hl << " de=" << z->de
                              << " bc=" << z->bc << " 7ffd=" << int(context->emulatorState.p7FFD)
                              << " dffd=" << int(context->emulatorState.pDFFD) << std::dec << "\n";
                    break;
                }
            before = now;
            reported++;
        }
    }
    if (std::getenv("PROFI_DUMP"))
    {
        // Registers and the code around PC (development aid)
        Z80* z = context->pCore->GetZ80();
        std::cout << std::hex << "af=" << z->af << " bc=" << z->bc << " de=" << z->de << " hl=" << z->hl << " ix=" << z->ix
                  << " iy=" << z->iy << " sp=" << z->sp << " iff1=" << int(z->iff1) << " im=" << int(z->im) << std::dec << "\n";
        if (const WD1793* fdc = context->pBetaDisk)
            std::cout << std::hex << "fdc cmd=" << int(fdc->getCommandRegister()) << " trk=" << int(fdc->getTrackRegister())
                      << " sec=" << int(fdc->getSectorRegister()) << " status=" << int(fdc->getStatusRegister())
                      << " side=" << int(fdc->getSideUp()) << std::dec << "\n";
        if (WD1793* fdcw = context->pBetaDisk; fdcw && fdcw->getDrive())
            std::cout << "fdd physical track=" << int(fdcw->getDrive()->getTrack()) << " side=" << fdcw->getDrive()->getSide() << "\n";
        std::cout << "code";
        for (int i = -16; i < 48; i++)
            std::cout << " " << std::hex << int(z->DirectRead(static_cast<uint16_t>(z->pc + i)));
        std::cout << std::dec << "\n";
    }
    if (portTrace)
    {
        portTrace->stop();
        if (FILE* f = std::fopen(portTraceFile, "w"))
        {
            for (const PortTraceEvent& e : portTrace->getAll())
                std::fprintf(f, "F%u pc=%04X %s %04X %02X decoded=%04X %s dev=%u\n", e.frameNumber, e.pc,
                             e.isOut() ? "OUT" : "IN ", e.rawPort, e.value, e.decodedPort,
                             e.wasDecoded() ? "hit" : "miss", static_cast<unsigned>(e.deviceId));
            std::fclose(f);
        }
    }
    if (const char* pagesPrefix = std::getenv("PROFI_DUMP_PAGES"))
    {
        // The hi-res screen pages at the end (development aid): <prefix>-<page>.bin for 4, 6, 0x38, 0x3A
        for (uint16_t page : {uint16_t(4), uint16_t(6), uint16_t(0x38), uint16_t(0x3A)})
        {
            const std::string file = std::string(pagesPrefix) + "-" + std::to_string(page) + ".bin";
            if (FILE* f = std::fopen(file.c_str(), "wb"))
            {
                std::fwrite(context->pMemory->RAMPageAddress(page), 1, 0x4000, f);
                std::fclose(f);
            }
        }
    }
    if (const char* ramFile = std::getenv("PROFI_DUMP_RAM"))
    {
        // All RAM pages in page order at the end (development aid)
        if (FILE* f = std::fopen(ramFile, "wb"))
        {
            for (uint16_t page = 0; page <= context->pMemory->GetRamMask(); page++)
                std::fwrite(context->pMemory->RAMPageAddress(page), 1, 0x4000, f);
            std::fclose(f);
        }
    }
    if (const char* memFile = std::getenv("PROFI_DUMP_MEM"))
    {
        // The Z80's 64K view at the end (development aid)
        std::vector<uint8_t> mem(65536);
        for (uint32_t a = 0; a < 65536; a++)
            mem[a] = context->pCore->GetZ80()->DirectRead(static_cast<uint16_t>(a));
        FILE* f = std::fopen(memFile, "wb");
        if (f)
        {
            std::fwrite(mem.data(), 1, mem.size(), f);
            std::fclose(f);
        }
    }
    std::cout << "pc=" << std::hex << context->pCore->GetZ80()->pc << " p7FFD=" << int(context->emulatorState.p7FFD)
              << " pDFFD=" << int(context->emulatorState.pDFFD) << std::dec << " frame T=" << context->config.frame
              << " clock=" << context->emulatorState.current_z80_frequency << "\n";
}

/// region <PROFI-PLUS: ROM BIOS Plus 0.41h1 (docs/inprogress/2026-10-04-profi-plus/design.md, phase P4b)>

class ProfiPlusBoot_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;

    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
        // BIOS Plus's board test probes the AY: the test build fits none unless asked (software-zoo.md section 10)
        SoundCardScope sound(TestSound::TurboSound);
        _emulator = _manager->CreateEmulatorWithModel("profi-plus-boot", "PROFI-PLUS", LoggerLevel::LogError);
        if (!_emulator)
            GTEST_SKIP() << "PROFI-PLUS is not creatable (missing configs/profi or data/rom/profi/bios-plus-041h1.rom)";
    }

    void TearDown() override
    {
        if (_emulator)
        {
            const std::string id = _emulator->GetId();
            _emulator.reset();
            _manager->RemoveEmulator(id);
        }
    }
};

/// @brief The board test at power-on reports every device of the board "Ok" (no HDD image attached: the HDD lines
///        are not part of it). BIOS Plus 0.41h1 keeps the result in its system variable (IY + 2), one bit per failed
///        device, cleared when the device passes (SYS page #06A5..#06E4): bit 0 FDC, bit 1 parallel (8255), bits 2
///        and 3 serial (8253 + 8251, #2720), bit 4 RTC; bits 5 / 6 the sound chip (one of them clears: AY or YM),
///        bit 7 the hard disk. The screen prints "Ok" for the serial interface when bits 1 and 2 are clear (#0746).
///        Checked against a mutant without the COM port: (IY + 2) = #AC, serial failed and nothing else
TEST_F(ProfiPlusBoot_Test, BoardTestReportsEveryDeviceOk)
{
    // Slower than the 50 ms guideline on purpose: the real BIOS boots to its board-test screen
    EmulatorContext* context = _emulator->GetContext();
    _emulator->EnableTurboMode();
    _emulator->RunNFrames(300, true);

    const Z80* z80 = context->pCore->GetZ80();
    const uint16_t iy = z80->iy;
    const uint8_t result = context->pCore->GetZ80()->DirectRead(static_cast<uint16_t>(iy + 2));
    std::ostringstream where;
    where << std::hex << "pc=" << z80->pc << " iy=" << iy << " (iy+2)=" << int(result);
    ASSERT_EQ(iy, 0x4000) << "BIOS Plus's system variables: " << where.str();
    EXPECT_EQ(result & 0x01, 0) << "Floppy Disc Controller: " << where.str();
    EXPECT_EQ(result & 0x02, 0) << "Parallel interface: " << where.str();
    EXPECT_EQ(result & 0x0C, 0) << "Serial interface (8253 + 8251): " << where.str();
    EXPECT_EQ(result & 0x10, 0) << "RTC: " << where.str();
    EXPECT_NE(result & 0x60, 0x60) << "Sound Chip: " << where.str();
}

/// @brief The 8251 is the machine's own serial port: ComPort= plugs the peer into it through the network manager,
///        and the state report names it
TEST_F(ProfiPlusBoot_Test, ComPortPlugsIntoThe8251)
{
    EmulatorContext* context = _emulator->GetContext();
    NetworkManager::Change change;
    std::string error;
    ASSERT_TRUE(NetworkManager::ParseChange({{"com_port", "plug"}}, change, error)) << error;
    ASSERT_TRUE(context->pCore->GetNetworkManager()->RequestChange(change, error)) << error;

    ASSERT_NE(context->pMachineSerialPeer, nullptr);
    EXPECT_STREQ(context->pMachineSerialPeer->Kind(), "plug");
    EXPECT_EQ(context->pComPort, nullptr) << "no 16550 on #xxEF";
    auto* decoder = dynamic_cast<PortDecoder_Profi*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    EXPECT_EQ(decoder->GetUsart().Peer(), context->pMachineSerialPeer);

    const NetworkManager::Status st = context->pCore->GetNetworkManager()->GetStatus();
    EXPECT_EQ(st.serialPort, "profi-8251");
    EXPECT_TRUE(st.machineSerial.fitted);
    EXPECT_EQ(st.machineSerial.flavor, "usart8251");
    EXPECT_EQ(st.machineSerial.peer, "plug");
}

/// @brief TTD: the BIOS's COM setup is recorded and replays to the same 8253 / 8251 state after a seek back
TEST_F(ProfiPlusBoot_Test, TtdReplaysTheComPortExactly)
{
    // Slower than the 50 ms guideline on purpose: the BIOS programs the COM port during its boot
    EmulatorContext* context = _emulator->GetContext();
    FeatureManager* features = _emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;
    ASSERT_NE(ttd, nullptr);
    ASSERT_TRUE(ttd->StartRecording());
    _emulator->RunNFrames(2);
    const uint64_t before = context->emulatorState.frame_counter;
    _emulator->RunNFrames(150);
    const uint64_t end = context->emulatorState.frame_counter;
    auto* decoder = dynamic_cast<PortDecoder_Profi*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    const Pit8253::State pit = decoder->GetPit().GetState();
    const Usart8251::State usart = decoder->GetUsart().GetState();
    EXPECT_EQ(Pit8253::ModeOf(pit.counter[0]), 3) << "the BIOS set the baud divider";
    ttd->StopRecording();

    ASSERT_TRUE(ttd->SeekTo({before, 0}));
    _emulator->RunNFrames(static_cast<int>(end - before));
    ASSERT_EQ(context->emulatorState.frame_counter, end);
    EXPECT_EQ(std::memcmp(&decoder->GetPit().GetState(), &pit, sizeof(pit)), 0) << "the 8253 as recorded";
    EXPECT_EQ(std::memcmp(&decoder->GetUsart().GetState(), &usart, sizeof(usart)), 0) << "the 8251 as recorded";
}

/// @brief PQ-DOS from a floppy and from a hard disk (docs/inprogress/2026-10-04-profi-plus/TODO.md, phase P4c). The
///        disks are in testdata/machines/profi/pqdos (README there); the hard disk image is the 2 GB Karabas Pro one cut
///        to 2.9 MB by tools/machines/profi/pqdosimage. DOS Navigator draws its panels itself, so the checks look for
///        the strings the programs keep in memory once they are loaded
class ProfiPlusPqDos_Test : public ProfiPlusBoot_Test
{
protected:
    /// The 64K the Z80 sees holds `text`
    bool CpuMemoryHas(const std::string& text)
    {
        Z80* z80 = _emulator->GetContext()->pCore->GetZ80();
        std::string ram;
        ram.reserve(0x10000);
        for (uint32_t a = 0; a < 0x10000; a++)
            ram.push_back(static_cast<char>(z80->DirectRead(static_cast<uint16_t>(a))));
        return ram.find(text) != std::string::npos;
    }

    /// Run up to `frames` frames, 50 at a time, until the CPU memory holds `text`
    bool RunUntilText(const std::string& text, int frames)
    {
        for (int done = 0; done < frames; done += 50)
        {
            _emulator->RunNFrames(50, true);
            if (CpuMemoryHas(text))
                return true;
        }
        return false;
    }

    void TapEnter()
    {
        Keyboard* keyboard = _emulator->GetContext()->pKeyboard;
        keyboard->PressKey(ZXKEY_ENTER);
        _emulator->RunNFrames(6, true);
        keyboard->ReleaseKey(ZXKEY_ENTER);
        _emulator->RunNFrames(12, true);
    }
};

/// @brief The PQ-DOS floppy in drive A: ROM BIOS Plus boots it ("Insert bootable disk into floppy drive & press Enter"),
///        PQ-DOS shows its startup menu and, when the 30 s timeout picks the shell entry, runs DOS Navigator
TEST_F(ProfiPlusPqDos_Test, BootsFromTheFloppyToDosNavigator)
{
    // Slower than the 50 ms guideline on purpose: the BIOS and PQ-DOS boot from a floppy (about 2700 frames)
    _emulator->EnableTurboMode();
    _emulator->RunNFrames(300, true);   // the BIOS board test, then its boot prompt
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(TestPathHelper::GetTestDataPath("machines/profi/pqdos/pqdos1.fdi"), 0, &error)) << error;
    TapEnter();
    ASSERT_TRUE(RunUntilText("PQ-DOS Startup Menu", 2000)) << "PQ-DOS did not start, pc=" << std::hex
                                                           << _emulator->GetContext()->pCore->GetZ80()->pc;
    EXPECT_TRUE(RunUntilText("DOS Navigator", 3000)) << "the menu timeout did not start DOS Navigator, pc=" << std::hex
                                                     << _emulator->GetContext()->pCore->GetZ80()->pc;
}

/// @brief The cut-down PQ-DOS hard disk on ide0.master: the BIOS board test finds it, and PQ-DOS boots from it straight
///        into DOS Navigator on C:\ with no floppy (the hard disk is the BIOS's first boot choice)
TEST_F(ProfiPlusPqDos_Test, BootsFromTheHardDiskToDosNavigator)
{
    // Slower than the 50 ms guideline on purpose: the BIOS and PQ-DOS boot from a hard disk (about 1300 frames)
    EmulatorContext* context = _emulator->GetContext();
    MediaSource source;
    source.path = TestPathHelper::GetTestDataPath("machines/profi/pqdos/pqdos-hdd-small.img");
    InsertOptions options;
    options.immediate = true;
    ASSERT_TRUE(context->pMediaManager->Insert("ide0.master", source, options).Ok());
    _emulator->Reset();   // the BIOS looks for the disk at power-on

    _emulator->EnableTurboMode();
    EXPECT_TRUE(RunUntilText("DOS Navigator", 3000)) << "PQ-DOS did not reach DOS Navigator, pc=" << std::hex
                                                     << context->pCore->GetZ80()->pc;
    EXPECT_FALSE(CpuMemoryHas("PQ-DOS Startup Menu")) << "a floppy menu: the disk was not the boot device";
}

/// endregion </PROFI-PLUS>
