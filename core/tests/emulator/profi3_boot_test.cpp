// Profi v3 (model PROFI3) boot tests with the factory ROMs in data/rom/profi/.
//
// Guards the v3 chain: the PROFI3 model is creatable (configs/profi3), it resets into the SYS ROM with the DOS latch
// on like v5, and the factory Kramis BIOS runs to its menu on the v3 board, which has no palette and no extended
// ports. Skipped when the ROM image is not deployed.
//
// Design: docs/inprogress/2026-10-01-profi-v3-v5 (design.md section 2, tdd-plan.md section 1)

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/keyboard/keyboard.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include "3rdparty/lodepng/lodepng.h"
#include "emulator/video/screen.h"

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "pch.h"
#include "stdafx.h"

class Profi3Boot_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;

    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);

        _emulator = _manager->CreateEmulatorWithModelAndRAM("profi3-boot", "PROFI3", 512, LoggerLevel::LogError);
        if (!_emulator)
            GTEST_SKIP() << "PROFI3 is not creatable (missing configs/profi3 or data/rom/profi/kramis-v03.rom)";
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

    /// Tap keys on the ZX matrix: a comma list where one character is that key, ENT is Enter and Cn is Caps Shift + n
    /// (Cn with 6 / 7 = cursor down / up). Each key is held 6 frames and released for 12
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

    /// Text of the standard ZX screen (RAM page `screenPage`) decoded with the 48K ROM font (ROM page 3, #3D00);
    /// '?' for a cell that is not a font glyph
    static std::string DecodeScreen(EmulatorContext* context, uint8_t screenPage)
    {
        Memory* memory = context->pMemory;
        const uint8_t* vram = memory->RAMPageAddress(screenPage);
        const uint8_t* font = memory->ROMPageHostAddress(3) + 0x3D00;
        std::string result;
        for (uint8_t row = 0; row < 24; row++)
        {
            for (uint8_t col = 0; col < 32; col++)
            {
                uint8_t glyph[8];
                for (int k = 0; k < 8; k++)
                {
                    const uint16_t y = row * 8 + k;
                    glyph[k] = vram[(((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2)) + col];
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

/// @brief Power-on state: the v3 board, 512K, SYS ROM paged in, DOS latch on, latches clear, the v3 frame
TEST_F(Profi3Boot_Test, ResetStateIsSysRomOnTheV3Board)
{
    EmulatorContext* context = _emulator->GetContext();
    ASSERT_EQ(context->config.mem_model, MM_PROFI3);
    EXPECT_EQ(context->config.ramsize, 512u);

    EXPECT_EQ(context->emulatorState.p7FFD, 0x00);
    EXPECT_EQ(context->emulatorState.pDFFD, 0x00);
    EXPECT_NE(context->emulatorState.flags & CF_TRDOS, 0) << "DOS latch is on after reset";
    EXPECT_EQ(context->pMemory->GetROMPage(), 0) << "SYS ROM (page 0) at #0000";

    EXPECT_EQ(context->config.frame, 69888u) << "the 3.2 board's sync PROM (0A1DFAFD)";
    EXPECT_EQ(context->config.intstart, 3571u) << "INT 12580 T before the paper";
}

/// Paper colour of attribute cell (row, col) of the standard screen (RAM page 5)
static uint8_t Paper(EmulatorContext* context, int row, int col)
{
    return static_cast<uint8_t>((context->pMemory->RAMPageAddress(5)[0x1800 + row * 32 + col] >> 3) & 0x07);
}

/// The v3 BIOS menu as its attributes draw it: a blue screen, a white menu box at rows 10-17, columns 9-22, and the
/// selected item highlighted on row 11 (yellow on V0.2, blue on V0.3). The BIOS uses its own font, so no text is
/// decoded
static bool KramisMenuOnScreen(EmulatorContext* context)
{
    const uint8_t highlight = Paper(context, 11, 10);
    if (highlight == 7 || Paper(context, 11, 21) != highlight)
        return false;  // highlight bar
    for (int col = 9; col <= 22; col++)
    {
        if (Paper(context, 10, col) != 7 || Paper(context, 17, col) != 7)
            return false;  // box top and bottom
    }
    for (int row = 10; row <= 17; row++)
    {
        if (Paper(context, row, 9) != 7 || Paper(context, row, 22) != 7)
            return false;  // box sides
        if (Paper(context, row, 0) != 1 || Paper(context, row, 31) != 1)
            return false;  // blue screen around it
    }
    return true;
}

/// @brief The factory v3 firmware (JV "KRAMIS" BIOS V0.2 + TR-DOS 5.03) runs to its menu on the v3 board: standard
///        screen, no hi-res, still the SYS session's code. Slower than 50 ms on purpose: it boots a real BIOS
TEST_F(Profi3Boot_Test, KramisV02ReachesItsMenu)
{
    EmulatorContext* context = _emulator->GetContext();
    ASSERT_TRUE(_emulator->LoadROM("rom\\profi\\kramis-v02.rom"));
    _emulator->Reset(true);
    _emulator->EnableTurboMode();  // asserts on emulated state (VRAM attributes) only

    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return KramisMenuOnScreen(context); }, 600);
    ASSERT_TRUE(KramisMenuOnScreen(context)) << "the Kramis BIOS menu did not appear";
    EXPECT_EQ(context->emulatorState.pDFFD & 0x80, 0) << "the v3 BIOS does not use hi-res";
    EXPECT_EQ(context->pScreen->GetVideoMode(), M_PROFI);
}

/// @brief The default v3 image ("ТОО Фирма ПРОФИ" BIOS V0.3 + TR-DOS 5.04T, configs/profi3) reaches its menu
TEST_F(Profi3Boot_Test, KramisV03ReachesItsMenu)
{
    EmulatorContext* context = _emulator->GetContext();
    EXPECT_NE(std::string(context->config.profi3_rom_path).find("kramis-v03.rom"), std::string::npos)
        << "the PROFI3 default ROM: " << context->config.profi3_rom_path;
    _emulator->EnableTurboMode();

    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return KramisMenuOnScreen(context); }, 600);
    EXPECT_TRUE(KramisMenuOnScreen(context)) << "the V0.3 BIOS menu did not appear";
}

/// @brief The menu's "Sinclair" entry starts the 128 machine: its own boot menu (the Pentagon 128 editor) appears
TEST_F(Profi3Boot_Test, KramisMenuSinclairStartsThe128Menu)
{
    EmulatorContext* context = _emulator->GetContext();
    ASSERT_TRUE(_emulator->LoadROM("rom\\profi\\kramis-v02.rom"));   // the V0.2 menu and its TR-DOS 5.03
    _emulator->Reset(true);
    _emulator->EnableTurboMode();
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return KramisMenuOnScreen(context); }, 600);
    ASSERT_TRUE(KramisMenuOnScreen(context));
    _emulator->RunNFrames(25, true);

    TapKeys("C6,ENT");                  // second entry: Sinclair
    auto shown = [&] { return DecodeScreen(context, (context->emulatorState.p7FFD & 0x08) ? 7 : 5); };
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return shown().find("48 BASIC") != std::string::npos; }, 300);
    const std::string screen = shown();
    EXPECT_NE(screen.find("Tape Loader"), std::string::npos) << screen;
    EXPECT_NE(screen.find("48 BASIC"), std::string::npos) << screen;
    EXPECT_EQ(context->emulatorState.flags & CF_TRDOS, 0) << "the SYS session is over";
}

