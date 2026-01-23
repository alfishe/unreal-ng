#include "trdosanalyzer.h"

#include "debugger/analyzers/analyzermanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/io/fdc/wd1793.h"
#include "common/uuid.h"

#include <sstream>
#include <iomanip>

// ==================== TRDOSEvent formatting ====================

std::string TRDOSEvent::format() const
{
    std::ostringstream ss;
    ss << "[" << std::setw(10) << timestamp << "] ";
    
    switch (type)
    {
        case TRDOSEventType::TRDOS_ENTRY:
            ss << "TR-DOS Entry (PC=$" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << context.pc << ")";
            // Add Interrupt State info
            ss << " [" << (context.iff1 ? "EI" : "DI") << " IM" << (int)context.im << "]";

            break;
        case TRDOSEventType::TRDOS_EXIT:
            ss << "TR-DOS Exit";
            break;
        case TRDOSEventType::COMMAND_START:
            ss << "Command: ";
            switch (command)
            {
                case TRDOSCommand::LOAD: ss << "LOAD"; break;
                case TRDOSCommand::SAVE: ss << "SAVE"; break;
                case TRDOSCommand::CAT: ss << "CAT"; break;
                case TRDOSCommand::RUN: ss << "RUN"; break;
                case TRDOSCommand::FORMAT: ss << "FORMAT"; break;
                default: ss << "UNKNOWN"; break;
            }
            if (!filename.empty())
            {
                ss << " \"" << filename << "\"";
            }
            break;
        case TRDOSEventType::FDC_CMD_READ:
            ss << "FDC Read Sector T" << (int)track << "/S" << (int)sector;
            break;
        case TRDOSEventType::FDC_CMD_WRITE:
            ss << "FDC Write Sector T" << (int)track << "/S" << (int)sector;
            break;
        case TRDOSEventType::FDC_CMD_SEEK:
            ss << "FDC Seek T" << (int)track;
            break;
        case TRDOSEventType::FDC_CMD_RESTORE:
            ss << "FDC Restore (T0)";
            break;
        case TRDOSEventType::SECTOR_TRANSFER:
            ss << "Sector Transfer: " << bytesTransferred << " bytes, T" << (int)track << "/S" << (int)sector;
            break;
        case TRDOSEventType::FDC_CMD_READ_ADDR:
            ss << "FDC Read Address";
            break;
        case TRDOSEventType::FDC_CMD_READ_TRACK:
            ss << "FDC Read Track (T" << (int)track << ")";
            break;
        case TRDOSEventType::FDC_CMD_WRITE_TRACK:
            ss << "FDC Write Track (Format T" << (int)track << ")";
            break;
        case TRDOSEventType::FDC_CMD_STEP:
            ss << "FDC Step";
            break;
        case TRDOSEventType::FILE_FOUND:
            ss << "File Found: " << filename;
            break;
        case TRDOSEventType::FILE_NOT_FOUND:
            ss << "File Not Found: " << filename;
            break;
        case TRDOSEventType::MODULE_LOAD:
            ss << "Module Loaded: " << filename;
            break;
        case TRDOSEventType::MODULE_SAVE:
            ss << "Module Saved: " << filename;
            break;
        case TRDOSEventType::ERROR_CRC:
            ss << "*** CRC ERROR ***";
            break;
        case TRDOSEventType::ERROR_LOST_DATA:
            ss << "*** DATA LOST ERROR ***";
            break;
        case TRDOSEventType::ERROR_WRITE_PROTECT:
            ss << "*** WRITE PROTECT ERROR ***";
            break;

        case TRDOSEventType::ERROR_RNF:
            ss << "*** ERROR: Record Not Found ***";
            break;
        case TRDOSEventType::LOADER_DETECTED:
            ss << "Custom Loader Activity Detected";
            break;
        case TRDOSEventType::PROTECTION_DETECTED:
            ss << "Protection Check Detected";
            break;
        default:
            ss << "Event type " << static_cast<int>(type);
            break;
    }
    
    return ss.str();
}

// ==================== TRDOSAnalyzer implementation ====================

TRDOSAnalyzer::TRDOSAnalyzer(EmulatorContext* context)
    : _context(context)
    , _events(SEMANTIC_BUFFER_SIZE)
    , _rawFdcEvents(RAW_BUFFER_SIZE)
    , _rawBreakpointEvents(RAW_BUFFER_SIZE)
{
    _uuid = UUID::Generate().toString();
}

