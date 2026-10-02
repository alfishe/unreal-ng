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

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <vector>

#include "3rdparty/lodepng/lodepng.h"
#include "emulator/video/screen.h"

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
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
    ASSERT_TRUE(_emulator->LoadDisk("testdata/loaders/trd/zx-format8.trd"));

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
///        PROFI_PROGRAM = a .tap / .tzx (Sinclair 48 + LOAD "") or a .trd (TR-DOS + RUN); PROFI_MODEL = PROFI / PROFI3;
///        PROFI_TURBO=1 presses the TURBO switch; PROFI_CONTENTION=0 turns the waits off; PROFI_FRAMES = frames to run after the load starts (default 1500);
///        PROFI_SHOTS = comma list of frame numbers to save as scratch/profi/<PROFI_NAME>-<frame>.png; PROFI_KEYS =
///        keys to tap after the load (TapKeys format), at frame PROFI_KEYS_AT (default: at once) and again every
///        PROFI_KEYS_EVERY frames. The emulated test programs of
///        docs/inprogress/2026-10-01-profi-v3-v5 (README of the materials' test-programs folder) run through this
TEST_F(ProfiBoot_Test, DISABLED_RunProgram)
{
    const char* program = std::getenv("PROFI_PROGRAM");
    if (!program)
        GTEST_SKIP() << "PROFI_PROGRAM not set";
    const char* model = std::getenv("PROFI_MODEL");
    if (model && std::string(model) != "PROFI")
    {
        const std::string id = _emulator->GetId();
        _emulator.reset();
        _manager->RemoveEmulator(id);
        _emulator = _manager->CreateEmulatorWithModel("profi-program", model, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << model;
    }
    EmulatorContext* context = _emulator->GetContext();
    if (const char* contention = std::getenv("PROFI_CONTENTION"); contention && contention[0] == '0')
        context->pFeatureManager->setFeature(Features::kContention, false);
    if (const char* turbo = std::getenv("PROFI_TURBO"); turbo && turbo[0] == '1')
        _emulator->SetFrontPanelSwitch(FrontPanelSwitch::Turbo, true);
    const std::string path(program);
    const bool v3 = model && std::string(model) == "PROFI3";   // the Kramis menu: Sinclair second, TR-DOS fifth
    const bool profi = !model || std::string(model).rfind("PROFI", 0) == 0;
    if (!profi)
    {
        // A reference machine (48K, 128K, ...): its ROM boots to BASIC; a .tap only
        _emulator->RunNFrames(150, true);
        std::string error;
        ASSERT_TRUE(_emulator->LoadTape(path, &error)) << error;
        TapKeys("ENT,J,SP,SP,ENT");   // leave the copyright screen / the 128 menu (Tape Loader), then LOAD ""
    }
    else
        _emulator->RunNFrames(600, true);   // the BIOS menu
    const bool disk = path.size() > 4 && (path.substr(path.size() - 4) == ".trd" || path.substr(path.size() - 4) == ".TRD");
    if (!profi)
    {
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
    for (int f = 1; f <= frames; f++)
    {
        _emulator->RunNFrames(1, true);
        if (keys && keysAt > 0 && (f == keysAt || (every > 0 && f > keysAt && (f - keysAt) % every == 0)))
            TapKeys(keys);
        if (std::find(shots.begin(), shots.end(), f) == shots.end() && f != frames)
            continue;
        FramebufferDescriptor& fb = context->pScreen->GetFramebufferDescriptor();
        const std::string file = "scratch/profi/" + name + "-" + std::to_string(f) + ".png";
        lodepng_encode32_file(file.c_str(), fb.memoryBuffer, fb.width, fb.height);
    }
    std::cout << "pc=" << std::hex << context->pCore->GetZ80()->pc << " p7FFD=" << int(context->emulatorState.p7FFD)
              << " pDFFD=" << int(context->emulatorState.pDFFD) << std::dec << " frame T=" << context->config.frame
              << " clock=" << context->emulatorState.current_z80_frequency << "\n";
}
