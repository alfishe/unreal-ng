#include "breakpoints_test.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "3rdparty/message-center/messagecenter.h"
#include "common/stringhelper.h"
#include "common/timehelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "pch.h"

/// region <SetUp / TearDown>

void BreakpointManager_test::SetUp()
{
    // Ensure complete isolation - dispose any existing MessageCenter from previous tests
    MessageCenter::DisposeDefaultMessageCenter();

    // Create fresh context and manager for each test
    _context = new EmulatorContext(LoggerLevel::LogError);
    _brkManager = new BreakpointManagerCUT(_context);
}

void BreakpointManager_test::TearDown()
{
    // Clean up test-specific resources first
    if (_brkManager)
    {
        delete _brkManager;
        _brkManager = nullptr;
    }

    if (_context)
    {
        delete _context;
        _context = nullptr;
    }

    // Force complete disposal of MessageCenter and all its observers
    // This ensures no state leakage between tests
    MessageCenter::DisposeDefaultMessageCenter();
}

/// endregion </Setup / TearDown>

TEST_F(BreakpointManager_test, addMemoryBreakpoint)
{
    const size_t initialCount = _brkManager->GetBreakpointsCount();
    const size_t expectedCount = initialCount + 1;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = BRK_MEM_EXECUTE;
    breakpoint->z80address = 0x0000;
    uint16_t brkID = _brkManager->AddBreakpoint(breakpoint);

    if (brkID == BRK_INVALID)
    {
        FAIL() << "BRK_INVALID issued as breakpoint ID";
    }

    const size_t finalCount = _brkManager->GetBreakpointsCount();

    if (finalCount != expectedCount)
    {
        std::string message = StringHelper::Format(
            "Add breakpoint failed. Expected %d breakpoints after add, detected %d", expectedCount, finalCount);
        FAIL() << message;
    }
}

TEST_F(BreakpointManager_test, addPortBreakpoint)
{
    const size_t initialCount = _brkManager->GetBreakpointsCount();
    const size_t expectedCount = initialCount + 1;

    // Create port breakpoint descriptor
    BreakpointDescriptor breakpoint;
    breakpoint.type = BreakpointTypeEnum::BRK_IO;
    breakpoint.ioType = BRK_IO_IN;  // Test with port input breakpoint
    breakpoint.z80address = 0xFE;   // Test with port 0xFE

    // Add the breakpoint
    uint16_t brkID = _brkManager->AddBreakpoint(&breakpoint);

    if (brkID == BRK_INVALID)
    {
        FAIL() << "BRK_INVALID issued as breakpoint ID";
    }

    // Verify breakpoint was added
    const size_t finalCount = _brkManager->GetBreakpointsCount();
    if (finalCount != expectedCount)
    {
        std::string message = StringHelper::Format(
            "Add breakpoint failed. Expected %d breakpoints after add, detected %d", expectedCount, finalCount);
        FAIL() << message;
    }

    // Verify breakpoint can be found
    BreakpointDescriptor* foundBreakpoint = _brkManager->FindPortBreakpoint(0xFE);
    if (!foundBreakpoint)
    {
        FAIL() << "Added breakpoint could not be found";
    }

    // Verify breakpoint properties
    EXPECT_EQ(foundBreakpoint->type, BreakpointTypeEnum::BRK_IO);
    EXPECT_EQ(foundBreakpoint->ioType, BRK_IO_IN);
    EXPECT_EQ(foundBreakpoint->z80address, 0xFE);
    EXPECT_EQ(foundBreakpoint->breakpointID, brkID);
}

