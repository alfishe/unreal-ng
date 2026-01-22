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

TEST_F(BreakpointManager_test, negativeInvalidPageType)
{
    // BANK_INVALID should still work (marks as non-page-specific)
    uint16_t brkID = _brkManager->AddExecutionBreakpointInPage(0x1000, 0, BANK_INVALID, "test");
    
    // This is allowed - BANK_INVALID indicates non-page-specific
    EXPECT_NE(brkID, BRK_INVALID);
}

// ============================================================================
// Page-Specific Breakpoint Matching Tests (HandlePCChange / FindAddressBreakpoint)
// ============================================================================

/// @brief Test that page-specific breakpoint is found when correct page is active
TEST_F(BreakpointManager_test, FindAddressBreakpoint_MatchesCorrectPage)
{
    // Add page-specific breakpoint at address 0x0100 in ROM page 2
    uint16_t brkID = _brkManager->AddExecutionBreakpointInPage(0x0100, 2, BANK_ROM, "test_analyzer");
    ASSERT_NE(brkID, BRK_INVALID);
    
    // Create page descriptor matching the breakpoint
    MemoryPageDescriptor matchingPage;
    matchingPage.mode = BANK_ROM;
    matchingPage.page = 2;
    matchingPage.addressInPage = 0x0100;
    
    // Should find the breakpoint
    BreakpointDescriptor* found = _brkManager->FindAddressBreakpoint(0x0100, matchingPage);
    ASSERT_NE(found, nullptr) << "Should find page-specific breakpoint when page matches";
    EXPECT_EQ(found->breakpointID, brkID);
}

/// @brief Test that page-specific breakpoint is NOT found when different page is active
TEST_F(BreakpointManager_test, FindAddressBreakpoint_DoesNotMatchDifferentPage)
{
    // Add page-specific breakpoint at address 0x0100 in ROM page 2
    uint16_t brkID = _brkManager->AddExecutionBreakpointInPage(0x0100, 2, BANK_ROM, "test_analyzer");
    ASSERT_NE(brkID, BRK_INVALID);
    
    // Create page descriptor with DIFFERENT page
    MemoryPageDescriptor differentPage;
    differentPage.mode = BANK_ROM;
    differentPage.page = 0;  // Different page!
    differentPage.addressInPage = 0x0100;
    
    // Should NOT find the breakpoint (page mismatch)
    BreakpointDescriptor* found = _brkManager->FindAddressBreakpoint(0x0100, differentPage);
    EXPECT_EQ(found, nullptr) << "Should NOT find page-specific breakpoint when page differs";
}

/// @brief Test that address-only (wildcard) breakpoint is found in any page
TEST_F(BreakpointManager_test, FindAddressBreakpoint_WildcardMatchesAnyPage)
{
    // Add address-only (wildcard) breakpoint at 0x0100
    uint16_t brkID = _brkManager->AddExecutionBreakpoint(0x0100, "test_analyzer");
    ASSERT_NE(brkID, BRK_INVALID);
    
    // Test matching in ROM page 0
    MemoryPageDescriptor romPage0;
    romPage0.mode = BANK_ROM;
    romPage0.page = 0;
    romPage0.addressInPage = 0x0100;
    
    BreakpointDescriptor* found1 = _brkManager->FindAddressBreakpoint(0x0100, romPage0);
    ASSERT_NE(found1, nullptr) << "Wildcard breakpoint should match in ROM page 0";
    EXPECT_EQ(found1->breakpointID, brkID);
    
    // Test matching in ROM page 2
    MemoryPageDescriptor romPage2;
    romPage2.mode = BANK_ROM;
    romPage2.page = 2;
    romPage2.addressInPage = 0x0100;
    
    BreakpointDescriptor* found2 = _brkManager->FindAddressBreakpoint(0x0100, romPage2);
    ASSERT_NE(found2, nullptr) << "Wildcard breakpoint should match in ROM page 2";
    EXPECT_EQ(found2->breakpointID, brkID);
    
    // Test matching in RAM page
    MemoryPageDescriptor ramPage;
    ramPage.mode = BANK_RAM;
    ramPage.page = 5;
    ramPage.addressInPage = 0x0100;
    
    BreakpointDescriptor* found3 = _brkManager->FindAddressBreakpoint(0x0100, ramPage);
    ASSERT_NE(found3, nullptr) << "Wildcard breakpoint should match in RAM page";
    EXPECT_EQ(found3->breakpointID, brkID);
}