TRDOSAnalyzer::~TRDOSAnalyzer()
{
    if (_manager)
    {
        onDeactivate();
    }
}

// ==================== IAnalyzer interface ====================

void TRDOSAnalyzer::onActivate(AnalyzerManager* manager)
{
    _manager = manager;
    
    // Get WD1793 reference from context
    // Note: This assumes Beta128 disk interface is active
    if (_context && _context->pBetaDisk)
    {
        _fdc = _context->pBetaDisk;
        _fdc->addObserver(this);
    }
    
    // Register TR-DOS ROM breakpoints using page-specific breakpoints
    // This ensures they only trigger when executing in TR-DOS ROM, not other ROMs at same address
    if (_context && _context->pMemory && _context->pMemory->base_dos_rom != nullptr)
    {
        /* 
           DEBUG: Force global breakpoints to rule out paging issues. 
           The page calculation might be off or the debugger dispatch might fail for banked ROMs.
        */
        
        // Memory& memory = *_context->pMemory;
        // uint8_t dosRomPage = static_cast<uint8_t>(memory.GetROMPageFromAddress(memory.base_dos_rom));
        
        // Use page-specific breakpoints that only fire when TR-DOS ROM is mapped
        // manager->requestExecutionBreakpointInPage(BP_TRDOS_ENTRY, dosRomPage, BANK_ROM, _registrationId);
        // manager->requestExecutionBreakpointInPage(BP_COMMAND_DISPATCH, dosRomPage, BANK_ROM, _registrationId);
        // manager->requestExecutionBreakpointInPage(BP_EXIT, dosRomPage, BANK_ROM, _registrationId);
        
        // Fallback to global for debugging:
        manager->requestExecutionBreakpoint(BP_TRDOS_ENTRY, _registrationId);
        manager->requestExecutionBreakpoint(BP_COMMAND_DISPATCH, _registrationId);
        manager->requestExecutionBreakpoint(BP_SERVICE_ENTRY, _registrationId);
        manager->requestExecutionBreakpoint(BP_EXIT, _registrationId);
    }
    else
    {
        // Fallback: use regular breakpoints (will trigger in any ROM - less accurate)
        manager->requestExecutionBreakpoint(BP_TRDOS_ENTRY, _registrationId);
        manager->requestExecutionBreakpoint(BP_SERVICE_ENTRY, _registrationId);
        manager->requestExecutionBreakpoint(BP_COMMAND_DISPATCH, _registrationId);
        manager->requestExecutionBreakpoint(BP_DOS_ENTRY, _registrationId);
        manager->requestExecutionBreakpoint(BP_EXIT, _registrationId);
    }
    
    // Check if TR-DOS is already active
    // Refer to trdos_rom_interaction_analysis.md "Reliable DOS ROM Active Detection Algorithm"
    
    // For the analyzer, if the user activated it, we should be in "Monitoring" mode.
    // The previous logic tried to be "Context Aware" (only capture if CPU is in TR-DOS).
    // But FDC commands can happen via quick traps.
    // So we will default to IN_TRDOS (Active Monitoring) processing breakpoints and FDC events.
    
    // We default to IDLE to allow proper state transitions (including Custom Loaders).
    // If we are actually in TR-DOS, the next instruction/hook will transition us.
    // However, if we want to catch "already inside" case, we could check PC/ROM.
    // But for "Custom Loader" detection (which enters from RAM), we MUST start IDLE
    // if we are in RAM.
    
    bool isAlreadyActive = false; 
    
    // Check if we are physically in TR-DOS ROM (paged in + PC in range)
    if (_context && _context->pMemory && _context->pCore && _context->pCore->GetZ80())
    {
       // Implementation specific check: check if TR-DOS ROM is active
       // Simplified: just trust breakpoints for transition, or use heuristic
       // For now, start IDLE. The first breakpoint hit or FDC command will wake us up.
    }
    
    // Reset state
    if (isAlreadyActive)
    {
        _state = TRDOSAnalyzerState::IN_TRDOS;
        
        // Emit implicit entry event since we are already active and might miss the breakpoint
        TRDOSEvent entryEvent{};
        
        // Capture context if available
        if (_context && _context->pCore && _context->pCore->GetZ80())
        {
            entryEvent.timestamp = _context->pCore->GetZ80()->tt;
            entryEvent.context.pc = _context->pCore->GetZ80()->pc;
            entryEvent.context.iff1 = _context->pCore->GetZ80()->iff1;
            entryEvent.context.im = _context->pCore->GetZ80()->im;
        }
        else
        {
            entryEvent.timestamp = 0;
        }
        
        entryEvent.frameNumber = _context ? _context->emulatorState.frame_counter : 0;
        entryEvent.type = TRDOSEventType::TRDOS_ENTRY;
        entryEvent.flags = 0x01; // Implied
        emitEvent(std::move(entryEvent));
    }
    else
    {
        _state = TRDOSAnalyzerState::IDLE;
    }
    
    _currentCommand = TRDOSCommand::UNKNOWN;
}