TEST_F(BreakpointManager_test, executionBreakpoint)
{
    std::atomic<bool> breakpointTriggered{false};
    uint16_t breakpointAddress = 0x0000;

    /// region <Initialize>
    Emulator* emulator = new Emulator(LoggerLevel::LogError);
    bool init = emulator->Init();
    EXPECT_EQ(init, true) << "Unable to initialize emulator instance";
    emulator->DebugOn();

    EmulatorContext* context = emulator->GetContext();
    BreakpointManager* breakpointManager = emulator->GetBreakpointManager();

    // Register MessageCenter event handler as lambda
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Z80* z80 = emulator->GetContext()->pCore->GetZ80();
    auto handler = [&breakpointTriggered, z80](int id, Message* message) {
        breakpointTriggered.store(true);
        z80->Resume();  // Resume Z80 directly, not through Emulator (mainloop not started)
    };
    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, handler);

    /// endregion </Initialize>

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = BRK_MEM_EXECUTE;
    breakpoint->z80address = breakpointAddress;
    breakpointManager->AddBreakpoint(breakpoint);

    emulator->RunSingleCPUCycle(false);

    // Wait for async callback to execute (max 200ms)
    auto start = std::chrono::steady_clock::now();
    while (!breakpointTriggered.load() && std::chrono::steady_clock::now() - start < std::chrono::milliseconds(200))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Remove observer BEFORE checking result to prevent callback accessing invalid memory
    messageCenter.RemoveObserver(NC_EXECUTION_BREAKPOINT, handler);

    if (!breakpointTriggered.load())
    {
        // Clean up before failing
        emulator->Stop();
        emulator->Release();
        delete emulator;
        FAIL() << StringHelper::Format("Execution breakpoint on address: $%04X wasn't triggered", breakpointAddress)
               << std::endl;
    }

    /// region <Release>
    emulator->Stop();
    emulator->Release();
    delete emulator;
    /// endregion </Release>
}

TEST_F(BreakpointManager_test, memoryReadBreakpoint)
{
    std::atomic<bool> breakpointTriggered{false};
    uint8_t testCommands[] = {
        0x21, 0x00, 0x40,  // $0000 LD HL, $4000
        0x7E,              // $0003 LD A, (HL)
        0x76               // $0004 HALT
    };
    uint16_t breakpointAddress = 0x4000;  // Let's break on $4000 memory address read

    /// region <Initialize>
    Emulator* emulator = new Emulator(LoggerLevel::LogError);
    bool init = emulator->Init();

    EmulatorContext* context = emulator->GetContext();
    BreakpointManager* breakpointManager = emulator->GetBreakpointManager();

    // Enable global debugging (memory debug interface, CPU level debug) + enable Breakpoints feature
    emulator->DebugOn();

    // Force features to false first, then true to ensure onFeatureChanged is called
    context->pFeatureManager->setFeature(Features::kDebugMode, false);
    context->pFeatureManager->setFeature(Features::kBreakpoints, false);
    context->pFeatureManager->setFeature(Features::kDebugMode, true);
    context->pFeatureManager->setFeature(Features::kBreakpoints, true);

    // Transfer test Z80 command sequence to ROM space (From address $0000)
    Memory* memory = emulator->GetMemory();

    // Manually update feature cache to ensure it's propagated
    memory->UpdateFeatureCache();

    for (int i = 0; i < sizeof(testCommands) / sizeof(testCommands[0]); i++)
    {
        memory->DirectWriteToZ80Memory(i, testCommands[i]);
    }

    // Register MessageCenter event handler as lambda
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Z80* z80 = emulator->GetContext()->pCore->GetZ80();
    auto handler = [&breakpointTriggered, z80](int id, Message* message) {
        breakpointTriggered.store(true);
        z80->Resume();  // Resume Z80 directly, not through Emulator (mainloop not started)
    };
    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, handler);

    /// endregion </Initialize>

    // Create memory read breakpoint on specified address in Z80 address space (bank-independent)
    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = BRK_MEM_READ;
    breakpoint->z80address = breakpointAddress;
    breakpointManager->AddBreakpoint(breakpoint);

    emulator->RunNCPUCycles(50);

    // Wait for async callback to execute (max 200ms)
    auto start = std::chrono::steady_clock::now();
    while (!breakpointTriggered.load() && std::chrono::steady_clock::now() - start < std::chrono::milliseconds(200))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Remove observer BEFORE checking result to prevent callback accessing invalid memory
    messageCenter.RemoveObserver(NC_EXECUTION_BREAKPOINT, handler);

    if (!breakpointTriggered.load())
    {
        emulator->Stop();
        emulator->Release();
        delete emulator;
        FAIL() << StringHelper::Format("Memory read breakpoint on address: $%04X wasn't triggered", breakpointAddress)
               << std::endl;
    }

    /// region <Release>
    emulator->Stop();
    emulator->Release();
    delete emulator;
    /// endregion </Release>
}

