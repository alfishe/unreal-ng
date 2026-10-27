#pragma once
#include "stdafx.h"

#include "common/modulelogger.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/listing/listingparser.h"
#include "debugger/targets/debugcpuid.h"
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
    std::unique_ptr<class ScriptCallbackManager> _scriptCallbacks;
    // The card CPU in the General Sound slot (gs-debugger design §3.2): its breakpoints and hooks
    std::unique_ptr<class GSCardTarget> _gsTarget;
    // The General Sound of a MultiSound slot card (the `ms` CPU)
    std::unique_ptr<class GSCardTarget> _msTarget;
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
    /// Script callbacks (debugger/callbacks/scriptcallbacks.h)
    ScriptCallbackManager* GetScriptCallbacks() { return _scriptCallbacks.get(); }
    /// The General Sound slot's card CPU (exists for the machine's lifetime; Available() while a card with a CPU is
    /// fitted)
    GSCardTarget* GetGSTarget() { return _gsTarget.get(); }
    /// A card CPU's target (gs, ms); nullptr for the main CPU
    GSCardTarget* GetCardTarget(DebugCpuId cpu);
    /// The breakpoints of a CPU: the main CPU's (GetBreakpointsManager), or a card's
    BreakpointManager* GetBreakpointsManager(DebugCpuId cpu);
    /// The CPU of a surface name ("main", "gs", "ms"; "neogs" names the GS slot, "multisound" the MultiSound's GS);
    /// false for an unknown name
    static bool ParseCpuName(const std::string& name, DebugCpuId& out);
    /// The General Sound slot's card was created or replaced (SoundManager): the card hooks follow it
    void OnGeneralSoundCardCreated();
    /// The slot cards were built or released (SlotManager): the MultiSound GS's hooks follow its card
    void OnSlotCardsChanged();

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
