#pragma once
#include "stdafx.h"

#include "common/modulelogger.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/listing/listingparser.h"
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
    ListingParser* _listing = nullptr;
    std::unique_ptr<Z80Disassembler> _disassembler = nullptr;
    std::unique_ptr<AnalyzerManager> _analyzerManager = nullptr;
    
    // Keyboard injection manager for automation/debugging
    class DebugKeyboardManager* _keyboardManager = nullptr;

    // Kempston Mouse injection funnel (automation, host input, TTD journal)
    class DebugMouseManager* _mouseManager = nullptr;

    // Kempston joystick injection funnel (automation, TTD journal)
    class DebugJoystickManager* _joystickManager = nullptr;

    // ROM editor control points for verified command input (input-verification.md)
    // Registered with (and owned by) the AnalyzerManager
    class EditorMonitor* _editorMonitor = nullptr;
    std::unique_ptr<class CommandTyper> _commandTyper;
    std::unique_ptr<class PcHistory> _pcHistory;
    std::unique_ptr<class AsmSyncService> _asmSync;
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
    ListingParser* GetListingParser();
    std::unique_ptr<Z80Disassembler>& GetDisassembler();
    /// Inline: Z80::RunInstructionStartHooks asks it at every instruction (the CPU-step subscribers)
    AnalyzerManager* GetAnalyzerManager() { return _analyzerManager.get(); }
    DebugKeyboardManager* GetKeyboardManager();
    DebugMouseManager* GetMouseManager();
    DebugJoystickManager* GetJoystickManager();
    EditorMonitor* GetEditorMonitor() { return _editorMonitor; }
    CommandTyper* GetCommandTyper() { return _commandTyper.get(); }
    /// The PC history ring (pchistory.h): off until a debugger arms it
    PcHistory* GetPcHistory() { return _pcHistory.get(); }
    /// The asm-synchronizer's watch (debugger/asm/sync/asmsyncservice.h): no thread until a watch starts
    AsmSyncService* GetAsmSyncService() { return _asmSync.get(); }

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