void TRDOSAnalyzer::onDeactivate()
{
    // Unsubscribe from FDC events
    if (_fdc)
    {
        _fdc->removeObserver(this);
        _fdc = nullptr;
    }
    
    // Breakpoints are auto-cleaned by AnalyzerManager
    _manager = nullptr;
    
    // Reset state fully
    _state = TRDOSAnalyzerState::IDLE;
    _currentCommand = TRDOSCommand::UNKNOWN;
}

void TRDOSAnalyzer::onBreakpointHit(uint16_t address, Z80* cpu)
{
    // Layer 1: Capture raw breakpoint event with full context
    captureRawBreakpointEvent(address, cpu);
    
    // Layer 2: Semantic processing
    switch (address)
    {
        case BP_TRDOS_ENTRY:
            handleTRDOSEntry(cpu);
            break;
        case BP_COMMAND_DISPATCH:
        case BP_SERVICE_ENTRY:
             // Pass address so we know which entry point we're at
             // 3D13 has C register with command, 3D21 may not
            handleCommandDispatch(address, cpu);
            break;
        case BP_DOS_ENTRY:
            // Custom loader detected entering via non-standard point
            if (_state != TRDOSAnalyzerState::IN_TRDOS && _state != TRDOSAnalyzerState::IN_COMMAND)
            {
                 // Transition to Custom State
                 _state = TRDOSAnalyzerState::IN_CUSTOM;
                 
                 TRDOSEvent entryEvent{};
                 entryEvent.timestamp = cpu ? cpu->tt : 0;
                 entryEvent.type = TRDOSEventType::TRDOS_ENTRY;
                 entryEvent.context.pc = cpu ? cpu->pc : 0;
                 
                 // Capture Interrupt State
                 if (cpu)
                 {
                     entryEvent.context.iff1 = cpu->iff1;
                     entryEvent.context.im = cpu->im;
                 }
                 
                 // Get caller
                 entryEvent.context.callerAddress = 0;
                 if (cpu && _context && _context->pMemory)
                 {
                     uint16_t sp = cpu->sp;
                     uint8_t low = _context->pMemory->DirectReadFromZ80Memory(sp);
                     uint8_t high = _context->pMemory->DirectReadFromZ80Memory(sp + 1);
                     entryEvent.context.callerAddress = (high << 8) | low;
                 }
                 
                 entryEvent.flags = 0x02; // Custom entry flag
                 emitEvent(std::move(entryEvent));
            }
            break;
        case BP_EXIT:
            // Exit from TR-DOS - if we were in command, transition back to IN_TRDOS
            // This allows the next service call to be treated as a new command
            if (_state == TRDOSAnalyzerState::IN_COMMAND)
            {
                _state = TRDOSAnalyzerState::IN_TRDOS;
                _currentCommand = TRDOSCommand::UNKNOWN;
            }
            break;
    }
}

void TRDOSAnalyzer::onFrameEnd()
{
    // Exit detection will use page-specific breakpoints
    // once AnalyzerManager supports requestExecutionBreakpointInPage
}

// ==================== IWD1793Observer interface ====================

