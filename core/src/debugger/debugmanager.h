#pragma once
#include "stdafx.h"

#include "common/modulelogger.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/analyzers/analyzermanager.h"
#include "emulator/emulatorcontext.h"
#include <map>

class DebugManager
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DEBUGGER;
    const uint16_t _SUBMODULE = PlatformDebuggerSubmodulesEnum::SUBMODULE_DEBUG_GENERIC;
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;
    BreakpointManager* _breakpoints = nullptr;
    LabelManager* _labels = nullptr;
    std::unique_ptr<Z80Disassembler> _disassembler = nullptr;
    std::unique_ptr<AnalyzerManager> _analyzerManager = nullptr;
    
    // Keyboard injection manager for automation/debugging
    class DebugKeyboardManager* _keyboardManager = nullptr;
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
    std::unique_ptr<Z80Disassembler>& GetDisassembler();
    AnalyzerManager* GetAnalyzerManager();
    DebugKeyboardManager* GetKeyboardManager();

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
