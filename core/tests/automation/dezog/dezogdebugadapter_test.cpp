// DezogDebugAdapter tests: IDebugInterface over a live emulator (no sockets)

#include "dezogtestfixture.h"

#include "emulator/video/screen.h"

class DezogDebugAdapter_test : public DezogEmulatorFixture
{
};

/// region <Binding / machine info>

TEST_F(DezogDebugAdapter_test, ExplicitBindingResolvesEmulator)
{
    EXPECT_EQ(_adapter->getEmulator().get(), _emulator.get());
}

TEST_F(DezogDebugAdapter_test, UnboundAdapterResolvesMostRecentEmulator)
{
    DezogDebugAdapter dynamic;
    EXPECT_EQ(dynamic.getEmulator().get(), _emulator.get());
}

TEST_F(DezogDebugAdapter_test, MachineTypeFollowsMemoryModel)
{
    // Default test emulator is Pentagon → 128K-style machine
    EXPECT_EQ(_adapter->getMachineType(), dzrp::MachineType::ZX128K);

    _emulator->GetContext()->config.mem_model = MM_SPECTRUM48;
    EXPECT_EQ(_adapter->getMachineType(), dzrp::MachineType::ZX48K);
    EXPECT_EQ(_adapter->getSlots().size(), 2u);

    _emulator->GetContext()->config.mem_model = MM_PENTAGON;
}

TEST_F(DezogDebugAdapter_test, IsPausedReflectsEmulator)
{
    EXPECT_TRUE(_adapter->isPaused());
    _adapter->resume();
    EXPECT_FALSE(_adapter->isPaused());
    _emulator->Pause();
    EXPECT_TRUE(_adapter->isPaused());
}

/// endregion </Binding / machine info>

/// region <Registers>

TEST_F(DezogDebugAdapter_test, RegistersReflectZ80State)
{
    Z80State* z80 = _emulator->GetZ80State();
    ASSERT_NE(z80, nullptr);

    z80->pc = 0x1234;
    z80->sp = 0x5678;
    z80->af = 0x9ABC;
    z80->bc = 0xDEF0;
    z80->de = 0x1111;
    z80->hl = 0x2222;
    z80->ix = 0x3333;
    z80->iy = 0x4444;
    z80->alt.af = 0x5555;
    z80->alt.bc = 0x6666;
    z80->alt.de = 0x7777;
    z80->alt.hl = 0x8888;
    z80->i = 0x3F;
    z80->r_low = 0x7A;
    z80->im = 2;

    auto regs = _adapter->getRegisters();
    EXPECT_EQ(regs.pc, 0x1234);
    EXPECT_EQ(regs.sp, 0x5678);
    EXPECT_EQ(regs.af, 0x9ABC);
    EXPECT_EQ(regs.bc, 0xDEF0);
    EXPECT_EQ(regs.de, 0x1111);
    EXPECT_EQ(regs.hl, 0x2222);
    EXPECT_EQ(regs.ix, 0x3333);
    EXPECT_EQ(regs.iy, 0x4444);
    EXPECT_EQ(regs.af2, 0x5555);
    EXPECT_EQ(regs.bc2, 0x6666);
    EXPECT_EQ(regs.de2, 0x7777);
    EXPECT_EQ(regs.hl2, 0x8888);
    EXPECT_EQ(regs.i, 0x3F);
    EXPECT_EQ(regs.r, 0x7A);
    EXPECT_EQ(regs.im, 2);
}

TEST_F(DezogDebugAdapter_test, SetRegister16Bit)
{
    using R = dzrp::RegisterId;
    _adapter->setRegister(R::PC, 0x8000);
    _adapter->setRegister(R::SP, 0xFF00);
    _adapter->setRegister(R::AF, 0x1122);
    _adapter->setRegister(R::BC, 0x3344);
    _adapter->setRegister(R::DE, 0x5566);
    _adapter->setRegister(R::HL, 0x7788);
    _adapter->setRegister(R::IX, 0x99AA);
    _adapter->setRegister(R::IY, 0xBBCC);
    _adapter->setRegister(R::AF2, 0xDDEE);
    _adapter->setRegister(R::BC2, 0xFF01);
    _adapter->setRegister(R::DE2, 0x0203);
    _adapter->setRegister(R::HL2, 0x0405);
    _adapter->setRegister(R::IM, 1);

    const Z80State* z80 = _emulator->GetZ80State();
    EXPECT_EQ(z80->pc, 0x8000);
    EXPECT_EQ(z80->sp, 0xFF00);
    EXPECT_EQ(z80->af, 0x1122);
    EXPECT_EQ(z80->bc, 0x3344);
    EXPECT_EQ(z80->de, 0x5566);
    EXPECT_EQ(z80->hl, 0x7788);
    EXPECT_EQ(z80->ix, 0x99AA);
    EXPECT_EQ(z80->iy, 0xBBCC);
    EXPECT_EQ(z80->alt.af, 0xDDEE);
    EXPECT_EQ(z80->alt.bc, 0xFF01);
    EXPECT_EQ(z80->alt.de, 0x0203);
    EXPECT_EQ(z80->alt.hl, 0x0405);
    EXPECT_EQ(z80->im, 1);
}

