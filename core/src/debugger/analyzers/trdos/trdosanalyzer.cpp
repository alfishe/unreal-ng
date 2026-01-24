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
    
    // Frame-based timing is more readable than raw tstates
    ss << "[Frame " << frameNumber << "] ";
    
    switch (type)
    {
        case TRDOSEventType::TRDOS_ENTRY:
            ss << "TR-DOS Entered";
            break;
            
        case TRDOSEventType::TRDOS_EXIT:
            ss << "TR-DOS Exit";
            break;
            
        case TRDOSEventType::COMMAND_START:
            // User command (typed at A> prompt or from BASIC)
            if (userCommand != TRDOSUserCommand::UNKNOWN)
            {
                switch (userCommand)
                {
                    case TRDOSUserCommand::CAT: 
                        ss << "CAT - List directory"; 
                        break;
                    case TRDOSUserCommand::SAVE: 
                        ss << "SAVE \"" << filename << "\""; 
                        break;
                    case TRDOSUserCommand::LOAD_RUN_MERGE: 
                        ss << "LOAD/RUN \"" << filename << "\""; 
                        break;
                    case TRDOSUserCommand::FORMAT: 
                        ss << "FORMAT disk"; 
                        break;
                    case TRDOSUserCommand::MOVE: 
                        ss << "RENAME \"" << filename << "\""; 
                        break;
                    case TRDOSUserCommand::ERASE: 
                        ss << "ERASE \"" << filename << "\""; 
                        break;
                    case TRDOSUserCommand::COPY: 
                        ss << "COPY \"" << filename << "\""; 
                        break;
                    case TRDOSUserCommand::MERGE: 
                        ss << "MERGE \"" << filename << "\""; 
                        break;
                    case TRDOSUserCommand::VERIFY: 
                        ss << "VERIFY \"" << filename << "\""; 
                        break;
                    default: 
                        ss << "Command (token 0x" << std::hex << static_cast<int>(userCommand) << std::dec << ")"; 
                        break;
                }
            }
            else if (service != TRDOSService::UNKNOWN)
            {
                // Low-level API service call
                switch (service)
                {
                    case TRDOSService::RESTORE: 
                        ss << "Drive: Head to Track 0"; 
                        break;
                    case TRDOSService::SELECT_DRIVE: 
                        ss << "Drive: Select"; 
                        break;
                    case TRDOSService::SEEK_TRACK: 
                        ss << "Drive: Seek to Track " << (int)track; 
                        break;
                    case TRDOSService::READ_SECTORS: 
                    case TRDOSService::LOAD_FILE:
                        if (!filename.empty())
                            ss << "Loading \"" << filename << "\"";
                        else
                            ss << "Read Sector T" << (int)track << "/S" << (int)sector;
                        break;
                    case TRDOSService::WRITE_SECTORS: 
                    case TRDOSService::SAVE_FILE:
                        if (!filename.empty())
                            ss << "Saving \"" << filename << "\"";
                        else
                            ss << "Write Sector T" << (int)track << "/S" << (int)sector;
                        break;
                    case TRDOSService::CATALOG: 
                        ss << "Reading Catalog"; 
                        break;
                    case TRDOSService::FIND_FILE: 
                        ss << "Finding \"" << filename << "\""; 
                        break;
                    case TRDOSService::FORMAT_TRACK: 
                        ss << "Formatting Track " << (int)track; 
                        break;
                    default: 
                        ss << "API Call (service=" << static_cast<int>(service) << ")"; 
                        break;
                }
            }
            else
            {
                ss << "Command Started";
            }
            break;
            
        case TRDOSEventType::FDC_CMD_READ:
            ss << "Read Sector T" << (int)track << "/S" << (int)sector;
            break;
            
        case TRDOSEventType::FDC_CMD_WRITE:
            ss << "Write Sector T" << (int)track << "/S" << (int)sector;
            break;
            
        case TRDOSEventType::FDC_CMD_SEEK:
            ss << "Seek to Track " << (int)track;
            break;
            
        case TRDOSEventType::FDC_CMD_RESTORE:
            ss << "Head to Track 0";
            break;
            
        case TRDOSEventType::SECTOR_TRANSFER:
            ss << "Transferred " << bytesTransferred << " bytes (T" << (int)track << "/S" << (int)sector << ")";
            break;
            
        case TRDOSEventType::FDC_CMD_READ_ADDR:
            ss << "Read Address Mark";
            break;
            
        case TRDOSEventType::FDC_CMD_READ_TRACK:
            ss << "Read Track " << (int)track;
            break;
            
        case TRDOSEventType::FDC_CMD_WRITE_TRACK:
            ss << "Format Track " << (int)track;
            break;
            
        case TRDOSEventType::FDC_CMD_STEP:
            ss << "Step Head";
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
        Memory& memory = *_context->pMemory;
        uint8_t dosRomPage = static_cast<uint8_t>(memory.GetROMPageFromAddress(memory.base_dos_rom));
        
        // Use page-specific breakpoints that only fire when TR-DOS ROM is mapped
        // These addresses are in the TR-DOS ROM address space
        manager->requestExecutionBreakpointInPage(BP_TRDOS_ENTRY, dosRomPage, BANK_ROM, _registrationId);    // $3D00
        manager->requestExecutionBreakpointInPage(BP_INIT_VARS, dosRomPage, BANK_ROM, _registrationId);      // $3DCE
        manager->requestExecutionBreakpointInPage(BP_SERVICE_ENTRY, dosRomPage, BANK_ROM, _registrationId);  // $3D13
        manager->requestExecutionBreakpointInPage(BP_COMMAND_ENTRY, dosRomPage, BANK_ROM, _registrationId);  // $3D1A - User command entry
        manager->requestExecutionBreakpointInPage(BP_EXIT, dosRomPage, BANK_ROM, _registrationId);           // $0077
        
        // Internal command dispatcher is in lower ROM range - also page-specific
        manager->requestExecutionBreakpointInPage(BP_CMD_DISPATCHER, dosRomPage, BANK_ROM, _registrationId); // $030A
    }
    else
    {
        // Fallback: use regular breakpoints (will trigger in any ROM - less accurate)
        manager->requestExecutionBreakpoint(BP_TRDOS_ENTRY, _registrationId);
        manager->requestExecutionBreakpoint(BP_SERVICE_ENTRY, _registrationId);
        manager->requestExecutionBreakpoint(BP_INIT_VARS, _registrationId);
        manager->requestExecutionBreakpoint(BP_COMMAND_ENTRY, _registrationId);  // User command entry
        manager->requestExecutionBreakpoint(BP_CMD_DISPATCHER, _registrationId); // Internal command dispatcher (A = token)
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
    
    _currentService = TRDOSService::UNKNOWN;
    _currentUserCommand = TRDOSUserCommand::UNKNOWN;
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
    _currentService = TRDOSService::UNKNOWN;
    _currentUserCommand = TRDOSUserCommand::UNKNOWN;
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
            
        case BP_COMMAND_ENTRY:
            // User command entry point ($3D1A) - read token from CH_ADD
            handleUserCommandEntry(cpu);
            break;
            
        case BP_SERVICE_ENTRY:
            // Low-level disk API ($3D13) - C register contains service code
            handleServiceCall(address, cpu);
            break;
            
        case BP_INIT_VARS:
            // Init system variables - just note we're in TR-DOS
            if (_state == TRDOSAnalyzerState::IDLE)
            {
                _state = TRDOSAnalyzerState::IN_TRDOS;
            }
            break;
            
        case BP_CMD_DISPATCHER:
            // Internal command dispatcher ($030A) - A register contains BASIC token
            // This fires when TR-DOS is already active at A> prompt
            handleInternalCommandDispatch(cpu);
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
                _currentService = TRDOSService::UNKNOWN;
                _currentUserCommand = TRDOSUserCommand::UNKNOWN;
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

    
    // Note: COMMAND_START events are generated by handleServiceCall() 
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

void TRDOSAnalyzer::handleServiceCall(uint16_t address, Z80* cpu)
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
    // At 0x3D13, the C register contains the TR-DOS SERVICE code (not user command!)
    if (address != BP_SERVICE_ENTRY)
    {
        return;
    }

    // Don't emit for internal service calls during command execution
    // (game loaders call READ_SECTORS many times during a single LOAD operation)
    if (_state == TRDOSAnalyzerState::IN_COMMAND)
    {
        // Just log the service call without emitting a new COMMAND_START
        return;
    }
    
    // Must be IN_TRDOS state now
    if (_state != TRDOSAnalyzerState::IN_TRDOS)
    {
        return;
    }
    
    _state = TRDOSAnalyzerState::IN_COMMAND;
    _currentService = identifyService(cpu);
    
    uint64_t now = cpu ? cpu->tt : 0;
    
    TRDOSEvent event{};
    event.timestamp = now;
    event.frameNumber = _context ? _context->emulatorState.frame_counter : 0;
    event.type = TRDOSEventType::COMMAND_START;
    event.service = _currentService;
    event.userCommand = _currentUserCommand;  // Set from previous BP_COMMAND_ENTRY hit
    
    // Read filename + extension from TR-DOS system variables ($5CDD + $5CE5)
    event.filename = readTRDOSFilename();
    
    emitEvent(std::move(event));
}