/// @brief The menu's "TR-DOS" entry starts the board's TR-DOS 5.03 at its A> prompt
TEST_F(Profi3Boot_Test, KramisMenuTrDosStartsTrDos503)
{
    EmulatorContext* context = _emulator->GetContext();
    ASSERT_TRUE(_emulator->LoadROM("rom\\profi\\kramis-v02.rom"));   // the V0.2 menu and its TR-DOS 5.03
    _emulator->Reset(true);
    _emulator->EnableTurboMode();
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return KramisMenuOnScreen(context); }, 600);
    ASSERT_TRUE(KramisMenuOnScreen(context));
    _emulator->RunNFrames(25, true);

    TapKeys("C6,C6,C6,C6,ENT");         // fifth entry: TR-DOS
    EmulatorTestHelper::RunUntil(_emulator.get(),
        [&] { return DecodeScreen(context, 5).find("A>") != std::string::npos; }, 400);
    const std::string screen = DecodeScreen(context, 5);
    EXPECT_NE(screen.find("TR-DOS Ver 5.03"), std::string::npos) << screen;
    EXPECT_NE(screen.find("A>"), std::string::npos) << screen;
}

/// @brief Klug CP/M 2.3 (testdata/machines/profi/cpm/v3) boots on a v3 from the Kramis V0.3 menu's "Profi-DOS"
///        entry to its sign-on and the "RAM Disk E: format?" question. Its boot sector reads the system through the
///        TR-DOS ROM and needs TR-DOS 5.04T (Kramis V0.3): with V0.2's TR-DOS 5.03 the reads land on the wrong
///        cylinders (5.03 switches to double stepping on this 5 x 1024-byte disk) - on any board, also with V0.2 on a
///        v5 - while BIOS 2.0's TR-DOS 6.08 boots it on a v3 board too (traced, docs/inprogress/2026-10-01-profi-v3-v5)
TEST_F(Profi3Boot_Test, KlugCpmBootsFromKramisV03)
{
    // Slower than the 50 ms guideline on purpose: the BIOS and CP/M boot from a floppy. V0.3 is the PROFI3 default
    EmulatorContext* context = _emulator->GetContext();
    _emulator->RunNFrames(600, true);   // the Kramis menu, settled
    ASSERT_TRUE(KramisMenuOnScreen(context));
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(TestPathHelper::GetTestDataPath("machines/profi/cpm/v3/klug-cpm-2.3.td0"), 0, &error))
        << error;
    TapKeys("ENT");   // the first entry: Profi-DOS
    _emulator->RunNFrames(1500, true);

    // Klug prints with its own 4-pixel font (64 columns), so the screen is checked by ink, the system by its sign-on
    // in RAM: the system tracks loaded, and the sign-on and the configuration report were drawn
    // All 512K: where the system sits depends on the BIOS's own paging
    const uint8_t* ramBase = context->pMemory->RAMPageAddress(0);
    const std::string ram(reinterpret_cast<const char*>(ramBase), 512u * 1024u);
    EXPECT_NE(ram.find("CP/M BIOS Ver 2.3"), std::string::npos) << "the system tracks were not loaded";
    const uint8_t* screen = context->pMemory->RAMPageAddress((context->emulatorState.p7FFD & 0x08) ? 7 : 5);
    uint32_t inkLines = 0;
    for (uint32_t y = 0; y < 192; y++)
    {
        const uint32_t row = ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2);
        bool any = false;
        for (uint32_t x = 0; x < 32 && !any; x++)
            any = screen[row + x] != 0;
        inkLines += any ? 1u : 0u;
    }
    EXPECT_GE(inkLines, 40u) << "the sign-on and the configuration report";
}