TEST_F(BreakpointManager_test, memoryWriteBreakpoint)
{
    std::atomic<bool> breakpointTriggered{false};
    uint8_t testCommands[] =
    {
        0x21, 0x00, 0x40,  // $0000 LD HL, $4000
        0x3E, 0xA3,        // $0003 LD A, $A3
        0x77,              // $0005 LD (HL), A
        0x76               // $0006 HALT
    };
    uint16_t breakpointAddress = 0x4000;  // Let's break on $4000 memory address read

    /// region <Initialize>
    Emulator* emulator = new Emulator(LoggerLevel::LogError);
    bool init = emulator->Init();

    EmulatorContext* context = emulator->GetContext();
    BreakpointManager* breakpointManager = emulator->GetBreakpointManager();

    // Enable global debugging (memory debug interface, CPU level debug) + enable Breakpoints feature
    emulator->DebugOn();

    // Force features to false first, then true to ensure onFeatureChanged is called
    context->pFeatureManager->setFeature(Features::kDebugMode, false);
    context->pFeatureManager->setFeature(Features::kBreakpoints, false);
    context->pFeatureManager->setFeature(Features::kDebugMode, true);
    context->pFeatureManager->setFeature(Features::kBreakpoints, true);

    // Transfer test Z80 command sequence to ROM space (From address $0000)
    Memory* memory = emulator->GetMemory();

    // Manually update feature cache to ensure it's propagated
    memory->UpdateFeatureCache();

    for (int i = 0; i < sizeof(testCommands) / sizeof(testCommands[0]); i++)
    {
        memory->DirectWriteToZ80Memory(i, testCommands[i]);
    }

    // Register MessageCenter event handler as lambda
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Z80* z80 = emulator->GetContext()->pCore->GetZ80();
    auto handler = [&breakpointTriggered, z80](int id, Message* message)
    {
        breakpointTriggered.store(true);
        z80->Resume();  // Resume Z80 directly, not through Emulator (mainloop not started)
    };
    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, handler);

    /// endregion </Initialize>

    // Create memory write breakpoint on specified address in Z80 address space (bank-independent)
    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = BRK_MEM_WRITE;
    breakpoint->z80address = breakpointAddress;
    breakpointManager->AddBreakpoint(breakpoint);

    emulator->RunNCPUCycles(50, false);

    // Wait for async callback to execute (max 200ms)
    auto start = std::chrono::steady_clock::now();
    while (!breakpointTriggered.load() && std::chrono::steady_clock::now() - start < std::chrono::milliseconds(200))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Remove observer BEFORE checking result to prevent callback accessing invalid memory
    messageCenter.RemoveObserver(NC_EXECUTION_BREAKPOINT, handler);

    if (!breakpointTriggered.load())
    {
        emulator->Stop();
        emulator->Release();
        delete emulator;
        FAIL() << StringHelper::Format("Memory write breakpoint on address: $%04X wasn't triggered", breakpointAddress)
               << std::endl;
    }

    /// region <Release>
    emulator->Stop();
    emulator->Release();
    delete emulator;
    /// endregion </Release>
}