/// Handle user command entry at $3D1A - read BASIC token from CH_ADD
void TRDOSAnalyzer::handleUserCommandEntry(Z80* cpu)
{
    if (!_context || !_context->pMemory)
    {
        return;
    }
    
    // Ensure we're in TR-DOS
    if (_state == TRDOSAnalyzerState::IDLE)
    {
        _state = TRDOSAnalyzerState::IN_TRDOS;
    }
    
    // Read user command from CH_ADD (pointer at $5C5D)
    _currentUserCommand = identifyUserCommand();
    
    // Emit USER_COMMAND event with the BASIC token
    TRDOSEvent event{};
    event.timestamp = cpu ? cpu->tt : 0;
    event.frameNumber = _context ? _context->emulatorState.frame_counter : 0;
    event.type = TRDOSEventType::COMMAND_START;
    event.userCommand = _currentUserCommand;
    event.service = TRDOSService::UNKNOWN;  // No service call yet
    event.flags = 0x10;  // Flag as user command (not service call)
    
    // Read filename + extension from TR-DOS system variables ($5CDD + $5CE5)
    event.filename = readTRDOSFilename();
    
    emitEvent(std::move(event));
}

/// Handle internal command dispatcher at $030A - read token from memory at (HL)
/// This fires when TR-DOS is already active at the A> prompt (resident loop)
/// Note: At $030A, the instruction is "LD A,(HL)" which hasn't executed yet,
///       so we read the token from memory at (HL), not from A register.
void TRDOSAnalyzer::handleInternalCommandDispatch(Z80* cpu)
{
    if (!cpu || !_context || !_context->pMemory)
    {
        return;
    }
    
    // We're definitely in TR-DOS if we hit this
    _state = TRDOSAnalyzerState::IN_TRDOS;
    
    // At $030A, the instruction is "LD A,(HL)" - breakpoint fires BEFORE execution
    // So we need to read the token from memory at address (HL), not from A register
    uint16_t commandStrAddr = cpu->hl;
    uint8_t token = _context->pMemory->DirectReadFromZ80Memory(commandStrAddr);
    
    // Map token to user command
    switch (token)
    {
        case 0xCF: _currentUserCommand = TRDOSUserCommand::CAT; break;
        case 0xF8: _currentUserCommand = TRDOSUserCommand::SAVE; break;
        case 0xEF: _currentUserCommand = TRDOSUserCommand::LOAD_RUN_MERGE; break;  // Needs disambiguation
        case 0xD0: _currentUserCommand = TRDOSUserCommand::FORMAT; break;
        case 0xD1: _currentUserCommand = TRDOSUserCommand::MOVE; break;
        case 0xD2: _currentUserCommand = TRDOSUserCommand::ERASE; break;
        case 0xFF: _currentUserCommand = TRDOSUserCommand::COPY; break;
        case 0xD5: _currentUserCommand = TRDOSUserCommand::MERGE; break;
        case 0xD6: _currentUserCommand = TRDOSUserCommand::VERIFY; break;
        default:   _currentUserCommand = TRDOSUserCommand::UNKNOWN; break;
    }
    
    // Emit command event
    TRDOSEvent event{};
    event.timestamp = cpu->tt;
    event.frameNumber = _context->emulatorState.frame_counter;
    event.type = TRDOSEventType::COMMAND_START;
    event.userCommand = _currentUserCommand;
    event.service = TRDOSService::UNKNOWN;  // User command, not API call
    event.flags = 0x20;  // Flag as internal dispatcher command
    
    // Capture context
    event.context.pc = cpu->pc;
    event.context.iff1 = cpu->iff1;
    event.context.im = cpu->im;
    
    // Only read filename for file-oriented commands
    // Based on TR-DOS ROM jump table analysis (x3008):
    // CAT, FORMAT, DRIVE(*) - don't use filename
    // SAVE, LOAD, RUN, ERASE, NEW, VERIFY, MERGE, COPY - use filename at $5CDD
    if (commandRequiresFilename(_currentUserCommand))
    {
        event.filename = readTRDOSFilename();
    }
    
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
    _currentService = TRDOSService::UNKNOWN;
    _currentUserCommand = TRDOSUserCommand::UNKNOWN;
}

