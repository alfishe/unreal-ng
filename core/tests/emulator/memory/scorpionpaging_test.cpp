#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/models/scorpionfixture.h"

/// @brief Scorpion ZS 256 memory paging truth table (implementation-plan Task 3).
///        Latches are driven through EmulatorState + Memory::UpdateZ80Banks()
///        directly — the decoder integration (#1FFD arm) lands with Task 4;
///        only the #7FFD lock test goes through the real decoder path.
///        Reference: docs/inprogress/2026-09-07-scorpion-zs256-clone/design.md §3.
class ScorpionPaging_Test : public ScorpionMachineFixture
{
protected:
    /// @brief Latch #7FFD / #1FFD and rebuild the bank map in one step
    void ApplyLatches(uint8_t p7FFD, uint8_t p1FFD)
    {
        EmulatorState& state = _context->emulatorState;
        state.p7FFD = p7FFD;
        state.p1FFD = p1FFD;
        _memory->UpdateZ80Banks();
    }

    /// @brief Encode a bank number into the latch pair (inverse of the bank3 assembly)
    static void EncodeBankLatches(uint8_t bank, uint8_t& p7FFD, uint8_t& p1FFD)
    {
        p7FFD = static_cast<uint8_t>(bank & 0b111);
        p1FFD = static_cast<uint8_t>(((bank & 0x08) ? 0x10 : 0) | ((bank & 0x30) << 2));
    }
};

/// @brief Power-on: both paging latches are 0 and the boot map is
///        ROM0(BASIC 128) / RAM 5 / RAM 2 / RAM 0
TEST_F(ScorpionPaging_Test, BootLatchesAndBankMap)
{
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x00);
    EXPECT_EQ(_context->emulatorState.p1FFD, 0x00);

    EXPECT_EQ(BankTag(0x0000), 0xC0) << "boot ROM is bundle page 0 (BASIC 128)";
    EXPECT_EQ(BankTag(0x4000), 0x45);
    EXPECT_EQ(BankTag(0x8000), 0x42);
    EXPECT_EQ(BankTag(0xC000), 0x40);
}

/// @brief All 16 #C000 banks on a stock 256 KB machine: #7FFD[2:0] + #1FFD[4]
TEST_F(ScorpionPaging_Test, AllSixteenBanksAtC000)
{
    EXPECT_EQ(_memory->GetRamMask(), 0x0F) << "256 KB -> 16 RAM banks";

    for (uint8_t bank = 0; bank < 16; bank++)
    {
        uint8_t p7FFD, p1FFD;
        EncodeBankLatches(bank, p7FFD, p1FFD);
        ApplyLatches(p7FFD, p1FFD);

        EXPECT_EQ(BankTag(0xC000), static_cast<uint8_t>(ScorpionRamTagBase | bank))
            << "bank " << static_cast<int>(bank) << " must map at #C000";
    }
}

/// @brief The 1 MB extension adds #1FFD bits 6/7: 64 banks decode at #C000
TEST_F(ScorpionPaging_Test, SixtyFourBanksAtC000WithOneMegabyte)
{
    ASSERT_TRUE(RebuildWithModel(MM_SCORP, RAM_1024));
    EXPECT_EQ(_memory->GetRamMask(), 0x3F) << "1024 KB -> 64 RAM banks";

    for (uint8_t bank = 0; bank < 64; bank++)
    {
        uint8_t p7FFD, p1FFD;
        EncodeBankLatches(bank, p7FFD, p1FFD);
        ApplyLatches(p7FFD, p1FFD);

        EXPECT_EQ(BankTag(0xC000), static_cast<uint8_t>(ScorpionRamTagBase | bank))
            << "bank " << static_cast<int>(bank) << " must map at #C000 on the 1 MB machine";
    }
}

/// @brief Bank bits beyond the physical RAM size are masked off:
///        bank 20 written to a 256 KB machine lands on bank 20 & 15 = 4
TEST_F(ScorpionPaging_Test, RamMaskClampsToPhysicalSize)
{
    EXPECT_EQ(_memory->GetRamMask(), 0x0F);

    ApplyLatches(0x04, 0x40);  // bank3 assembly: 4 | (0x40 >> 2) = 20
    EXPECT_EQ(BankTag(0xC000), 0x44) << "bank 20 must clamp to bank 4 on 256 KB";
}

/// @brief The #0000 priority chain: ROM0 default, #7FFD bit4 -> ROM1,
///        #1FFD bit1 -> Shadow Monitor (outranking the ROM1 select),
///        #1FFD bit0 -> RAM bank 0
TEST_F(ScorpionPaging_Test, Bank0PriorityChain)
{
    ApplyLatches(0x00, 0x00);
    EXPECT_EQ(BankTag(0x0000), 0xC0) << "default: ROM0 (BASIC 128)";

    ApplyLatches(0x10, 0x00);
    EXPECT_EQ(BankTag(0x0000), 0xC1) << "#7FFD bit4: ROM1 (48K BASIC)";

    ApplyLatches(0x00, 0x02);
    EXPECT_EQ(BankTag(0x0000), 0xC2) << "#1FFD bit1: Shadow Monitor";

    ApplyLatches(0x10, 0x02);
    EXPECT_EQ(BankTag(0x0000), 0xC2) << "monitor select outranks the #7FFD ROM bit";

    ApplyLatches(0x00, 0x01);
    EXPECT_EQ(BankTag(0x0000), 0x40) << "#1FFD bit0: RAM bank 0 wins over every ROM";
}