/// @brief The SP-DOS system disk (testdata/machines/profi/cpm/sp-dos) boots on the v3 from the Kramis menu's
///        "Profi-DOS" entry to "SP-DOS Shell by Michael Markowsky" (hi-res, CPU at 3 MHz)
TEST_F(Profi3Boot_Test, SpDosBootsToItsShell)
{
    // Slower than the 50 ms guideline on purpose: the BIOS and SP-DOS boot from a floppy
    EmulatorContext* context = _emulator->GetContext();
    _emulator->RunNFrames(600, true);   // the Kramis menu, settled
    ASSERT_TRUE(KramisMenuOnScreen(context));
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(TestPathHelper::GetTestDataPath("machines/profi/cpm/sp-dos/unicopy-sp-dos.td0"), 0,
                                    &error))
        << error;
    TapKeys("ENT");   // the first entry: Profi-DOS
    _emulator->RunNFrames(1500, true);

    std::string ram;
    for (uint32_t a = 0; a < 0x10000; a++)
        ram.push_back(static_cast<char>(context->pCore->GetZ80()->DirectRead(static_cast<uint16_t>(a))));
    EXPECT_NE(ram.find("SP-DOS Shell by Michael Markowsky"), std::string::npos)
        << "the shell did not load, pc=" << std::hex << context->pCore->GetZ80()->pc;
    EXPECT_NE(context->emulatorState.pDFFD & 0x80, 0) << "hi-res";
}