TEST_F(BreakpointManager_test, portInBreakpoint)
{
    std::atomic<bool> breakpointTriggered{false};
    uint8_t testCommands[] =
    {
        0xAF,        // $0000 XOR A - Ensure A = 0
        0xDB, 0x00,  // $0001 IN A,($00) - Read from port $00
        0x76         // $0003 HALT
    };
    uint8_t portNumber = 0x00;  // Test port input from port $00

    /// region <Initialize>
    Emulator* emulator = new Emulator(LoggerLevel::LogError);
    bool init = emulator->Init();
    emulator->DebugOn();

    EmulatorContext* context = emulator->GetContext();
    BreakpointManager* breakpointManager = emulator->GetBreakpointManager();

    // Enable global debugging (memory debug interface, CPU level debug) + enable Breakpoints feature
    context->pFeatureManager->setFeature(Features::kDebugMode, true);
    context->pFeatureManager->setFeature(Features::kBreakpoints, true);

    // Transfer test Z80 command sequence to ROM space (From address $0000)
    Memory* memory = emulator->GetMemory();

    for (int i = 0; i < sizeof(testCommands) / sizeof(testCommands[0]); i++)
    {
        memory->DirectWriteToZ80Memory(i, testCommands[i]);
    }

    // Register MessageCenter event handler as lambda
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Z80* z80 = emulator->GetContext()->pCore->GetZ80();
    auto handler = [&breakpointTriggered, z80](int id, Message* message) {
        breakpointTriggered.store(true);
        z80->Resume();  // Resume Z80 directly, not through Emulator (mainloop not started)
    };
    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, handler);

    /// endregion </Initialize>

    // Create port input breakpoint
    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_IO;
    breakpoint->ioType = BRK_IO_IN;
    breakpoint->z80address = portNumber;
    breakpointManager->AddBreakpoint(breakpoint);

    emulator->RunNCPUCycles(20, false);

    // Wait for async callback to execute (max 200ms)
    auto start = std::chrono::steady_clock::now();
    while (!breakpointTriggered.load() && 
           std::chrono::steady_clock::now() - start < std::chrono::milliseconds(200))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Remove observer BEFORE checking result to prevent callback accessing invalid memory
    messageCenter.RemoveObserver(NC_EXECUTION_BREAKPOINT, handler);

    if (!breakpointTriggered.load())
    {
        emulator->Stop();
        emulator->Release();
        delete emulator;
        FAIL() << StringHelper::Format("Port input breakpoint on port: $%02X wasn't triggered", portNumber) << std::endl;
    }

    /// region <Release>
    emulator->Stop();
    emulator->Release();
    delete emulator;
    /// endregion </Release>
}

TEST_F(BreakpointManager_test, portOutBreakpoint)
{
    std::atomic<bool> breakpointTriggered{false};
    uint8_t testCommands[] =
    {
        0xAF,        // $0000 XOR A - Ensure A = 0
        0xD3, 0xFE,  // $0001 OUT ($FE), A
        0x76         // $0003 HALT
    };
    uint8_t portNumber = 0xFE;  // Test port output to port 0xFE

    /// region <Initialize>
    Emulator* emulator = new Emulator(LoggerLevel::LogError);
    bool init = emulator->Init();
    emulator->DebugOn();

    EmulatorContext* context = emulator->GetContext();
    BreakpointManager* breakpointManager = emulator->GetBreakpointManager();

    // Transfer test Z80 command sequence to ROM space (From address $0000)
    Memory* memory = emulator->GetMemory();

    for (int i = 0; i < sizeof(testCommands) / sizeof(testCommands[0]); i++)
    {
        memory->DirectWriteToZ80Memory(i, testCommands[i]);
    }

    // Register MessageCenter event handler as lambda
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Z80* z80 = emulator->GetContext()->pCore->GetZ80();
    auto handler = [&breakpointTriggered, z80](int id, Message* message) {
        breakpointTriggered.store(true);
        z80->Resume();  // Resume Z80 directly, not through Emulator (mainloop not started)
    };
    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, handler);

    /// endregion </Initialize>

    // Create port output breakpoint
    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_IO;
    breakpoint->ioType = BRK_IO_OUT;
    breakpoint->z80address = portNumber;
    breakpointManager->AddBreakpoint(breakpoint);

    emulator->RunNCPUCycles(20, false);

    // Wait for async callback to execute (max 200ms)
    auto start = std::chrono::steady_clock::now();
    while (!breakpointTriggered.load() && 
           std::chrono::steady_clock::now() - start < std::chrono::milliseconds(200))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Remove observer BEFORE checking result to prevent callback accessing invalid memory
    messageCenter.RemoveObserver(NC_EXECUTION_BREAKPOINT, handler);

    if (!breakpointTriggered.load())
    {
        emulator->Stop();
        emulator->Release();
        delete emulator;
        FAIL() << StringHelper::Format("Port output breakpoint on port: $%02X wasn't triggered", portNumber) << std::endl;
    }

    /// region <Release>
    emulator->Stop();
    emulator->Release();
    delete emulator;
    /// endregion </Release>
}

