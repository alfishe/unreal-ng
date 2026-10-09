// ZX Spectrum Next boot chain (P3): the boot ROM at power-on, config mode and the NR #03 / #04 mapping, the SPI
// port pair with an SD card in SPI mode, NR #02 reset between instructions. Design:
// docs/inprogress/2026-10-07-zx-next/design-boot-and-firmware.md, research-fpga-vhdl.md sections 7 and 14.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstring>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"

class NextBoot_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    NextMemory* _memory = nullptr;
    PortDecoder_Next* _ports = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(
            "NEXT", LoggerLevel::LogError, RamPowerOn::Zero,
            [](CONFIG& config) { std::strncpy(config.next_boot_rom_path, "rom/next/nextboot.rom", sizeof config.next_boot_rom_path - 1); });
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = dynamic_cast<NextMemory*>(_context->pMemory);
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_memory, nullptr);
        ASSERT_NE(_ports, nullptr);
        ASSERT_TRUE(_memory->HasBootRom()) << "data/rom/next/nextboot.rom is not found";
        // The test helper forces the 48K ROM mode (RM_SOS) for a BASIC-ready wait: back to the power-on latches
        _context->emulatorState.p7FFD = 0;
        _memory->UpdateZ80Banks();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Out(uint16_t port, uint8_t value) { _ports->DecodePortOut(port, value, 0); }
    uint8_t In(uint16_t port) { return _ports->DecodePortIn(port, 0); }
    void NextReg(uint8_t reg, uint8_t value)
    {
        Out(0x243B, reg);
        Out(0x253B, value);
    }
};

TEST_F(NextBoot_Test, PowerOnIsConfigModeWithTheBootRomAtZero)
{
    EXPECT_TRUE(_memory->InConfigMode());
    EXPECT_TRUE(_memory->BootRomEnabled());
    EXPECT_EQ(_z80->pc, 0);
    // DI; IM 1; JP #0080 (bootrom.vhd)
    EXPECT_EQ(_memory->PeekSlot(0), 0xF3);
    EXPECT_EQ(_memory->PeekSlot(1), 0xED);
    EXPECT_EQ(_memory->PeekSlot(2), 0x56);
    EXPECT_EQ(_memory->PeekSlot(3), 0xC3);
    // the boot ROM is read-only
    _memory->SlotWriteFast(0x0000, 0x00);
    EXPECT_EQ(_memory->PeekSlot(0), 0xF3);
    // the system area is empty until the firmware fills it
    Out(0x243B, 0x04);
    EXPECT_EQ(_memory->ROMPageHostAddress(0)[0x100], 0xFF);
}

TEST_F(NextBoot_Test, AnyNr03WriteSwitchesTheBootRomOffButKeepsConfigMode)
{
    NextReg(0x03, 0x00);
    EXPECT_FALSE(_memory->BootRomEnabled());
    EXPECT_TRUE(_memory->InConfigMode());
    // slot 0 now shows the NR #04 bank (0 after reset: the first 16K of the system area), writable
    _memory->SlotWriteFast(0x0000, 0xAB);
    EXPECT_EQ(_memory->PeekSlot(0), 0xAB);
    EXPECT_EQ(_memory->ROMPageHostAddress(0)[0], 0xAB);
    NextReg(0x04, 0x02);
    _memory->SlotWriteFast(0x2100, 0xCD);  // slot 1 = the second half of bank 2
    EXPECT_EQ(_memory->ROMPageHostAddress(2)[0x2100], 0xCD);
    NextReg(0x04, 0x10);  // bank 16 is RAM page 0
    _memory->SlotWriteFast(0x0010, 0xEF);
    EXPECT_EQ(_memory->RAMPageAddress(0)[0x10], 0xEF);
}

TEST_F(NextBoot_Test, MachineTypeLeavesConfigModeAndASoftResetStartsThePersonality)
{
    NextReg(0x03, 0x00);
    // the firmware loads a ROM through bank 0 and picks the 128K machine type
    NextReg(0x04, 0x00);
    _memory->SlotWriteFast(0x0000, 0x76);  // HALT at #0000
    EXPECT_EQ(_memory->PeekSlot(0), 0x76) << "in config mode";
    NextReg(0x03, 0x82);                   // 128K
    EXPECT_FALSE(_memory->InConfigMode());
    EXPECT_EQ(_memory->PeekSlot(0), 0x76) << "the ROM 0 of the personality is the system area at #000000";
    _z80->pc = 0x1234;
    NextReg(0x02, 0x01);                   // soft reset: runs after the next instruction
    EXPECT_EQ(_z80->pc, 0x1234) << "a reset between instructions, not inside the register write";
    _z80->EngineStep();                    // any instruction, then the reset
    EXPECT_EQ(_z80->pc, 0) << "reset";
    EXPECT_FALSE(_memory->BootRomEnabled());
    EXPECT_EQ(_memory->PeekSlot(0), 0x76);
}

