#pragma once
#include "stdafx.h"

#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/labels/labelmanager.h"
#include "emulator/emulatorcontext.h"
#include <map>

class DebugManager
{
    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;
    BreakpointManager* _breakpoints = nullptr;
    LabelManager* _labels = nullptr;
    Z80Disassembler* _disassembler = nullptr;
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    DebugManager() = delete;        // Disable default constructor. C++ 11 feature
    DebugManager(EmulatorContext* context);
    virtual ~DebugManager();

    /// endregion </Constructors / Destructors>

    /// region <Properties>

    BreakpointManager* GetBreakpointsManager();
    LabelManager* GetLabelManager();
    Z80Disassembler* GetDisassembler();

    /// endregion </Properties>

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