/// Identify low-level SERVICE from C register at $3D13
TRDOSService TRDOSAnalyzer::identifyService(Z80* cpu)
{
    if (!cpu)
    {
        return TRDOSService::UNKNOWN;
    }
    
    // C register contains the SERVICE code (not user command!)
    uint8_t svcCode = cpu->c;
    
    // Map to TRDOSService enum (values match C register directly)
    switch (svcCode)
    {
        case 0x00: return TRDOSService::RESTORE;
        case 0x01: return TRDOSService::SELECT_DRIVE;
        case 0x02: return TRDOSService::SEEK_TRACK;
        case 0x03: return TRDOSService::SET_SECTOR;
        case 0x04: return TRDOSService::SET_DMA;
        case 0x05: return TRDOSService::READ_SECTORS;
        case 0x06: return TRDOSService::WRITE_SECTORS;
        case 0x07: return TRDOSService::CATALOG;
        case 0x08: return TRDOSService::READ_DESCRIPTOR;
        case 0x09: return TRDOSService::WRITE_DESCRIPTOR;
        case 0x0A: return TRDOSService::FIND_FILE;
        case 0x0B: return TRDOSService::SAVE_FILE;
        case 0x0C: return TRDOSService::SAVE_BASIC;
        case 0x0D: return TRDOSService::EXIT;
        case 0x0E: return TRDOSService::LOAD_FILE;
        case 0x12: return TRDOSService::DELETE_SECTOR;
        case 0x13: return TRDOSService::MOVE_DESC_IN;
        case 0x14: return TRDOSService::MOVE_DESC_OUT;
        case 0x15: return TRDOSService::FORMAT_TRACK;
        case 0x16: return TRDOSService::SELECT_SIDE_0;
        case 0x17: return TRDOSService::SELECT_SIDE_1;
        case 0x18: return TRDOSService::READ_SYS_SECTOR;
        default:   return TRDOSService::UNKNOWN;
    }
}