TEST_F(DezogDebugAdapter_test, SetRegister8BitMergesIntoPair)
{
    using R = dzrp::RegisterId;
    _adapter->setRegister(R::AF, 0x0000);
    _adapter->setRegister(R::A, 0x42);
    _adapter->setRegister(R::F, 0x01);
    _adapter->setRegister(R::HL, 0x0000);
    _adapter->setRegister(R::H, 0xAB);
    _adapter->setRegister(R::L, 0xCD);
    _adapter->setRegister(R::IX, 0x0000);
    _adapter->setRegister(R::IXH, 0x12);
    _adapter->setRegister(R::IXL, 0x34);
    _adapter->setRegister(R::IY, 0x0000);
    _adapter->setRegister(R::IYH, 0x56);
    _adapter->setRegister(R::IYL, 0x78);
    _adapter->setRegister(R::BC2, 0x0000);
    _adapter->setRegister(R::B2, 0x9A);
    _adapter->setRegister(R::C2, 0xBC);
    _adapter->setRegister(R::R, 0x55);
    _adapter->setRegister(R::I, 0x66);

    auto regs = _adapter->getRegisters();
    EXPECT_EQ(regs.af, 0x4201);
    EXPECT_EQ(regs.hl, 0xABCD);
    EXPECT_EQ(regs.ix, 0x1234);
    EXPECT_EQ(regs.iy, 0x5678);
    EXPECT_EQ(regs.bc2, 0x9ABC);
    EXPECT_EQ(regs.r, 0x55);
    EXPECT_EQ(regs.i, 0x66);
}

TEST_F(DezogDebugAdapter_test, SetRegisterIMClampsToValidRange)
{
    _adapter->setRegister(dzrp::RegisterId::IM, 7);
    EXPECT_EQ(_adapter->getRegisters().im, 2);
}

/// endregion </Registers>

/// region <Memory>

TEST_F(DezogDebugAdapter_test, MemoryRoundTrip)
{
    std::vector<uint8_t> pattern = {0xDE, 0xAD, 0xBE, 0xEF};
    _adapter->writeMemory(0x8000, pattern);
    EXPECT_EQ(_adapter->readMemory(0x8000, 4), pattern);

    // Underlying memory sees the same bytes
    Memory* memory = _emulator->GetMemory();
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x8002), 0xBE);
}

TEST_F(DezogDebugAdapter_test, MemoryWrapsAt64K)
{
    std::vector<uint8_t> pattern = {0x11, 0x22, 0x33, 0x44};
    _adapter->writeMemory(0xFFFE, pattern);
    EXPECT_EQ(_adapter->readMemory(0xFFFE, 4), pattern);

    // Bytes 2..3 wrapped to 0x0000/0x0001 - ROM there, so wrapping writes are
    // discarded but the read path must still wrap cleanly (no crash, 4 bytes)
    auto wrapped = _adapter->readMemory(0x0000, 2);
    ASSERT_EQ(wrapped.size(), 2u);
}

TEST_F(DezogDebugAdapter_test, ReadMemoryZeroLength)
{
    EXPECT_TRUE(_adapter->readMemory(0x8000, 0).empty());
}

/// endregion </Memory>

/// region <Banking>

TEST_F(DezogDebugAdapter_test, SlotsReport128KLayout)
{
    auto slots = _adapter->getSlots();
    ASSERT_EQ(slots.size(), 4u);
    EXPECT_TRUE(slots[0] == DezogDebugAdapter::ROM_BANK_BASE || slots[0] == DezogDebugAdapter::ROM_BANK_BASE + 1);
    EXPECT_EQ(slots[1], 5);
    EXPECT_EQ(slots[2], 2);
}