void TRDOSAnalyzer::onFDCCommand(uint8_t command, const WD1793& fdc)
{
    // Layer 1: Capture raw FDC event with full context
    Z80* cpu = (_context && _context->pCore) ? _context->pCore->GetZ80() : nullptr;
    if (cpu)
    {
        captureRawFDCEvent(fdc, cpu);
    }
    
    // Layer 2: Semantic processing
    if (_state == TRDOSAnalyzerState::IDLE)
    {
        // Relax restriction: Monitor FDC even in IDLE to catch custom loaders.
        // But check PC to distinguish "Implicit Standard" from "Custom".
        
        uint16_t pc = 0;
        if (_context && _context->pCore && _context->pCore->GetZ80())
        {
            pc = _context->pCore->GetZ80()->pc;
        }

        bool inStandardRom = (pc >= 0x3D00 && pc <= 0x3FFF);

        if (inStandardRom)
        {
            // We missed the entry point (e.g. quick FDC poll loop inside ROM),
            // but we are definitely running standard TR-DOS code.
            _state = TRDOSAnalyzerState::IN_TRDOS;
            
            // Optional: Emit implicit entry? No, let's keep it clean.
        }
        else
        {
            // We are executing FDC commands from RAM (or non-TR-DOS ROM).
            // This is a custom loader.
            _state = TRDOSAnalyzerState::IN_CUSTOM;
            
            TRDOSEvent entryEvent{};
            entryEvent.timestamp = _context && _context->pCore && _context->pCore->GetZ80() ? _context->pCore->GetZ80()->tt : 0;
            entryEvent.type = TRDOSEventType::LOADER_DETECTED;
            entryEvent.context.pc = pc;
            
            if (_context && _context->pCore && _context->pCore->GetZ80())
            {
                entryEvent.context.iff1 = _context->pCore->GetZ80()->iff1;
                entryEvent.context.im = _context->pCore->GetZ80()->im;
            }
            
            emitEvent(std::move(entryEvent));
        }
    }
    
    uint64_t now = 0;
    if (_context && _context->pCore && _context->pCore->GetZ80())
    {
        now = _context->pCore->GetZ80()->tt;
    }

    
    // Note: COMMAND_START events are generated by handleCommandDispatch() 
    // when breakpoint at 0x3D13 is hit - it has proper command byte extraction.
    // FDC observer only tracks state for FDC-level events, not command semantics.

    TRDOSEvent event{};
    event.timestamp = now;
    event.frameNumber = _context ? _context->emulatorState.frame_counter : 0;
    
    // Capture CPU context
    if (_context && _context->pCore && _context->pCore->GetZ80())
    {
        event.context.pc = _context->pCore->GetZ80()->pc;
        event.context.iff1 = _context->pCore->GetZ80()->iff1;
        event.context.im = _context->pCore->GetZ80()->im;
    }
    event.track = fdc.getTrackRegister();
    event.sector = fdc.getSectorRegister();
    event.fdcCommand = command;
    event.fdcStatus = fdc.getStatusRegister();
    
    // Decode command type
    uint8_t cmdType = command & 0xF0;
    switch (cmdType)
    {
        case 0x00: // Restore
            event.type = TRDOSEventType::FDC_CMD_RESTORE;
            break;
        case 0x10: // Seek
            event.type = TRDOSEventType::FDC_CMD_SEEK;
            break;
        case 0x20: case 0x30: // Step
        case 0x40: case 0x50: // Step In
        case 0x60: case 0x70: // Step Out
            event.type = TRDOSEventType::FDC_CMD_STEP;
            break;
        case 0x80: case 0x90: // Read Sector
            event.type = TRDOSEventType::FDC_CMD_READ;
            _state = TRDOSAnalyzerState::IN_SECTOR_OP;
            break;
        case 0xA0: case 0xB0: // Write Sector
            event.type = TRDOSEventType::FDC_CMD_WRITE;
            _state = TRDOSAnalyzerState::IN_SECTOR_OP;
            break;
        case 0xC0: // Read Address
            event.type = TRDOSEventType::FDC_CMD_READ_ADDR;
            break;
        case 0xE0: // Read Track
            event.type = TRDOSEventType::FDC_CMD_READ_TRACK;
            break;
        case 0xF0: // Write Track
            event.type = TRDOSEventType::FDC_CMD_WRITE_TRACK;
            break;
        default:
            return; // Force interrupt or unknown
    }
    
    emitEvent(std::move(event));
}

void TRDOSAnalyzer::onFDCPortAccess(uint8_t port, uint8_t value, bool isWrite, const WD1793& fdc)
{
    (void)port; (void)value; (void)isWrite; (void)fdc;
    // Could track data register accesses for byte-level analysis
    // For now, we rely on command-level events
}

