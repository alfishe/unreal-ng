#include "pch.h"

#include "breakpoints_test.h"

#include <vector>
#include "common/dumphelper.h"
#include "common/stringhelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
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

}

TEST_F(BreakpointManager_test, addPortBreakpoint)
{

}