TEST_F(DezogDebugAdapter_test, SetSlotRAMBank3)
{
    _adapter->setSlot(3, 7);
    EXPECT_EQ(_adapter->getSlots()[3], 7);
    EXPECT_EQ(_emulator->GetMemory()->GetRAMPageForBank3(), 7);

    _adapter->setSlot(3, 0);
    EXPECT_EQ(_adapter->getSlots()[3], 0);
}

TEST_F(DezogDebugAdapter_test, SetSlotROMAliases)
{
    _adapter->setSlot(0, DezogDebugAdapter::ROM1_ALIAS);
    EXPECT_EQ(_adapter->getSlots()[0], DezogDebugAdapter::ROM_BANK_BASE + 1);
    EXPECT_EQ(_emulator->GetMemory()->GetROMPage() & 1, 1);

    _adapter->setSlot(0, DezogDebugAdapter::ROM0_ALIAS);
    EXPECT_EQ(_adapter->getSlots()[0], DezogDebugAdapter::ROM_BANK_BASE);

    _adapter->setSlot(0, DezogDebugAdapter::ROM_BANK_BASE + 1);
    EXPECT_EQ(_adapter->getSlots()[0], DezogDebugAdapter::ROM_BANK_BASE + 1);
}

TEST_F(DezogDebugAdapter_test, SetSlotIgnoresOutOfRange)
{
    auto before = _adapter->getSlots();
    _adapter->setSlot(7, 3);  // no such slot
    EXPECT_EQ(_adapter->getSlots(), before);
}

TEST_F(DezogDebugAdapter_test, WriteBankVisibleThroughSlot)
{
    std::vector<uint8_t> data(16, 0x5A);
    data[0] = 0xA5;
    _adapter->writeBank(6, data);

    _adapter->setSlot(3, 6);
    auto read = _adapter->readMemory(0xC000, 16);
    EXPECT_EQ(read, data);

    _adapter->setSlot(3, 0);
}

TEST_F(DezogDebugAdapter_test, WriteBankIgnoresInvalidBank)
{
    // Must not crash / write anywhere
    _adapter->writeBank(0xFF, std::vector<uint8_t>(16, 0x00));
    SUCCEED();
}

/// endregion </Banking>

/// region <Breakpoints>

TEST_F(DezogDebugAdapter_test, AddBreakpointRegistersWithOwner)
{
    uint16_t id = _adapter->addBreakpoint(0x8006);
    ASSERT_NE(id, 0);

    BreakpointManager* bpManager = _emulator->GetBreakpointManager();
    BreakpointDescriptor* desc = bpManager->GetBreakpointById(id);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->z80address, 0x8006);
    EXPECT_EQ(desc->owner, DezogDebugAdapter::BREAKPOINT_OWNER);
    EXPECT_TRUE(desc->memoryType & BRK_MEM_EXECUTE);
    EXPECT_FALSE(_adapter->isBreakpointTemporary(id));

    // Adding a breakpoint switches the emulator into debug mode
    EXPECT_TRUE(_emulator->IsDebug());

    _adapter->removeBreakpoint(id);
    EXPECT_EQ(bpManager->GetBreakpointById(id), nullptr);
}

TEST_F(DezogDebugAdapter_test, AddBreakpointInBankUsesPageDescriptor)
{
    // Wire bank 3 → DZRP bank 2 → RAM page 2
    uint16_t id = _adapter->addBreakpoint(0x8010, 3);
    ASSERT_NE(id, 0);

    BreakpointDescriptor* desc = _emulator->GetBreakpointManager()->GetBreakpointById(id);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->z80address, 0x8010);
    _adapter->removeBreakpoint(id);
}

TEST_F(DezogDebugAdapter_test, TemporaryBreakpointsTrackedAndCleared)
{
    uint16_t permanent = _adapter->addBreakpoint(0x8006);
    uint16_t temp1 = _adapter->addBreakpoint(0x8001, 0, "", true);
    uint16_t temp2 = _adapter->addBreakpoint(0x8003, 0, "", true);
    ASSERT_NE(permanent, 0);
    ASSERT_NE(temp1, 0);
    ASSERT_NE(temp2, 0);

    EXPECT_EQ(_adapter->getTemporaryBreakpointCount(), 2u);
    EXPECT_TRUE(_adapter->isBreakpointTemporary(temp1));
    EXPECT_TRUE(_adapter->isBreakpointTemporary(temp2));
    EXPECT_FALSE(_adapter->isBreakpointTemporary(permanent));

    _adapter->clearTemporaryBreakpoints();

    BreakpointManager* bpManager = _emulator->GetBreakpointManager();
    EXPECT_EQ(_adapter->getTemporaryBreakpointCount(), 0u);
    EXPECT_EQ(bpManager->GetBreakpointById(temp1), nullptr);
    EXPECT_EQ(bpManager->GetBreakpointById(temp2), nullptr);
    EXPECT_NE(bpManager->GetBreakpointById(permanent), nullptr);

    _adapter->removeBreakpoint(permanent);
}

