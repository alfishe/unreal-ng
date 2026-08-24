// ZX-Evo (ATM3, 512K BaseConf zxevo.rom) boot regression tests.
//
// Guards three fixes without which the machine never reached the interactive
// EVO Reset Service shell (it either booted the TSConf ROM0 half with a
// garbage screen or hung in a sticky TR-DOS session):
//  1. ATM3/ATM710 port handlers must call Memory::UpdateZ80Banks() - the full
//     original set_banks() equivalent including the TR-DOS session-flag tail
//     (CF_SETDOSROM / CF_LEAVEDOSRAM re-arming). Calling the decoder's
//     window-only updateMemoryBanks() directly left CF_TRDOS sticky after the
//     session closed, dead-arming the tracker and booting to TR-DOS forever.
//  2. The memory-manager port gate (xx77 / xFF7 / x7F7) must mirror
//     CF_DOSPORTS: pBF.0 (shaden) OR ~cpm (aFF77 bit 9 clear) OR CF_TRDOS.
//  3. The Z-Controller data port (low byte 0x57) returns 0xFF ("no card")
//     so the EVO-DOS SD detection fails cleanly instead of consuming
//     floating-bus garbage.
//
// Expected steady states (zxevo.rom layout, verified against the reference):
//  - ~5s after reset: the service shell disk-boot retry loop idles in
//    48K-BASIC-pair ROM page 20 (FFF7[4] = 0x134 -> type 0x100 ROM-from-7FFD)
//    with the TR-DOS tracker armed (flags == CF_SETDOSROM) and the boot menu
//    visible on screen.
//  - Menu key 'U' ("128k basic"): ROM page 30 (128K editor ROM pair) with
//    initialized BASIC sysvars (ERR_NR = 0xFF).

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/keyboard/keyboard.h>
#include <emulator/platform.h>
#include <gtest/gtest.h>

#include "pch.h"
#include "stdafx.h"

class ZXEvoBoot_Test : public ::testing::Test
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

    /// Boot through the service-ROM config phase into the interactive shell
    std::shared_ptr<Emulator> BootToServiceShell()
    {
        auto emulator = _manager->CreateEmulatorWithModelAndRAM("zxevo-boot", "ATM3", 4096, LoggerLevel::LogError);
        EXPECT_NE(emulator, nullptr);
        if (!emulator)
            return emulator;

        // 300 frames: BaseConf init (ROM26/27 handoff, CMOS setup) + boot-menu
        // timeout + disk-boot retry loop settling
        emulator->RunNFrames(300, true);
        return emulator;
    }
};

TEST_F(ZXEvoBoot_Test, BootsToInteractiveServiceShell)
{
    auto emulator = BootToServiceShell();
    ASSERT_NE(emulator, nullptr);

    EmulatorContext* context = emulator->GetContext();
    EmulatorState& state = context->emulatorState;
    Z80& z80 = *context->pCore->GetZ80();
    Memory* memory = context->pMemory;

    // TR-DOS session tracker healthy: armed (SETDOSROM), no sticky session.
    // With the old window-only paging bug the flags stayed
    // CF_TRDOS | CF_DOSPORTS | CF_LEAVEDOSRAM and the machine booted TR-DOS.
    EXPECT_TRUE(state.flags & CF_SETDOSROM) << "flags=" << std::hex << (int)state.flags;
    EXPECT_FALSE(state.flags & CF_TRDOS) << "flags=" << std::hex << (int)state.flags;

    // Bank 0 holds the 48K-BASIC-pair boot shell (ROM page 20 via FFF7[4] = 0x134)
    ASSERT_TRUE(memory->IsBank0ROM());
    EXPECT_EQ(memory->GetROMPage(), 20u);

    // Live system: R advances frame to frame (HALT idle loop woken by INTs)
    uint8_t r1 = z80.r_low;
    emulator->RunNFrames(1, true);
    uint8_t r2 = z80.r_low;
    emulator->RunNFrames(1, true);
    uint8_t r3 = z80.r_low;
    EXPECT_TRUE(r1 != r2 || r2 != r3);

    // The boot menu is rendered: the framebuffer is not blank
    Screen* screen = context->pScreen;
    uint32_t* buffer = nullptr;
    size_t size = 0;
    screen->GetFramebufferData(&buffer, &size);
    ASSERT_NE(buffer, nullptr);
    ASSERT_GT(size, 0u);
    size_t litPixels = 0;
    for (size_t i = 0; i < size; i++)
    {
        if (buffer[i] & 0x00FFFFFF)  // any non-black ARGB pixel
            litPixels++;
    }
    EXPECT_GT(litPixels, size / 100);  // > 1% lit (menu + colored window)
}

TEST_F(ZXEvoBoot_Test, MenuKeyUBoots128KBasic)
{
    auto emulator = BootToServiceShell();
    ASSERT_NE(emulator, nullptr);

    EmulatorContext* context = emulator->GetContext();
    Memory* memory = context->pMemory;
    Keyboard* keyboard = context->pKeyboard;
    ASSERT_NE(keyboard, nullptr);

    // 'U' = "U. 128k basic" in the EVO Reset Service menu
    keyboard->PressKey(ZXKEY_U);
    emulator->RunNFrames(6, true);
    keyboard->ReleaseKey(ZXKEY_U);
    emulator->RunNFrames(300, true);

    // 128K BASIC: bank 0 = 128K editor ROM pair (page 30), no TR-DOS session
    ASSERT_TRUE(memory->IsBank0ROM());
    EXPECT_EQ(memory->GetROMPage(), 30u);
    EXPECT_FALSE(context->emulatorState.flags & CF_TRDOS);

    // BASIC initialized its system variables (ERR_NR = 0xFF = "no error yet")
    EXPECT_EQ(memory->MemoryReadDebug(0x5C3A, false), 0xFF);
}
