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
#include "_helpers/testpathhelper.h"
#include "loaders/disk/loader_trd.h"
#include "base/featuremanager.h"
#include "emulator/ports/portdiagrecorder.h"
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

/// CP/M console capture: every character CP/M prints goes through the ATM BIOS CONOUT vector #F809
/// (C = the character; `CPM-ATM dizasm` lea5a -> #F809). The step hook sees the next instruction's PC,
/// so PC == #F809 means CONOUT is about to run with its argument in C - at full emulation speed
class CpmConsoleHook : public IMachineStepHook
{
public:
    explicit CpmConsoleHook(Z80* z80) : _z80(z80) {}
    void OnMachineStep(uint32_t t) override
    {
        (void)t;
        if (_z80->pc == 0xF809 && _z80->pc != _lastPc)
            text += static_cast<char>(_z80->c & 0x7F);

        _lastPc = _z80->pc;
    }
    std::string text;

private:
    Z80* _z80;
    uint16_t _lastPc = 0;
};

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

    /// Type a CP/M command line and ENTER the way a user does: a key that did not echo is pressed again,
    /// a key that echoed the wrong character is erased (CAPS SHIFT + 0) and typed again. The ROM's
    /// keyboard handler sometimes turns a key into scan code + 1 - the next matrix row ("DIR" -> "DKR",
    /// "R" -> "4", " " -> "Z"). Probed: every row read returns the current matrix and the scan runs
    /// once per frame, so the slip is in the ROM's shift-state machine (#5F40, l141d..l14c3); whether
    /// the real board shows it with this key timing is open (docs/inprogress/2026-10-01-atm450/TODO.md)
    static constexpr int kTypeHold = 12;
    void TypeAtPrompt(const CpmConsoleHook& console, const std::string& line)
    {
        for (char c : line)
        {
            for (int tries = 0; tries < 6; tries++)
            {
                const size_t before = console.text.size();
                TapChar(c);
                EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return console.text.size() != before; }, 60, 1);
                if (console.text.size() == before)
                    continue;  // missed in a deaf phase: press again
                if (console.text.back() == c)
                    break;
                // Mistranslated (see kTypeHold): erase it like a user would and type it again
                const size_t echoed = console.text.size();
                Tap({ZXKEY_CAPS_SHIFT, ZXKEY_0}, kTypeHold);
                EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return console.text.size() != echoed; }, 60, 1);
            }
        }
        Tap({ZXKEY_ENTER}, kTypeHold);
    }

    void TapChar(char c)
    {
        if (c == ':')
            Tap({ZXKEY_SYM_SHIFT, ZXKEY_Z}, kTypeHold);
        else if (c == ' ')
            Tap({ZXKEY_SPACE}, kTypeHold);
        else if (c == 'H')
            Tap({ZXKEY_H}, kTypeHold);
        else if (c == 'I')
            Tap({ZXKEY_I}, kTypeHold);
        else if (c >= 'A' && c <= 'Z')
            Tap({static_cast<ZXKeysEnum>(0x41 + (c - 'A'))}, kTypeHold);
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

/// Menu entry 0: CP/M with the ATM CP/M system disk (testdata/machines/atm450/cpm/sys.trd, the
/// NedoPC "CP/M-SYSTEM" disk for ATM1 / ATM2 / ATM2+) in drive A. CCP and BDOS come from the ROM (the
/// decrypted loader); the BIOS signs on, runs the ROM's default autostart "B:XC /R" - XC is not on
/// this disk, so the CCP answers "B:XC?" - and leaves the A> prompt. DIR on the floppy then lists the
/// disk's catalog, read through the TR-DOS-session FDC path.
/// Slow by nature (~2 s): boots the real ROM and CP/M, then types at the prompt
TEST_F(ATM450Boot_Test, CpmBootsFromSystemDiskAndListsIt)
{
    LoaderTRD trd(_context, TestPathHelper::GetTestDataPath("machines/atm450/cpm/sys.trd"));
    ASSERT_TRUE(trd.loadImage());
    _context->coreState.diskDrives[0]->insertDisk(trd.getImage());

    Z80* z80 = _context->pCore->GetZ80();
    ASSERT_EQ(z80->GetMachineStepHook(), nullptr) << "ATM450 uses no step hook of its own";
    CpmConsoleHook console(z80);
    z80->SetMachineStepHook(&console);

    ASSERT_TRUE(WaitForBootMenu());
    ASSERT_EQ(MenuIndex(), 0);
    console.text.clear();  // the menu prints its items through the same vector
    auto prompt = [&] { return console.text.find("A>") != std::string::npos; };
    auto signedOn = [&] { return console.text.find("CP/M") != std::string::npos; };
    for (int tries = 0; tries < 4 && !signedOn(); tries++)
    {
        Tap({ZXKEY_ENTER});
        EmulatorTestHelper::RunUntil(_emulator.get(), signedOn, 150, 5);
    }
    EmulatorTestHelper::RunUntil(_emulator.get(), prompt, 300, 5);
    ASSERT_TRUE(prompt()) << "console: " << console.text;
    EXPECT_NE(console.text.find("CP/M  V2.2"), std::string::npos) << console.text;
    EXPECT_NE(console.text.find("BIOS  V1.03"), std::string::npos) << console.text;
    EXPECT_NE(console.text.find("B:XC?"), std::string::npos) << "the ROM's default autostart" << console.text;

    // B: is the floppy (A: is the BIOS's electronic disk, empty after a cold boot)
    size_t before = console.text.size();
    TypeAtPrompt(console, "DIR B:");
    auto backAtPrompt = [&] { return console.text.find("A>", before + 6) != std::string::npos; };
    EmulatorTestHelper::RunUntil(_emulator.get(), backAtPrompt, 600, 5);
    const std::string catalog = console.text.substr(before);
    ASSERT_TRUE(backAtPrompt()) << catalog;
    EXPECT_NE(catalog.find("DIR B:"), std::string::npos) << catalog;
    for (const char* file : {"STAT     COM", "PIP      COM", "SUBMIT   COM", "FORMAT   COM", "SYSGEN   COM"})
        EXPECT_NE(catalog.find(file), std::string::npos) << file << " missing from the catalog:\n" << catalog;

    before = console.text.size();
    TypeAtPrompt(console, "DIR A:");
    EmulatorTestHelper::RunUntil(_emulator.get(), backAtPrompt, 600, 5);
    EXPECT_NE(console.text.find("NO FILE", before), std::string::npos) << console.text.substr(before);

    z80->SetMachineStepHook(nullptr);
}

/// Menu entry 0: CP/M. Before starting it the system ROM measures the frame, samples the PAL marker
/// (#FE bit 7, from the protected PLM on the board) 16 times after HALT and decrypts its CP/M loader
/// with the result. With the right Z timing the loader runs from RAM in the CP/M user map (RAM at
/// #0000, aFE = #3E), opens a TR-DOS session and reads the system tracks of drive A. No disk is
/// inserted (the repository has no ATM 4.50 CP/M system disk), so the test stops at the disk access
TEST_F(ATM450Boot_Test, MenuCpmLoaderReachesTheDisk)
{
    ASSERT_TRUE(WaitForBootMenu());
    ASSERT_EQ(MenuIndex(), 0);

    // The port trace sees what reaches the WD1793: a command write to #1F that the decoder passed on
    ASSERT_TRUE(_emulator->GetFeatureManager()->setFeature(Features::kPortTrace, true));
    PortDiagnosticRecorder* recorder = _context->pPortDecoder->getPortTraceRecorder();
    ASSERT_NE(recorder, nullptr);
    recorder->start();
    auto fdcCommandSent = [&] {
        for (const PortTraceEvent& event : recorder->getAll())
        {
            if ((event.flags & PortTraceFlags::kDirectionOut) && (event.rawPort & 0x00FF) == 0x1F && event.wasDecoded())
                return true;
        }
        return false;
    };

    // ENTER starts CP/M; a press in the menu's deaf phase is retried like a user would
    for (int tries = 0; tries < 4 && !fdcCommandSent(); tries++)
    {
        Tap({ZXKEY_ENTER});
        EmulatorTestHelper::RunUntil(_emulator.get(), fdcCommandSent, 150, 5);
    }
    EXPECT_TRUE(fdcCommandSent()) << "the CP/M loader never commanded the FDC - wrong Z key, the loader stayed encrypted";
    recorder->stop();
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