TEST_F(DezogDebugAdapter_test, RemoveTemporaryBreakpointUntracksIt)
{
    uint16_t temp = _adapter->addBreakpoint(0x8001, 0, "", true);
    ASSERT_NE(temp, 0);
    _adapter->removeBreakpoint(temp);
    EXPECT_EQ(_adapter->getTemporaryBreakpointCount(), 0u);
    EXPECT_FALSE(_adapter->isBreakpointTemporary(temp));
}

TEST_F(DezogDebugAdapter_test, RemoveUnknownBreakpointIsHarmless)
{
    _adapter->removeBreakpoint(0xBEEF);
    SUCCEED();
}

/// endregion </Breakpoints>

/// region <Watchpoints>

TEST_F(DezogDebugAdapter_test, WatchpointCreatesOneBreakpointPerByte)
{
    BreakpointManager* bpManager = _emulator->GetBreakpointManager();
    size_t before = bpManager->GetBreakpointsCount();

    ASSERT_TRUE(_adapter->addWatchpoint(0x9000, 0, 4, dzrp::WatchAccess::WRITE));
    EXPECT_EQ(bpManager->GetBreakpointsCount(), before + 4);
    EXPECT_EQ(_adapter->getWatchpointCount(), 1u);

    // Idempotent re-add
    ASSERT_TRUE(_adapter->addWatchpoint(0x9000, 0, 4, dzrp::WatchAccess::WRITE));
    EXPECT_EQ(bpManager->GetBreakpointsCount(), before + 4);

    _adapter->removeWatchpoint(0x9000, 0, 4, dzrp::WatchAccess::WRITE);
    EXPECT_EQ(bpManager->GetBreakpointsCount(), before);
    EXPECT_EQ(_adapter->getWatchpointCount(), 0u);
}

TEST_F(DezogDebugAdapter_test, WatchpointAccessTypesMapToMemoryTypes)
{
    BreakpointManager* bpManager = _emulator->GetBreakpointManager();

    ASSERT_TRUE(_adapter->addWatchpoint(0x9100, 0, 1, dzrp::WatchAccess::READ));
    ASSERT_TRUE(_adapter->addWatchpoint(0x9200, 0, 1, dzrp::WatchAccess::READ_WRITE));

    bool sawReadOnly = false, sawReadWrite = false;
    for (const auto& [id, desc] : bpManager->GetAllBreakpoints())
    {
        if (desc->z80address == 0x9100)
            sawReadOnly = (desc->memoryType & BRK_MEM_READ) && !(desc->memoryType & BRK_MEM_WRITE);
        if (desc->z80address == 0x9200)
            sawReadWrite = (desc->memoryType & BRK_MEM_READ) && (desc->memoryType & BRK_MEM_WRITE);
    }
    EXPECT_TRUE(sawReadOnly);
    EXPECT_TRUE(sawReadWrite);

    _adapter->removeWatchpoint(0x9100, 0, 1, dzrp::WatchAccess::READ);
    _adapter->removeWatchpoint(0x9200, 0, 1, dzrp::WatchAccess::READ_WRITE);
}

TEST_F(DezogDebugAdapter_test, WatchpointRejectsZeroSize)
{
    EXPECT_FALSE(_adapter->addWatchpoint(0x9000, 0, 0, dzrp::WatchAccess::WRITE));
}

TEST_F(DezogDebugAdapter_test, WatchpointClampsAt64K)
{
    BreakpointManager* bpManager = _emulator->GetBreakpointManager();
    size_t before = bpManager->GetBreakpointsCount();

    ASSERT_TRUE(_adapter->addWatchpoint(0xFFFE, 0, 8, dzrp::WatchAccess::WRITE));
    EXPECT_EQ(bpManager->GetBreakpointsCount(), before + 2);

    _adapter->removeWatchpoint(0xFFFE, 0, 8, dzrp::WatchAccess::WRITE);
    EXPECT_EQ(bpManager->GetBreakpointsCount(), before);
}

