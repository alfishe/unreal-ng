#pragma once

#include "debugger/analyzers/ianalyzer.h"
#include "debugger/analyzers/trdos/trdosevent.h"
#include "emulator/io/fdc/iwd1793observer.h"
#include "common/ringbuffer.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

// Forward declarations
class AnalyzerManager;
class WD1793;
class Z80;
class EmulatorContext;

/// Analyzer state machine states
enum class TRDOSAnalyzerState : uint8_t
{
    IDLE,           // Monitoring for TR-DOS entry
    IN_TRDOS,       // Inside TR-DOS ROM, command not identified
    IN_COMMAND,     // Command identified, tracking operation
    IN_SECTOR_OP,   // Mid-sector read/write
    IN_MULTI_OP,    // Multi-sector operation
    COMPLETING,     // Operation finishing
    IN_CUSTOM,      // Custom loader detected
};

/// TR-DOS Analyzer: Captures and aggregates disk operations into semantic events.
///
/// Implements both IAnalyzer (for breakpoint-based ROM monitoring) and
/// IWD1793Observer (for FDC event callbacks).
///
/// Usage:
/// 1. Register with AnalyzerManager
/// 2. Activate to start capturing
/// 3. Query events via getEvents()
/// 4. Deactivate when done
class TRDOSAnalyzer : public IAnalyzer, public IWD1793Observer
{
public:
    // Buffer sizes (raw > semantic since semantic are aggregated)
    static constexpr size_t RAW_BUFFER_SIZE = 4096;       // Raw FDC/breakpoint events
    static constexpr size_t SEMANTIC_BUFFER_SIZE = 2048;  // Aggregated semantic events

    // TR-DOS ROM breakpoint addresses (External Entry Points - $3Dxx range)
    static constexpr uint16_t BP_TRDOS_ENTRY = 0x3D00;      // TR-DOS entry point (from BASIC)
    static constexpr uint16_t BP_COMMAND_PROCESSOR = 0x3D03;// Command processor from BASIC
    static constexpr uint16_t BP_SERVICE_ENTRY = 0x3D13;    // Low-level service API (C = service code)
    static constexpr uint16_t BP_COMMAND_ENTRY = 0x3D1A;    // User command entry (read token from CH_ADD)
    static constexpr uint16_t BP_INIT_VARS = 0x3D21;        // Init system variables
    static constexpr uint16_t BP_DOS_ENTRY = 0x3D2F;        // DOS entry (via RET/JP)
    static constexpr uint16_t BP_EXIT = 0x0077;             // TR-DOS exit (RET to BASIC)
    
    // TR-DOS ROM breakpoint addresses (Internal Command Processor - for resident loop sniffing)
    // When TR-DOS is already active at A> prompt, commands go through internal loop, not $3Dxx gates
    static constexpr uint16_t BP_CMD_DISPATCHER = 0x030A;   // Command token dispatcher (A = BASIC token)
    static constexpr uint16_t BP_CMD_LOOP = 0x02CB;         // Command processor loop entry
    static constexpr uint16_t BP_CMD_TOKENIZED = 0x02EF;    // After command tokenization
    
    // Command handler entry points (from jump table at $3008)
    static constexpr uint16_t BP_CAT_HANDLER = 0x0433;      // CAT/LIST command handler
    static constexpr uint16_t BP_DRIVE_HANDLER = 0x1018;    // Drive selection handler  
    static constexpr uint16_t BP_FORMAT_HANDLER = 0x1EC2;   // FORMAT command handler
    static constexpr uint16_t BP_NEW_HANDLER = 0x053A;      // NEW command handler
    static constexpr uint16_t BP_ERASE_HANDLER = 0x0787;    // ERASE command handler
    static constexpr uint16_t BP_LOAD_HANDLER = 0x1815;     // LOAD command handler
    static constexpr uint16_t BP_SAVE_HANDLER = 0x1AD0;     // SAVE command handler
    static constexpr uint16_t BP_MERGE_HANDLER = 0x19B1;    // MERGE command handler
    static constexpr uint16_t BP_RUN_HANDLER = 0x1D4D;      // RUN command handler entry
    static constexpr uint16_t BP_VERIFY_HANDLER = 0x1810;   // VERIFY command handler
    static constexpr uint16_t BP_COPY_HANDLER = 0x0690;     // COPY command handler
    static constexpr uint16_t BP_FORCE_BOOT = 0x027B;       // Forces "boot" filename injection
    
    // ROM Trampoline for custom loaders (uses IX = internal routine address)
    static constexpr uint16_t BP_ROM_TRAMPOLINE = 0x3D2F;   // ROM switch trampoline (push ix; jp $3D2F)

    /// ROM routine info structure for label/description lookup
    struct ROMRoutineInfo
    {
        const char* label;
        const char* description;
    };
    
    /// Map of known TR-DOS ROM routines for O(1) address lookup
    static const std::unordered_map<uint16_t, ROMRoutineInfo> romRoutineMap;
    
    /// Map of BASIC/internal tokens to TR-DOS user commands
    static const std::unordered_map<uint8_t, TRDOSUserCommand> tokenToCommandMap;

    /// Constructor
    /// @param context Emulator context for memory/CPU access
    explicit TRDOSAnalyzer(EmulatorContext* context);
    
    ~TRDOSAnalyzer() override;

    // ==================== IAnalyzer interface ====================
    
    std::string getName() const override { return "TRDOSAnalyzer"; }
    std::string getUUID() const override { return _uuid; }
    
