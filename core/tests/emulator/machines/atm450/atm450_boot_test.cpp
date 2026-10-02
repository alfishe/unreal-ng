// ATM Turbo 2 v4.50 boot tests on the shipped data/rom/atm1.rom (tdd-plan phase 3).
//
// After reset the 4.50 always starts in the system ROM (CPSYS set at reset, ATM manual appendix 2 /
// UnrealSpeccy z80.cpp). The system ROM loads the "Sinclair" palette, switches to the 640x200 hi-res
// mode (OUT #BE) and draws the boot menu: CP/M, TR-DOS 48, SPECTRUM 128, SPECTRUM 48. Each entry is
// driven like a user would: CAPS SHIFT + 6 moves the bar down, ENTER starts it.
//
// Boot-bound tests asserting on emulated state (latches, ROM page, VRAM decoded against the ROM font) -
// never on rendered pixels - so EnableTurboMode() is safe. The menu boot takes ~150 frames.

#include <emulator/cpu/core.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/keyboard/keyboard.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <emulator/video/screen.h>
#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/ports/models/portdecoder_atm450.h"
#include "pch.h"
#include "stdafx.h"

namespace
{
    constexpr uint16_t kRomSys = 0;
    constexpr uint16_t kRomDos = 1;
    constexpr uint16_t kRom128 = 2;
    constexpr uint16_t kRomSos = 3;
}  // namespace

