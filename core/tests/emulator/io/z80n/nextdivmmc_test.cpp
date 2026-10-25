// The Next's DivMMC against divmmc.vhd and zxnext.vhd of core 3.02.03: what maps it (the automap entries and the conditions of the
// ROM-3-only ones), what releases it (NR #0A bit 4, the resets, the port enable), the memory it shows with CONMEM and MAPRAM.
// The audit and the gap numbers (D1-D7): docs/inprogress/2026-10-07-zx-next/audit-divmmc-gap-2026-10-10.md

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

class NextDivMmc_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    PortDecoder_Next* _ports = nullptr;
    NextMemory* _memory = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        _memory = dynamic_cast<NextMemory*>(_context->pMemory);
        ASSERT_NE(_ports, nullptr);
        ASSERT_NE(_memory, nullptr);
        std::memset(_memory->ROMPageHostAddress(4), 0xD1, 0x2000);                                        // the DivMMC ROM
        std::memset(_memory->ROMPageHostAddress(9) + 0x2000, 0x33, 0x2000);                               // its RAM bank 3
        _ports->Board().Write(0x83, 0xFF);
        ASSERT_FALSE(_memory->InConfigMode());
    }
    void TearDown() override { EmulatorTestHelper::CleanupEmulator(_emulator); }

    NextDivMmc& Div() { return _ports->DivMmc(); }
    NextBoard& Board() { return _ports->Board(); }
    void Out(uint16_t port, uint8_t value) { _ports->DecodePortOut(port, value, 0); }
    /// One opcode fetch at `address`: the hook the CPU runs before and after it
    void Fetch(uint16_t address)
    {
        Div().BeforeMachineM1(address);
        Div().OnMachineM1(address);
    }
    /// The automap on (NR #0A bit 4) and the 48K BASIC ROM paged (ROM 1 of the 128K family)
    void AutomapOnWithRomThree()
    {
        Board().Write(0x0A, 0x10);
        Out(0x7FFD, 0x10);
        ASSERT_TRUE(_memory->BasicRomVisible());
    }
};

// zxnext.vhd 3036-3057 / 3138: the ROM-3-only entries need the MMU to map ROM at the fetch (sram_pre_override(0)); RAM in the slot is the
// program's own code. The always-valid entries (NR #B9) need only sram_pre_override(2), which RAM in the slot has too
TEST_F(NextDivMmc_Test, RomThreeEntriesNeedTheMmuToMapRomAtTheFetch)
{
    AutomapOnWithRomThree();
    Fetch(0x0038);
    EXPECT_TRUE(Div().Automapped()) << "ROM in slot 0, ROM 3: the interrupt entry traps";
    _ports->OnRetn();
    ASSERT_FALSE(Div().Automapped());

    Board().Write(0x50, 0x14);  // RAM in slot 0
    Fetch(0x0038);
    EXPECT_FALSE(Div().Automapped()) << "an interrupt vector in the program's own RAM";
    Fetch(0x0008);
    EXPECT_FALSE(Div().Automapped()) << "RST 8 in the program's own RAM";
    Fetch(0x0000);
    EXPECT_TRUE(Div().Automapped()) << "#0000 is valid whatever the MMU maps (NR #B9 bit 0)";
    _ports->OnRetn();

    Board().Write(0x50, 0xFF);
    Board().Write(0x51, 0x15);  // RAM in slot 1
    Div().BeforeMachineM1(0x3D00);
    EXPECT_FALSE(Div().Automapped()) << "code at #3Dxx in RAM is not the TR-DOS entry";
    Board().Write(0x51, 0xFF);
    Div().BeforeMachineM1(0x3D00);
    EXPECT_TRUE(Div().Automapped()) << "ROM again: the #3Dxx entry is instant";
}

