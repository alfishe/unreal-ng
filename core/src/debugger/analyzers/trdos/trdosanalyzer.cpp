#include "trdosanalyzer.h"

#include "debugger/analyzers/analyzermanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/io/fdc/wd1793.h"
#include "common/uuid.h"

#include <sstream>
#include <iomanip>
#include <unordered_map>

/// Map of known TR-DOS ROM routines for O(1) address lookup (from trdos504t.asm analysis)
const std::unordered_map<uint16_t, TRDOSAnalyzer::ROMRoutineInfo> TRDOSAnalyzer::romRoutineMap =
{
    // ========== Standard entry points ==========
    {0x3D00, {"TRDOS_ENTRY",     "TR-DOS main entry point from BASIC"}},
    {0x3D03, {"CMD_PROCESSOR",   "Command processor entry from BASIC"}},
    {0x3D13, {"SERVICE_ENTRY",   "Low-level service API (C = service code)"}},
    {0x3D1A, {"COMMAND_ENTRY",   "User command entry (read token from CH_ADD)"}},
    {0x3D21, {"INIT_VARS",       "Initialize system variables"}},
    {0x3D2F, {"ROM_TRAMPOLINE",  "ROM switch trampoline (IX = target routine)"}},
    {0x0077, {"TRDOS_EXIT",      "Exit TR-DOS and return to BASIC"}},
    
    // ========== Internal command processing ==========
    {0x030A, {"CMD_DISPATCHER",  "Command token dispatcher (A = BASIC token)"}},
    {0x02CB, {"CMD_LOOP",        "Command processor loop entry"}},
    {0x02EF, {"CMD_TOKENIZED",   "After command tokenization"}},
    
    // ========== Command handlers (from $3008 jump table) ==========
    {0x0433, {"CAT_HANDLER",     "CAT/LIST command handler"}},
    {0x1018, {"DRIVE_HANDLER",   "Drive selection (A:, B:, C:, D:)"}},
    {0x1EC2, {"FORMAT_HANDLER",  "FORMAT command handler"}},
    {0x053A, {"NEW_HANDLER",     "NEW command handler"}},
    {0x0787, {"ERASE_HANDLER",   "ERASE command handler"}},
    {0x1815, {"LOAD_HANDLER",    "LOAD command handler"}},
    {0x1AD0, {"SAVE_HANDLER",    "SAVE command handler"}},
    {0x19B1, {"MERGE_HANDLER",   "MERGE command handler"}},
    {0x1D4D, {"RUN_HANDLER",     "RUN command handler"}},
    {0x1810, {"VERIFY_HANDLER",  "VERIFY command handler"}},
    {0x0690, {"COPY_HANDLER",    "COPY command handler"}},
    {0x027B, {"FORCE_BOOT",      "Force 'boot' filename injection"}},
    
    // ========== Drive control ==========
    {0x3D4C, {"INIT_DRIVE",      "Initialize drive A: and reset WD1793"}},
    {0x3D67, {"DRIVE_SELECT_A",  "Select drive A: for operation"}},
    {0x3DC8, {"ACTIVATE_DRIVE",  "Activate/select drive for operation"}},
    {0x2FC3, {"SEND_CMD",        "Send command to FDC (port $1F)"}},
    {0x2F90, {"BUFFER_COPY",     "Copy data between buffers"}},
    
    // ========== Multi-sector I/O ==========
    {0x1E75, {"SECTOR_LOOP",     "Sector read/write iterator loop"}},
    {0x1E36, {"READ_TRACK_IDX",  "Read track index area"}},
    {0x1E67, {"SETUP_IO",        "Setup sector I/O parameters"}},
    
    // ========== Data transfer routines ==========
    {0x3FE5, {"READ_BYTES",      "Read sector data bytes via DRQ polling"}},
    {0x3FCA, {"WRITE_BYTES",     "Write sector data bytes via DRQ polling"}},
    {0x3F0E, {"READ_SECT_CMD",   "Issue READ_SECTOR ($80) command"}},
    {0x3F0A, {"WRITE_SECT_CMD",  "Issue WRITE_SECTOR ($A0) command"}},
    {0x3F15, {"SECTOR_RETRY",    "Sector read/write with retry logic"}},
    {0x3F00, {"WRITE_SECTOR",    "Write single sector routine"}},
    
    // ========== Register setup ==========
    {0x3F06, {"SET_DMA_ADDR",    "Set transfer address to ($5D00)"}},
    {0x3F02, {"SET_SECTOR_REG",  "Set sector number to ($5CFF)"}},
    
    // ========== FDC status/control ==========
    {0x3F33, {"READ_STATUS",     "Read FDC status register (port $1F)"}},
    {0x3EF5, {"WAIT_DRQ",        "Wait for DRQ/INTRQ signal (port $FF)"}},
    {0x3E63, {"SEEK_TRACK",      "Seek head to track in A register"}},
    {0x3E44, {"SEEK_WAIT",       "Wait for SEEK command completion"}},
    {0x3EE7, {"HANDLE_ERROR",    "Handle disk error (TAPE LOADING ERROR)"}},
    
    // ========== ROM/Memory paging ==========
    {0x2A53, {"ROM_PAGE_OUT",    "Page out TR-DOS ROM (OUT (C),A)"}},
    {0x2A4B, {"SET_ROM_PAGE",    "Set ROM page number at $5C01"}},
    
    // ========== INTRQ wait loop ==========
    {0x3D9C, {"WAIT_INTRQ",      "Wait for FDC INTRQ with BREAK check"}},
    {0x3DA0, {"WAIT_INTRQ_CHK",  "INTRQ wait loop - checking BREAK key"}},
    {0x3DA5, {"CHK_BREAK_KEY",   "Check if BREAK key pressed during wait"}},
    {0x3D9A, {"SEND_CMD_WAIT",   "Send FDC command and wait for INTRQ"}},
    {0x3D98, {"RESTORE_WAIT",    "RESTORE command with INTRQ wait"}},
    {0x3D97, {"FDC_CMD_WAIT",    "Execute FDC command and wait for completion"}},
    {0x3DB2, {"FIND_INDEX",      "Wait for disk index pulse"}},
    
    // ========== Buffer/catalog operations ==========
    {0x2FD6, {"LOAD_SECTOR_BUF", "Load sector to buffer at $5D25"}},
    {0x2FC6, {"CHECK_CAT_END",   "Check end of catalog listing"}},
    {0x1D87, {"CAT_ENTRY_SKIP",  "Skip to next catalog entry position"}},
    {0x1D8F, {"CAT_PRINT_ENTRY", "Print catalog entry to screen"}},
    {0x1D9A, {"CAT_NEXT_ENTRY",  "Move to next catalog entry"}},
    {0x1DA2, {"CAT_FORMAT_LINE", "Format catalog line for display"}},
    {0x1DB1, {"CAT_PRINT_INFO",  "Print file info (size/type)"}},
    {0x1DB8, {"CAT_COUNT_FILES", "Count files in directory"}},
    {0x1DC0, {"CAT_GET_INFO",    "Get catalog information"}},
    {0x1C57, {"SETUP_FILENAME",  "Setup filename for file operations"}},
    
    // ========== Screen/text output ==========
    {0x1A00, {"PRINT_CHAR_A",    "Print character in A register"}},
    {0x1A3B, {"PRINT_STRING",    "Print null-terminated string"}},
    
    // ========== Sector I/O helpers ==========
    {0x1E22, {"SECTOR_SETUP",    "Setup sector parameters for I/O"}},
    {0x1E29, {"SECTOR_EXEC",     "Execute sector read/write operation"}},
    {0x1E31, {"TRACK_READ_HDR",  "Read track header/sector ID"}},
    {0x1E35, {"VERIFY_SECTOR",   "Verify sector read operation"}},
    
    // ========== Command input ==========
    {0x1D90, {"CMD_INPUT",       "Enter command at A> prompt"}},
    {0x1D96, {"CMD_INPUT_RET",   "Return from command input"}},
    {0x1D97, {"CLS_OPEN_STREAM", "Clear screen and open stream 0"}}
};

