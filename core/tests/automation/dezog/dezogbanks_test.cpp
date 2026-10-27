// DezogDebugAdapter banks, ports and breakpoint ids: the findings of the live DZRP suite
// (tools/verification/dezog/verify_dzrp_emulator.py) on the Pentagon, the 48K and the ZX Next

#include "dezogtestfixture.h"

#include "emulator/video/screen.h"

namespace
{
constexpr uint8_t kRegMmu0 = 0x50;           // NR #50-#57: the Next's slot 0-7
constexpr uint8_t kProgramSlot = 4;          // 0x8000-0x9FFF: the fixture's program and its watch target
constexpr uint8_t kBankA = 40, kBankB = 41;  // the two 8K halves of one 16K page
constexpr uint8_t kRegScratch = 0x16;        // NR #16 stores its byte
constexpr uint8_t kByte = 0x5A;

/// DeZog's wire bank byte: bank + 1, 0 = no bank
constexpr uint8_t Wire(uint8_t bank) { return static_cast<uint8_t>(bank + 1); }
}  // namespace

/// region <Pentagon (128K)>

class DezogBanks_test : public DezogEmulatorFixture
{
protected:
    /// Let the machine run a few frames; true when no breakpoint stopped it
    bool runsWithoutStop()
    {
        freeRunFrames(4);
        return noFurtherEvents();
    }
};

TEST_F(DezogBanks_test, TwoBreakpointsAtOneAddressOwnTheirIds)
{
    // DeZog sends one CMD_ADD_BREAKPOINT per breakpoint, logpoint and assertion: removing one keeps the other
    const uint16_t a = _adapter->addBreakpoint(PROGRAM_JP);
    const uint16_t b = _adapter->addBreakpoint(PROGRAM_JP);
    ASSERT_NE(a, 0);
    ASSERT_NE(b, 0);
    EXPECT_NE(a, b);

    _adapter->removeBreakpoint(a);
    EXPECT_EQ(_emulator->GetBreakpointManager()->GetBreakpointById(a), nullptr);
    EXPECT_NE(_emulator->GetBreakpointManager()->GetBreakpointById(b), nullptr);
    _adapter->removeBreakpoint(b);
}

TEST_F(DezogBanks_test, ClearingTemporariesKeepsTheUserBreakpointAtTheSameAddress)
{
    const uint16_t user = _adapter->addBreakpoint(PROGRAM_JP);
    const uint16_t temp = _adapter->addBreakpoint(PROGRAM_JP, 0, "", true);
    ASSERT_NE(user, 0);
    ASSERT_NE(temp, 0);
    EXPECT_NE(user, temp);

    _adapter->clearTemporaryBreakpoints();
    EXPECT_NE(_emulator->GetBreakpointManager()->GetBreakpointById(user), nullptr);
    _adapter->removeBreakpoint(user);
}

TEST_F(DezogBanks_test, AWatchpointIsNotSharedWithAnEqualBreakpointOfAnotherOwner)
{
    BreakpointManager* bpManager = _emulator->GetBreakpointManager();
    const uint16_t other = bpManager->AddMemWriteBreakpoint(WATCH_TARGET);
    ASSERT_NE(other, BRK_INVALID);

    ASSERT_TRUE(_adapter->addWatchpoint(WATCH_TARGET, 0, 1, dzrp::WatchAccess::WRITE));
    _adapter->removeWatchpoint(WATCH_TARGET, 0, 1, dzrp::WatchAccess::WRITE);
    EXPECT_NE(bpManager->GetBreakpointById(other), nullptr) << "removing DeZog's watchpoint took the other one";
    bpManager->RemoveBreakpointByID(other);
}