/// Identify USER COMMAND from BASIC token at CH_ADD address
TRDOSUserCommand TRDOSAnalyzer::identifyUserCommand()
{
    if (!_context || !_context->pMemory)
    {
        return TRDOSUserCommand::UNKNOWN;
    }
    
    Memory* memory = _context->pMemory;
    
    // Read CH_ADD pointer from $5C5D (2 bytes, little endian)
    uint16_t chAdd = memory->DirectReadFromZ80Memory(0x5C5D) |
                     (memory->DirectReadFromZ80Memory(0x5C5E) << 8);
    
    // Read the BASIC token at that address
    uint8_t token = memory->DirectReadFromZ80Memory(chAdd);
    
    // Map BASIC token to user command
    switch (token)
    {
        case 0xCF: return TRDOSUserCommand::CAT;
        case 0xF8: return TRDOSUserCommand::SAVE;
        case 0xEF: return TRDOSUserCommand::LOAD_RUN_MERGE;  // Needs further disambiguation
        case 0xD0: return TRDOSUserCommand::FORMAT;
        case 0xD1: return TRDOSUserCommand::MOVE;
        case 0xD2: return TRDOSUserCommand::ERASE;
        case 0xFF: return TRDOSUserCommand::COPY;
        case 0xD5: return TRDOSUserCommand::MERGE;
        case 0xD6: return TRDOSUserCommand::VERIFY;
        default:   return TRDOSUserCommand::UNKNOWN;
    }
}

