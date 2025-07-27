#include "pch.h"

#include "breakpoints_test.h"

#include <vector>
#include "3rdparty/message-center/messagecenter.h"
#include "common/dumphelper.h"
#include "common/stringhelper.h"
#include "common/timehelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

/// region <SetUp / TearDown>

void BreakpointManager_test::SetUp()
{
    _context = new EmulatorContext(LoggerLevel::LogError);
    _brkManager = new BreakpointManagerCUT(_context);
}

void BreakpointManager_test::TearDown()
{
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
        std::string message = StringHelper::Format("Add breakpoint failed. Expected %d breakpoints after add, detected %d", expectedCount, finalCount);
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
    breakpoint.ioType = BRK_IO_IN; // Test with port input breakpoint
    breakpoint.z80address = 0xFE; // Test with port 0xFE

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
        std::string message = StringHelper::Format("Add breakpoint failed. Expected %d breakpoints after add, detected %d", expectedCount, finalCount);
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
    volatile bool breakpointTriggered = false;
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
    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, [=, &breakpointTriggered](int id, Message* message) mutable
        {
            breakpointTriggered = true;

            //sleep_ms(20);
            emulator->Resume();
        }
    );

    /// endregion </Initialize>

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = BRK_MEM_EXECUTE;
    breakpoint->z80address = breakpointAddress;
    breakpointManager->AddBreakpoint(breakpoint);

    emulator->RunSingleCPUCycle(false);

    if (!breakpointTriggered)
    {
        FAIL() << StringHelper::Format("Execution breakpoint on address: $%04X wasn't triggered", breakpointAddress) << std::endl;
    }

    /// region <Release>

    emulator->Stop();
    emulator->Release();
    delete emulator;
    /// endregion </Release>
}

TEST_F(BreakpointManager_test, memoryReadBreakpoint)
{
    volatile bool breakpointTriggered = false;
    uint8_t testCommands[] =
    {
        0x21, 0x00, 0x40,       // $0000 LD HL, $4000
        0x7E,                   // $0003 LD A, (HL)
        0x76                    // $0004 HALT
    };
    uint16_t breakpointAddress = 0x4000;    // Let's break on $4000 memory address read

    /// region <Initialize>
    Emulator* emulator = new Emulator(LoggerLevel::LogError);
    bool init = emulator->Init();


    EmulatorContext* context = emulator->GetContext();
    BreakpointManager* breakpointManager = emulator->GetBreakpointManager();

    // Enable global debugging (memory debug interface, CPU level debug) + enable Breakpoints feature
    emulator->DebugOn();
    context->pFeatureManager->setFeature(Features::kBreakpoints, true);

    // Transfer test Z80 command sequence to ROM space (From address $0000)
    Memory* memory = emulator->GetMemory();
    for (int i = 0; i < sizeof(testCommands) / sizeof (testCommands[0]); i++)
    {
        memory->DirectWriteToZ80Memory(i, testCommands[i]);
    }

    // Register MessageCenter event handler as lambda
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, [=, &breakpointTriggered](int id, Message* message) mutable
          {
              breakpointTriggered = true;

              //sleep_ms(20);
              emulator->Resume();
          }
    );

    /// endregion </Initialize>

    // Create memory read breakpoint on specified address in Z80 address space (bank-independent)
    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = BRK_MEM_READ;
    breakpoint->z80address = breakpointAddress;
    breakpointManager->AddBreakpoint(breakpoint);

    emulator->RunNCPUCycles(10);

    if (!breakpointTriggered)
    {
        FAIL() << StringHelper::Format("Memory read breakpoint on address: $%04X wasn't triggered", breakpointAddress) << std::endl;
    }

    /// region <Release>

    emulator->Stop();
    emulator->Release();
    delete emulator;
    /// endregion </Release>
}