/// @brief Test that page-specific breakpoint takes precedence over wildcard
TEST_F(BreakpointManager_test, FindAddressBreakpoint_PageSpecificTakesPrecedence)
{
    // First add wildcard breakpoint
    uint16_t wildcardID = _brkManager->AddExecutionBreakpoint(0x0100, "wildcard_owner");
    ASSERT_NE(wildcardID, BRK_INVALID);
    
    // Then add page-specific breakpoint at same address
    uint16_t pageSpecificID = _brkManager->AddExecutionBreakpointInPage(0x0100, 2, BANK_ROM, "page_specific_owner");
    ASSERT_NE(pageSpecificID, BRK_INVALID);
    
    // When querying with matching page, should return page-specific (higher precedence)
    MemoryPageDescriptor matchingPage;
    matchingPage.mode = BANK_ROM;
    matchingPage.page = 2;
    matchingPage.addressInPage = 0x0100;
    
    BreakpointDescriptor* found = _brkManager->FindAddressBreakpoint(0x0100, matchingPage);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->breakpointID, pageSpecificID) << "Page-specific should take precedence over wildcard";
    EXPECT_EQ(found->owner, "page_specific_owner");
    
    // When querying with different page, should fall back to wildcard
    MemoryPageDescriptor differentPage;
    differentPage.mode = BANK_ROM;
    differentPage.page = 0;  // Different page
    differentPage.addressInPage = 0x0100;
    
    BreakpointDescriptor* fallback = _brkManager->FindAddressBreakpoint(0x0100, differentPage);
    ASSERT_NE(fallback, nullptr);
    EXPECT_EQ(fallback->breakpointID, wildcardID) << "Should fall back to wildcard when page doesn't match";
    EXPECT_EQ(fallback->owner, "wildcard_owner");
}

/// @brief Test ROM vs RAM page type distinction
TEST_F(BreakpointManager_test, FindAddressBreakpoint_DistinguishesROMvsRAM)
{
    // Add breakpoint in ROM page 0
    uint16_t romBp = _brkManager->AddExecutionBreakpointInPage(0x0000, 0, BANK_ROM, "rom_owner");
    ASSERT_NE(romBp, BRK_INVALID);
    
    // Add breakpoint at same address but in RAM page 0
    uint16_t ramBp = _brkManager->AddExecutionBreakpointInPage(0x0000, 0, BANK_RAM, "ram_owner");
    ASSERT_NE(ramBp, BRK_INVALID);
    
    // Query with ROM context
    MemoryPageDescriptor romContext;
    romContext.mode = BANK_ROM;
    romContext.page = 0;
    romContext.addressInPage = 0x0000;
    
    BreakpointDescriptor* foundRom = _brkManager->FindAddressBreakpoint(0x0000, romContext);
    ASSERT_NE(foundRom, nullptr);
    EXPECT_EQ(foundRom->breakpointID, romBp);
    EXPECT_EQ(foundRom->owner, "rom_owner");
    
    // Query with RAM context
    MemoryPageDescriptor ramContext;
    ramContext.mode = BANK_RAM;
    ramContext.page = 0;
    ramContext.addressInPage = 0x0000;
    
    BreakpointDescriptor* foundRam = _brkManager->FindAddressBreakpoint(0x0000, ramContext);
    ASSERT_NE(foundRam, nullptr);
    EXPECT_EQ(foundRam->breakpointID, ramBp);
    EXPECT_EQ(foundRam->owner, "ram_owner");
}

// ============================================================================
// Page-Specific Breakpoint Lifecycle Tests (Register, Find, Remove)
// ============================================================================

