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
            ss << "TR-DOS Entry (PC=$" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << pc << ")";
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
        case TRDOSEventType::ERROR_CRC:
            ss << "*** ERROR: CRC Error ***";
            break;
        case TRDOSEventType::ERROR_RNF:
            ss << "*** ERROR: Record Not Found ***";
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
        manager->requestExecutionBreakpointInPage(BP_TRDOS_ENTRY, dosRomPage, BANK_ROM, _registrationId);
        manager->requestExecutionBreakpointInPage(BP_COMMAND_DISPATCH, dosRomPage, BANK_ROM, _registrationId);
        manager->requestExecutionBreakpointInPage(BP_EXIT, dosRomPage, BANK_ROM, _registrationId);
    }
    else
    {
        // Fallback: use regular breakpoints (will trigger in any ROM - less accurate)
        manager->requestExecutionBreakpoint(BP_TRDOS_ENTRY, _registrationId);
        manager->requestExecutionBreakpoint(BP_COMMAND_DISPATCH, _registrationId);
        manager->requestExecutionBreakpoint(BP_EXIT, _registrationId);
    }
    
    // Reset state
    _state = TRDOSAnalyzerState::IDLE;
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
    _state = TRDOSAnalyzerState::IDLE;
}

void TRDOSAnalyzer::onBreakpointHit(uint16_t address, Z80* cpu)
{
    switch (address)
    {
        case BP_TRDOS_ENTRY:
            handleTRDOSEntry(cpu);
            break;
        case BP_COMMAND_DISPATCH:
            handleCommandDispatch(cpu);
            break;
        // BP_EXIT is not used - exit detected in onFrameEnd
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
    if (_state == TRDOSAnalyzerState::IDLE)
    {
        return; // Only capture when in TR-DOS
    }
    
    uint64_t now = 0; // TODO: Get timing from FDC once accessor is available
    
    TRDOSEvent event{};
    event.timestamp = now;
    event.frameNumber = 0; // Frame counter not readily available
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
    
    uint64_t now = 0; // TODO: Get timing from FDC once accessor is available
    
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
    _lastQueryTime = 0;
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
    event.frameNumber = 0;
    event.type = TRDOSEventType::TRDOS_ENTRY;
    event.pc = cpu ? cpu->pc : 0;
    
    // Get caller address from stack
    if (cpu)
    {
        uint16_t sp = cpu->sp;
        (void)sp; // TODO: Read return address from stack
    }
    
    _commandStartTime = now;
    emitEvent(std::move(event));
}

void TRDOSAnalyzer::handleCommandDispatch(Z80* cpu)
{
    if (_state != TRDOSAnalyzerState::IN_TRDOS)
    {
        return;
    }
    
    _state = TRDOSAnalyzerState::IN_COMMAND;
    _currentCommand = identifyCommand(BP_COMMAND_DISPATCH, cpu);
    
    uint64_t now = cpu ? cpu->tt : 0;
    
    TRDOSEvent event{};
    event.timestamp = now;
    event.frameNumber = 0;
    event.type = TRDOSEventType::COMMAND_START;
    event.command = _currentCommand;
    
    // Try to read filename from TR-DOS system area
    event.filename = readFilenameFromMemory(0x5CD1);
    
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
    (void)address; (void)cpu;
    
    // TODO: Read command type from TR-DOS system variables or analyze call stack
    // For now, return UNKNOWN - will be enhanced later
    
    return TRDOSCommand::UNKNOWN;
}

std::string TRDOSAnalyzer::readFilenameFromMemory(uint16_t address)
{
    (void)address;
    
    // TODO: Read filename from memory
    // TR-DOS stores filename at $5CD1 (8 chars)
    
    return "";
}