// ============================================================================
// Ownership Tests
// ============================================================================

TEST_F(BreakpointManager_test, ownerDefaultsToInteractive)
{
    // Add a breakpoint without specifying owner
    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = BRK_MEM_EXECUTE;
    breakpoint->z80address = 0x1000;
    uint16_t brkID = _brkManager->AddBreakpoint(breakpoint);
    
    ASSERT_NE(brkID, BRK_INVALID);
    
    // Retrieve by ID and check owner
    BreakpointDescriptor* found = _brkManager->GetBreakpointById(brkID);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->owner, BreakpointManager::OWNER_INTERACTIVE);
}

TEST_F(BreakpointManager_test, ownerSetExplicitly)
{
    // Add breakpoint with explicit owner via convenience method
    uint16_t brkID = _brkManager->AddExecutionBreakpoint(0x2000, "test_analyzer");
    
    ASSERT_NE(brkID, BRK_INVALID);
    
    // Retrieve by ID and check owner
    BreakpointDescriptor* found = _brkManager->GetBreakpointById(brkID);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->owner, "test_analyzer");
}

TEST_F(BreakpointManager_test, pageSpecificBreakpointROM)
{
    // Add page-specific breakpoint in ROM page 2
    uint16_t brkID = _brkManager->AddExecutionBreakpointInPage(0x0100, 2, BANK_ROM, "trdos_analyzer");
    
    ASSERT_NE(brkID, BRK_INVALID);
    
    // Count should increase
    EXPECT_GE(_brkManager->GetBreakpointsCount(), 1);
}

TEST_F(BreakpointManager_test, pageSpecificBreakpointRAM)
{
    // Add page-specific breakpoint in RAM page 5
    uint16_t brkID = _brkManager->AddExecutionBreakpointInPage(0xC000, 5, BANK_RAM, "memory_analyzer");
    
    ASSERT_NE(brkID, BRK_INVALID);
    EXPECT_GE(_brkManager->GetBreakpointsCount(), 1);
}

TEST_F(BreakpointManager_test, multipleOwnersAtDifferentAddresses)
{
    // Two different owners at different addresses
    uint16_t bp1 = _brkManager->AddExecutionBreakpoint(0x1000, "analyzer_a");
    uint16_t bp2 = _brkManager->AddExecutionBreakpoint(0x2000, "analyzer_b");
    uint16_t bp3 = _brkManager->AddExecutionBreakpoint(0x3000, BreakpointManager::OWNER_INTERACTIVE);
    
    ASSERT_NE(bp1, BRK_INVALID);
    ASSERT_NE(bp2, BRK_INVALID);
    ASSERT_NE(bp3, BRK_INVALID);
    
    // Check each breakpoint has correct owner using GetBreakpointById
    BreakpointDescriptor* found1 = _brkManager->GetBreakpointById(bp1);
    BreakpointDescriptor* found2 = _brkManager->GetBreakpointById(bp2);
    BreakpointDescriptor* found3 = _brkManager->GetBreakpointById(bp3);
    
    ASSERT_NE(found1, nullptr);
    ASSERT_NE(found2, nullptr);
    ASSERT_NE(found3, nullptr);
    
    EXPECT_EQ(found1->owner, "analyzer_a");
    EXPECT_EQ(found2->owner, "analyzer_b");
    EXPECT_EQ(found3->owner, BreakpointManager::OWNER_INTERACTIVE);
}