/// Check if a user command requires filename extraction from $5CDD
/// Based on TR-DOS ROM jump table analysis (x3008)
bool TRDOSAnalyzer::commandRequiresFilename(TRDOSUserCommand cmd)
{
    switch (cmd)
    {
        // File-oriented commands - need filename from $5CDD
        case TRDOSUserCommand::SAVE:
        case TRDOSUserCommand::LOAD_RUN_MERGE:
        case TRDOSUserCommand::ERASE:
        case TRDOSUserCommand::MOVE:   // NEW/RENAME
        case TRDOSUserCommand::COPY:
        case TRDOSUserCommand::MERGE:
        case TRDOSUserCommand::VERIFY:
            return true;
            
        // Commands that don't use filename
        case TRDOSUserCommand::CAT:       // Directory listing
        case TRDOSUserCommand::FORMAT:    // Disk format (uses disk label, not file)
        case TRDOSUserCommand::UNKNOWN:
        default:
            return false;
    }
}

/// Read TR-DOS filename from standard system variables
/// Filename: $5CDD (8 bytes), Extension: $5CE5 (1 byte)
/// Returns empty string if data is garbage (not yet parsed)
std::string TRDOSAnalyzer::readTRDOSFilename()
{
    if (!_context || !_context->pMemory)
    {
        return "";
    }
    
    Memory* mem = _context->pMemory;
    
    // TR-DOS system variables for filename
    constexpr uint16_t TRDOS_FILENAME = 0x5CDD;  // 8 bytes
    constexpr uint16_t TRDOS_EXTENSION = 0x5CE5; // 1 byte
    
    // First byte check - detect garbage data
    uint8_t firstByte = mem->DirectReadFromZ80Memory(TRDOS_FILENAME);
    
    // Skip if:
    // - 0x00 = Empty/end of directory
    // - 0x01 = Deleted file marker
    // - < 0x20 = Control character (garbage)
    if (firstByte == 0x00 || firstByte == 0x01 || firstByte < 0x20)
    {
        return "";
    }
    
    // Read 8-byte filename
    std::string filename;
    for (int i = 0; i < 8; ++i)
    {
        uint8_t ch = mem->DirectReadFromZ80Memory(TRDOS_FILENAME + i);
        
        // Only accept printable ASCII (0x20-0x7E)
        if (ch >= 0x20 && ch <= 0x7E)
        {
            filename += static_cast<char>(ch);
        }
        else
        {
            // Non-printable = garbage, return empty
            return "";
        }
    }
    
    // Trim trailing spaces (TR-DOS pads with 0x20)
    while (!filename.empty() && filename.back() == ' ')
    {
        filename.pop_back();
    }
    
    // If filename is all spaces, return empty
    if (filename.empty())
    {
        return "";
    }
    
    // Read 1-byte extension from $5CE5
    uint8_t ext = mem->DirectReadFromZ80Memory(TRDOS_EXTENSION);
    if (ext >= 0x20 && ext <= 0x7E && ext != ' ')
    {
        filename += '.';
        filename += static_cast<char>(ext);
    }
    
    return filename;
}