void TRDOSAnalyzer::onFDCCommandComplete(uint8_t status, const WD1793& fdc)
{
    if (_state == TRDOSAnalyzerState::IDLE)
    {
        return;
    }
    
    uint64_t now = 0;
    if (_context && _context->pCore && _context->pCore->GetZ80())
    {
        now = _context->pCore->GetZ80()->tt;
    }

    
    // Check for errors
    if (status & 0x08) // CRC Error
    {
        TRDOSEvent errEvent{};
        errEvent.timestamp = now;
        errEvent.type = TRDOSEventType::ERROR_CRC;
        errEvent.fdcStatus = status;
        emitEvent(std::move(errEvent));
    }
    
    if (status & 0x10) // Record Not Found
    {
        TRDOSEvent errEvent{};
        errEvent.timestamp = now;
        errEvent.type = TRDOSEventType::ERROR_RNF;
        errEvent.fdcStatus = status;
        emitEvent(std::move(errEvent));
    }
    
    // Emit sector transfer event if we were in a sector op
    if (_state == TRDOSAnalyzerState::IN_SECTOR_OP)
    {
        TRDOSEvent event{};
        event.timestamp = now;
        event.frameNumber = _context ? _context->emulatorState.frame_counter : 0;
        
        if (_context && _context->pCore && _context->pCore->GetZ80())
        {
            event.context.pc = _context->pCore->GetZ80()->pc;
        }
        event.type = TRDOSEventType::SECTOR_TRANSFER;
        event.track = fdc.getTrackRegister();
        event.sector = fdc.getSectorRegister();
        event.fdcStatus = status;
        event.bytesTransferred = 256; // Standard TR-DOS sector size
        
        emitEvent(std::move(event));
        
        _state = TRDOSAnalyzerState::IN_COMMAND;
    }
}

// ==================== Query API ====================

std::vector<TRDOSEvent> TRDOSAnalyzer::getEvents() const
{
    return _events.getAll();
}

std::vector<TRDOSEvent> TRDOSAnalyzer::getEventsSince(uint64_t timestamp) const
{
    return _events.getSince(timestamp);
}

std::vector<TRDOSEvent> TRDOSAnalyzer::getNewEvents()
{
    auto events = _events.getSince(_lastQueryTime);
    if (!events.empty())
    {
        _lastQueryTime = events.back().timestamp + 1;
    }
    return events;
}

size_t TRDOSAnalyzer::getEventCount() const
{
    return _events.size();
}

void TRDOSAnalyzer::clear()
{
    _events.clear();
    _rawFdcEvents.clear();
    _rawBreakpointEvents.clear();
    _rawBuffersOverflow = false;
    _lastQueryTime = 0;
}

// ==================== Raw Event Query API ====================

std::vector<RawFDCEvent> TRDOSAnalyzer::getRawFDCEvents() const
{
    return _rawFdcEvents.getAll();
}

std::vector<RawFDCEvent> TRDOSAnalyzer::getRawFDCEventsSince(uint64_t timestamp) const
{
    return _rawFdcEvents.getSince(timestamp);
}

std::vector<RawBreakpointEvent> TRDOSAnalyzer::getRawBreakpointEvents() const
{
    return _rawBreakpointEvents.getAll();
}

std::vector<RawBreakpointEvent> TRDOSAnalyzer::getRawBreakpointEventsSince(uint64_t timestamp) const
{
    return _rawBreakpointEvents.getSince(timestamp);
}

// ==================== Private methods ====================

void TRDOSAnalyzer::emitEvent(TRDOSEvent&& event)
{
    _lastEventTime = event.timestamp;
    _events.push(std::move(event));
}

void TRDOSAnalyzer::handleTRDOSEntry(Z80* cpu)
{
    if (_state != TRDOSAnalyzerState::IDLE)
    {
        return; // Already in TR-DOS
    }
    
    _state = TRDOSAnalyzerState::IN_TRDOS;
    
    uint64_t now = cpu ? cpu->tt : 0;
    
    TRDOSEvent event{};
    event.timestamp = now;
    event.frameNumber = _context ? _context->emulatorState.frame_counter : 0;
    event.type = TRDOSEventType::TRDOS_ENTRY;
    event.type = TRDOSEventType::TRDOS_ENTRY;
    event.context.pc = cpu ? cpu->pc : 0;
    if (cpu)
    {
        event.context.iff1 = cpu->iff1;
        event.context.im = cpu->im;
    }
    
    // Get caller address from stack
    event.context.callerAddress = 0;
    if (cpu && _context && _context->pMemory)
    {
        uint16_t sp = cpu->sp;
        uint8_t low = _context->pMemory->DirectReadFromZ80Memory(sp);
        uint8_t high = _context->pMemory->DirectReadFromZ80Memory(sp + 1);
        event.context.callerAddress = (high << 8) | low;
    }
    
    _commandStartTime = now;
    emitEvent(std::move(event));
    
    /* 
       Also implied entry if we just started monitoring? 
       Actually handleTRDOSEntry is called on breakpoint, so it's explicit.
    */
}

