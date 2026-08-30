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

/// region <Session lifecycle>

TEST_F(DezogDebugAdapter_test, SessionCloseDropsDezogBreakpointsKeepsOthersAndResumes)
{
    BreakpointManager* bpManager = _emulator->GetBreakpointManager();

    // Foreign breakpoint (CLI/GUI owner) must survive
    uint16_t foreign = bpManager->AddExecutionBreakpoint(0x1234);
    ASSERT_NE(foreign, BRK_INVALID);

    uint16_t bp = _adapter->addBreakpoint(PROGRAM_JP);
    uint16_t temp = _adapter->addBreakpoint(PROGRAM_STORE, 0, "", true);
    ASSERT_TRUE(_adapter->addWatchpoint(WATCH_TARGET, 0, 2, dzrp::WatchAccess::WRITE));
    ASSERT_NE(bp, 0);
    ASSERT_NE(temp, 0);
    ASSERT_TRUE(_emulator->IsPaused());

    _adapter->onSessionClosed();

    EXPECT_EQ(bpManager->GetBreakpointById(bp), nullptr);
    EXPECT_EQ(bpManager->GetBreakpointById(temp), nullptr);
    EXPECT_NE(bpManager->GetBreakpointById(foreign), nullptr);
    EXPECT_EQ(bpManager->GetBreakpointsCount(), 1u);
    EXPECT_EQ(_adapter->getTemporaryBreakpointCount(), 0u);
    EXPECT_EQ(_adapter->getWatchpointCount(), 0u);
    EXPECT_FALSE(_emulator->IsPaused());

    bpManager->RemoveBreakpointByID(foreign);
}

TEST_F(DezogDebugAdapter_test, SessionCloseWhileRunningIsHarmless)
{
    _adapter->resume();
    _adapter->onSessionClosed();
    EXPECT_FALSE(_emulator->IsPaused());
    EXPECT_EQ(pendingEvents(), 0u);
}

/// endregion </Session lifecycle>

/// region <Real-world shapes>

TEST_F(DezogDebugAdapter_test, ROMBreakpointHitViaIM1Interrupt)
{
    // EI; loop: JR loop  → maskable interrupt every frame vectors to ROM 0x0038
    _adapter->writeMemory(PROGRAM_START, {0xFB, 0x18, 0xFE});
    _adapter->setRegister(dzrp::RegisterId::PC, PROGRAM_START);
    _adapter->setRegister(dzrp::RegisterId::SP, 0xFF00);
    _adapter->setRegister(dzrp::RegisterId::IM, 1);

    uint16_t id = _adapter->addBreakpoint(0x0038);
    ASSERT_NE(id, 0);

    _adapter->resume();

    PauseEvent ev{};
    ASSERT_TRUE(waitForEvent(ev));
    EXPECT_EQ(ev.reason, dzrp::BreakReason::BREAKPOINT);
    EXPECT_EQ(ev.address, 0x0038);
    EXPECT_EQ(_emulator->GetZ80State()->pc, 0x0038);

    _adapter->removeBreakpoint(id);
}

