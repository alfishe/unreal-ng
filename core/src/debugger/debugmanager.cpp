#include "stdafx.h"

#include "debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/audiocapture/audiocaptureanalyzer.h"
#include "debugger/analyzers/basic-lang/commandtyper.h"
#include "debugger/analyzers/basic-lang/editormonitor.h"
#include "debugger/analyzers/aylog/ayloganalyzer.h"
#include "debugger/analyzers/coverage/coverageanalyzer.h"
#include "debugger/analyzers/trdos/trdosanalyzer.h"
#include "debugger/joystick/debugjoystickmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/mouse/debugmousemanager.h"

/// region <Constructors / Destructors>

DebugManager::DebugManager(EmulatorContext* context)
{
    _context = context;
    _logger = _context->pModuleLogger;

    // Create all child components first
    _breakpoints = new BreakpointManager(_context);
    _labels = new LabelManager(_context);
    _listing = new ListingParser(_context);
    _analyzerManager = std::make_unique<AnalyzerManager>(_context);
    
    // Keyboard injection manager for automation
    _keyboardManager = new DebugKeyboardManager(_context);
    _mouseManager = new DebugMouseManager(_context);
    _joystickManager = new DebugJoystickManager(_context);
    _commandTyper = std::make_unique<CommandTyper>(_context);
    
    // Initialize AnalyzerManager after all components are created
    // Pass 'this' because _context->pDebugManager isn't set yet
    _analyzerManager->init(this);
    
    // Register built-in analyzers
    _analyzerManager->registerAnalyzer("trdos", std::make_unique<TRDOSAnalyzer>(_context));
    _analyzerManager->registerAnalyzer("coverage", std::make_unique<CoverageAnalyzer>());
    _analyzerManager->registerAnalyzer("aylog", std::make_unique<AYLogAnalyzer>(_context));
    _analyzerManager->registerAnalyzer("audiocapture", std::make_unique<AudioCaptureAnalyzer>(_context));

    // Editor control points for verified command input (input-verification.md)
    auto editorMonitor = std::make_unique<EditorMonitor>(_context);
    _editorMonitor = editorMonitor.get();
    _analyzerManager->registerAnalyzer(EditorMonitor::ANALYZER_ID, std::move(editorMonitor));

    _disassembler = std::make_unique<Z80Disassembler>(_context);
    _disassembler->SetLogger(_context->pModuleLogger);
}

DebugManager::~DebugManager()
{
    // AnalyzerManager::~AnalyzerManager() deactivates every analyzer, which releases their
    // breakpoints through BreakpointManager - so it must go before _breakpoints is deleted
    // (as a unique_ptr member it would otherwise be destroyed after this body has run).
    _commandTyper.reset();
    _analyzerManager.reset();
    _editorMonitor = nullptr;

    if (_keyboardManager)
    {
        delete _keyboardManager;
        _keyboardManager = nullptr;
    }

    if (_mouseManager)
    {
        delete _mouseManager;
        _mouseManager = nullptr;
    }

    if (_joystickManager)
    {
        delete _joystickManager;
        _joystickManager = nullptr;
    }
    
    if (_labels)
    {
        delete _labels;
        _labels = nullptr;
    }

    if (_listing)
    {
        delete _listing;
        _listing = nullptr;
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

ListingParser* DebugManager::GetListingParser()
{
    return _listing;
}

std::unique_ptr<Z80Disassembler>& DebugManager::GetDisassembler()
{
    return _disassembler;
}

AnalyzerManager* DebugManager::GetAnalyzerManager()
{
    return _analyzerManager.get();
}

DebugKeyboardManager* DebugManager::GetKeyboardManager()
{
    return _keyboardManager;
}

DebugMouseManager* DebugManager::GetMouseManager()
{
    return _mouseManager;
}

DebugJoystickManager* DebugManager::GetJoystickManager()
{
    return _joystickManager;
}

/// endregion </Properties>

/// region <Breakpoint management>

void DebugManager::AddBreakpoint(BreakpointTypeEnum type, uint16_t address)
{
    (void)type;
    (void)address;
}

void DebugManager::RemoveBreakpoint(BreakpointTypeEnum type, uint16_t address)
{
    (void)type;
    (void)address;
}

void DebugManager::RemoveAllBreakpoints()
{

}

void DebugManager::DisableBreakpoint(BreakpointTypeEnum type, uint16_t address)
{
    (void)type;
    (void)address;
}

void DebugManager::EnableBreakpoint(BreakpointTypeEnum type, uint16_t address)
{
    (void)type;
    (void)address;
}

/// endregion </Breakpoint management>

/// region <State management>

/// endregion </State management>

/// region <Peripheral management>

/// endregion </Peripheral management>