void TRDOSAnalyzer::handleCommandDispatch(uint16_t address, Z80* cpu)
{
    // Auto-transition: If we hit command dispatch from IDLE, we're entering TR-DOS
    if (_state == TRDOSAnalyzerState::IDLE)
    {
        _state = TRDOSAnalyzerState::IN_TRDOS;
        
        // Emit an implied entry event so usage is consistent
        TRDOSEvent entryEvent{};
        entryEvent.timestamp = cpu ? cpu->tt : 0;
        entryEvent.frameNumber = _context ? _context->emulatorState.frame_counter : 0;
        entryEvent.type = TRDOSEventType::TRDOS_ENTRY;
        entryEvent.context.pc = cpu ? cpu->pc : 0;
        entryEvent.flags = 0x01; // Flag as "implied" entry
        emitEvent(std::move(entryEvent));
    }

    // Only emit COMMAND_START when at BP_SERVICE_ENTRY (0x3D13)
    // At 0x3D13, the C register contains the TR-DOS service/command number
    // At 0x3D21 (BP_COMMAND_DISPATCH), C register may not contain valid command
    if (address != BP_SERVICE_ENTRY)
    {
        return;
    }

    // If in IN_COMMAND state, this is an internal service call - don't emit new COMMAND_START
    if (_state == TRDOSAnalyzerState::IN_COMMAND)
    {
        // Internal service call during command execution - skip semantic event
        return;
    }
    
    // Must be IN_TRDOS state now
    if (_state != TRDOSAnalyzerState::IN_TRDOS)
    {
        return;
    }
    
    _state = TRDOSAnalyzerState::IN_COMMAND;
    _currentCommand = identifyCommand(BP_SERVICE_ENTRY, cpu);
    
    uint64_t now = cpu ? cpu->tt : 0;
    
    TRDOSEvent event{};
    event.timestamp = now;
    event.frameNumber = _context ? _context->emulatorState.frame_counter : 0;
    event.type = TRDOSEventType::COMMAND_START;
    event.command = _currentCommand;
    
    // Try to read filename from TR-DOS system area ($5CDD = filename, 8 chars)
    event.filename = readFilenameFromMemory(0x5CDD);
    
    emitEvent(std::move(event));
}

void TRDOSAnalyzer::handleTRDOSExit(Z80* cpu)
{
    if (_state == TRDOSAnalyzerState::IDLE)
    {
        return;
    }
    
    uint64_t now = cpu ? cpu->tt : 0;
    
    TRDOSEvent event{};
    event.timestamp = now;
    event.type = TRDOSEventType::TRDOS_EXIT;
    
    emitEvent(std::move(event));
    
    _state = TRDOSAnalyzerState::IDLE;
    _currentCommand = TRDOSCommand::UNKNOWN;
}

TRDOSCommand TRDOSAnalyzer::identifyCommand(uint16_t address, Z80* cpu)
{
    (void)address;  // Unused for now
    if (!cpu)
    {
        return TRDOSCommand::UNKNOWN;
    }
    
    // TR-DOS 0x3D13 service entry: service number passed in C register
    // This is the standard way TR-DOS receives commands from BASIC
    uint8_t cmdByte = cpu->c;
    
    // Map TR-DOS command bytes to TRDOSCommand enum
    switch (cmdByte)
    {
        case 0x01: return TRDOSCommand::LOAD;
        case 0x02: return TRDOSCommand::SAVE;
        case 0x03: return TRDOSCommand::VERIFY;
        case 0x04: return TRDOSCommand::MERGE;
        case 0x05: return TRDOSCommand::RUN;
        case 0x06: return TRDOSCommand::ERASE;
        case 0x07: return TRDOSCommand::MOVE;
        case 0x0F: return TRDOSCommand::CAT;
        case 0x09: return TRDOSCommand::COPY;
        case 0x0A: return TRDOSCommand::FORMAT;
        default:  return TRDOSCommand::UNKNOWN;  // Unmapped service
    }
}