TEST_F(DezogDebugAdapter_test, RapidStepLoopNeverLosesOrDuplicatesStops)
{
    installProgram();
    _adapter->setRegister(dzrp::RegisterId::PC, PROGRAM_LOOP);

    // Instruction cycle of the RAM program: 8001 → 8003 → 8006 → 8001 ...
    const uint16_t next[] = {PROGRAM_STORE, PROGRAM_JP, PROGRAM_LOOP};
    uint16_t pc = PROGRAM_LOOP;

    for (int i = 0; i < 60; ++i)
    {
        uint16_t target = (pc == PROGRAM_LOOP) ? next[0] : (pc == PROGRAM_STORE) ? next[1] : next[2];

        _adapter->clearTemporaryBreakpoints();
        ASSERT_NE(_adapter->addBreakpoint(target, 0, "", true), 0) << "step " << i;
        _adapter->resume();

        PauseEvent ev{};
        ASSERT_TRUE(waitForEvent(ev)) << "step " << i;
        ASSERT_EQ(ev.address, target) << "step " << i;
        ASSERT_EQ(_emulator->GetZ80State()->pc, target) << "step " << i;
        _adapter->clearTemporaryBreakpoints();
        pc = target;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(pendingEvents(), 0u);
}

TEST_F(DezogDebugAdapter_test, ReadFull64KMatchesMemory)
{
    auto all = _adapter->readMemory(0x0000, 0xFFFF);
    ASSERT_EQ(all.size(), 0xFFFFu);

    Memory* memory = _emulator->GetMemory();
    for (uint32_t a : {0x0000u, 0x0038u, 0x3FFFu, 0x4000u, 0x8000u, 0xC000u, 0xFFFEu})
        EXPECT_EQ(all[a], memory->DirectReadFromZ80Memory(static_cast<uint16_t>(a))) << std::hex << a;
}

/// endregion </Real-world shapes>

/// region <Instruction history (TTD reverse debugging)>

#include "debugger/ttd/timetravelmanager.h"

class DezogHistory_test : public DezogEmulatorFixture
{
protected:
    // Run the RAM loop until the JP breakpoint `hits` times, leaving the
    // emulator paused at PROGRAM_JP with recorded history behind it.
    void runToJp(int hits)
    {
        uint16_t id = _adapter->addBreakpoint(PROGRAM_JP);
        ASSERT_NE(id, 0);
        for (int i = 0; i < hits; ++i)
        {
            _adapter->resume();
            PauseEvent ev{};
            ASSERT_TRUE(waitForEvent(ev)) << "hit " << i;
            ASSERT_EQ(ev.address, PROGRAM_JP);
        }
        _adapter->removeBreakpoint(id);
    }

    ttd::TimeTravelManager* ttd() { return _emulator->GetContext()->pTimeTravelManager; }
};

TEST_F(DezogHistory_test, AvailableButNotRecordingUntilSessionOpens)
{
    ASSERT_NE(ttd(), nullptr);
    EXPECT_TRUE(_adapter->isHistoryAvailable());
    EXPECT_FALSE(_adapter->isHistoryRecording());
    EXPECT_EQ(_adapter->getHistoryEntry(0), std::nullopt);  // no timeline yet

    _adapter->onSessionOpened();
    EXPECT_TRUE(_adapter->isHistoryRecording());
    EXPECT_TRUE(_emulator->IsPaused());  // starting a recording must not resume a paused target
}

TEST_F(DezogHistory_test, DisabledViaFlagReportsUnavailable)
{
    _adapter->setHistoryEnabled(false);
    EXPECT_FALSE(_adapter->isHistoryAvailable());
    _adapter->onSessionOpened();
    EXPECT_FALSE(_adapter->isHistoryRecording());
    EXPECT_EQ(_adapter->getHistoryEntry(0), std::nullopt);
}

// NOTE ON SEMANTICS: DeZog index i is served by physically seeking the TTD
// engine to a distinct earlier M1 boundary and reading the live state there.
// The exact instruction index i lands on is a property of TTD's M1-granular
// reverse-seek (a pre-execution breakpoint has already fetched the current
// instruction's M1), so these tests assert the *coherence* invariants that
// actually matter for a debugger, not a hard-coded PC-per-index trace:
//   - each entry's opcodes/SP-word match memory at that entry's PC/SP
//   - successive indices move strictly backward in TTD time
//   - PCs belong to the known instruction set of the recorded program
//   - out-of-range queries are NON-destructive: the adapter stays in browse
//     mode, the browsable history survives a bad index, and the return to the
//     present happens on the next forward command
static bool isProgramPc(uint16_t pc)
{
    return pc == 0x8000 || pc == 0x8001 || pc == 0x8003 || pc == 0x8006;
}

TEST_F(DezogHistory_test, EntriesWalkStrictlyBackThroughRecordedInstructions)
{
    _adapter->onSessionOpened();
    installProgram();
    runToJp(2);

    // Cache-backed strategy: entries carry historic regs/opcodes/slots straight
    // from the TTD per-frame decode cache; the emulator itself stays at the
    // present (no per-read seek). Monotonic-backward ordering is guaranteed by
    // the index→(frame,entry) mapping, so we assert the visible invariants.
    for (uint32_t i = 0; i < 6; ++i)
    {
        auto e = _adapter->getHistoryEntry(i);
        ASSERT_TRUE(e.has_value()) << "index " << i;
        EXPECT_EQ(_adapter->getHistoryCursor(), static_cast<int64_t>(i));
        EXPECT_EQ(e->slots.size(), 4u);
        // dzrp slot encoding like getSlots(): slot 0 = ROM (8/9), then RAM banks.
        // The TTD cache stores the raw ROM page - getHistoryEntry normalizes.
        EXPECT_TRUE(e->slots[0] == DezogDebugAdapter::ROM_BANK_BASE ||
                    e->slots[0] == DezogDebugAdapter::ROM_BANK_BASE + 1);
        EXPECT_EQ(e->slots[1], 5);
        EXPECT_EQ(e->slots[2], 2);
        EXPECT_TRUE(isProgramPc(e->regs.pc)) << "index " << i << " pc=" << std::hex << e->regs.pc;
    }

    // Eventually runs out of recorded history. Out-of-range is NON-destructive:
    // nothing moved under the cache strategy, so the adapter stays in browse
    // mode and the browsable history survives the bad index.
    bool sawEnd = false;
    for (uint32_t i = 6; i < 64; ++i)
    {
        if (!_adapter->getHistoryEntry(i).has_value())
        {
            sawEnd = true;
            EXPECT_EQ(_adapter->getHistoryCursor(), static_cast<int64_t>(i - 1));  // last good index
            EXPECT_TRUE(_adapter->isHistoryRecording());  // browse is read-only: capture continues
            EXPECT_EQ(_emulator->GetZ80State()->pc, PROGRAM_JP);  // present never disturbed
            auto again = _adapter->getHistoryEntry(0);
            ASSERT_TRUE(again.has_value()) << "out-of-range query wiped the browsable history";
            EXPECT_TRUE(isProgramPc(again->regs.pc));
            _adapter->pause();  // forward command → back to the present, recording resumes
            PauseEvent ev{};
            EXPECT_TRUE(waitForEvent(ev));  // consume the manual-pause notification
            EXPECT_EQ(_adapter->getHistoryCursor(), -1);
            EXPECT_TRUE(_adapter->isHistoryRecording());
            return;
        }
    }
    ASSERT_TRUE(sawEnd) << "history never reported end-of-range";
}

TEST_F(DezogHistory_test, OutOfRangeQueryKeepsBrowsableHistory)
{
    // Regression: a failed query must NOT wipe the browsable history. The old
    // seek-per-read code left history on failure (restoring the present and
    // restarting recording — the whole browsable past was lost to one bad
    // index); under the cache strategy nothing moved, so staying in browse
    // mode is correct and lossless.
    _adapter->onSessionOpened();
    installProgram();
    runToJp(1);

    ASSERT_TRUE(_adapter->getHistoryEntry(0).has_value());
    // Way past the oldest recorded instruction (also exercises the
    // early-terminated backward walk at the recording baseline)
    EXPECT_EQ(_adapter->getHistoryEntry(1'000'000), std::nullopt);
    EXPECT_TRUE(_adapter->isHistoryRecording());  // browse is read-only: capture continues, history intact
    auto again = _adapter->getHistoryEntry(1);
    ASSERT_TRUE(again.has_value()) << "bad index wiped the browsable history";
    EXPECT_EQ(_adapter->getHistoryCursor(), 1);
    EXPECT_EQ(_emulator->GetZ80State()->pc, PROGRAM_JP);  // present never disturbed
}

TEST_F(DezogHistory_test, EntryOpcodesAndSpWordAreCoherentWithMemory)
{
    _adapter->onSessionOpened();
    installProgram();
    runToJp(1);

    for (uint32_t i = 0; i < 4; ++i)
    {
        auto e = _adapter->getHistoryEntry(i);
        ASSERT_TRUE(e.has_value()) << i;
        // While materialized at this entry, live memory must match the entry payload
        auto mem = _adapter->readMemory(e->regs.pc, 4);
        for (int b = 0; b < 4; ++b)
            EXPECT_EQ(e->opcodes[b], mem[b]) << "index " << i << " byte " << b;
        auto sp = _adapter->readMemory(e->regs.sp, 2);
        EXPECT_EQ(e->spContent, static_cast<uint16_t>(sp[0] | (sp[1] << 8))) << "index " << i;
    }
}

TEST_F(DezogHistory_test, MemoryDuringBrowseIsPresentPerDeZogModel)
{
    // DeZog's model: during reverse debugging the memory you see is the ACTUAL
    // (present) memory, not historic — the cache-backed strategy matches this by
    // serving historic registers from the cache while leaving the emulator at the
    // present, so readMemory returns present memory (no per-read seek).
    _adapter->onSessionOpened();
    installProgram();
    runToJp(1);

    // Present: the store at 8003 has executed → watch target holds 1.
    EXPECT_EQ(_adapter->readMemory(WATCH_TARGET, 1)[0], 0x01);

    // Browse back to the DI at 8000: the ENTRY carries historic registers, but
    // readMemory still reflects the present (value 1), per the DeZog model.
    bool sawDi = false;
    for (uint32_t i = 0; i < 32; ++i)
    {
        auto e = _adapter->getHistoryEntry(i);
        if (!e.has_value())
            break;
        if (e->regs.pc == PROGRAM_START)
        {
            sawDi = true;
            EXPECT_EQ(_adapter->readMemory(WATCH_TARGET, 1)[0], 0x01) << "memory is present, not historic";
        }
    }
    EXPECT_TRUE(sawDi);

    // Resume returns to the present and continues; next hit as usual.
    uint16_t id = _adapter->addBreakpoint(PROGRAM_JP);
    _adapter->resume();
    EXPECT_EQ(_adapter->getHistoryCursor(), -1);
    PauseEvent ev{};
    ASSERT_TRUE(waitForEvent(ev));
    EXPECT_EQ(ev.address, PROGRAM_JP);
    EXPECT_EQ(_adapter->readMemory(WATCH_TARGET, 1)[0], 0x01);
    EXPECT_TRUE(_adapter->isHistoryRecording());
    _adapter->removeBreakpoint(id);
}

TEST_F(DezogHistory_test, ForwardWithinHistoryIsCachedAndStable)
{
    _adapter->onSessionOpened();
    installProgram();
    runToJp(3);

    // Record the PC each index resolves to, walking back...
    uint16_t pcAt[6];
    for (uint32_t i = 0; i < 6; ++i)
    {
        auto e = _adapter->getHistoryEntry(i);
        ASSERT_TRUE(e.has_value()) << i;
        pcAt[i] = e->regs.pc;
    }

    // ...then jump forward within history: each index must resolve to the SAME
    // PC it did on the way back (positions are cached by TimePoint).
    for (int i = 5; i >= 0; --i)
    {
        auto e = _adapter->getHistoryEntry(static_cast<uint32_t>(i));
        ASSERT_TRUE(e.has_value()) << i;
        EXPECT_EQ(e->regs.pc, pcAt[i]) << "forward revisit of index " << i;
        EXPECT_EQ(_adapter->getHistoryCursor(), i);
    }
}

TEST_F(DezogHistory_test, HistoryContinuesAcrossStops)
{
    _adapter->onSessionOpened();
    installProgram();
    runToJp(1);
    ASSERT_TRUE(_adapter->getHistoryEntry(1).has_value());  // browse, then continue

    runToJp(1);  // resume leaves history, recording continues, one more loop

    // History still browsable and coherent after the intervening run
    uint32_t valid = 0;
    for (uint32_t i = 0; i < 6; ++i)
    {
        auto e = _adapter->getHistoryEntry(i);
        if (!e.has_value())
            break;
        EXPECT_TRUE(isProgramPc(e->regs.pc)) << i;
        auto mem = _adapter->readMemory(e->regs.pc, 1);
        EXPECT_EQ(e->opcodes[0], mem[0]) << i;
        ++valid;
    }
    EXPECT_GE(valid, 4u);
}

// §6 regression (the reported "Break: Reached end of instruction history"):
// DeZog spot-fetches history at EVERY stop, and every forward command used to
// restart recording via StartRecording - wiping the timeline on each
// browse/stop cycle, so reverse-continue could never walk past the latest
// stop. Under the DebuggerLive mode browse is read-only and recording never
// stops: instructions recorded BEFORE a browse must still resolve AFTER the
// browse/continue cycles.
TEST_F(DezogHistory_test, HistorySurvivesBrowseAndStopCycles)
{
    _adapter->onSessionOpened();
    installProgram();
    runToJp(3);  // ~9 recorded instructions before any browse

    ASSERT_TRUE(_adapter->getHistoryEntry(8).has_value());  // deep pre-browse entry
    EXPECT_TRUE(ttd()->IsDebuggerLive());
    EXPECT_TRUE(ttd()->IsRecording());  // browsing must not stop the capture
    const size_t checkpointsBefore = ttd()->GetCheckpointCount();

    // The DeZog stop pattern repeated: spot-fetch -> forward command -> stop
    // -> spot-fetch (each runToJp hit resumes, i.e. leaves the browse).
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        ASSERT_TRUE(_adapter->getHistoryEntry(0).has_value()) << cycle;
        EXPECT_TRUE(ttd()->IsRecording()) << cycle;
        runToJp(1);
        ASSERT_TRUE(_adapter->getHistoryEntry(0).has_value()) << cycle;
    }

    // The timeline only grew, and pre-browse instructions are still browsable
    // (with the old wipe, only the ~3 post-wipe steps survived and index 8/10
    // returned out-of-range).
    EXPECT_GE(ttd()->GetCheckpointCount(), checkpointsBefore);
    for (uint32_t i : {0u, 8u, 10u})
    {
        auto e = _adapter->getHistoryEntry(i);
        ASSERT_TRUE(e.has_value()) << i;
        EXPECT_TRUE(isProgramPc(e->regs.pc)) << i;
    }
    _adapter->resume();
}