/// @brief RAM at #0000 must be writable: writes land in RAM bank 0, not the
///        trash page the ROM windows redirect to
TEST_F(ScorpionPaging_Test, RamAtZeroWindowRoundTrip)
{
    ApplyLatches(0x00, 0x01);
    ASSERT_EQ(BankTag(0x0000), 0x40);

    DirectWrite(0x0000, 0x5A);
    EXPECT_EQ(BankTag(0x0000), 0x5A) << "#0000 write/read must hit RAM bank 0";

    DirectWrite(0x0000, 0x40);  // restore the self-identifying tag
}

/// @brief An open TR-DOS session maps ROM3 regardless of #7FFD bit4 — the one
///        deliberate divergence from the generic path (design §3, HW §4.4
///        rule 3): with bit4 clear the generic path would map the service ROM
TEST_F(ScorpionPaging_Test, TrDosSessionMapsRom3RegardlessOfRomSelect)
{
    ApplyLatches(0x00, 0x00);

    EmulatorState& state = _context->emulatorState;
    state.flags |= CF_TRDOS;
    _memory->UpdateZ80Banks();

    EXPECT_EQ(BankTag(0x0000), 0xC3) << "open session maps ROM3 (TR-DOS), not the service ROM";
    EXPECT_TRUE(state.flags & CF_DOSPORTS) << "FDC ports are on the bus during a session";
    EXPECT_TRUE(state.flags & CF_LEAVEDOSRAM) << "session closes on RAM execution";
    EXPECT_FALSE(state.flags & CF_SETDOSROM) << "the #3Dxx trap is not re-armed inside a session";
}

/// @brief The #3Dxx trap arms only while a DOS-capable ROM slot is visible at
///        #0000 (ROM1 or the Shadow Monitor — never RAM) with a Beta128 present
TEST_F(ScorpionPaging_Test, TrapArmRules)
{
    EmulatorState& state = _context->emulatorState;

    ApplyLatches(0x10, 0x00);
    EXPECT_TRUE(state.flags & CF_SETDOSROM) << "ROM1 selected: trap armed";

    ApplyLatches(0x00, 0x02);
    EXPECT_TRUE(state.flags & CF_SETDOSROM) << "Shadow Monitor selected: trap armed";

    ApplyLatches(0x10, 0x01);
    EXPECT_FALSE(state.flags & CF_SETDOSROM) << "RAM at #0000: trap must not arm";

    ApplyLatches(0x04, 0x00);
    EXPECT_FALSE(state.flags & CF_SETDOSROM) << "plain ROM0 (bit4 clear): trap not armed";
}

/// @brief #1FFD bit 2 is the RS-232 line on hardware and must not affect
///        banking in any way (HW §12 item 9)
TEST_F(ScorpionPaging_Test, OneFFDBitTwoIgnored)
{
    ApplyLatches(0x00, 0x04);
    EXPECT_EQ(BankTag(0x0000), 0xC0) << "bit2 must not touch the #0000 chain";
    EXPECT_EQ(BankTag(0xC000), 0x40) << "bit2 must not touch bank3";

    ApplyLatches(0x10, 0x14);
    EXPECT_EQ(BankTag(0x0000), 0xC1) << "bit2 must not override the ROM select";
}

/// @brief #7FFD D5 locks the whole port (the locking write itself applies),
///        while the #1FFD path stays live. The decoder path is exercised for
///        the lock; the #1FFD half is driven at latch level because the
///        decoder arm arrives with Task 4
TEST_F(ScorpionPaging_Test, SevenFFDLockBlocksPortButOneFFDStaysLive)
{
    WritePort(0x7FFD, 0x23);  // bank 3 + lock
    EXPECT_EQ(BankTag(0xC000), 0x43) << "the locking write applies";
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x23);

    WritePort(0x7FFD, 0x05);  // blocked by the lock
    EXPECT_EQ(BankTag(0xC000), 0x43) << "further #7FFD writes are ignored";
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x23) << "the latch itself stays frozen";

    // #1FFD remains live: its bank3 extension still moves banking
    ApplyLatches(0x23, 0x10);  // #1FFD bit4 -> bank 3 | 8 = 11
    EXPECT_EQ(BankTag(0xC000), 0x4B) << "the #1FFD path is not affected by the #7FFD lock";
}

/// @brief The fixed windows never move under any latch combination
TEST_F(ScorpionPaging_Test, FixedWindowsUnderAnyLatches)
{
    ApplyLatches(0x17, 0x00);
    ApplyLatches(0x00, 0x03);
    ApplyLatches(0x10, 0x02);

    EXPECT_EQ(BankTag(0x4000), 0x45) << "#4000 is hardwired to RAM page 5";
    EXPECT_EQ(BankTag(0x8000), 0x42) << "#8000 is hardwired to RAM page 2";
}