TEST_F(DezogBanks_test, TheRomBanksAreOneMappingInSlotsAndBreakpoints)
{
    Memory* memory = _emulator->GetMemory();
    // The fixture boots into 48K BASIC: the SOS ROM is DeZog's bank 9
    EXPECT_EQ(_adapter->getSlots()[0], DezogDebugAdapter::ROM_BANK_BASE + 1);
    EXPECT_EQ(DezogDebugAdapter::bankForRomPage(*memory, memory->GetROMPage()), DezogDebugAdapter::ROM_BANK_BASE + 1);
    EXPECT_EQ(DezogDebugAdapter::romPageForBank(*memory, DezogDebugAdapter::ROM1_ALIAS),
              DezogDebugAdapter::romPageForBank(*memory, DezogDebugAdapter::ROM_BANK_BASE + 1));

    // EI; loop: JR loop -> IM 1 vectors to 0x0038 in the mapped ROM every frame
    _adapter->writeMemory(PROGRAM_START, {0xFB, 0x18, 0xFE});
    _adapter->setRegister(dzrp::RegisterId::PC, PROGRAM_START);
    _adapter->setRegister(dzrp::RegisterId::SP, 0xFF00);
    _adapter->setRegister(dzrp::RegisterId::IM, 1);

    // ROM bank 8 (the 128K editor ROM) is not mapped: no stop
    const uint16_t other = _adapter->addBreakpoint(0x0038, Wire(DezogDebugAdapter::ROM_BANK_BASE));
    ASSERT_NE(other, 0);
    EXPECT_TRUE(runsWithoutStop());
    _adapter->removeBreakpoint(other);

    // ROM bank 9 is: the stop names it
    const uint16_t mapped = _adapter->addBreakpoint(0x0038, Wire(DezogDebugAdapter::ROM_BANK_BASE + 1));
    ASSERT_NE(mapped, 0);
    _adapter->resume();
    PauseEvent ev{};
    ASSERT_TRUE(waitForEvent(ev));
    EXPECT_EQ(ev.reason, dzrp::BreakReason::BREAKPOINT);
    EXPECT_EQ(ev.address, 0x0038);
    EXPECT_EQ(ev.bank, Wire(DezogDebugAdapter::ROM_BANK_BASE + 1));
    _adapter->removeBreakpoint(mapped);
}

TEST_F(DezogBanks_test, TheStopCarriesTheBankOfItsAddress)
{
    // NTF_PAUSE's bank byte is bank + 1 of the stop address: the program at 0x8000 runs in bank 2
    installProgram();
    const uint16_t id = _adapter->addBreakpoint(PROGRAM_JP);
    ASSERT_NE(id, 0);
    _adapter->resume();
    PauseEvent ev{};
    ASSERT_TRUE(waitForEvent(ev));
    EXPECT_EQ(ev.address, PROGRAM_JP);
    EXPECT_EQ(ev.bank, Wire(2));
    _adapter->removeBreakpoint(id);
}

TEST_F(DezogBanks_test, APortWriteGoesThroughTheMachinesDecoder)
{
    // Port #FE is the border: a raw port array would not reach the screen
    _adapter->writePort(0x00FE, 0x05);
    EXPECT_EQ(_emulator->GetContext()->pScreen->GetBorderColor(), 5);
    _adapter->writePort(0x00FE, 0x02);
    EXPECT_EQ(_emulator->GetContext()->pScreen->GetBorderColor(), 2);
}

TEST_F(DezogBanks_test, InterruptsOnOffSetsBothFlipFlops)
{
    Z80State* z80 = _emulator->GetZ80State();
    _adapter->setInterrupts(true);
    EXPECT_EQ(z80->iff1, 1);
    EXPECT_EQ(z80->iff2, 1);
    _adapter->setInterrupts(false);
    EXPECT_EQ(z80->iff1, 0);
    EXPECT_EQ(z80->iff2, 0);
}

/// endregion </Pentagon (128K)>

/// region <48K>

class DezogBanks48K_test : public DezogBanks_test
{
protected:
    std::string ModelName() const override { return "48K"; }
};

TEST_F(DezogBanks48K_test, TheSlotsAreBankZeroAndOneAndBreakpointsUseThem)
{
    ASSERT_EQ(_adapter->getMachineType(), dzrp::MachineType::ZX48K);
    EXPECT_EQ(_adapter->getSlots(), (std::vector<uint8_t>{0, 1}));

    // Bank 0 is the ROM, 0x0000-0x3FFF only: a RAM address in it is refused
    EXPECT_EQ(_adapter->addBreakpoint(PROGRAM_JP, Wire(0)), 0);

    installProgram();
    const uint16_t id = _adapter->addBreakpoint(PROGRAM_JP, Wire(1));
    ASSERT_NE(id, 0);
    _adapter->resume();
    PauseEvent ev{};
    ASSERT_TRUE(waitForEvent(ev));
    EXPECT_EQ(ev.address, PROGRAM_JP);
    EXPECT_EQ(ev.bank, Wire(1));
    _adapter->removeBreakpoint(id);
}

