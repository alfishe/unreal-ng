// ATM710 TR-DOS entry boot regression tests.
//
// Guards PortDecoder_ATM710::IsDosPortsEnabled() (the DOSEN || SYSEN
// memory-manager port gate) and the absence of an xxF7 readback. With the
// old PEN-inclusive gate plus a fabricated FFF7 readback, the stock TR-DOS
// 5.04T $3D38 hardware probe (LDIR'd to RAM $5C92: OUT (F7),0 / IN A,(F7) /
// CP 1E / CP 1F) PASSED, so selecting "TR-DOS" from the 128K menu took the
// RST 08 sys-BIOS launcher: the ATM BIOS menu reappeared instead of TR-DOS.
// On every ZX Spectrum this entry boots classic TR-DOS: the probe must see
// the floating bus and fail, the $3D2F chain re-arms the TR-DOS session
// from ROM, and TR-DOS 5.03 BASIC comes up with its banner on the standard
// ZX screen.
//
// Verified flow (Phase9 of the investigation): BIOS boot menu ->
// SPECTRUM 128 (2x CS+6 + ENTER) -> 128K menu "TR-DOS" (4x CS+6 + ENTER).
// Settled state matches the ZXMAK2 boot.szx reference snapshot byte for
// byte: aFF77 = 0xFF77, pFF77 = 0x00AB.

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/keyboard/keyboard.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <gtest/gtest.h>

#include "_helpers/test_path_helper.h"
#include "loaders/disk/loader_trd.h"
#include "pch.h"
#include "stdafx.h"

class ATM710TrdosBoot_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;

protected:
    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
        auto emulatorIds = _manager->GetEmulatorIds();
        for (const auto& id : emulatorIds)
        {
            _manager->RemoveEmulator(id);
        }
    }

    void TearDown() override
    {
        auto emulatorIds = _manager->GetEmulatorIds();
        for (const auto& id : emulatorIds)
        {
            _manager->RemoveEmulator(id);
        }
    }

    /// Boot through the ATM BIOS menu into the SPECTRUM 128 boot menu
    /// (2x "down" = CAPS SHIFT + 6, then ENTER)
    std::shared_ptr<Emulator> Enter128Menu(const std::string& id)
    {
        auto emulator = _manager->CreateEmulatorWithModelAndRAM(id, "ATM710", 1024, LoggerLevel::LogError);
        EXPECT_NE(emulator, nullptr);
        if (!emulator)
            return emulator;
        emulator->RunNFrames(300, true);

        Keyboard* keyboard = emulator->GetContext()->pKeyboard;
        for (int i = 0; i < 2; i++)
        {
            PressDown(emulator, 4);
        }
        keyboard->PressKey(ZXKEY_ENTER);
        emulator->RunNFrames(12, true);
        keyboard->ReleaseKey(ZXKEY_ENTER);
        emulator->RunNFrames(250, true);
        return emulator;
    }

    /// One menu "down" press: CAPS SHIFT + 6 held together
    static void PressDown(const std::shared_ptr<Emulator>& emulator, int holdFrames = 8)
    {
        Keyboard* keyboard = emulator->GetContext()->pKeyboard;
        keyboard->PressKey(ZXKEY_CAPS_SHIFT);
        keyboard->PressKey(ZXKEY_6);
        emulator->RunNFrames(holdFrames, true);
        keyboard->ReleaseKey(ZXKEY_6);
        keyboard->ReleaseKey(ZXKEY_CAPS_SHIFT);
        emulator->RunNFrames(holdFrames, true);
    }

    /// Decode rows of the standard ZX bitmap screen on a RAM page to ASCII by
    /// matching each 8-byte cell against the classic font (ROM page 0, $3D00).
    /// Unmatched glyphs decode as '?' (same as the investigation tooling).
    static std::string DecodeZXRows(EmulatorContext* context, uint8_t screenPage, uint8_t rowFrom, uint8_t rowTo)
    {
        Memory* memory = context->pMemory;
        const uint8_t* vram = memory->RAMPageAddress(screenPage);
        const uint8_t* font = memory->ROMPageHostAddress(0) + 0x3D00;
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
                    if (memcmp(glyph, font + (uint32_t)(c - 0x20) * 8, 8) == 0)
                    {
                        best = (char)c;
                        break;
                    }
                }
                result += best;
            }
            result += '\n';
        }
        return result;
    }

    /// Decode rows of the M_ATMTX 80-column text screen (the ATM BIOS video
    /// mode): chars1 = vp[0x1C0 + 64r + n/2], chars0 = vp[0x2000 + same]
    static std::string DecodeTextRows(EmulatorContext* context, uint8_t rowFrom, uint8_t rowTo)
    {
        EmulatorState& state = context->emulatorState;
        Memory* memory = context->pMemory;
        uint8_t videoPage = (state.p7FFD & 0x08) ? 7 : 5;
        const uint8_t* vp = memory->RAMPageAddress(videoPage);
        std::string result;
        for (uint32_t row = rowFrom; row < rowTo; row++)
        {
            for (uint32_t n = 0; n < 80; n++)
            {
                uint32_t byteIdx = 0x1C0 + 64 * row + n / 2;
                bool fromP0 = (n % 2 == 0);
                uint8_t code = fromP0 ? vp[byteIdx] : vp[0x2000 + byteIdx];
                result += (code >= 0x20 && code < 0x7F) ? (char)code : (code == 0 ? ' ' : '.');
            }
            result += '\n';
        }
        return result;
    }

    /// The BIOS menu renders double-width cells ("C  P  /  M") - compare
    /// against space-stripped text
    static std::string StripSpaces(const std::string& text)
    {
        std::string result;
        for (char c : text)
        {
            if (c != ' ')
                result += c;
        }
        return result;
    }
};

