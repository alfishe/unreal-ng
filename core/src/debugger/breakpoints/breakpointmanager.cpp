#include "stdafx.h"

#include "common/logger.h"

#include "breakpointmanager.h"

/// region <Constructors / destructors>

BreakpointManager::BreakpointManager(EmulatorContext* context)
{
    _context = context;
}

BreakpointManager::~BreakpointManager()
{
    _context = nullptr;
}

/// endregion </Constructors / destructors>

/// region <Management methods>

void BreakpointManager::ClearBreakpoints()
{
    _breakpointMap.clear();
}

uint16_t BreakpointManager::AddBreakpoint(BreakpointDescriptor descriptor)
{
    uint16_t result = BRK_INVALID;

    return result;
}

bool BreakpointManager::RemoveBreakpoint(BreakpointDescriptor descriptor)
{
    bool result = false;

    return result;
}

bool BreakpointManager::RemoveBreakpoint(uint16_t breakpointID)
{
    bool result = false;

    return result;
}

/// endregion </Management methods>