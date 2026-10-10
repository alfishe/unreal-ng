// The Next's Multiface (zxnext.vhd + multiface.vhd): the NMI NextZXOS starts snapshots with, its ROM / RAM at #0000-#3FFF, the ports
// Source: D (zxnext.vhd / multiface.vhd of core 3.02.03)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstring>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"

class NextMultiface_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder_Next* _ports = nullptr;
    NextMemory* _memory = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        _memory = dynamic_cast<NextMemory*>(_context->pMemory);
        ASSERT_NE(_ports, nullptr);
        ASSERT_NE(_memory, nullptr);
        std::memset(_memory->ROMPageHostAddress(5), 0xA5, 0x2000);  // "enNextMf.rom"
        std::memset(_memory->ROMPageHostAddress(5) + 0x2000, 0x5A, 0x2000);
        _ports->Board().Write(0x83, 0xFF);  // the ports exist
    }
    void TearDown() override { EmulatorTestHelper::CleanupEmulator(_emulator); }
};

// NR #02 bit 3 is the M1 button: it needs NR #06 bit 3; the NMI session opens, the memory follows the #0066 fetch
TEST_F(NextMultiface_Test, ButtonStartsAnNmiSessionAndTheFetchOfTheVectorComesFromTheRom)
{
    NextMultiface& mf = _ports->Multiface();
    _ports->Board().Write(0x02, 0x08);
    EXPECT_FALSE(mf.NmiHold()) << "NR #06 bit 3 is off: no NMI";
    EXPECT_EQ(_ports->Board().Read(0x02) & 8, 0);

    _ports->Board().Write(0x02, 0x00);
    _ports->Board().Write(0x06, 0x08);
    _ports->Board().Write(0x02, 0x08);
    EXPECT_TRUE(mf.NmiHold());
    EXPECT_TRUE(mf.IsActive());
    EXPECT_FALSE(mf.MemoryIn()) << "the memory comes in with the fetch at #0066";
    EXPECT_NE(_ports->Board().Read(0x02) & 8, 0) << "NR #02 bit 3 reads 1 once the NMI was generated";

    mf.BeforeMachineM1(0x0100);
    EXPECT_FALSE(mf.MemoryIn());
    mf.BeforeMachineM1(0x0066);
    EXPECT_TRUE(mf.MemoryIn());
    EXPECT_EQ(_memory->PeekSlot(0x0000), 0xA5) << "the Multiface ROM in slot 0";
    EXPECT_EQ(_memory->PeekSlot(0x2000), 0x5A) << "its RAM in slot 1";

    _ports->OnRetn();  // RETN: the session ends and the memory goes
    EXPECT_FALSE(mf.IsActive());
    EXPECT_NE(_memory->PeekSlot(0x0000), 0xA5);
}

// While the Multiface session is open the DRIVE button does nothing, and the other way round
TEST_F(NextMultiface_Test, TheTwoNmiSourcesDoNotOverlap)
{
    _ports->Board().Write(0x06, 0x18);  // both buttons on
    _ports->Board().Write(0x0A, 0x10);
    ASSERT_TRUE(_ports->GenerateMultifaceNmi());
    EXPECT_FALSE(_ports->GenerateDriveNmi()) << "mf_is_active blocks the DivMMC's NMI";
    EXPECT_FALSE(_ports->GenerateMultifaceNmi()) << "one NMI at a time";
    _ports->OnRetn();
    EXPECT_TRUE(_ports->GenerateDriveNmi());
    EXPECT_FALSE(_ports->GenerateMultifaceNmi()) << "the DivMMC's handler is in: no Multiface NMI";
}