std::string TRDOSAnalyzer::readFilenameFromMemory(uint16_t address)
{
    if (!_context || !_context->pMemory)
    {
        return "";
    }
    
    std::string filename;
    for (int i = 0; i < 8; ++i)
    {
        uint8_t ch = _context->pMemory->DirectReadFromZ80Memory(address + i);
        if (ch == 0 || ch == ' ') // Stop at null or space padding? TR-DOS pads with spaces usually.
        {
             if (ch == 0) break;
        }
        filename += static_cast<char>(ch);
    }
    
    // Trim trailing spaces
    while (!filename.empty() && filename.back() == ' ')
    {
        filename.pop_back();
    }
    
    return filename;
}

void TRDOSAnalyzer::captureRawFDCEvent(const WD1793& fdc, Z80* cpu)
{
    // Overflow protection: stop capturing if buffer is full
    if (_rawFdcEvents.isFull())
    {
        _rawBuffersOverflow = true;
        return;
    }
    
    RawFDCEvent event{};
    event.tstate = cpu ? cpu->tt : 0;
    event.timestamp = event.tstate;  // Keep in sync for RingBuffer
    event.frameNumber = _context ? _context->emulatorState.frame_counter : 0;
    
    // FDC state - use actual method names from WD1793
    event.commandReg = 0;  // TODO: WD1793 doesn't expose command register directly
    event.statusReg = fdc.getStatusRegister();
    event.trackReg = fdc.getTrackRegister();
    event.sectorReg = fdc.getSectorRegister();
    event.dataReg = fdc.getDataRegister();
    event.systemReg = 0;  // TODO: System register (port 0xFF) not in WD1793 interface
    
    // Z80 context
    if (cpu)
    {
        event.pc = cpu->pc;
        event.sp = cpu->sp;
        event.a = cpu->a;
        event.f = cpu->f;
        event.b = cpu->b;
        event.c = cpu->c;
        event.d = cpu->d;
        event.e = cpu->e;
        event.h = cpu->h;
        event.l = cpu->l;
        event.iff1 = cpu->iff1;
        event.iff2 = cpu->iff2;
        event.im = cpu->im;
        
        // Capture stack snapshot (16 bytes = 8 return addresses)
        if (_context && _context->pMemory)
        {
            for (int i = 0; i < 16; ++i)
            {
                event.stack[i] = _context->pMemory->DirectReadFromZ80Memory(cpu->sp + i);
            }
        }
    }
    
    _rawFdcEvents.push(std::move(event));
}

void TRDOSAnalyzer::captureRawBreakpointEvent(uint16_t address, Z80* cpu)
{
    // Overflow protection: stop capturing if buffer is full
    if (_rawBreakpointEvents.isFull())
    {
        _rawBuffersOverflow = true;
        return;
    }
    
    RawBreakpointEvent event{};
    event.tstate = cpu ? cpu->tt : 0;
    event.timestamp = event.tstate;  // Keep in sync for RingBuffer
    event.frameNumber = _context ? _context->emulatorState.frame_counter : 0;
    event.address = address;
    
    if (cpu)
    {
        event.pc = cpu->pc;
        event.sp = cpu->sp;
        event.af = (cpu->a << 8) | cpu->f;
        event.bc = (cpu->b << 8) | cpu->c;
        event.de = (cpu->d << 8) | cpu->e;
        event.hl = (cpu->h << 8) | cpu->l;
        event.af_ = (cpu->alt.a << 8) | cpu->alt.f;
        event.bc_ = (cpu->alt.b << 8) | cpu->alt.c;
        event.de_ = (cpu->alt.d << 8) | cpu->alt.e;
        event.hl_ = (cpu->alt.h << 8) | cpu->alt.l;
        event.ix = cpu->ix;
        event.iy = cpu->iy;
        event.i = cpu->i;
        event.r = (cpu->r_hi << 8) | cpu->r_low;  // R register is split
        event.iff1 = cpu->iff1;
        event.iff2 = cpu->iff2;
        event.im = cpu->im;
        
        // Capture stack snapshot
        if (_context && _context->pMemory)
        {
            for (int i = 0; i < 16; ++i)
            {
                event.stack[i] = _context->pMemory->DirectReadFromZ80Memory(cpu->sp + i);
            }
        }
    }
    
    _rawBreakpointEvents.push(std::move(event));
}