TEST_F(DezogDebugAdapter_test, RemoveUnknownWatchpointIsHarmless)
{
    _adapter->removeWatchpoint(0x1234, 0, 1, dzrp::WatchAccess::READ);
    SUCCEED();
}

/// endregion </Watchpoints>

/// region <Execution + notifications>

TEST_F(DezogDebugAdapter_test, PauseNotifiesManualWithPC)
{
    installProgram();
    _adapter->resume();
    ASSERT_FALSE(_emulator->IsPaused());

    _adapter->pause();
    EXPECT_TRUE(_emulator->IsPaused());

    PauseEvent ev{};
    ASSERT_TRUE(waitForEvent(ev));
    EXPECT_EQ(ev.reason, dzrp::BreakReason::MANUAL);
    EXPECT_EQ(ev.address, _emulator->GetZ80State()->pc);
}

TEST_F(DezogDebugAdapter_test, PauseWhenAlreadyPausedStillNotifies)
{
    ASSERT_TRUE(_emulator->IsPaused());
    _adapter->setRegister(dzrp::RegisterId::PC, 0x1234);

    _adapter->pause();

    PauseEvent ev{};
    ASSERT_TRUE(waitForEvent(ev));
    EXPECT_EQ(ev.reason, dzrp::BreakReason::MANUAL);
    EXPECT_EQ(ev.address, 0x1234);
}

TEST_F(DezogDebugAdapter_test, ExecutionBreakpointHitNotifiesAndPauses)
{
    installProgram();
    uint16_t id = _adapter->addBreakpoint(PROGRAM_JP);
    ASSERT_NE(id, 0);

    _adapter->resume();

    PauseEvent ev{};
    ASSERT_TRUE(waitForEvent(ev));
    EXPECT_EQ(ev.reason, dzrp::BreakReason::BREAKPOINT);
    EXPECT_EQ(ev.address, PROGRAM_JP);
    EXPECT_TRUE(_emulator->IsPaused());
    EXPECT_EQ(_emulator->GetZ80State()->pc, PROGRAM_JP);

    // Exactly one notification for one stop
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(pendingEvents(), 0u);

    _adapter->removeBreakpoint(id);
}

TEST_F(DezogDebugAdapter_test, TemporaryBreakpointHitThenCleared)
{
    installProgram();
    uint16_t temp = _adapter->addBreakpoint(PROGRAM_STORE, 0, "", true);
    ASSERT_NE(temp, 0);

    _adapter->resume();

    PauseEvent ev{};
    ASSERT_TRUE(waitForEvent(ev));
    EXPECT_EQ(ev.reason, dzrp::BreakReason::BREAKPOINT);
    EXPECT_EQ(ev.address, PROGRAM_STORE);

    // Server clears temp BPs on notification - emulate that contract here
    _adapter->clearTemporaryBreakpoints();
    EXPECT_EQ(_adapter->getTemporaryBreakpointCount(), 0u);
    EXPECT_EQ(_emulator->GetBreakpointManager()->GetBreakpointById(temp), nullptr);

    // Resume must now run freely (no stale temp breakpoint re-triggers)
    _adapter->resume();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_FALSE(_emulator->IsPaused());
    EXPECT_EQ(pendingEvents(), 0u);
}

TEST_F(DezogDebugAdapter_test, ResumeAfterBreakpointContinuesToNextHit)
{
    installProgram();
    uint16_t id = _adapter->addBreakpoint(PROGRAM_JP);
    ASSERT_NE(id, 0);

    _adapter->resume();
    PauseEvent first{};
    ASSERT_TRUE(waitForEvent(first));

    _adapter->resume();
    PauseEvent second{};
    ASSERT_TRUE(waitForEvent(second));
    EXPECT_EQ(second.reason, dzrp::BreakReason::BREAKPOINT);
    EXPECT_EQ(second.address, PROGRAM_JP);

    _adapter->removeBreakpoint(id);
}

TEST_F(DezogDebugAdapter_test, WriteWatchpointHitNotifies)
{
    installProgram();
    ASSERT_TRUE(_adapter->addWatchpoint(WATCH_TARGET, 0, 1, dzrp::WatchAccess::WRITE));

    _adapter->resume();

    PauseEvent ev{};
    ASSERT_TRUE(waitForEvent(ev));
    EXPECT_EQ(ev.reason, dzrp::BreakReason::WATCHPOINT_WRITE);
    EXPECT_EQ(ev.address, WATCH_TARGET);
    EXPECT_TRUE(_emulator->IsPaused());

    _adapter->removeWatchpoint(WATCH_TARGET, 0, 1, dzrp::WatchAccess::WRITE);
}