/// @brief Development probe (disabled): boot the Kramis BIOS and print what it leaves on the screen
TEST_F(Profi3Boot_Test, DISABLED_ProbeKramisBios)
{
    EmulatorContext* context = _emulator->GetContext();
    if (const char* rom = std::getenv("PROFI3_PROBE_ROM"))
    {
        ASSERT_TRUE(_emulator->LoadROM(rom));
        _emulator->Reset(true);
    }
    _emulator->EnableTurboMode();
    _emulator->RunNFrames(300, true);
    std::cout << "pc=" << std::hex << context->pCore->GetZ80()->pc << " p7FFD=" << int(context->emulatorState.p7FFD)
              << " pDFFD=" << int(context->emulatorState.pDFFD) << " rom=" << int(context->pMemory->GetROMPage())
              << std::dec << " mode=" << Screen::GetVideoModeName(context->pScreen->GetVideoMode()) << "\n";
    std::cout << DecodeScreen(context, (context->emulatorState.p7FFD & 0x08) ? 7 : 5);
    for (int row = 0; row < 24; row++)
    {
        for (int col = 0; col < 32; col++)
            std::cout << int(Paper(context, row, col));
        std::cout << "\n";
    }
    FramebufferDescriptor& fb = context->pScreen->GetFramebufferDescriptor();
    lodepng_encode32_file("scratch/profi3-probe.png", fb.memoryBuffer, fb.width, fb.height);
}

/// @brief Development probe (disabled): boot to the menu, tap the keys in PROFI3_PROBE_KEYS (comma list: one
///        character = that key, ENT = Enter, Cn = Caps Shift + n), then dump the screen to scratch/profi3-keys.png
TEST_F(Profi3Boot_Test, DISABLED_ProbeMenuKeys)
{
    EmulatorContext* context = _emulator->GetContext();
    if (const char* rom = std::getenv("PROFI3_PROBE_ROM"))
    {
        ASSERT_TRUE(_emulator->LoadROM(rom));
        _emulator->Reset(true);
    }
    _emulator->EnableTurboMode();
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return KramisMenuOnScreen(context); }, 600);
    _emulator->RunNFrames(25, true);

    const char* keys = std::getenv("PROFI3_PROBE_KEYS");
    TapKeys(keys ? keys : "");
    _emulator->RunNFrames(250, true);
    std::cout << "pc=" << std::hex << context->pCore->GetZ80()->pc << " p7FFD=" << int(context->emulatorState.p7FFD)
              << " pDFFD=" << int(context->emulatorState.pDFFD) << " rom=" << int(context->pMemory->GetROMPage())
              << " flags=" << int(context->emulatorState.flags) << std::dec << "\n";
    std::cout << DecodeScreen(context, (context->emulatorState.p7FFD & 0x08) ? 7 : 5);
    FramebufferDescriptor& fb = context->pScreen->GetFramebufferDescriptor();
    lodepng_encode32_file("scratch/profi3-keys.png", fb.memoryBuffer, fb.width, fb.height);
}