TEST_F(BreakpointManager_test, memoryWriteBreakpoint)
{
    volatile bool breakpointTriggered = false;
    uint8_t testCommands[] =
    {
        0x21, 0x00, 0x40,       // $0000 LD HL, $4000
        0x3E, 0xA3,             // $0003 LD A, $A3
        0x77,                   // $0005 LD (HL), A
        0x76                    // $0006 HALT
    };
    uint16_t breakpointAddress = 0x4000;    // Let's break on $4000 memory address read

    /// region <Initialize>
    Emulator* emulator = new Emulator(LoggerLevel::LogError);
    bool init = emulator->Init();

    EmulatorContext* context = emulator->GetContext();
    BreakpointManager* breakpointManager = emulator->GetBreakpointManager();

    // Enable global debugging (memory debug interface, CPU level debug) + enable Breakpoints feature
    emulator->DebugOn();
    context->pFeatureManager->setFeature(Features::kBreakpoints, true);

    // Transfer test Z80 command sequence to ROM space (From address $0000)
    Memory* memory = emulator->GetMemory();
    for (int i = 0; i < sizeof(testCommands) / sizeof (testCommands[0]); i++)
    {
        memory->DirectWriteToZ80Memory(i, testCommands[i]);
    }

    // Register MessageCenter event handler as lambda
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, [=, &breakpointTriggered](int id, Message* message) mutable
          {
              breakpointTriggered = true;

              //sleep_ms(20);
              emulator->Resume();
          }
    );

    /// endregion </Initialize>

    // Create memory write breakpoint on specified address in Z80 address space (bank-independent)
    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = BRK_MEM_WRITE;
    breakpoint->z80address = breakpointAddress;
    breakpointManager->AddBreakpoint(breakpoint);

    emulator->RunNCPUCycles(6, false);

    if (!breakpointTriggered)
    {
        FAIL() << StringHelper::Format("Memory write breakpoint on address: $%04X wasn't triggered", breakpointAddress) << std::endl;
    }

    /// region <Release>

    emulator->Stop();
    emulator->Release();
    delete emulator;
    /// endregion </Release>
}


TEST_F(BreakpointManager_test, portInBreakpoint)
{
    volatile bool breakpointTriggered = false;
    uint8_t testCommands[] =
    {
        0xAF,                   // $0000 XOR A - Ensure A = 0
        0xED, 0x70, 0x00, 0x00, // $0001 IN A, ($00)
        0x76                    // $0005 HALT
    };
    uint8_t portNumber = 0x00;  // Test port input from port 0

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
    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, [=, &breakpointTriggered](int id, Message* message) mutable
    {
        breakpointTriggered = true;
        emulator->Resume();
    });

    /// endregion </Initialize>

    // Create port input breakpoint
    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_IO;
    breakpoint->ioType = BRK_IO_IN;
    breakpoint->z80address = portNumber;
    breakpointManager->AddBreakpoint(breakpoint);

    emulator->RunNCPUCycles(4, false);

    if (!breakpointTriggered)
    {
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
    volatile bool breakpointTriggered = false;
    uint8_t testCommands[] =
    {
        0xAF,                   // $0000 XOR A - Ensure A = 0
        0xD3, 0xFE,             // $0001 OUT ($FE), A
        0x76                    // $0003 HALT
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
    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, [=, &breakpointTriggered](int id, Message* message) mutable
          {
              breakpointTriggered = true;
              emulator->Resume();
          }
    );

    /// endregion </Initialize>

    // Create port output breakpoint
    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_IO;
    breakpoint->ioType = BRK_IO_OUT;
    breakpoint->z80address = portNumber;
    breakpointManager->AddBreakpoint(breakpoint);

    emulator->RunNCPUCycles(4, false);

    if (!breakpointTriggered)
    {
        FAIL() << StringHelper::Format("Port output breakpoint on port: $%02X wasn't triggered", portNumber) << std::endl;
    }

    /// region <Release>
    emulator->Stop();
    emulator->Release();
    delete emulator;
    /// endregion </Release>
}