/// @brief Test registering execution breakpoint in specific ROM page
TEST_F(BreakpointManager_test, PageSpecific_RegisterExecuteInROM_Positive)
{
    uint16_t brkID = _brkManager->AddExecutionBreakpointInPage(0x3D00, 3, BANK_ROM, "trdos_analyzer");
    
    ASSERT_NE(brkID, BRK_INVALID) << "Should successfully register execution breakpoint in ROM page";
    EXPECT_GE(_brkManager->GetBreakpointsCount(), 1);
    
    // Verify descriptor fields
    BreakpointDescriptor* bp = _brkManager->GetBreakpointById(brkID);
    ASSERT_NE(bp, nullptr);
    EXPECT_EQ(bp->z80address, 0x3D00);
    EXPECT_EQ(bp->page, 3);
    EXPECT_EQ(bp->pageType, BANK_ROM);
    EXPECT_EQ(bp->matchType, BRK_MATCH_BANK_ADDR);
    EXPECT_TRUE(bp->memoryType & BRK_MEM_EXECUTE);
}

/// @brief Test registering read breakpoint in specific RAM page
TEST_F(BreakpointManager_test, PageSpecific_RegisterReadInRAM_Positive)
{
    uint16_t brkID = _brkManager->AddMemReadBreakpointInPage(0xC000, 5, BANK_RAM, "memory_analyzer");
    
    ASSERT_NE(brkID, BRK_INVALID) << "Should successfully register read breakpoint in RAM page";
    
    BreakpointDescriptor* bp = _brkManager->GetBreakpointById(brkID);
    ASSERT_NE(bp, nullptr);
    EXPECT_EQ(bp->z80address, 0xC000);
    EXPECT_EQ(bp->page, 5);
    EXPECT_EQ(bp->pageType, BANK_RAM);
    EXPECT_TRUE(bp->memoryType & BRK_MEM_READ);
}

/// @brief Test registering write breakpoint in specific RAM page
TEST_F(BreakpointManager_test, PageSpecific_RegisterWriteInRAM_Positive)
{
    uint16_t brkID = _brkManager->AddMemWriteBreakpointInPage(0xD000, 7, BANK_RAM, "write_monitor");
    
    ASSERT_NE(brkID, BRK_INVALID) << "Should successfully register write breakpoint in RAM page";
    
    BreakpointDescriptor* bp = _brkManager->GetBreakpointById(brkID);
    ASSERT_NE(bp, nullptr);
    EXPECT_EQ(bp->z80address, 0xD000);
    EXPECT_EQ(bp->page, 7);
    EXPECT_EQ(bp->pageType, BANK_RAM);
    EXPECT_TRUE(bp->memoryType & BRK_MEM_WRITE);
}

/// @brief Test registering combined (read+write+execute) breakpoint in specific page
TEST_F(BreakpointManager_test, PageSpecific_RegisterCombinedInPage_Positive)
{
    uint16_t brkID = _brkManager->AddCombinedMemoryBreakpointInPage(
        0x4000, 
        BRK_MEM_READ | BRK_MEM_WRITE | BRK_MEM_EXECUTE,
        2,
        BANK_RAM,
        "combined_analyzer");
    
    ASSERT_NE(brkID, BRK_INVALID) << "Should successfully register combined breakpoint in page";
    
    BreakpointDescriptor* bp = _brkManager->GetBreakpointById(brkID);
    ASSERT_NE(bp, nullptr);
    EXPECT_TRUE(bp->memoryType & BRK_MEM_READ);
    EXPECT_TRUE(bp->memoryType & BRK_MEM_WRITE);
    EXPECT_TRUE(bp->memoryType & BRK_MEM_EXECUTE);
    EXPECT_EQ(bp->page, 2);
    EXPECT_EQ(bp->pageType, BANK_RAM);
}

/// @brief Test finding page-specific breakpoint with matching page context (positive)
TEST_F(BreakpointManager_test, PageSpecific_FindWithMatchingPage_Positive)
{
    uint16_t brkID = _brkManager->AddExecutionBreakpointInPage(0x0200, 1, BANK_ROM, "test");
    ASSERT_NE(brkID, BRK_INVALID);
    
    // Create matching page context
    MemoryPageDescriptor matchingPage;
    matchingPage.mode = BANK_ROM;
    matchingPage.page = 1;
    matchingPage.addressInPage = 0x0200;
    
    BreakpointDescriptor* found = _brkManager->FindAddressBreakpoint(0x0200, matchingPage);
    ASSERT_NE(found, nullptr) << "Should find breakpoint with matching page";
    EXPECT_EQ(found->breakpointID, brkID);
}