TEST_F(DezogDebugAdapter_test, ReadWriteWatchpointReportsReadReason)
{
    installProgram();
    ASSERT_TRUE(_adapter->addWatchpoint(WATCH_TARGET, 0, 1, dzrp::WatchAccess::READ_WRITE));

    _adapter->resume();

    PauseEvent ev{};
    ASSERT_TRUE(waitForEvent(ev));
    // Direction is not carried by the payload: combined watchpoints report READ
    EXPECT_EQ(ev.reason, dzrp::BreakReason::WATCHPOINT_READ);
    EXPECT_EQ(ev.address, WATCH_TARGET);

    _adapter->removeWatchpoint(WATCH_TARGET, 0, 1, dzrp::WatchAccess::READ_WRITE);
}

TEST_F(DezogDebugAdapter_test, BreakpointOnOtherEmulatorIsIgnored)
{
    // A second, independent emulator hitting a breakpoint must not leak into our session
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto other = manager->CreateEmulator("dezog-other", LoggerLevel::LogError);
    ASSERT_NE(other, nullptr);
    other->GetContext()->config.reset_rom = RM_SOS;
    other->Reset();
    other->StartAsync();
    for (int i = 0; i < 50 && other->GetState() != StateRun; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    other->Pause();

    DezogDebugAdapter otherAdapter(other);
    std::vector<uint8_t> program = {0xF3, 0x3E, 0x01, 0x32, 0x00, 0x90, 0xC3, 0x01, 0x80};
    otherAdapter.writeMemory(PROGRAM_START, program);
    otherAdapter.setRegister(dzrp::RegisterId::PC, PROGRAM_START);
    uint16_t id = otherAdapter.addBreakpoint(PROGRAM_JP);
    ASSERT_NE(id, 0);

    otherAdapter.resume();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_TRUE(other->IsPaused());

    // Our adapter (bound to _emulator) saw nothing
    EXPECT_EQ(pendingEvents(), 0u);

    otherAdapter.removeBreakpoint(id);
    other->GetBreakpointManager()->ClearBreakpoints();
    other->Resume();
    std::string otherId = other->GetId();
    other.reset();
    manager->RemoveEmulator(otherId);
}

/// endregion </Execution + notifications>

/// region <State / border>

TEST_F(DezogDebugAdapter_test, CaptureAndRestoreStateRoundTrip)
{
    _adapter->writeMemory(0x8000, {0xAA, 0xBB});
    _adapter->setRegister(dzrp::RegisterId::PC, 0x8000);
    _adapter->setRegister(dzrp::RegisterId::HL, 0x1357);

    auto state = _adapter->captureState();
    ASSERT_FALSE(state.empty());
    EXPECT_TRUE(_emulator->IsPaused());

    _adapter->writeMemory(0x8000, {0x11, 0x22});
    _adapter->setRegister(dzrp::RegisterId::PC, 0x9000);
    _adapter->setRegister(dzrp::RegisterId::HL, 0x0000);

    _adapter->restoreState(state);
    EXPECT_TRUE(_emulator->IsPaused());

    std::vector<uint8_t> expected = {0xAA, 0xBB};
    EXPECT_EQ(_adapter->readMemory(0x8000, 2), expected);
    auto regs = _adapter->getRegisters();
    EXPECT_EQ(regs.pc, 0x8000);
    EXPECT_EQ(regs.hl, 0x1357);
}

TEST_F(DezogDebugAdapter_test, RestoreEmptyStateIsHarmless)
{
    _adapter->setRegister(dzrp::RegisterId::PC, 0x4321);
    _adapter->restoreState({});
    EXPECT_EQ(_adapter->getRegisters().pc, 0x4321);
}

TEST_F(DezogDebugAdapter_test, SetBorderUpdatesScreen)
{
    _adapter->setBorder(5);
    EXPECT_EQ(_emulator->GetContext()->pScreen->GetBorderColor(), 5);

    _adapter->setBorder(0xFF);  // masked to 3 bits
    EXPECT_EQ(_emulator->GetContext()->pScreen->GetBorderColor(), 7);
}

/// endregion </State / border>