TEST_F(ATM710TrdosBoot_Test, FreshBootShowsATMBIOSMenu)
{
    // Machine boot to the ATM BIOS menu is the expected cold-start behavior:
    // the v7.10 BIOS takes over in text video mode 6 with CPM set and shows
    // its launcher menu before any mode is selected
    auto emulator = _manager->CreateEmulatorWithModelAndRAM("atm710-bios-boot", "ATM710", 1024, LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    emulator->RunNFrames(300, true);

    EmulatorContext* context = emulator->GetContext();
    EmulatorState& state = context->emulatorState;

    EXPECT_EQ(state.pFF77 & 7, 6) << "BIOS menu must run in the ATM text video mode";
    std::string screen = StripSpaces(DecodeTextRows(context, 0, 24));
    EXPECT_NE(screen.find("CP/M"), std::string::npos) << "BIOS menu text:\n" << screen;
    EXPECT_NE(screen.find("SPECTRUM128"), std::string::npos) << "BIOS menu text:\n" << screen;
}

TEST_F(ATM710TrdosBoot_Test, MenuTRDOSBootsClassicTRDOS)
{
    // The regression: "TR-DOS" from the 128K boot menu (same entry as
    // USR 15616 / "trdos 48") must boot classic TR-DOS, not the sys-BIOS
    // launcher. With the broken gate + FFF7 readback this flow re-activated
    // the ATM BIOS menu instead.
    auto emulator = Enter128Menu("atm710-trdos-boot");
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    EmulatorState& state = context->emulatorState;

    // The 128K boot menu is up with its "TR-DOS" entry (5th item)
    std::string menu = DecodeZXRows(context, (state.p7FFD & 0x08) ? 7 : 5, 6, 15);
    ASSERT_NE(menu.find("TR-DOS"), std::string::npos) << "128 menu:\n" << menu;

    // Bootable TR-DOS disk in drive A: (kept from the verified user flow)
    {
        std::string trdPath = TestPathHelper::GetTestDataPath("loaders/trd/EyeAche.trd");
        LoaderTRD trdLoader(context, trdPath);
        ASSERT_TRUE(trdLoader.loadImage()) << "TRD not loaded: " << trdPath;
    }

    // 4x down to "TR-DOS", then ENTER
    Keyboard* keyboard = context->pKeyboard;
    for (int i = 0; i < 4; i++)
    {
        PressDown(emulator);
    }
    keyboard->PressKey(ZXKEY_ENTER);
    emulator->RunNFrames(12, true);
    keyboard->ReleaseKey(ZXKEY_ENTER);
    emulator->RunNFrames(400, true);

    // Classic TR-DOS 5.03 banner on the standard ZX screen
    std::string screen = DecodeZXRows(context, (state.p7FFD & 0x08) ? 7 : 5, 0, 10);
    EXPECT_NE(screen.find("TR-DOS Ver 5.03"), std::string::npos) << "screen:\n" << screen;
    EXPECT_NE(screen.find("BETA 128"), std::string::npos) << "screen:\n" << screen;

    // ...NOT the sys-BIOS launcher: that one switches the ATM text mode (6).
    // TR-DOS 5.03 runs on the standard ZX screen (video mode 3).
    EXPECT_EQ(state.pFF77 & 7, 3) << "BIOS activation would show video mode 6";

    // Settled hardware state, byte-identical to the ZXMAK2 boot.szx reference
    EXPECT_EQ(state.aFF77, 0xFF77);
    EXPECT_EQ(state.pFF77, 0xAB);

    // TR-DOS session closed cleanly: tracker re-armed, no sticky session
    EXPECT_TRUE(state.flags & CF_SETDOSROM) << "flags=" << std::hex << (int)state.flags;
    EXPECT_FALSE(state.flags & CF_TRDOS) << "flags=" << std::hex << (int)state.flags;

    // The machine is executing the TR-DOS ROM from bank 0
    EXPECT_TRUE(context->pMemory->IsBank0ROM());
}