class ATM450Boot_Test : public ::testing::Test
{
protected:
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("", "ATM450", LoggerLevel::LogError);
        ASSERT_TRUE(_emulator);
        _context = _emulator->GetContext();
        _emulator->EnableTurboMode();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetId());
    }

    /// Run until the system ROM shows its menu: hi-res mode selected (aFE = #BE) and the bar loop reached
    bool WaitForBootMenu()
    {
        EmulatorState& state = _context->emulatorState;
        auto menuUp = [&] { return state.aFE == 0xBE && state.atmPalette[0] != 0xFF000000u; };
        EmulatorTestHelper::RunUntil(_emulator.get(), menuUp, 300);
        if (!menuUp())
            return false;
        _emulator->RunNFrames(30, true);  // the menu is drawn and polls the keyboard
        return true;
    }

    void Tap(std::initializer_list<ZXKeysEnum> keys, int holdFrames = 6)
    {
        Keyboard* keyboard = _context->pKeyboard;
        for (ZXKeysEnum key : keys)
            keyboard->PressKey(key);
        _emulator->RunNFrames(holdFrames, true);
        for (ZXKeysEnum key : keys)
            keyboard->ReleaseKey(key);
        _emulator->RunNFrames(holdFrames, true);
    }

    /// The system ROM keeps the menu bar index in RAM page 0, offset #01DD (found by RAM diff of
    /// successive CAPS SHIFT + 6 presses). Its menu loop alternates between two phases every ~50 frames
    /// and one of them does not scan the keyboard, so a press can be missed exactly as on the real
    /// machine - drive it like a user watching the bar: press again until the bar has moved
    uint8_t MenuIndex() const { return _context->pMemory->RAMPageAddress(0)[0x01DD]; }

    bool SelectMenuItem(uint8_t index)
    {
        for (int tries = 0; MenuIndex() < index && tries < 12; tries++)
        {
            const uint8_t before = MenuIndex();
            Tap({ZXKEY_CAPS_SHIFT, ZXKEY_6});
            EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return MenuIndex() != before; }, 60, 1);
        }
        if (MenuIndex() != index)
            return false;

        // ENTER leaves the hi-res menu (every entry but CP/M switches to the ZX screen)
        for (int tries = 0; _context->emulatorState.aFE == 0xBE && tries < 4; tries++)
        {
            Tap({ZXKEY_ENTER});
            EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return _context->emulatorState.aFE != 0xBE; }, 60, 1);
        }
        return _context->emulatorState.aFE != 0xBE;
    }

    /// Decode the standard ZX screen of RAM page 5 / 7 to ASCII against the 48K ROM font (atm1.rom page 3)
    std::string DecodeZXScreen()
    {
        Memory* memory = _context->pMemory;
        const uint8_t page = (_context->emulatorState.p7FFD & 0x08) ? 7 : 5;
        const uint8_t* vram = memory->RAMPageAddress(page);
        const uint8_t* font = memory->ROMPageHostAddress(kRomSos) + 0x3D00;

        std::string result;
        for (uint8_t row = 0; row < 24; row++)
        {
            for (uint8_t col = 0; col < 32; col++)
            {
                uint8_t glyph[8];
                for (int k = 0; k < 8; k++)
                {
                    const uint16_t y = static_cast<uint16_t>(row * 8 + k);
                    const uint16_t addr = static_cast<uint16_t>(((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2));
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
                if (best != ' ')
                    result += best;
            }
            result += '\n';
        }
        return result;
    }

    bool RunUntilScreenShows(const std::string& text, int maxFrames)
    {
        auto shows = [&] { return DecodeZXScreen().find(text) != std::string::npos; };
        EmulatorTestHelper::RunUntil(_emulator.get(), shows, maxFrames);
        return shows();
    }
};

/// T3.3: reset (RESET=128) starts in the system ROM, which loads the Sinclair palette into the
/// --grbGRB palette RAM and draws its menu in the 640x200 hi-res mode
TEST_F(ATM450Boot_Test, SystemRomBootMenu)
{
    EmulatorState& state = _context->emulatorState;
    EXPECT_EQ(_context->pMemory->GetROMPage(), kRomSys) << "CPSYS is set at reset";

    // The Sinclair palette arrives first (before the menu's own colors replace it): blue (cell 1) is
    // the high blue line alone, bright blue (cell 9) both lines - the empirical pin of the ATM1 layout
    auto paletteWritten = [&] { return state.atmPalette[1] != 0xFFC72200u; };
    EmulatorTestHelper::RunUntil(_emulator.get(), paletteWritten, 100, 1);
    ASSERT_TRUE(paletteWritten()) << "the system ROM never wrote the palette";
    EXPECT_EQ(state.atmPalette[1], 0xFFAA0000u) << "blue = #0000AA";
    EXPECT_EQ(state.atmPalette[9], 0xFFFF0000u) << "bright blue = #0000FF";
    EXPECT_EQ(state.atmPalette[0], 0xFF000000u) << "black";
    EXPECT_EQ(state.atmPalette[15], 0xFFFFFFFFu) << "bright white";

    ASSERT_TRUE(WaitForBootMenu());
    EXPECT_EQ(state.aFE, 0xBE) << "ROM at #0000, mode RG0 = 0 / RG1 = 1: 640x200";
    EXPECT_EQ(_context->pScreen->_vid.mode, M_ATMHR);
    EXPECT_EQ(state.aFB & 0x80, 0x80) << "CPSYS";
    EXPECT_EQ(_context->pMemory->GetROMPage(), kRomSys);
}

/// Menu entry 1: TR-DOS 48 - the TR-DOS 5.03 banner on the ZX screen, dos ROM at #0000
TEST_F(ATM450Boot_Test, MenuTrdos48)
{
    ASSERT_TRUE(WaitForBootMenu());
    ASSERT_TRUE(SelectMenuItem(1));

    ASSERT_TRUE(RunUntilScreenShows("TR-DOS", 300)) << DecodeZXScreen();
    EXPECT_NE(DecodeZXScreen().find("TechnologyResearch"), std::string::npos) << DecodeZXScreen();

    EmulatorState& state = _context->emulatorState;
    EXPECT_EQ(state.aFE & 0x60, 0x60) << "ZX screen mode";
    EXPECT_EQ(state.aFB & 0x80, 0x00) << "CPSYS off";
    EXPECT_EQ(_context->pScreen->_vid.mode, M_ZX48);
}

/// Menu entry 2: SPECTRUM 128 - the 128K menu, 128 ROM at #0000
TEST_F(ATM450Boot_Test, MenuSpectrum128)
{
    ASSERT_TRUE(WaitForBootMenu());
    ASSERT_TRUE(SelectMenuItem(2));

    ASSERT_TRUE(RunUntilScreenShows("1986SinclairResearch", 300)) << DecodeZXScreen();
    EXPECT_NE(DecodeZXScreen().find("128BASIC"), std::string::npos) << DecodeZXScreen();
    EXPECT_EQ(_context->pMemory->GetROMPage(), kRom128);
}

/// Menu entry 3: SPECTRUM 48 - the 48K copyright, 48 ROM at #0000
TEST_F(ATM450Boot_Test, MenuSpectrum48)
{
    ASSERT_TRUE(WaitForBootMenu());
    ASSERT_TRUE(SelectMenuItem(3));

    ASSERT_TRUE(RunUntilScreenShows("1982SinclairResearch", 300)) << DecodeZXScreen();
    EXPECT_EQ(_context->pMemory->GetROMPage(), kRomSos);
}

/// RESET=DOS (RM_DOS): straight into TR-DOS - aFE = #E0 (ROM, ZX screen), CPSYS off, 7FFD.4 kept
TEST_F(ATM450Boot_Test, ResetIntoTrdos)
{
    _context->pCore->Reset(RM_DOS);
    EmulatorState& state = _context->emulatorState;
    EXPECT_EQ(state.aFE, 0xE0);
    EXPECT_EQ(state.aFB, 0x00);
    EXPECT_EQ(state.p7FFD & 0x10, 0x10);
    EXPECT_EQ(_context->pMemory->GetROMPage(), kRomDos);

    ASSERT_TRUE(RunUntilScreenShows("TR-DOS", 300)) << DecodeZXScreen();
}
