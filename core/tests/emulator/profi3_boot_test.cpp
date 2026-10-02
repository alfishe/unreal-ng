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
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "3rdparty/lodepng/lodepng.h"
#include "emulator/video/screen.h"

#include "_helpers/emulatortesthelper.h"
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
            GTEST_SKIP() << "PROFI3 is not creatable (missing configs/profi3 or data/rom/profi/kramis-v02.rom)";
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
    _emulator->EnableTurboMode();  // asserts on emulated state (VRAM attributes) only

    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return KramisMenuOnScreen(context); }, 600);
    ASSERT_TRUE(KramisMenuOnScreen(context)) << "the Kramis BIOS menu did not appear";
    EXPECT_EQ(context->emulatorState.pDFFD & 0x80, 0) << "the v3 BIOS does not use hi-res";
    EXPECT_EQ(context->pScreen->GetVideoMode(), M_PROFI);
}

/// @brief The other factory v3 image ("ТОО Фирма ПРОФИ" BIOS V0.3 + TR-DOS 5.04T) reaches its menu too
TEST_F(Profi3Boot_Test, KramisV03ReachesItsMenu)
{
    EmulatorContext* context = _emulator->GetContext();
    ASSERT_TRUE(_emulator->LoadROM("rom/profi/kramis-v03.rom"));
    _emulator->Reset(true);
    _emulator->EnableTurboMode();

    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return KramisMenuOnScreen(context); }, 600);
    EXPECT_TRUE(KramisMenuOnScreen(context)) << "the V0.3 BIOS menu did not appear";
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