/// endregion </48K>

/// region <ZX Next>

class DezogBanksNext_test : public DezogBanks_test
{
protected:
    std::string ModelName() const override { return "NEXT"; }

    void mapProgramSlot(uint8_t bank) { _adapter->writeNextReg(static_cast<uint8_t>(kRegMmu0 + kProgramSlot), bank); }
};

TEST_F(DezogBanksNext_test, ABankBreakpointFiresOnlyWhileThatEightKilobyteBankIsMapped)
{
    ASSERT_EQ(_adapter->getMachineType(), dzrp::MachineType::ZXNEXT);
    // The same program in both halves of one 16K page; the breakpoint is on the first half
    mapProgramSlot(kBankB);
    installProgram();
    mapProgramSlot(kBankA);
    installProgram();

    const uint16_t id = _adapter->addBreakpoint(PROGRAM_JP, Wire(kBankA));
    ASSERT_NE(id, 0);

    mapProgramSlot(kBankB);
    EXPECT_TRUE(runsWithoutStop()) << "the other half of the 16K page stopped on bank " << int(kBankA) << "'s breakpoint";

    mapProgramSlot(kBankA);
    _adapter->setRegister(dzrp::RegisterId::PC, PROGRAM_START);
    _adapter->resume();
    PauseEvent ev{};
    ASSERT_TRUE(waitForEvent(ev));
    EXPECT_EQ(ev.reason, dzrp::BreakReason::BREAKPOINT);
    EXPECT_EQ(ev.address, PROGRAM_JP);
    EXPECT_EQ(ev.bank, Wire(kBankA)) << "the stop names the 8K bank in the address' slot";
    _adapter->removeBreakpoint(id);
}

TEST_F(DezogBanksNext_test, WriteBankFillsAnEightKilobyteBank)
{
    std::vector<uint8_t> data(0x2000);
    for (size_t i = 0; i < data.size(); i++)
        data[i] = static_cast<uint8_t>(i * 7 + 1);
    _adapter->writeBankMemory(kBankB, 0, {0, 0});

    _adapter->writeBank(kBankA, data);
    EXPECT_EQ(_adapter->readBankMemory(kBankA, 0, 0x2000), data);
    EXPECT_EQ(_adapter->readBankMemory(kBankB, 0, 2), (std::vector<uint8_t>{0, 0})) << "the other half is another bank";
}

TEST_F(DezogBanksNext_test, WriteMemoryGoesThroughTheMmu)
{
    mapProgramSlot(kBankA);
    _adapter->writeMemory(0x8000, {0x11, 0x22});
    EXPECT_EQ(_adapter->readBankMemory(kBankA, 0, 2), (std::vector<uint8_t>{0x11, 0x22}));
    EXPECT_EQ(_adapter->readMemory(0x8000, 2), (std::vector<uint8_t>{0x11, 0x22}));

    mapProgramSlot(kBankB);
    EXPECT_NE(_adapter->readMemory(0x8000, 2), (std::vector<uint8_t>{0x11, 0x22}));
}

TEST_F(DezogBanksNext_test, TheRomBankReadsWhatTheCpuReads)
{
    // MMU 0xFF is the ROM in slots 0 and 1: READ_BANK_MEM 0xFF at 0 equals READ_MEM at 0
    _adapter->writeNextReg(kRegMmu0, 0xFF);
    _adapter->writeNextReg(kRegMmu0 + 1, 0xFF);
    EXPECT_EQ(_adapter->readBankMemory(0xFF, 0, 16), _adapter->readMemory(0x0000, 16));
}

TEST_F(DezogBanksNext_test, PortsReachTheNextRegisters)
{
    _adapter->writePort(0x243B, kRegScratch);
    _adapter->writePort(0x253B, kByte);
    EXPECT_EQ(_adapter->readNextReg(kRegScratch), kByte);
    EXPECT_EQ(_adapter->readPort(0x253B), kByte);
}

/// endregion </ZX Next>