/// @brief Test NOT finding page-specific breakpoint with wrong page (negative)
TEST_F(BreakpointManager_test, PageSpecific_FindWithWrongPage_Negative)
{
    uint16_t brkID = _brkManager->AddExecutionBreakpointInPage(0x0200, 1, BANK_ROM, "test");
    ASSERT_NE(brkID, BRK_INVALID);
    
    // Create NON-matching page context (different page number)
    MemoryPageDescriptor wrongPage;
    wrongPage.mode = BANK_ROM;
    wrongPage.page = 2;  // Wrong page!
    wrongPage.addressInPage = 0x0200;
    
    BreakpointDescriptor* found = _brkManager->FindAddressBreakpoint(0x0200, wrongPage);
    EXPECT_EQ(found, nullptr) << "Should NOT find breakpoint with wrong page number";
}

/// @brief Test NOT finding page-specific breakpoint with wrong page type (negative)
TEST_F(BreakpointManager_test, PageSpecific_FindWithWrongPageType_Negative)
{
    uint16_t brkID = _brkManager->AddExecutionBreakpointInPage(0x0200, 1, BANK_ROM, "test");
    ASSERT_NE(brkID, BRK_INVALID);
    
    // Create NON-matching page context (different page type)
    MemoryPageDescriptor wrongType;
    wrongType.mode = BANK_RAM;  // Wrong type! (ROM vs RAM)
    wrongType.page = 1;
    wrongType.addressInPage = 0x0200;
    
    BreakpointDescriptor* found = _brkManager->FindAddressBreakpoint(0x0200, wrongType);
    EXPECT_EQ(found, nullptr) << "Should NOT find ROM breakpoint when querying RAM context";
}

/// @brief Test removing page-specific breakpoint by ID (positive)
TEST_F(BreakpointManager_test, PageSpecific_RemoveByID_Positive)
{
    uint16_t brkID = _brkManager->AddExecutionBreakpointInPage(0x1000, 2, BANK_ROM, "to_remove");
    ASSERT_NE(brkID, BRK_INVALID);
    
    size_t countBefore = _brkManager->GetBreakpointsCount();
    
    bool removed = _brkManager->RemoveBreakpointByID(brkID);
    EXPECT_TRUE(removed) << "Should successfully remove page-specific breakpoint by ID";
    
    size_t countAfter = _brkManager->GetBreakpointsCount();
    EXPECT_EQ(countAfter, countBefore - 1);
    
    // Verify it's really gone
    BreakpointDescriptor* found = _brkManager->GetBreakpointById(brkID);
    EXPECT_EQ(found, nullptr) << "Removed breakpoint should not be findable";
}

/// @brief Test removing non-existent breakpoint (negative)
TEST_F(BreakpointManager_test, PageSpecific_RemoveNonExistent_Negative)
{
    uint16_t fakeID = 9999;
    
    bool removed = _brkManager->RemoveBreakpointByID(fakeID);
    EXPECT_FALSE(removed) << "Should fail to remove non-existent breakpoint";
}

