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
    _brkManager = new BreakpointManager(_context);
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
    FAIL() << "Not implemented yet";
}

TEST_F(BreakpointManager_test, executionBreakpoint)
{
    volatile bool breakpointTriggered = false;
    uint16_t breakpointAddress = 0x0000;

    /// region <Initialize>
    Emulator* emulator = new Emulator(LoggerLevel::LogError);
    bool init = emulator->Init();
    emulator->DebugOn();

    EmulatorContext* context = emulator->GetContext();
    BreakpointManager* breakpointManager = emulator->GetBreakpointManager();

    // Register MessageCenter event handler as lambda
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.AddObserver(NC_LOGGER_BREAKPOINT, [=, &breakpointTriggered](int id, Message* message) mutable
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
    emulator->DebugOn();

    EmulatorContext* context = emulator->GetContext();
    BreakpointManager* breakpointManager = emulator->GetBreakpointManager();

    // Transfer test Z80 command sequence to ROM space (From address $0000)
    Memory* memory = emulator->GetMemory();
    for (int i = 0; i < sizeof(testCommands) / sizeof (testCommands[0]); i++)
    {
        memory->DirectWriteToZ80Memory(i, testCommands[i]);
    }

    // Register MessageCenter event handler as lambda
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.AddObserver(NC_LOGGER_BREAKPOINT, [=, &breakpointTriggered](int id, Message* message) mutable
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

    emulator->RunNCPUCycles(3);

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
    emulator->DebugOn();

    EmulatorContext* context = emulator->GetContext();
    BreakpointManager* breakpointManager = emulator->GetBreakpointManager();

    // Transfer test Z80 command sequence to ROM space (From address $0000)
    Memory* memory = emulator->GetMemory();
    for (int i = 0; i < sizeof(testCommands) / sizeof (testCommands[0]); i++)
    {
        memory->DirectWriteToZ80Memory(i, testCommands[i]);
    }

    // Register MessageCenter event handler as lambda
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.AddObserver(NC_LOGGER_BREAKPOINT, [=, &breakpointTriggered](int id, Message* message) mutable
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

    emulator->RunNCPUCycles(6);

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
    FAIL() << "Not implemented yet";
}

TEST_F(BreakpointManager_test, portOutBreakpoint)
{
    FAIL() << "Not implemented yet";
}