// The enable / disable ports (NR #0A bits 7:6: +3 mode #3F / #BF) page the memory in and out; the +3 Multiface is invisible until a button
TEST_F(NextMultiface_Test, PortsPageTheMemoryAndInvisibleHidesIt)
{
    NextMultiface& mf = _ports->Multiface();
    EXPECT_EQ(mf.EnablePort(), 0x3F);
    EXPECT_EQ(mf.DisablePort(), 0xBF);
    uint8_t value = 0;
    EXPECT_FALSE(mf.PortRead(0x3F, 0x3F, value)) << "invisible: the enable port does not page it in";
    EXPECT_FALSE(mf.MemoryIn());

    _ports->Board().Write(0x06, 0x08);
    _ports->Board().Write(0x02, 0x08);  // the button: visible now
    _context->emulatorState.p7FFD = 0x37;
    EXPECT_TRUE(mf.PortRead(0x3F, 0x7F3F, value));
    EXPECT_EQ(value, 0x37) << "+3 mode: A15:A12 = 7 reads #7FFD";
    EXPECT_TRUE(mf.MemoryIn());
    mf.PortRead(0xBF, 0xBF, value);
    EXPECT_FALSE(mf.MemoryIn()) << "a read of the disable port pages it out";
    EXPECT_FALSE(mf.NmiHold()) << "(+3 mode) and ends the NMI session";

    // 128 mode: the enable port is #BF, the disable port #3F
    _ports->Board().Write(0x0A, 0x40);
    EXPECT_EQ(mf.EnablePort(), 0xBF);
    EXPECT_EQ(mf.DisablePort(), 0x3F);
}

// While the Multiface memory is in, the DivMMC's automap is not active (zxnext.vhd: sram_pre_override = "000"): a fetch at an entry
// point does not map it, so the RETN of the Multiface's handler lands in the ROM it expects (NextZXOS starts a snapshot by RETN to
// a RET of the 48K ROM at #2313)
TEST_F(NextMultiface_Test, TheDivMmcDoesNotAutomapWhileTheMultifaceIsIn)
{
    _ports->Board().Write(0x0A, 0x10);
    _ports->Board().Write(0xB8, 0x80);  // entry #0038
    _ports->Board().Write(0xB9, 0x80);  // valid without ROM 3
    _ports->Board().Write(0x06, 0x08);
    _ports->Board().Write(0x02, 0x08);
    _ports->Multiface().BeforeMachineM1(0x0066);
    ASSERT_TRUE(_ports->Multiface().MemoryIn());
    _ports->DivMmc().BeforeMachineM1(0x0038);
    _ports->DivMmc().OnMachineM1(0x0038);
    EXPECT_FALSE(_ports->DivMmc().Automapped()) << "the entry fetched while the Multiface memory is in maps nothing";
    _ports->OnRetn();
    _ports->DivMmc().OnMachineM1(0x0038);
    EXPECT_TRUE(_ports->DivMmc().Automapped()) << "after the session the entry works again";
}

// The entries that need the 48K ROM do not map the DivMMC when a Layer 2 read mapping stands over the ROM (zxnext.vhd
// sram_divmmc_automap_rom3_en has "not sram_layer2_map_en"): the L2Port test runs its IM1 handler in Layer 2 and the DivMMC stayed
// mapped for the rest of the program
TEST_F(NextMultiface_Test, TheRomThreeEntriesIgnoreALayer2ReadMapping)
{
    _ports->Board().Write(0x0A, 0x10);
    _ports->Board().Write(0xB8, 0x80);  // entry #0038, valid only with ROM 3
    _ports->Board().Write(0xB9, 0x00);
    _context->emulatorState.p7FFD = 0x10;  // the 48K ROM (ROM 3 with #1FFD bit 2)
    _context->emulatorState.p1FFD = 0x04;
    _memory->ApplyClassicPaging(0x10, 0x04);
    ASSERT_TRUE(_memory->BasicRomVisible());
    // Layer 2 reads over #0000-#3FFF, writes too: port #123B bit 2 (read) | bit 0 (write) | bit 1 (enabled)
    _ports->DecodePortOut(0x123B, 0x07, 0);
    ASSERT_TRUE(_memory->Layer2ReadsAt(0x0038));
    _ports->DivMmc().OnMachineM1(0x0038);
    EXPECT_FALSE(_ports->DivMmc().Automapped()) << "Layer 2 supplies the fetch: no automap";
    _ports->DecodePortOut(0x123B, 0x00, 0);
    ASSERT_FALSE(_memory->Layer2ReadsAt(0x0038));
    _ports->DivMmc().OnMachineM1(0x0038);
    EXPECT_TRUE(_ports->DivMmc().Automapped());
}