/// @brief Test multiple page-specific breakpoints at same address in different pages
TEST_F(BreakpointManager_test, PageSpecific_MultipleAtSameAddress_Positive)
{
    // Register breakpoint at 0x0000 in ROM page 0
    uint16_t bp1 = _brkManager->AddExecutionBreakpointInPage(0x0000, 0, BANK_ROM, "rom0_owner");
    ASSERT_NE(bp1, BRK_INVALID);
    
    // Register breakpoint at 0x0000 in ROM page 1
    uint16_t bp2 = _brkManager->AddExecutionBreakpointInPage(0x0000, 1, BANK_ROM, "rom1_owner");
    ASSERT_NE(bp2, BRK_INVALID);
    
    // Register breakpoint at 0x0000 in RAM page 0
    uint16_t bp3 = _brkManager->AddExecutionBreakpointInPage(0x0000, 0, BANK_RAM, "ram0_owner");
    ASSERT_NE(bp3, BRK_INVALID);
    
    // All should be different breakpoints
    EXPECT_NE(bp1, bp2);
    EXPECT_NE(bp1, bp3);
    EXPECT_NE(bp2, bp3);
    
    // Each should be findable in its own context
    MemoryPageDescriptor rom0; rom0.mode = BANK_ROM; rom0.page = 0; rom0.addressInPage = 0x0000;
    MemoryPageDescriptor rom1; rom1.mode = BANK_ROM; rom1.page = 1; rom1.addressInPage = 0x0000;
    MemoryPageDescriptor ram0; ram0.mode = BANK_RAM; ram0.page = 0; ram0.addressInPage = 0x0000;
    
    BreakpointDescriptor* found1 = _brkManager->FindAddressBreakpoint(0x0000, rom0);
    BreakpointDescriptor* found2 = _brkManager->FindAddressBreakpoint(0x0000, rom1);
    BreakpointDescriptor* found3 = _brkManager->FindAddressBreakpoint(0x0000, ram0);
    
    ASSERT_NE(found1, nullptr);
    ASSERT_NE(found2, nullptr);
    ASSERT_NE(found3, nullptr);
    
    EXPECT_EQ(found1->owner, "rom0_owner");
    EXPECT_EQ(found2->owner, "rom1_owner");
    EXPECT_EQ(found3->owner, "ram0_owner");
}

/// @brief Test that duplicate page-specific breakpoint returns same ID (idempotent)
TEST_F(BreakpointManager_test, PageSpecific_DuplicateRegistration_ReturnsExisting)
{
    // Register first time
    uint16_t bp1 = _brkManager->AddExecutionBreakpointInPage(0x3D00, 3, BANK_ROM, "owner1");
    ASSERT_NE(bp1, BRK_INVALID);
    
    size_t countAfterFirst = _brkManager->GetBreakpointsCount();
    
    // Register again with same address, page, and type
    uint16_t bp2 = _brkManager->AddExecutionBreakpointInPage(0x3D00, 3, BANK_ROM, "owner2");
    ASSERT_NE(bp2, BRK_INVALID);
    
    // Should return same ID (idempotent)
    EXPECT_EQ(bp1, bp2) << "Duplicate registration should return existing breakpoint ID";
    
    // Count should not increase
    size_t countAfterSecond = _brkManager->GetBreakpointsCount();
    EXPECT_EQ(countAfterSecond, countAfterFirst) << "Duplicate should not create new breakpoint";
}

/// @brief Test removing one page-specific breakpoint doesn't affect others at same address
TEST_F(BreakpointManager_test, PageSpecific_RemoveOne_OthersUnaffected)
{
    // Register breakpoints at same address in different pages
    uint16_t bp1 = _brkManager->AddExecutionBreakpointInPage(0x0100, 0, BANK_ROM, "keep1");
    uint16_t bp2 = _brkManager->AddExecutionBreakpointInPage(0x0100, 1, BANK_ROM, "remove");
    uint16_t bp3 = _brkManager->AddExecutionBreakpointInPage(0x0100, 2, BANK_ROM, "keep2");
    
    ASSERT_NE(bp1, BRK_INVALID);
    ASSERT_NE(bp2, BRK_INVALID);
    ASSERT_NE(bp3, BRK_INVALID);
    
    // Remove middle one
    bool removed = _brkManager->RemoveBreakpointByID(bp2);
    EXPECT_TRUE(removed);
    
    // Verify bp2 is gone
    EXPECT_EQ(_brkManager->GetBreakpointById(bp2), nullptr);
    
    // Verify bp1 and bp3 still exist
    EXPECT_NE(_brkManager->GetBreakpointById(bp1), nullptr);
    EXPECT_NE(_brkManager->GetBreakpointById(bp3), nullptr);
    
    // Verify they're still findable in their contexts
    MemoryPageDescriptor page0; page0.mode = BANK_ROM; page0.page = 0; page0.addressInPage = 0x0100;
    MemoryPageDescriptor page2; page2.mode = BANK_ROM; page2.page = 2; page2.addressInPage = 0x0100;
    
    EXPECT_NE(_brkManager->FindAddressBreakpoint(0x0100, page0), nullptr);
    EXPECT_NE(_brkManager->FindAddressBreakpoint(0x0100, page2), nullptr);
}
