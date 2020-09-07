#include "stdafx.h"

#include "common/logger.h"

#include "debugmanager.h"

/// region <Constructors / Destructors>

DebugManager::DebugManager(EmulatorContext* context)
{
    _context = context;

    _breakpoints = new BreakpointManager(context);
}

DebugManager::~DebugManager()
{
    if (_breakpoints)
    {
        delete _breakpoints;
        _breakpoints = nullptr;
    }

    _context = nullptr;
}

/// endregion </Constructors / Destructors>

/// region <Breakpoint management>

void DebugManager::AddBreakpoint(BreakpointTypeEnum type, uint16_t address)
{

}

void DebugManager::RemoveBreakpoint(BreakpointTypeEnum type, uint16_t address)
{

}

void DebugManager::RemoveAllBreakpoints()
{

}

void DebugManager::DisableBreakpoint(BreakpointTypeEnum type, uint16_t address)
{

}

void DebugManager::EnableBreakpoint(BreakpointTypeEnum type, uint16_t address)
{

}

/// endregion </Breakpoint management>

/// region <State management>

/// endregion </State management>

/// region <Peripheral management>

/// endregion </Peripheral management>