// zxnext.vhd 4112: NR #0A bit 4 = 0 is automap_reset - the latches go at once (divmmc.vhd 108-148)
TEST_F(NextDivMmc_Test, ClearingNr0aBit4DropsTheHeldAutomapAndTheButtonLatch)
{
    AutomapOnWithRomThree();
    Fetch(0x0000);
    ASSERT_TRUE(Div().Automapped());
    EXPECT_STREQ(_memory->SlotKind(0), "divmmc rom");
    Board().Write(0x0A, 0x00);
    EXPECT_FALSE(Div().Automapped());
    EXPECT_STRNE(_memory->SlotKind(0), "divmmc rom") << "the memory follows at once";
    Board().Write(0x0A, 0x10);
    EXPECT_FALSE(Div().Automapped()) << "the latch is gone, enabling it again does not bring it back";

    Board().Write(0x06, 0x10);
    ASSERT_TRUE(_ports->GenerateDriveNmi());
    ASSERT_TRUE(Div().ButtonPending());
    Board().Write(0x0A, 0x00);
    EXPECT_FALSE(Div().ButtonPending());
    EXPECT_FALSE(Div().NmiHold());
}

// divmmc.vhd 94-100 (3.02.00: bank 3 fills the lower 8K even with CONMEM): MAPRAM, not CONMEM, chooses between the ROM and RAM bank 3
TEST_F(NextDivMmc_Test, MapramBankThreeFillsTheLowerEightKilobytesEvenWithConmem)
{
    Out(0xE3, 0x80 | 0x01);
    EXPECT_STREQ(_memory->SlotKind(0), "divmmc rom");
    EXPECT_EQ(_memory->PeekSlot(0x0100), 0xD1);

    Out(0xE3, 0xC0 | 0x03);  // CONMEM, MAPRAM, bank 3
    EXPECT_STREQ(_memory->SlotKind(0), "divmmc ram") << "CONMEM does not beat MAPRAM";
    EXPECT_EQ(_memory->PeekSlot(0x0100), 0x33);
    _memory->SlotWriteFast(0x0100, 0x77);
    EXPECT_EQ(_memory->PeekSlot(0x0100), 0x33) << "slot 0 is read-only";
    _memory->SlotWriteFast(0x2100, 0x77);
    EXPECT_EQ(_memory->PeekSlot(0x2100), 0x33) << "bank 3 is read-only while MAPRAM is set";

    Board().Write(0x09, 0x08);  // NR #09 bit 3 clears MAPRAM
    EXPECT_STREQ(_memory->SlotKind(0), "divmmc rom");
    _memory->SlotWriteFast(0x2100, 0x77);
    EXPECT_EQ(_memory->PeekSlot(0x2100), 0x77) << "bank 3 is writable again";
}

// zxnext.vhd 2907-2908: the #0066 entry maps for the DRIVE button's NMI only, instant with NR #BB bit 1, delayed with bit 0
TEST_F(NextDivMmc_Test, TheNmiEntryFollowsNrBbBitsOneAndZero)
{
    AutomapOnWithRomThree();
    Board().Write(0x06, 0x10);

    Board().Write(0xBB, 0x00);
    ASSERT_TRUE(_ports->GenerateDriveNmi());
    Fetch(0x0066);
    EXPECT_FALSE(Div().Automapped()) << "neither bit: the button's NMI runs the ROM's handler";
    _ports->OnRetn();

    Board().Write(0xBB, 0x01);
    ASSERT_TRUE(_ports->GenerateDriveNmi());
    Div().BeforeMachineM1(0x0066);
    EXPECT_FALSE(Div().Automapped()) << "delayed: the opcode at #0066 is the ROM's";
    Div().OnMachineM1(0x0066);
    EXPECT_TRUE(Div().Automapped());
    EXPECT_STREQ(_memory->SlotKind(0), "divmmc rom");
    _ports->OnRetn();

    Board().Write(0xBB, 0x02);
    ASSERT_TRUE(_ports->GenerateDriveNmi());
    Div().BeforeMachineM1(0x0066);
    EXPECT_TRUE(Div().Automapped()) << "instant: the opcode comes from the DivMMC";
}