/// Map of BASIC/internal tokens to TR-DOS user commands - O(1) lookup
/// Note: Both BASIC tokens (0xCF=CAT) and internal A> prompt tokens (0x80=LIST) are supported
const std::unordered_map<uint8_t, TRDOSUserCommand> TRDOSAnalyzer::tokenToCommandMap =
{
    // BASIC tokens (when called from BASIC)
    {0xCF, TRDOSUserCommand::CAT},            // CAT
    {0xF8, TRDOSUserCommand::SAVE},           // SAVE
    {0xEF, TRDOSUserCommand::LOAD_RUN_MERGE}, // LOAD/RUN/MERGE (needs disambiguation)
    {0xD0, TRDOSUserCommand::FORMAT},         // FORMAT
    {0xD1, TRDOSUserCommand::MOVE},           // MOVE/RENAME
    {0xD2, TRDOSUserCommand::ERASE},          // ERASE
    {0xFF, TRDOSUserCommand::COPY},           // COPY
    {0xD5, TRDOSUserCommand::MERGE},          // MERGE
    {0xD6, TRDOSUserCommand::VERIFY},         // VERIFY
    
    // Internal tokens used at A> prompt
    {0x80, TRDOSUserCommand::CAT}             // LIST at A> prompt
};

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
                        ss << "API Call (service=0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(service) << std::dec << ")"; 
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
            
        case TRDOSEventType::COMMAND_COMPLETE:
            // Operation completed - show what was done
            if (!filename.empty())
            {
                int sectors = bytesTransferred / 256;
                if (bytesTransferred > 0)
                {
                    ss << "Loaded \"" << filename << "\" - " << sectors << " sector" << (sectors != 1 ? "s" : "");
                }
                else
                {
                    ss << "Operation \"" << filename << "\" completed";
                }
            }
            else if (bytesTransferred > 0)
            {
                int sectors = bytesTransferred / 256;
                ss << "Operation completed - " << sectors << " sector" << (sectors != 1 ? "s" : "s");
            }
            else
            {
                ss << "Operation completed";
            }
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
        manager->requestExecutionBreakpointInPage(BP_ROM_TRAMPOLINE, dosRomPage, BANK_ROM, _registrationId); // $3D2F - ROM switch trampoline
        manager->requestExecutionBreakpointInPage(BP_EXIT, dosRomPage, BANK_ROM, _registrationId);           // $0077
        
        // Internal command dispatcher is in lower ROM range - also page-specific
        manager->requestExecutionBreakpointInPage(BP_CMD_DISPATCHER, dosRomPage, BANK_ROM, _registrationId); // $030A
        
        // ALL command handler breakpoints for complete command detection
        manager->requestExecutionBreakpointInPage(BP_CAT_HANDLER, dosRomPage, BANK_ROM, _registrationId);    // $0433 - CAT/LIST
        manager->requestExecutionBreakpointInPage(BP_LOAD_HANDLER, dosRomPage, BANK_ROM, _registrationId);   // $1815 - LOAD
        manager->requestExecutionBreakpointInPage(BP_SAVE_HANDLER, dosRomPage, BANK_ROM, _registrationId);   // $1AD0 - SAVE
        manager->requestExecutionBreakpointInPage(BP_RUN_HANDLER, dosRomPage, BANK_ROM, _registrationId);    // $1D4D - RUN
        manager->requestExecutionBreakpointInPage(BP_ERASE_HANDLER, dosRomPage, BANK_ROM, _registrationId);  // $0787 - ERASE
        manager->requestExecutionBreakpointInPage(BP_COPY_HANDLER, dosRomPage, BANK_ROM, _registrationId);   // $0690 - COPY
        manager->requestExecutionBreakpointInPage(BP_FORMAT_HANDLER, dosRomPage, BANK_ROM, _registrationId); // $1EC2 - FORMAT
        manager->requestExecutionBreakpointInPage(BP_VERIFY_HANDLER, dosRomPage, BANK_ROM, _registrationId); // $1810 - VERIFY
        manager->requestExecutionBreakpointInPage(BP_MERGE_HANDLER, dosRomPage, BANK_ROM, _registrationId);  // $19B1 - MERGE
        manager->requestExecutionBreakpointInPage(BP_FORCE_BOOT, dosRomPage, BANK_ROM, _registrationId);     // $027B - Force "boot" injection
    }
    else
    {
        // Fallback: use regular breakpoints (will trigger in any ROM - less accurate)
        manager->requestExecutionBreakpoint(BP_TRDOS_ENTRY, _registrationId);
        manager->requestExecutionBreakpoint(BP_SERVICE_ENTRY, _registrationId);
        manager->requestExecutionBreakpoint(BP_INIT_VARS, _registrationId);
        manager->requestExecutionBreakpoint(BP_COMMAND_ENTRY, _registrationId);
        manager->requestExecutionBreakpoint(BP_CMD_DISPATCHER, _registrationId);
        manager->requestExecutionBreakpoint(BP_ROM_TRAMPOLINE, _registrationId);
        manager->requestExecutionBreakpoint(BP_EXIT, _registrationId);
        
        // All command handlers
        manager->requestExecutionBreakpoint(BP_CAT_HANDLER, _registrationId);
        manager->requestExecutionBreakpoint(BP_LOAD_HANDLER, _registrationId);
        manager->requestExecutionBreakpoint(BP_SAVE_HANDLER, _registrationId);
        manager->requestExecutionBreakpoint(BP_RUN_HANDLER, _registrationId);
        manager->requestExecutionBreakpoint(BP_ERASE_HANDLER, _registrationId);
        manager->requestExecutionBreakpoint(BP_COPY_HANDLER, _registrationId);
        manager->requestExecutionBreakpoint(BP_FORMAT_HANDLER, _registrationId);
        manager->requestExecutionBreakpoint(BP_VERIFY_HANDLER, _registrationId);
        manager->requestExecutionBreakpoint(BP_MERGE_HANDLER, _registrationId);
        manager->requestExecutionBreakpoint(BP_FORCE_BOOT, _registrationId);
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
    
    // Flush any pending raw events (e.g. polling loops)
    flushPendingRawEvents();
    
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
            
        case BP_ROM_TRAMPOLINE:  // Same as BP_DOS_ENTRY ($3D2F)
            // $3D2F trampoline - custom loader using internal ROM routines
            // IX register contains the target internal routine address
            // This is the universal entry point for custom loaders
            handleROMTrampoline(cpu);
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
            
        case BP_RUN_HANDLER:
            // $1D4D - RUN command handler entry (definitive RUN detection)
            _currentUserCommand = TRDOSUserCommand::LOAD_RUN_MERGE;
            if (_state == TRDOSAnalyzerState::IDLE)
            {
                _state = TRDOSAnalyzerState::IN_TRDOS;
            }
            
            // Emit COMMAND_START semantic event for RUN - but only once per operation
            // Guard: only emit if we don't already have an active operation with RUN command
            if (cpu && _context && !(_operation.active && _operation.userCommand == TRDOSUserCommand::LOAD_RUN_MERGE))
            {
                TRDOSEvent runEvent{};
                runEvent.timestamp = cpu->tt;
                runEvent.frameNumber = _context->emulatorState.frame_counter;
                runEvent.type = TRDOSEventType::COMMAND_START;
                runEvent.userCommand = TRDOSUserCommand::LOAD_RUN_MERGE;
                runEvent.filename = readTRDOSFilename();  // May be empty for "RUN" without param
                runEvent.context.pc = cpu->pc;
                emitEvent(std::move(runEvent));
                
                // Start operation tracking
                _state = TRDOSAnalyzerState::IN_COMMAND;
                _operation.active = true;
                _operation.service = TRDOSService::UNKNOWN;
                _operation.userCommand = TRDOSUserCommand::LOAD_RUN_MERGE;
                _operation.filename = runEvent.filename;
                _operation.startTime = cpu->tt;
                _operation.startFrame = _context->emulatorState.frame_counter;
                _operation.sectorsRead = 0;
                _operation.sectorsWritten = 0;
            }
            break;
        
        // ========== OTHER COMMAND HANDLERS ==========
        case BP_CAT_HANDLER:
            // $0433 - CAT/LIST command handler
            handleCommandHandler(cpu, TRDOSUserCommand::CAT, "");
            break;
            
        case BP_LOAD_HANDLER:
            // $1815 - LOAD command handler
            handleCommandHandler(cpu, TRDOSUserCommand::LOAD_RUN_MERGE, readTRDOSFilename());
            break;
            
        case BP_SAVE_HANDLER:
            // $1AD0 - SAVE command handler
            handleCommandHandler(cpu, TRDOSUserCommand::SAVE, readTRDOSFilename());
            break;
            
        case BP_ERASE_HANDLER:
            // $0787 - ERASE command handler
            handleCommandHandler(cpu, TRDOSUserCommand::ERASE, readTRDOSFilename());
            break;
            
        case BP_COPY_HANDLER:
            // $0690 - COPY command handler
            handleCommandHandler(cpu, TRDOSUserCommand::COPY, readTRDOSFilename());
            break;
            
        case BP_FORMAT_HANDLER:
            // $1EC2 - FORMAT command handler
            handleCommandHandler(cpu, TRDOSUserCommand::FORMAT, "");
            break;
            
        case BP_VERIFY_HANDLER:
            // $1810 - VERIFY command handler
            handleCommandHandler(cpu, TRDOSUserCommand::VERIFY, readTRDOSFilename());
            break;
            
        case BP_MERGE_HANDLER:
            // $19B1 - MERGE command handler
            handleCommandHandler(cpu, TRDOSUserCommand::MERGE, readTRDOSFilename());
            break;
            
        case BP_FORCE_BOOT:
            // $027B - Force "boot" filename injection (RUN without parameters)
            _currentUserCommand = TRDOSUserCommand::LOAD_RUN_MERGE;
            // Update filename to "boot" since that's what TR-DOS is forcing
            if (_operation.active)
            {
                _operation.filename = "boot";
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
    _currentFDCCommand = command;  // Save for END event
    if (cpu)
    {
        captureRawFDCEvent(fdc, cpu, command, RawFDCEventType::CMD_START);
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
    
    
    // Note: COMMAND_START events are generated by handleServiceCall() 
    // when breakpoint at 0x3D13 is hit - it has proper command byte extraction.
    // FDC observer only tracks state for FDC-level events, not command semantics.
    // IMPORTANT: We do NOT emit FDC commands as semantic events - they go to raw buffer only.
    // Semantic layer is for high-level operations (RUN "file", LOAD "file", etc.)

    
    // Decode command type for operation aggregation
    uint8_t cmdType = command & 0xF0;
    switch (cmdType)
    {
        case 0x80: case 0x90: // Read Sector
            _state = TRDOSAnalyzerState::IN_SECTOR_OP;
            if (_operation.active)
            {
                _operation.sectorsRead++;
            }
            break;
        case 0xA0: case 0xB0: // Write Sector
            _state = TRDOSAnalyzerState::IN_SECTOR_OP;
            if (_operation.active)
            {
                _operation.sectorsWritten++;
            }
            break;
        default:
            // Other commands don't change state
            break;
    }
}

void TRDOSAnalyzer::onFDCPortAccess(uint8_t port, uint8_t value, bool isWrite, const WD1793& fdc)
{
    (void)port; (void)value; (void)isWrite; (void)fdc;
    // Could track data register accesses for byte-level analysis
    // For now, we rely on command-level events
}

void TRDOSAnalyzer::onFDCCommandComplete(uint8_t status, const WD1793& fdc)
{
    // Layer 1: Capture raw FDC END event with status (always capture, even if IDLE)
    Z80* cpu = (_context && _context->pCore) ? _context->pCore->GetZ80() : nullptr;
    if (cpu)
    {
        captureRawFDCEvent(fdc, cpu, _currentFDCCommand, RawFDCEventType::CMD_END);
    }
    
    if (_state == TRDOSAnalyzerState::IDLE)
    {
        return;
    }
    
    uint64_t now = 0;
    if (_context && _context->pCore && _context->pCore->GetZ80())
    {
        now = _context->pCore->GetZ80()->tt;
    }

    
    // Check for errors and track if operation failed
    // NOTE: We do NOT close the operation on FDC error anymore.
    // TR-DOS often retries (SECTOR_RETRY routine), so a single CRC error
    // shouldn't mark the whole LOAD/SAVE as failed/completed.
    // The operation will be completed by the next Service Call or Exit.
    
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
    
    // Sector transfer tracking
    if (_state == TRDOSAnalyzerState::IN_SECTOR_OP)
    {
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

/// Helper to determine if a service code implies a filename is present at $5CDD
static bool serviceRequiresFilename(TRDOSService service)
{
    switch (service)
    {
        case TRDOSService::READ_SECTORS:    // Sometimes used for file operations
        case TRDOSService::WRITE_SECTORS:   // Sometimes used for file operations
        case TRDOSService::FIND_FILE:
        case TRDOSService::SAVE_FILE:
        case TRDOSService::SAVE_BASIC:
        case TRDOSService::LOAD_FILE:
        case TRDOSService::DELETE_SECTOR:   // Often involves directory manipulation
        case TRDOSService::MOVE_DESC_IN:
        case TRDOSService::MOVE_DESC_OUT:
        case TRDOSService::READ_DESCRIPTOR:
        case TRDOSService::WRITE_DESCRIPTOR:
            return true;
        default:
            return false;
    }
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

    // Only process service calls at BP_SERVICE_ENTRY (0x3D13)
    if (address != BP_SERVICE_ENTRY)
    {
        return;
    }

    // COMPLETE previous operation if one is active (new operation is starting)
    if (_operation.active)
    {
        uint64_t now = cpu ? cpu->tt : 0;
        uint32_t currentFrame = _context ? _context->emulatorState.frame_counter : 0;
        
        TRDOSEvent opEvent{};
        opEvent.timestamp = now;
        opEvent.frameNumber = currentFrame;
        opEvent.type = TRDOSEventType::COMMAND_COMPLETE;
        opEvent.service = _operation.service;
        opEvent.userCommand = _operation.userCommand;
        opEvent.filename = _operation.filename;
        opEvent.bytesTransferred = (_operation.sectorsRead + _operation.sectorsWritten) * 256;
        
        emitEvent(std::move(opEvent));
        
        // Reset for new operation
        _operation.active = false;
    }
    
    // Must be in TR-DOS (either IN_TRDOS or IN_COMMAND for chaining)
    if (_state != TRDOSAnalyzerState::IN_TRDOS && _state != TRDOSAnalyzerState::IN_COMMAND)
    {
        return;
    }
    
    _state = TRDOSAnalyzerState::IN_COMMAND;
    _currentService = identifyService(cpu);
    
    // START tracking NEW operation
    _operation.active = true;
    _operation.service = _currentService;
    _operation.userCommand = _currentUserCommand;  // From previous BP_COMMAND_ENTRY
    
    // Only read filename if the service actually uses it
    // This prevents "ghost" filenames from previous operations appearing on commands like RESTORE
    if (serviceRequiresFilename(_currentService))
    {
        _operation.filename = readTRDOSFilename();
    }
    else
    {
        _operation.filename.clear();
    }

    _operation.startTime = cpu ? cpu->tt : 0;
    _operation.startFrame = _context ? _context->emulatorState.frame_counter : 0;
    _operation.sectorsRead = 0;
    _operation.sectorsWritten = 0;
}

/// Handle user command entry at $3D1A - read BASIC token from CH_ADD
/// Helper to handle any command handler breakpoint
/// Emits COMMAND_START and starts operation tracking
void TRDOSAnalyzer::handleCommandHandler(Z80* cpu, TRDOSUserCommand cmd, const std::string& filename)
{
    if (!cpu || !_context)
    {
        return;
    }
    
    _currentUserCommand = cmd;
    
    if (_state == TRDOSAnalyzerState::IDLE)
    {
        _state = TRDOSAnalyzerState::IN_TRDOS;
    }
    
    // Guard: only emit if we don't already have an active operation with this command
    if (!(_operation.active && _operation.userCommand == cmd))
    {
        TRDOSEvent event{};
        event.timestamp = cpu->tt;
        event.frameNumber = _context->emulatorState.frame_counter;
        event.type = TRDOSEventType::COMMAND_START;
        event.userCommand = cmd;
        event.filename = filename;
        event.context.pc = cpu->pc;
        emitEvent(std::move(event));
        
        // Start operation tracking
        _state = TRDOSAnalyzerState::IN_COMMAND;
        _operation.active = true;
        _operation.service = TRDOSService::UNKNOWN;
        _operation.userCommand = cmd;
        _operation.filename = filename;
        _operation.startTime = cpu->tt;
        _operation.startFrame = _context->emulatorState.frame_counter;
        _operation.sectorsRead = 0;
        _operation.sectorsWritten = 0;
    }
}

void TRDOSAnalyzer::handleUserCommandEntry(Z80* cpu)
{
    (void)cpu;
    if (!_context || !_context->pMemory)
    {
        return;
    }
    
    // Ensure we're in TR-DOS
    if (_state == TRDOSAnalyzerState::IDLE)
    {
        _state = TRDOSAnalyzerState::IN_TRDOS;
    }
    
    // Read user command from CH_ADD (pointer at $5C5D) for later use
    _currentUserCommand = identifyUserCommand();
    
    // Note: We don't emit COMMAND_START here. Operation will be started by:
    // - $030A (handleInternalCommandDispatch) for RUN/LOAD commands
    // - $3D13 (handleServiceCall) for API-level operations
}

/// Handle internal command dispatcher at $030A
/// This fires when TR-DOS command parsing reaches the dispatch table (RUN, LOAD from BASIC)
void TRDOSAnalyzer::handleInternalCommandDispatch(Z80* cpu)
{
    if (!cpu || !_context)
    {
        return;
    }
    
    // Auto-transition: If we hit command dispatch from IDLE, we're entering TR-DOS
    if (_state == TRDOSAnalyzerState::IDLE)
    {
        _state = TRDOSAnalyzerState::IN_TRDOS;
        
        // Emit an implied entry event
        TRDOSEvent entryEvent{};
        entryEvent.timestamp = cpu->tt;
        entryEvent.frameNumber = _context ? _context->emulatorState.frame_counter : 0;
        entryEvent.type = TRDOSEventType::TRDOS_ENTRY;
        entryEvent.context.pc = cpu->pc;
        entryEvent.flags = 0x01; // Flag as "implied" entry
        emitEvent(std::move(entryEvent));
    }
    
    // COMPLETE previous operation if one is active (new command is starting)
    // BUT: Only emit if we have meaningful service information
    if (_operation.active)
    {
        // Suppress completion of UNKNOWN operations - they provide no useful info
        if (_operation.service != TRDOSService::UNKNOWN)
        {
            uint64_t now = cpu->tt;
            uint32_t currentFrame = _context ? _context->emulatorState.frame_counter : 0;
            
            TRDOSEvent opEvent{};
            opEvent.timestamp = now;
            opEvent.frameNumber = currentFrame;
            opEvent.type = TRDOSEventType::COMMAND_COMPLETE;
            opEvent.service = _operation.service;
            opEvent.userCommand = _operation.userCommand;
            opEvent.filename = _operation.filename;
            opEvent.bytesTransferred = (_operation.sectorsRead + _operation.sectorsWritten) * 256;
            
            emitEvent(std::move(opEvent));
        }
        
        // Reset for new operation
        _operation.active = false;
    }
    
    // START tracking NEW operation (RUN/LOAD command from BASIC)
    _state = TRDOSAnalyzerState::IN_COMMAND;
    
    _operation.active = true;
    _operation.service = TRDOSService::UNKNOWN;  // Will be determined by service calls
    
    // Try to get user command - fallback to A register if $3D1A wasn't hit
    if (_currentUserCommand == TRDOSUserCommand::UNKNOWN && cpu)
    {
        // A register contains command token at $030A
        // Note: At A> prompt, these are internal tokens, not BASIC tokens!
        uint8_t token = cpu->a;
        switch (token)
        {
            // Internal tokens used at A> prompt
            case 0x80: _currentUserCommand = TRDOSUserCommand::CAT; break;  // LIST at A> prompt
            
            // BASIC tokens (when called from BASIC)
            case 0xCF: _currentUserCommand = TRDOSUserCommand::CAT; break;
            case 0xF8: _currentUserCommand = TRDOSUserCommand::SAVE; break;
            case 0xEF: _currentUserCommand = TRDOSUserCommand::LOAD_RUN_MERGE; break;
            case 0xD0: _currentUserCommand = TRDOSUserCommand::FORMAT; break;
            case 0xD1: _currentUserCommand = TRDOSUserCommand::MOVE; break;
            case 0xD2: _currentUserCommand = TRDOSUserCommand::ERASE; break;
            case 0xFF: _currentUserCommand = TRDOSUserCommand::COPY; break;
            case 0xD5: _currentUserCommand = TRDOSUserCommand::MERGE; break;
            case 0xD6: _currentUserCommand = TRDOSUserCommand::VERIFY; break;
            default: break;  // Keep UNKNOWN
        }
        
        // Emit COMMAND_START for commands detected via token at dispatcher
        // This catches commands like LIST at A> prompt that don't hit specific handler BPs
        if (_currentUserCommand != TRDOSUserCommand::UNKNOWN)
        {
            TRDOSEvent startEvent{};
            startEvent.timestamp = cpu->tt;
            startEvent.frameNumber = _context->emulatorState.frame_counter;
            startEvent.type = TRDOSEventType::COMMAND_START;
            startEvent.userCommand = _currentUserCommand;
            startEvent.filename = readTRDOSFilename();
            startEvent.context.pc = cpu->pc;
            emitEvent(std::move(startEvent));
        }
    }
    
    _operation.userCommand = _currentUserCommand;
    _operation.filename = readTRDOSFilename();
    _operation.startTime = cpu->tt;
    _operation.startFrame = _context ? _context->emulatorState.frame_counter : 0;
    _operation.sectorsRead = 0;
    _operation.sectorsWritten = 0;
}

void TRDOSAnalyzer::handleTRDOSExit(Z80* cpu)
{
    if (_state == TRDOSAnalyzerState::IDLE)
    {
        return;
    }
    
    uint64_t now = cpu ? cpu->tt : 0;
    uint32_t currentFrame = _context ? _context->emulatorState.frame_counter : 0;
    
    // COMPLETE and emit ongoing operation before exit
    if (_operation.active)
    {
        TRDOSEvent opEvent{};
        opEvent.timestamp = now;
        opEvent.frameNumber = currentFrame;
        opEvent.type = TRDOSEventType::COMMAND_COMPLETE;
        opEvent.service = _operation.service;
        opEvent.userCommand = _operation.userCommand;
        opEvent.filename = _operation.filename;
        opEvent.bytesTransferred = (_operation.sectorsRead + _operation.sectorsWritten) * 256;
        
        // Note: formatted string will be generated by format() method when serialized
        
        emitEvent(std::move(opEvent));
        
        // Reset operation
        _operation.active = false;
    }
    
    // Emit exit
    TRDOSEvent event{};
    event.timestamp = now;
    event.frameNumber = currentFrame;
    event.type = TRDOSEventType::TRDOS_EXIT;
    
    emitEvent(std::move(event));
    
    _state = TRDOSAnalyzerState::IDLE;
    _currentService = TRDOSService::UNKNOWN;
    _currentUserCommand = TRDOSUserCommand::UNKNOWN;
}

/// Handle $3D2F trampoline call - custom loaders calling internal ROM routines
/// Pattern: push ix; jp $3D2F (IX contains target routine address)
void TRDOSAnalyzer::handleROMTrampoline(Z80* cpu)
{
    if (!cpu || !_context)
    {
        return;
    }
    
    // Mark that we're dealing with a ROM-internal loader
    _currentLoaderType = LoaderType::ROM_INTERNAL;
    
    // Auto-transition: If we hit trampoline from IDLE, we're entering TR-DOS
    if (_state == TRDOSAnalyzerState::IDLE)
    {
        _state = TRDOSAnalyzerState::IN_CUSTOM;
        
        // Emit an implied entry event
        TRDOSEvent entryEvent{};
        entryEvent.timestamp = cpu->tt;
        entryEvent.frameNumber = _context->emulatorState.frame_counter;
        entryEvent.type = TRDOSEventType::TRDOS_ENTRY;
        entryEvent.context.pc = cpu->pc;
        entryEvent.flags = 0x02; // Flag as "implied" entry from trampoline
        emitEvent(std::move(entryEvent));
    }
    
    // NOTE: We do NOT emit a semantic event here anymore.
    // The trampoline info is captured in raw breakpoint events instead.
    // This keeps semantic events for actual disk operations only.
    // 
    // The captureRawBreakpointEvent() function populates trampolineTarget,
    // trampolineLabel, and trampolineDesc fields when address == 0x3D2F.
}

/// Lookup ROM routine info by address - O(1) (file-scope helper)
static const TRDOSAnalyzer::ROMRoutineInfo* getInternalRoutineInfo(uint16_t address)
{
    auto it = TRDOSAnalyzer::romRoutineMap.find(address);
    return (it != TRDOSAnalyzer::romRoutineMap.end()) ? &it->second : nullptr;
}

/// Map IX value at $3D2F to internal routine name (legacy interface)
std::string TRDOSAnalyzer::getInternalRoutineName(uint16_t ixValue)
{
    const auto* info = getInternalRoutineInfo(ixValue);
    if (info)
    {
        return info->label;
    }
    
    // Return hex format for unknown routines
    char buf[32];
    snprintf(buf, sizeof(buf), "ROM_%04X", ixValue);
    return std::string(buf);
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
    
    // Lookup token in file-scope map - O(1)
    auto it = tokenToCommandMap.find(token);
    return (it != tokenToCommandMap.end()) ? it->second : TRDOSUserCommand::UNKNOWN;
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

/// Decode WD1793 status register based on command type
static std::string decodeWD1793Status(uint8_t command, uint8_t status)
{
    if (status == 0)
    {
        return "OK";
    }
    
    std::string desc;
    uint8_t cmdType = command & 0xF0;
    
    // Type I: RESTORE (0x00-0x0F), SEEK (0x10-0x1F), STEP (0x20-0x3F), STEP-IN (0x40-0x5F), STEP-OUT (0x60-0x7F)
    bool isTypeI = (cmdType >= 0x00 && cmdType <= 0x70);
    
    if (isTypeI)
    {
        // Type I Status Register
        if (status & 0x80) desc += "NOT_READY ";
        if (status & 0x40) desc += "WRITE_PROTECT ";
        if (status & 0x20) desc += "HEAD_LOADED ";
        if (status & 0x10) desc += "SEEK_ERROR ";
        if (status & 0x08) desc += "CRC_ERROR ";
        if (status & 0x04) desc += "TRACK_00 ";
        if (status & 0x02) desc += "INDEX ";
        if (status & 0x01) desc += "BUSY ";
    }
    else
    {
        // Type II/III: READ/WRITE SECTOR (0x80-0xBF), READ ADDRESS (0xC0-0xCF), READ TRACK (0xE0-0xEF), WRITE TRACK (0xF0-0xFF)
        if (status & 0x80) desc += "NOT_READY ";
        if (status & 0x40) desc += "WRITE_PROTECT ";  // Write commands only
        if (status & 0x20) desc += "RECORD_TYPE/WRITE_FAULT ";
        if (status & 0x10) desc += "RECORD_NOT_FOUND ";
        if (status & 0x08) desc += "CRC_ERROR ";
        if (status & 0x04) desc += "LOST_DATA ";
        if (status & 0x02) desc += "DATA_REQUEST ";
        if (status & 0x01) desc += "BUSY ";
    }
    
    // Remove trailing space
    if (!desc.empty() && desc.back() == ' ')
    {
        desc.pop_back();
    }
    
    return desc.empty() ? "OK" : desc;
}

/// Decode WD1793 command to human-readable name
static std::string decodeWD1793Command(uint8_t command)
{
    uint8_t cmdType = command & 0xF0;
    
    switch (cmdType)
    {
        case 0x00: return "RESTORE";
        case 0x10: return "SEEK";
        case 0x20: case 0x30: return "STEP";
        case 0x40: case 0x50: return "STEP_IN";
        case 0x60: case 0x70: return "STEP_OUT";
        case 0x80: case 0x90: return "READ_SECTOR";
        case 0xA0: case 0xB0: return "WRITE_SECTOR";
        case 0xC0: return "READ_ADDRESS";
        case 0xE0: return "READ_TRACK";
        case 0xF0: return "WRITE_TRACK";
        case 0xD0: return "FORCE_INTERRUPT";
        default: return "UNKNOWN";
    }
}

void TRDOSAnalyzer::captureRawFDCEvent(const WD1793& fdc, Z80* cpu, uint8_t command, RawFDCEventType eventType)
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
    
    // Event type (START/END)
    event.eventType = eventType;
    
    // FDC state - command is passed explicitly
    event.commandReg = command;
    event.statusReg = fdc.getStatusRegister();
    event.trackReg = fdc.getTrackRegister();
    event.sectorReg = fdc.getSectorRegister();
    event.dataReg = fdc.getDataRegister();
    event.systemReg = 0;  // TODO: System register (port 0xFF) not in WD1793 interface
    
    // Decode status register based on command type (WD1793 spec-compliant)
    event.statusDescription = decodeWD1793Status(command, event.statusReg);
    
    // Generate human-readable description
    char desc[256];
    std::string cmdName = decodeWD1793Command(command);
    
    if (eventType == RawFDCEventType::CMD_START)
    {
        snprintf(desc, sizeof(desc), "%s Track=%d Sector=%d", 
                 cmdName.c_str(), event.trackReg, event.sectorReg);
    }
    else // CMD_END
    {
        snprintf(desc, sizeof(desc), "%s -> %s",
                 cmdName.c_str(), event.statusDescription.c_str());
    }
    event.description = desc;
    
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
        
        // Capture stack snapshot as 8 return addresses (16-bit little-endian)
        if (_context && _context->pMemory)
        {
            for (int i = 0; i < 8; ++i)
            {
                uint16_t addr = cpu->sp + (i * 2);
                uint8_t lo = _context->pMemory->DirectReadFromZ80Memory(addr);
                uint8_t hi = _context->pMemory->DirectReadFromZ80Memory(addr + 1);
                event.stack[i] = (hi << 8) | lo;
            }
        }
    }
    
    _rawFdcEvents.push(std::move(event));
}

void TRDOSAnalyzer::flushPendingRawEvents()
{
    // Flush any pending polling event
    if (_trampolineRepeatCount > 0 && _lastTrampolineTarget != 0)
    {
        RawBreakpointEvent pollEvent{};
        pollEvent.tstate = _trampolineFirstTstate;
        pollEvent.timestamp = _trampolineFirstTstate;
        pollEvent.frameNumber = _trampolineFirstFrame;
        pollEvent.address = 0x3D2F;
        pollEvent.address_label = "ROM_TRAMPOLINE";
        pollEvent.page_type = "ROM";
        pollEvent.page_index = 4;
        pollEvent.page_offset = 0x3D2F;
        pollEvent.trampolineTarget = _lastTrampolineTarget;
        
        const auto* info = getInternalRoutineInfo(_lastTrampolineTarget);
        if (info)
        {
            pollEvent.trampolineLabel = info->label;
            pollEvent.trampolineDesc = info->description;
        }
        else
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "ROM_%04X", _lastTrampolineTarget);
            pollEvent.trampolineLabel = buf;
            pollEvent.trampolineDesc = "Unknown internal ROM routine";
        }
        
        pollEvent.repeatCount = _trampolineRepeatCount;
        
        char desc[256];
        snprintf(desc, sizeof(desc), "%s (%s) x%u",
                 pollEvent.trampolineLabel.c_str(),
                 pollEvent.trampolineDesc.c_str(),
                 pollEvent.repeatCount);
        pollEvent.description = desc;
        
        _rawBreakpointEvents.push(std::move(pollEvent));
        
        _lastTrampolineTarget = 0;
        _trampolineRepeatCount = 0;
    }
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
        
        // Capture stack snapshot as 8 return addresses (16-bit little-endian)
        if (_context && _context->pMemory)
        {
            for (int i = 0; i < 8; ++i)
            {
                uint16_t addr = cpu->sp + (i * 2);
                uint8_t lo = _context->pMemory->DirectReadFromZ80Memory(addr);
                uint8_t hi = _context->pMemory->DirectReadFromZ80Memory(addr + 1);
                event.stack[i] = (hi << 8) | lo;
            }
        }
        
        // Resolve trampoline target when breakpoint is at $3D2F
        // ROM code at $3D2F is: NOP; RET
        // Pattern: PUSH HL (target), then JP $3D2F - RET pops target from stack
        if (address == 0x3D2F && _context && _context->pMemory)
        {
            // Read target address from stack top
            uint8_t lo = _context->pMemory->DirectReadFromZ80Memory(cpu->sp);
            uint8_t hi = _context->pMemory->DirectReadFromZ80Memory(cpu->sp + 1);
            uint16_t targetAddr = (hi << 8) | lo;
            
            // Identify if this is a polling loop routine
            bool isPollingLoop = false;
            switch (targetAddr)
            {
                case 0x3DA0:  // WAIT_INTRQ_CHK - INTRQ wait loop
                case 0x3D9C:  // WAIT_INTRQ - same loop entry
                case 0x3D9A:  // SEND_CMD_WAIT - wait after command
                case 0x3EF5:  // WAIT_DRQ - DRQ polling loop
                    isPollingLoop = true;
                    break;
            }
            
            if (isPollingLoop)
            {
                // Check if we reached cap
                const uint32_t MAX_POLLING_EVENTS = 10000;
                
                // Deduplicate polling loops - count repeats
                if (targetAddr == _lastTrampolineTarget && _trampolineRepeatCount < MAX_POLLING_EVENTS)
                {
                    // Same target - just increment count
                    _trampolineRepeatCount++;
                    return;  // Don't emit individual events
                }
                else
                {
                    // Different target or cap reached - flush previous count if any
                    flushPendingRawEvents();
                    
                    _lastTrampolineTarget = targetAddr;
                    _trampolineRepeatCount = 1;
                    _trampolineFirstTstate = cpu->tt;
                    _trampolineFirstFrame = event.frameNumber;
                    return;  // We'll emit when context changes
                }
            }
            else
            {
                // Non-polling event - flush any pending polling count first
                flushPendingRawEvents();
                
                // Now emit the actual meaningful event
                event.trampolineTarget = targetAddr;
                const auto* info = getInternalRoutineInfo(targetAddr);
                if (info)
                {
                    event.trampolineLabel = info->label;
                    event.trampolineDesc = info->description;
                }
                else
                {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "ROM_%04X", targetAddr);
                    event.trampolineLabel = buf;
                    event.trampolineDesc = "Unknown internal ROM routine";
                }
                event.repeatCount = 1;
            }
        }
        else if (address != 0x3D2F)
        {
            // Non-trampoline event - flush any pending polling count
            flushPendingRawEvents();
            
            event.trampolineTarget = 0;
            event.repeatCount = 1;
        }
    }
    
    // Generate human-readable description based on breakpoint type
    char desc[256];
    if (address == 0x3D2F && event.trampolineTarget > 0)
    {
        // Trampoline call - show target routine
        snprintf(desc, sizeof(desc), "%s (%s)",
                 event.trampolineLabel.c_str(),
                 event.trampolineDesc.c_str());
    }
    else if (address == 0x3D13)
    {
        // Service call - show C register (service code)
        snprintf(desc, sizeof(desc), "Service call: C=$%02X", event.bc & 0xFF);
    }
    else if (address == 0x030A)
    {
        // Command dispatcher - show context
        snprintf(desc, sizeof(desc), "Internal command dispatch");
    }
    else if (address == 0x3D1A)
    {
        // User command entry
        snprintf(desc, sizeof(desc), "User command entry from BASIC");
    }
    else
    {
        // Generic breakpoint
        snprintf(desc, sizeof(desc), "%s", event.address_label.c_str());
    }
    event.description = desc;
    
    _rawBreakpointEvents.push(std::move(event));
}

/// Get human-readable label for known TR-DOS ROM addresses - O(1) lookup
std::string TRDOSAnalyzer::getAddressLabel(uint16_t address)
{
    std::string result;

    const auto* info = getInternalRoutineInfo(address);
    if (info && info->label)
    {
        result = info->label;
    }
    else
    {
        // Return hex address for unknown addresses
        std::ostringstream ss;
        ss << "x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << address;
        result = ss.str();
    }
    
    return result;
}