    void onActivate(AnalyzerManager* manager) override;
    void onDeactivate() override;
    void onBreakpointHit(uint16_t address, Z80* cpu) override;
    void onFrameEnd() override;
    
    // ==================== IWD1793Observer interface ====================
    
    void onFDCCommand(uint8_t command, const WD1793& fdc) override;
    void onFDCPortAccess(uint8_t port, uint8_t value, bool isWrite, const WD1793& fdc) override;
    void onFDCCommandComplete(uint8_t status, const WD1793& fdc) override;
    
    // ==================== Query API ====================
    
    /// Get all captured events
    std::vector<TRDOSEvent> getEvents() const;
    
    /// Get events since T-state timestamp
    std::vector<TRDOSEvent> getEventsSince(uint64_t timestamp) const;
    
    /// Get new events since last query
    std::vector<TRDOSEvent> getNewEvents();
    
    /// Get event count
    size_t getEventCount() const;
    
    // ==================== Raw Event Query API ====================
    
    /// Get all raw FDC events
    std::vector<RawFDCEvent> getRawFDCEvents() const;
    
    /// Get raw FDC events since T-state timestamp
    std::vector<RawFDCEvent> getRawFDCEventsSince(uint64_t timestamp) const;
    
    /// Get all raw breakpoint events
    std::vector<RawBreakpointEvent> getRawBreakpointEvents() const;
    
    /// Get raw breakpoint events since T-state timestamp
    std::vector<RawBreakpointEvent> getRawBreakpointEventsSince(uint64_t timestamp) const;
    
    /// Clear all captured events (both raw and semantic)
    void clear();
    
    /// Get current analyzer state
    TRDOSAnalyzerState getState() const { return _state; }
    
    /// Get statistics
    uint64_t getTotalEventsProduced() const { return _events.totalEventsProduced(); }
    uint64_t getTotalEventsEvicted() const { return _events.totalEventsEvicted(); }

private:
    std::string _uuid;
    EmulatorContext* _context;
    AnalyzerManager* _manager = nullptr;
    WD1793* _fdc = nullptr;
    
    // State machine
    TRDOSAnalyzerState _state = TRDOSAnalyzerState::IDLE;
    
    // Current operation context
    TRDOSService _currentService = TRDOSService::UNKNOWN;
    TRDOSUserCommand _currentUserCommand = TRDOSUserCommand::UNKNOWN;
    LoaderType _currentLoaderType = LoaderType::UNKNOWN;    // Current loader classification
    uint8_t _currentFDCCommand = 0;         // Current FDC command byte for END event
    uint64_t _commandStartTime = 0;
    uint64_t _lastEventTime = 0;
    
    // Event storage
    RingBuffer<TRDOSEvent> _events;
    RingBuffer<RawFDCEvent> _rawFdcEvents;
    RingBuffer<RawBreakpointEvent> _rawBreakpointEvents;
    
    // Tracking for getNewEvents()
    uint64_t _lastQueryTime = 0;
    
    // Overflow protection
    bool _rawBuffersOverflow = false;
    
    // Trampoline polling deduplication
    // Polling loops like WAIT_INTRQ are deduplicated - one event with repeatCount
    // FDC events remain fully serialized for semantic layer grouping
    uint16_t _lastTrampolineTarget = 0;     // Last trampoline target address
    uint32_t _trampolineRepeatCount = 0;    // How many times this target was called
    uint64_t _trampolineFirstTstate = 0;    // T-state when polling started
    uint32_t _trampolineFirstFrame = 0;     // Frame number when polling started
    
    // Operation tracking for semantic aggregation
    struct OngoingOperation {
        TRDOSService service = TRDOSService::UNKNOWN;
        TRDOSUserCommand userCommand = TRDOSUserCommand::UNKNOWN;
        std::string filename;
        uint64_t startTime = 0;
        uint32_t startFrame = 0;
        uint16_t sectorsRead = 0;
        uint16_t sectorsWritten = 0;
        bool active = false;
    };
    OngoingOperation _operation;
    
    // Private methods
    void flushPendingRawEvents(); // Flush pending polling events
    void emitEvent(TRDOSEvent&& event);
    void captureRawFDCEvent(const WD1793& fdc, Z80* cpu, uint8_t command, RawFDCEventType eventType);
    void captureRawBreakpointEvent(uint16_t address, Z80* cpu);
    void handleTRDOSEntry(Z80* cpu);
    void handleServiceCall(uint16_t address, Z80* cpu);     // Low-level API at $3D13
    void handleCommandHandler(Z80* cpu, TRDOSUserCommand cmd, const std::string& filename);
    void handleUserCommandEntry(Z80* cpu);                  // User command at $3D1A
    void handleInternalCommandDispatch(Z80* cpu);           // Internal dispatcher at $030A (A = token)
    void handleTRDOSExit(Z80* cpu);
    void handleROMTrampoline(Z80* cpu);                     // $3D2F trampoline (IX = target routine)
    TRDOSService identifyService(Z80* cpu);                 // Identify service from C register
    TRDOSUserCommand identifyUserCommand();                 // Identify command from CH_ADD token
    bool commandRequiresFilename(TRDOSUserCommand cmd);     // Check if command needs filename
    std::string readTRDOSFilename();                        // Read filename+ext from $5CDD/$5CE5
    std::string readFilenameFromMemory(uint16_t address);   // Legacy filename reader
    std::string getAddressLabel(uint16_t address);          // Get label for known TR-DOS addresses
    std::string getInternalRoutineName(uint16_t ixValue);   // Map IX to routine name for $3D2F
};