// zxnext.vhd 4175-4177: `reset` clears port_e3_reg, and divmmc.vhd 108-139 the latches - the NR #02 soft and hard resets too; the
// Multiface (multiface.vhd, `reset`) loses its session with them
TEST_F(NextDivMmc_Test, AnNr02ResetClearsThePortTheLatchesAndTheMultifaceSession)
{
    AutomapOnWithRomThree();
    Board().Write(0x06, 0x08);
    ASSERT_TRUE(_ports->GenerateMultifaceNmi());
    ASSERT_TRUE(_ports->Multiface().IsActive());
    _memory->PokeSlot(0x8000, 0x00);
    _z80->pc = 0x8000;
    Board().Write(0x02, 0x01);  // a soft reset: runs after the next instruction
    _z80->EngineStep();
    EXPECT_FALSE(_ports->Multiface().IsActive());

    Out(0xE3, 0xC0 | 0x03);
    Fetch(0x0000);
    ASSERT_TRUE(Div().Automapped());
    _memory->PokeSlot(0x8000, 0x00);
    _z80->pc = 0x8000;
    Board().Write(0x02, 0x02);  // a hard reset
    _z80->EngineStep();
    EXPECT_EQ(Div().ReadPort(), 0x00);
    EXPECT_FALSE(Div().Mapram());
    EXPECT_FALSE(Div().Automapped());
    EXPECT_FALSE(Div().Mapped());
}

// zxnext.vhd 2412 / 2608 / 2620 / 4112 / divmmc.vhd 98-99: NR #83 bit 0 enables #E3 and the DivMMC's memory, bit 3 the SPI pair
TEST_F(NextDivMmc_Test, ADisabledDivMmcPortIsNotDecodedAndUnmapsTheMemory)
{
    AutomapOnWithRomThree();
    Board().Write(0x83, 0xFE);
    Out(0xE3, 0x80);
    EXPECT_EQ(Div().ReadPort(), 0x00) << "the write found no port";
    Fetch(0x0000);
    EXPECT_FALSE(Div().Automapped()) << "automap_reset while the port is off";

    Board().Write(0x83, 0xFF);
    Out(0xE3, 0x80);
    EXPECT_TRUE(Div().Mapped());
    Fetch(0x0000);
    ASSERT_TRUE(Div().Automapped());
    Board().Write(0x83, 0xFE);
    EXPECT_FALSE(Div().Mapped()) << "i_en: neither CONMEM nor the latch maps it";
    EXPECT_FALSE(Div().Automapped());
    EXPECT_STRNE(_memory->SlotKind(0), "divmmc rom");
    Board().Write(0x83, 0xFF);
    EXPECT_TRUE(Div().Mapped()) << "CONMEM was never cleared";
    EXPECT_FALSE(Div().Automapped()) << "the latch was";

    // the SPI pair: with bit 3 off the select write finds no port, so a CMD0 gets no answer
    ASSERT_TRUE(_ports->InsertSdCard(0, std::make_unique<MemoryDisk>(4096), SdCardSpi::WriteMode::Session));
    Board().Write(0x83, 0xF7);
    Out(0xE7, 0xFE);
    const uint8_t cmd0[6] = {0x40, 0, 0, 0, 0, 0x95};
    for (uint8_t b : cmd0)
    {
        _z80->t += 20;
        Out(0xEB, b);
    }
    uint8_t r1 = 0xFF;
    for (int i = 0; i < 16 && r1 == 0xFF; i++)
    {
        _z80->t += 20;
        r1 = _ports->DecodePortIn(0xEB, 0);
    }
    EXPECT_EQ(r1, 0xFF) << "no card was selected: no answer";
}

// zxnext.vhd 5192-5198: the Multiface type (bits 7:6) and the SD swap (bit 5) are written in config mode only
TEST_F(NextDivMmc_Test, Nr0aBitsSevenToFiveAreConfigModeOnly)
{
    Board().Write(0x0A, 0xFF);
    EXPECT_EQ(Board().Stored(0x0A), 0x1B) << "bits 7:5 keep their value (0) outside config mode; bit 2 is not stored (zxnext.vhd)";
    EXPECT_FALSE(Board().SdSwap());
    _memory->SetConfigMode(true);
    Board().Write(0x0A, 0xE0);
    EXPECT_EQ(Board().Stored(0x0A), 0xE0);
    EXPECT_TRUE(Board().SdSwap());
    _memory->SetConfigMode(false);
    Board().Write(0x0A, 0x10);
    EXPECT_EQ(Board().Stored(0x0A), 0xF0) << "bits 7:5 stay as the config mode left them";
    EXPECT_EQ(Board().Read(0x0A), 0xF0 & 0xFB);
}
