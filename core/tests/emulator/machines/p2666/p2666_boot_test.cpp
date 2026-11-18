// Pentagon 2.666 LE boot: the board's ROM set (data/rom/p2666/p2666.rom) from power-on at every CPU clock.

#include <emulator/cpu/core.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <bit>
#include <cstring>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/ports/models/portdecoder_p2666.h"
#include "pch.h"
#include "stdafx.h"

class P2666Boot_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("P2666", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
};

/// Power-on: Gluk at #0000 with DOS on, 28 MHz, the logo in RAM page 5, the default palette (HR §7)
TEST_F(P2666Boot_Test, PowerOnStateIsTheBoards)
{
    EmulatorContext* context = _emulator->GetContext();
    EmulatorState& state = context->emulatorState;
    Memory* memory = context->pMemory;
    auto* decoder = dynamic_cast<PortDecoderP2666*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);

    EXPECT_EQ(state.base_z80_frequency, 3'562'500u);
    EXPECT_EQ(state.current_z80_frequency_multiplier, 8);
    EXPECT_TRUE(state.flags & CF_TRDOS);
    EXPECT_EQ(decoder->WindowPage(0), PortDecoderP2666::kRomFirstPage);
    EXPECT_EQ(decoder->WindowPage(1), 5);
    EXPECT_EQ(decoder->WindowPage(2), 2);
    EXPECT_EQ(decoder->WindowPage(3), 0);
    EXPECT_EQ(memory->GetROMPageForBank(0), 0);
    EXPECT_EQ(std::memcmp(memory->RAMPageAddress(5), memory->ROMPageHostAddress(5), 6912), 0) << "the boot logo";
    EXPECT_EQ(state.p2666.palette[0], 0xFFFF);
    EXPECT_EQ(state.p2666.palette[15], 0x0C0C);
}

/// Gluk boots and keeps running from its ROM page at every clock: a sanity check of the decoder and the map
TEST_F(P2666Boot_Test, DISABLED_ProbeGlukBoot)
{
    EmulatorContext* context = _emulator->GetContext();
    EmulatorState& state = context->emulatorState;
    Z80* z80 = context->pCore->GetZ80();
    _emulator->GetContext()->pCore->EnableTurboMode();
    for (int f = 0; f < 300; f++)
    {
        _emulator->RunFrame();
        if (f % 25 == 0)
        {
            std::printf("frame %d pc %04X x%d dos %d 7FFD %02X EFF7 %02X xx77 %02X w0 %02X w3 %02X\n", f, z80->pc,
                        state.current_z80_frequency_multiplier, (state.flags & CF_TRDOS) ? 1 : 0, state.p7FFD, state.pEFF7,
                        state.p2666.pxx77, dynamic_cast<PortDecoderP2666*>(context->pPortDecoder)->WindowPage(0),
                        dynamic_cast<PortDecoderP2666*>(context->pPortDecoder)->WindowPage(3));
        }
    }
    const uint8_t* screen = context->pMemory->RAMPageAddress(5);
    for (int row = 0; row < 24; row++)
    {
        for (int col = 0; col < 32; col++)
        {
            // first pixel line of each character cell
            const uint8_t* p = screen + ((row & 0x18) << 8) + ((row & 7) << 5) + col;
            int bits = 0;
            for (int l = 0; l < 8; l++)
                bits += std::popcount(p[l << 8]);
            std::printf("%c", bits == 0 ? ' ' : bits < 16 ? '.' : '#');
        }
        std::printf("\n");
    }
}