TEST_F(BreakpointManager_test, pageSpecificBreakpointDescriptorFields)
{
    // Add page-specific breakpoint and verify all descriptor fields
    uint16_t brkID = _brkManager->AddExecutionBreakpointInPage(0x0800, 3, BANK_ROM, "custom_owner");
    
    ASSERT_NE(brkID, BRK_INVALID);
    
    // Find the breakpoint by ID
    BreakpointDescriptor* found = _brkManager->GetBreakpointById(brkID);
    
    ASSERT_NE(found, nullptr) << "Could not find page-specific breakpoint by ID";
    
    // Verify all fields
    EXPECT_EQ(found->z80address, 0x0800);
    EXPECT_EQ(found->page, 3);
    EXPECT_EQ(found->pageType, BANK_ROM);
    EXPECT_EQ(found->owner, "custom_owner");
    EXPECT_EQ(found->type, BreakpointTypeEnum::BRK_MEMORY);
    EXPECT_TRUE(found->memoryType & BRK_MEM_EXECUTE);
}

TEST_F(BreakpointManager_test, removeBreakpointByID)
{
    uint16_t brkID = _brkManager->AddExecutionBreakpoint(0x5000, "to_be_removed");
    ASSERT_NE(brkID, BRK_INVALID);
    
    size_t countBefore = _brkManager->GetBreakpointsCount();
    
    _brkManager->RemoveBreakpointByID(brkID);
    
    size_t countAfter = _brkManager->GetBreakpointsCount();
    EXPECT_EQ(countAfter, countBefore - 1);
    
    // Should not be found anymore
    BreakpointDescriptor* found = _brkManager->GetBreakpointById(brkID);
    EXPECT_EQ(found, nullptr);
}

TEST_F(BreakpointManager_test, combinedMemoryBreakpointWithOwner)
{
    // Test combined memory breakpoint (read + write + execute)
    uint16_t brkID = _brkManager->AddCombinedMemoryBreakpoint(
        0x6000, 
        BRK_MEM_READ | BRK_MEM_WRITE | BRK_MEM_EXECUTE,
        "combined_owner");
    
    ASSERT_NE(brkID, BRK_INVALID);
    
    BreakpointDescriptor* found = _brkManager->GetBreakpointById(brkID);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->owner, "combined_owner");
    EXPECT_TRUE(found->memoryType & BRK_MEM_READ);
    EXPECT_TRUE(found->memoryType & BRK_MEM_WRITE);
    EXPECT_TRUE(found->memoryType & BRK_MEM_EXECUTE);
}

TEST_F(BreakpointManager_test, portBreakpointsWithOwner)
{
    // Port IN breakpoint with owner
    uint16_t bpIn = _brkManager->AddPortInBreakpoint(0xFE, "port_analyzer");
    ASSERT_NE(bpIn, BRK_INVALID);
    
    // Port OUT breakpoint with owner
    uint16_t bpOut = _brkManager->AddPortOutBreakpoint(0x7FFD, "port_analyzer");
    ASSERT_NE(bpOut, BRK_INVALID);
    
    // Both should be findable by ID
    BreakpointDescriptor* foundIn = _brkManager->GetBreakpointById(bpIn);
    BreakpointDescriptor* foundOut = _brkManager->GetBreakpointById(bpOut);
    
    ASSERT_NE(foundIn, nullptr);
    ASSERT_NE(foundOut, nullptr);
    
    EXPECT_EQ(foundIn->owner, "port_analyzer");
    EXPECT_EQ(foundOut->owner, "port_analyzer");
}

// Negative test: invalid parameters
TEST_F(BreakpointManager_test, negativeInvalidPageType)
{
    // BANK_INVALID should still work (marks as non-page-specific)
    uint16_t brkID = _brkManager->AddExecutionBreakpointInPage(0x1000, 0, BANK_INVALID, "test");
    
    // This is allowed - BANK_INVALID indicates non-page-specific
    EXPECT_NE(brkID, BRK_INVALID);
}