// Live-app regression (found via the ZRCP verifier against a WebAPI-created
// instance that had booted and free-run BASIC before the session attached):
// with REAL pre-session ROM frames (BASIC + interrupts) behind the baseline,
// the browsable history after installProgram + breakpoint runs must decode
// ONLY genuinely executed instructions. Mid-instruction PCs (operand-fetch
// addresses like 0x8002/0x8004/0x8005) or stale pre-edit ROM PCs in the walk
// mean a checkpoint/replay inconsistency - not extra history.
TEST_F(DezogHistory_test, HistoryStaysCoherentAfterPreSessionFreeRun)
{
    // Pre-session execution: real ROM frames + interrupts, like a WebAPI/GUI
    // instance that booted and ran before DeZog attached
    _emulator->Resume();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    _emulator->Pause();

    _adapter->onSessionOpened();  // parks wherever BASIC is (ROM, IFF on)
    installProgram();             // edits -> fresh baseline at PC=8000
    runToJp(3);

    std::vector<uint16_t> seen;
    for (uint32_t i = 0; i < 4096; ++i)
    {
        auto e = _adapter->getHistoryEntry(i);
        if (!e.has_value())
            break;
        seen.push_back(e->regs.pc);
    }
    ASSERT_GT(seen.size(), 6u) << "no browsable history after pre-session run";
    for (uint32_t k = 0; k < seen.size(); ++k)
        EXPECT_TRUE(isProgramPc(seen[k])) << "index " << k << " pc=" << std::hex << seen[k];
    _adapter->resume();
}

