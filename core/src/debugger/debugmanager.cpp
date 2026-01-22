#include "stdafx.h"

#include "debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/trdos/trdosanalyzer.h"

/// region <Constructors / Destructors>

DebugManager::DebugManager(EmulatorContext* context)
{
    _context = context;
    _logger = _context->pModuleLogger;

    // Create all child components first
    _breakpoints = new BreakpointManager(_context);
    _labels = new LabelManager(_context);
    _analyzerManager = std::make_unique<AnalyzerManager>(_context);
    
    // Initialize AnalyzerManager after all components are created
    // Pass 'this' because _context->pDebugManager isn't set yet
    _analyzerManager->init(this);
    
    // Register built-in analyzers
    _analyzerManager->registerAnalyzer("trdos", std::make_unique<TRDOSAnalyzer>(_context));

    _disassembler = std::make_unique<Z80Disassembler>(_context);
    _disassembler->SetLogger(_context->pModuleLogger);
}

DebugManager::~DebugManager()
{
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

std::unique_ptr<Z80Disassembler>& DebugManager::GetDisassembler()
{
    return _disassembler;
}

AnalyzerManager* DebugManager::GetAnalyzerManager()
{
    return _analyzerManager.get();
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