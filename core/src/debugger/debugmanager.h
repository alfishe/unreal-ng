#pragma once
#include "stdafx.h"

#include "emulatorcontext.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include <map>

class DebugManager
{
    /// region <Fields>
protected:
    EmulatorContext* _context;
    BreakpointManager* _breakpoints;
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    DebugManager() = delete;        // Disable default constructor. C++ 11 feature
    DebugManager(EmulatorContext* context);
    virtual ~DebugManager();

    /// endregion </Constructors / Destructors>

    /// region <CPU registers>
    /// endregion <CPU registers>

    /// region <Memory access>
    /// endregion </Memory access>x

    /// region <Breakpoint management>
public:
    void AddBreakpoint(BreakpointTypeEnum type, uint16_t address);
    void RemoveBreakpoint(BreakpointTypeEnum type, uint16_t address);
    void RemoveAllBreakpoints();

    void DisableBreakpoint(BreakpointTypeEnum type, uint16_t address);
    void EnableBreakpoint(BreakpointTypeEnum type, uint16_t address);

    /// endregion </Breakpoint management>
};