// Same regression shape as above, but on the paged 128K model: the live
// repro came from a WebAPI-created '128k' instance, where banked memory
// exercises a different page-store/checkpoint path than the 48K fixture.
TEST_F(DezogHistory_test, HistoryStaysCoherentAfterPreSessionFreeRun128K)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    ASSERT_NE(manager, nullptr);
    auto emulator = manager->CreateEmulatorWithModel("dezog-test-128k", "128k", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    emulator->Reset();
    emulator->StartAsync();
    for (int i = 0; i < 50 && emulator->GetState() != StateRun; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_EQ(emulator->GetState(), StateRun);

    DezogDebugAdapter adapter(emulator);
    adapter.setPauseNotifier([this](dzrp::BreakReason, uint16_t, uint8_t) {});

    // Pre-session execution: real ROM frames + interrupts on the paged model
    emulator->Resume();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    emulator->Pause();

    adapter.onSessionOpened();
    adapter.writeMemory(PROGRAM_START, {0xF3, 0x3E, 0x01, 0x32, 0x00, 0x90, 0xC3, 0x01, 0x80});
    adapter.writeMemory(WATCH_TARGET, {0x00});
    adapter.setRegister(dzrp::RegisterId::PC, PROGRAM_START);

    uint16_t id = adapter.addBreakpoint(PROGRAM_JP);
    ASSERT_NE(id, 0);
    for (int hit = 0; hit < 3; ++hit)
    {
        adapter.resume();
        // No event helper on this inline adapter - poll the parked state
        for (int i = 0; i < 300 && !emulator->IsPaused(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ASSERT_TRUE(emulator->IsPaused()) << "hit " << hit;
        ASSERT_EQ(emulator->GetZ80State()->pc, PROGRAM_JP) << "hit " << hit;
    }
    adapter.removeBreakpoint(id);

    std::vector<uint16_t> seen;
    for (uint32_t i = 0; i < 4096; ++i)
    {
        auto e = adapter.getHistoryEntry(i);
        if (!e.has_value())
            break;
        seen.push_back(e->regs.pc);
    }
    ASSERT_GT(seen.size(), 6u) << "no browsable history after pre-session run (128k)";
    for (uint32_t k = 0; k < seen.size(); ++k)
        EXPECT_TRUE(isProgramPc(seen[k])) << "index " << k << " pc=" << std::hex << seen[k];

    adapter.onSessionClosed();
    std::string emuId = emulator->GetId();
    emulator.reset();
    manager->RemoveEmulator(emuId);
}

// Live-app regression (§6): a DeZog run that crosses a real frame boundary
// followed by a DEEP walk forces BuildFrameCache on EARLIER frames - down to
// the baseline, which StartRecording captured mid-frame while paused (the
// "checkpoint N = start of frame N" invariant does not hold for it). A
// subsequent run must still re-hit a nearby breakpoint instantly and the
// walked history must decode only genuinely executed instructions: no
// mid-instruction PCs (NOP-sled phantoms through zeroed memory) and no
// stale pre-edit PCs.
TEST_F(DezogHistory_test, DeepBrowseAcrossFrameBoundaryThenResumeStaysCoherent)
{
    _emulator->Resume();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    _emulator->Pause();

    _adapter->onSessionOpened();
    installProgram();  // edits -> fresh baseline at PC=8000, mid-frame

    // Free-run the 3-instruction loop across ~2 frame boundaries (bp-driven
    // runs are over in a few dozen t-states and stay inside one frame), then
    // park mid-loop like a DeZog stop would.
    _adapter->resume();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    _adapter->pause();
    PauseEvent manual{};
    ASSERT_TRUE(waitForEvent(manual));  // consume the manual-pause notification
    ASSERT_TRUE(isProgramPc(_emulator->GetZ80State()->pc));

    // Deep walk: past the pause point's frame into earlier frames, reaching
    // the mid-frame baseline. Every decoded entry must be a real M1 of the
    // loop - operand addresses (8002/8004/8005) or pre-edit PCs mean the
    // baseline-frame replay diverged from what actually executed.
    std::vector<uint16_t> seen;
    for (uint32_t i = 0; i < 6000; ++i)
    {
        auto e = _adapter->getHistoryEntry(i);
        if (!e.has_value())
            break;
        seen.push_back(e->regs.pc);
    }
    ASSERT_GT(seen.size(), 100u) << "walk did not reach deep history";
    EXPECT_TRUE(_adapter->isHistoryRecording());
    for (uint32_t k = 0; k < seen.size(); ++k)
        EXPECT_TRUE(isProgramPc(seen[k])) << "index " << k << " pc=" << std::hex << seen[k];
    EXPECT_EQ(_adapter->getHistoryCursor(), static_cast<int64_t>(seen.size() - 1));

    // Resume after the deep browse: the machine never moved, so a breakpoint
    // three instructions away must re-hit immediately (the live bug free-ran
    // ~1.1s through zeroed RAM and BASIC before stumbling back into 8006).
    uint16_t id = _adapter->addBreakpoint(PROGRAM_JP);
    ASSERT_NE(id, 0);
    const auto t0 = std::chrono::steady_clock::now();
    _adapter->resume();
    PauseEvent hit{};
    ASSERT_TRUE(waitForEvent(hit, std::chrono::seconds(5))) << "breakpoint did not re-hit";
    const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    EXPECT_EQ(hit.address, PROGRAM_JP);
    EXPECT_LT(dt, 500) << "re-hit took " << dt << "ms - machine resumed in a corrupted state";
    _adapter->removeBreakpoint(id);
}

// §6 gap guard: if the emulator runs while recording is stopped (e.g.
// resumed from the host GUI), the unrecorded frames are unreplayable (no
// checkpoints, no journaled writes), so re-establishing live history must
// REFUSE the non-destructive resume and fall back to a fresh session.
TEST_F(DezogHistory_test, UnrecordedGapFallsBackToFreshSession)
{
    _adapter->onSessionOpened();
    installProgram();
    runToJp(2);

    // runToJp executes only a handful of instructions inside ONE frame (just
    // the baseline checkpoint); free-run while recording is still active to
    // cross frame boundaries and accrue a multi-checkpoint timeline.
    _emulator->Resume();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    _emulator->Pause();

    const size_t checkpointsBefore = ttd()->GetCheckpointCount();
    EXPECT_GT(checkpointsBefore, 1u);

    // Host-side resume (adapter not involved - it would auto-restart capture)
    ttd::TimeTravelManager* mgr = ttd();
    mgr->StopRecording();
    _emulator->Resume();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    _emulator->Pause();

    // A real gap accrued: the present advanced past the recorded end
    EXPECT_GT(mgr->CurrentPosition().frame, mgr->SessionEndPosition().frame);

    // The next debugger-session command re-establishes live history by
    // starting fresh (baseline only) instead of appending across the gap.
    _adapter->onSessionOpened();
    EXPECT_TRUE(mgr->IsRecording());
    EXPECT_TRUE(mgr->IsDebuggerLive());
    EXPECT_LT(mgr->GetCheckpointCount(), checkpointsBefore);
    _adapter->resume();
}

TEST_F(DezogHistory_test, RegisterWriteWhileBrowsingReturnsToPresentFirst)
{
    _adapter->onSessionOpened();
    installProgram();
    runToJp(1);

    ASSERT_TRUE(_adapter->getHistoryEntry(1).has_value());
    _adapter->setRegister(dzrp::RegisterId::HL, 0xABCD);
    EXPECT_EQ(_adapter->getHistoryCursor(), -1);
    EXPECT_EQ(_adapter->getRegisters().pc, PROGRAM_JP);  // present PC, not the historic one
    EXPECT_EQ(_adapter->getRegisters().hl, 0xABCD);
    EXPECT_TRUE(_adapter->isHistoryRecording());
}

TEST_F(DezogHistory_test, StateRestoreRestartsHistory)
{
    _adapter->onSessionOpened();
    installProgram();
    runToJp(2);
    auto snapshot = _adapter->captureState();
    ASSERT_FALSE(snapshot.empty());

    runToJp(1);
    _adapter->restoreState(snapshot);
    // Fresh recording segment from the restored state
    EXPECT_TRUE(_adapter->isHistoryRecording());
    EXPECT_EQ(_adapter->getHistoryCursor(), -1);
    // Restored PC is the JP we snapshotted at
    EXPECT_EQ(_adapter->getRegisters().pc, PROGRAM_JP);
    // A browse entry (if history already has one) is coherent; resume returns to present
    auto e = _adapter->getHistoryEntry(0);
    if (e.has_value())
        EXPECT_TRUE(isProgramPc(e->regs.pc));
    _adapter->resume();
    EXPECT_EQ(_adapter->getHistoryCursor(), -1);
}

TEST_F(DezogHistory_test, SessionCloseWhileBrowsingReturnsToPresentAndResumes)
{
    _adapter->onSessionOpened();
    installProgram();
    runToJp(1);
    ASSERT_TRUE(_adapter->getHistoryEntry(2).has_value());

    _adapter->onSessionClosed();
    EXPECT_EQ(_adapter->getHistoryCursor(), -1);
    EXPECT_FALSE(_emulator->IsPaused());
}

TEST_F(DezogHistory_test, DebuggerEditStartsNewHistorySegment)
{
    _adapter->onSessionOpened();
    installProgram();
    runToJp(2);
    ASSERT_TRUE(_adapter->getHistoryEntry(3).has_value());

    // Edit from the debugger (returns to present, restarts recording from the edited state)
    _adapter->writeMemory(0x8100, {0x00});
    EXPECT_EQ(_adapter->getHistoryCursor(), -1);
    EXPECT_TRUE(_adapter->isHistoryRecording());  // fresh segment, still recording

    // History resumes normally after the edit
    runToJp(1);
    uint32_t valid = 0;
    for (uint32_t i = 0; i < 4; ++i)
    {
        auto e = _adapter->getHistoryEntry(i);
        if (!e.has_value())
            break;
        EXPECT_TRUE(isProgramPc(e->regs.pc)) << i;
        ++valid;
    }
    EXPECT_GT(valid, 0u);  // fresh segment has the post-edit loop's instructions
    _adapter->resume();
}

TEST_F(DezogHistory_test, LatencyReportAfterFreeRun)
{
    // Realistic shape: the target ran freely for ~0.5 s (≈25 frames of history)
    // before the user pauses and starts stepping back.
    _adapter->onSessionOpened();
    installProgram();
    _adapter->resume();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    _adapter->pause();
    PauseEvent ev{};
    ASSERT_TRUE(waitForEvent(ev));

    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    for (uint32_t i = 0; i < 10; ++i)
        ASSERT_TRUE(_adapter->getHistoryEntry(i).has_value()) << i;
    auto t1 = clock::now();
    ASSERT_TRUE(_adapter->getHistoryEntry(500).has_value());
    auto t2 = clock::now();
    ASSERT_TRUE(_adapter->getHistoryEntry(20000).has_value());  // crosses frame checkpoints
    auto t3 = clock::now();
    _adapter->resume();
    _emulator->Pause();
    auto t4 = clock::now();

    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::cout << "[history-latency free-run] 10 sequential entries: " << ms(t0, t1) << " ms ("
              << ms(t0, t1) / 10 << " ms/entry), jump to 500: " << ms(t1, t2)
              << " ms, jump to 20000: " << ms(t2, t3) << " ms, return to present + resume: " << ms(t3, t4)
              << " ms\n";
}

TEST_F(DezogHistory_test, LatencyReport)
{
    _adapter->onSessionOpened();
    installProgram();
    runToJp(40);  // ~120 instructions of history

    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    for (uint32_t i = 0; i < 10; ++i)
        ASSERT_TRUE(_adapter->getHistoryEntry(i).has_value()) << i;  // DeZog "spot" prefetch shape
    auto t1 = clock::now();
    ASSERT_TRUE(_adapter->getHistoryEntry(60).has_value());           // deep jump (batched)
    auto t2 = clock::now();
    _adapter->resume();
    _emulator->Pause();
    auto t3 = clock::now();

    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::cout << "[history-latency] 10 sequential entries: " << ms(t0, t1) << " ms ("
              << ms(t0, t1) / 10 << " ms/entry), jump to index 60: " << ms(t1, t2)
              << " ms, return to present + resume: " << ms(t2, t3) << " ms\n";
    RecordProperty("ms_per_entry", ms(t0, t1) / 10);
}

/// endregion </Instruction history (TTD reverse debugging)>
