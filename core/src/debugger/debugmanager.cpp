#include "stdafx.h"

#include "debugmanager.h"

/// region <Constructors / Destructors>

DebugManager::DebugManager(EmulatorContext* context)
{
    _context = context;
    _logger = _context->pModuleLogger;

    _breakpoints = new BreakpointManager(context);
    _labels = new LabelManager(context);
}

DebugManager::~DebugManager()
{
    if (_disassembler)
    {
        delete _disassembler;
        _disassembler = nullptr;
    }

    if (_labels)
    {
        delete _labels;
        _labels = nullptr;
    }

    if (_breakpoints)
    {
        delete _breakpoints;
        _breakpoints = nullptr;
    }

    _context = nullptr;
}

/// endregion </Constructors / Destructors>

/// region <Properties>

BreakpointManager* DebugManager::GetBreakpointsManager()
{
    return _breakpoints;
}

LabelManager* DebugManager::GetLabelManager()
{
    return _labels;
}

Z80Disassembler* DebugManager::GetDisassembler()
{
    return _disassembler;
}

/// endregion </Properties>

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