/// Legacy function - kept for compatibility
std::string TRDOSAnalyzer::readFilenameFromMemory(uint16_t address)
{
    if (!_context || !_context->pMemory)
    {
        return "";
    }
    
    std::string filename;
    bool hasValidChars = false;
    
    for (int i = 0; i < 8; ++i)
    {
        uint8_t ch = _context->pMemory->DirectReadFromZ80Memory(address + i);
        
        // Stop at null terminator
        if (ch == 0)
        {
            break;
        }
        
        // Only accept printable ASCII characters (0x20-0x7E)
        if (ch >= 0x20 && ch <= 0x7E)
        {
            filename += static_cast<char>(ch);
            if (ch != ' ')
            {
                hasValidChars = true;
            }
        }
        else
        {
            return "";
        }
    }
    
    // Trim trailing spaces
    while (!filename.empty() && filename.back() == ' ')
    {
        filename.pop_back();
    }
    
    return hasValidChars ? filename : "";
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
    
    // Set address label based on known TR-DOS ROM entry points
    event.address_label = getAddressLabel(address);
    
    // Determine physical page info based on Z80 address
    // TR-DOS ROM is paged in at $0000-$3FFF when active
    if (address < 0x4000)
    {
        // Could be ROM (BASIC/TR-DOS) or RAM page
        // When in TR-DOS, this is TR-DOS ROM (page 4 typically)
        event.page_type = "ROM";
        event.page_index = 4;  // TR-DOS ROM
        event.page_offset = address;
    }
    else if (address < 0x8000)
    {
        event.page_type = "RAM";
        event.page_index = 5;  // Standard RAM 5 at $4000-$7FFF
        event.page_offset = address - 0x4000;
    }
    else if (address < 0xC000)
    {
        event.page_type = "RAM";
        event.page_index = 2;  // Standard RAM 2 at $8000-$BFFF
        event.page_offset = address - 0x8000;
    }
    else
    {
        event.page_type = "RAM";
        // Page 0 or banked - would need to read port $7FFD
        event.page_index = 0;
        event.page_offset = address - 0xC000;
    }
    
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

/// Get human-readable label for known TR-DOS ROM addresses
std::string TRDOSAnalyzer::getAddressLabel(uint16_t address)
{
    // TR-DOS ROM entry points and key addresses
    switch (address)
    {
        // Standard entry points
        case 0x3D00: return "TRDOS_ENTRY";
        case 0x3D03: return "CMD_PROCESSOR";
        case 0x3D13: return "SERVICE_ENTRY";
        case 0x3D1A: return "COMMAND_ENTRY";
        case 0x3D21: return "INIT_VARS";
        
        // Internal command processing
        case 0x030A: return "CMD_DISPATCHER";
        case 0x02CB: return "CMD_LOOP";
        case 0x02EF: return "CMD_TOKENIZED";
        
        // ROM trampoline / exit
        case 0x0077: return "ROM_TRAMPOLINE";
        
        // Command handlers (from x3008 jump table)
        case 0x0433: return "CAT_HANDLER";
        case 0x1018: return "DRIVE_HANDLER";
        case 0x1EC2: return "FORMAT_HANDLER";
        case 0x053A: return "NEW_HANDLER";
        case 0x0787: return "ERASE_HANDLER";
        case 0x1815: return "LOAD_HANDLER";
        case 0x1AD0: return "SAVE_HANDLER";
        case 0x19B1: return "MERGE_HANDLER";
        case 0x1D4D: return "RUN_HANDLER";
        case 0x1810: return "VERIFY_HANDLER";
        case 0x0690: return "COPY_HANDLER";
        
        // FDC routines
        case 0x3DC8: return "ACTIVATE_DRIVE";
        case 0x1C57: return "SETUP_FILENAME";
        
        default: return "";
    }
}