TEST_F(NextBoot_Test, SpiCardAnswersCmd0ThroughThePortPair)
{
    ASSERT_TRUE(_ports->InsertSdCard(0, std::make_unique<MemoryDisk>(4096), SdCardSpi::WriteMode::Session));
    auto& frameCounter = _context->emulatorState.frame_counter;
    auto spaced = [&]() { _z80->t += 20; };  // one byte time (16 clocks) between accesses
    (void)frameCounter;
    Out(0xE7, 0xFE);
    const uint8_t cmd0[6] = {0x40, 0, 0, 0, 0, 0x95};
    for (uint8_t b : cmd0)
    {
        spaced();
        Out(0xEB, b);
    }
    uint8_t r1 = 0xFF;
    for (int i = 0; i < 16 && r1 == 0xFF; i++)
    {
        spaced();
        r1 = In(0xEB);
    }
    EXPECT_EQ(r1, 0x01) << "idle state after CMD0";
    Out(0xE7, 0xFF);
}

TEST_F(NextBoot_Test, SpiAccessBeforeTheByteIsDoneIsIgnored)
{
    ASSERT_TRUE(_ports->InsertSdCard(0, std::make_unique<MemoryDisk>(4096), SdCardSpi::WriteMode::Session));
    Out(0xE7, 0xFE);
    _z80->t = 1000;
    Out(0xEB, 0x40);
    _z80->t = 1004;  // 4 clocks later: the previous byte is still shifting
    Out(0xEB, 0x00);
    EXPECT_EQ(_ports->SpiTooFastCount(), 1u);
}

// N3: NR #07 sets the CPU clock (applied at the frame boundary, composed with the host speed), NR #03 the machine type
// and the frame family (applied at the frame end), and the machine type decides which paging ports exist
TEST_F(NextBoot_Test, CpuSpeedRegisterSetsTheClockMultiplier)
{
    NextReg(0x07, 0x03);  // 28 MHz
    EXPECT_EQ(_context->emulatorState.hw_turbo_ratio, 8);
    Out(0x243B, 0x07);
    EXPECT_EQ(In(0x253B), 0x33) << "programmed speed and the speed in effect";
    _emulator->RunFrame(true);
    EXPECT_EQ(_context->emulatorState.current_z80_frequency, 28'000'000u) << "7 x 4 of the 3.5 MHz base, with the host at 1x";
    NextReg(0x07, 0x00);
    _emulator->RunFrame(true);
    EXPECT_EQ(_context->emulatorState.current_z80_frequency, 3'500'000u);
}

TEST_F(NextBoot_Test, MachineTimingChangesTheFrameAtTheFrameEnd)
{
    NextReg(0x03, 0x00);
    NextReg(0x03, 0x92);  // config mode: 128K machine type, timing family 1 (48K) allowed by bit 7
    EXPECT_FALSE(_memory->InConfigMode());
    EXPECT_EQ(_context->config.frame, 70908u) << "not before the frame end";
    _emulator->RunFrame(true);
    EXPECT_EQ(_context->config.frame, 69888u);
    EXPECT_EQ(_context->emulatorState.ula_timing_class, 1);
    NextReg(0x03, 0xC0);  // Pentagon timing family
    _emulator->RunFrame(true);
    EXPECT_EQ(_context->config.frame, 71680u);
    NextReg(0x03, 0xB0);  // +3
    _emulator->RunFrame(true);
    EXPECT_EQ(_context->config.frame, 70908u);
    EXPECT_EQ(_context->config.intlen, 32u);
}

TEST_F(NextBoot_Test, MachineTypeDecidesThePagingPorts)
{
    NextReg(0x03, 0x00);
    NextReg(0x03, 0x01);  // 48K
    Out(0x7FFD, 0x13);
    Out(0x1FFD, 0x05);
    EXPECT_EQ(_memory->GetMmu(6), 0) << "no paging in a 48K machine";
    NextReg(0x02, 0x01);  // a soft reset keeps the machine type
    _z80->Z80Step();
    NextReg(0x03, 0x00);  // the personality's own write: timing only, the type stays
    EXPECT_EQ(_ports->Board().MachineType(), 1);

    // 128K: #7FFD works, #1FFD does not
    NextReg(0x02, 0x01);
    _z80->Z80Step();
    EXPECT_EQ(_ports->Board().MachineType(), 1);
}
