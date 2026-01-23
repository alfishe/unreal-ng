#pragma once

#include "debugger/analyzers/ianalyzer.h"
#include "debugger/analyzers/trdos/trdosevent.h"
#include "emulator/io/fdc/iwd1793observer.h"
#include "common/ringbuffer.h"

#include <string>
#include <vector>
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

    // TR-DOS ROM breakpoint addresses
    static constexpr uint16_t BP_TRDOS_ENTRY = 0x3D00;  // TR-DOS entry point
    static constexpr uint16_t BP_SERVICE_ENTRY = 0x3D13;// Service entry (RST 8 / Standard)
    static constexpr uint16_t BP_COMMAND_DISPATCH = 0x3D21; // Command dispatch
    static constexpr uint16_t BP_DOS_ENTRY = 0x3D2F;    // DOS entry (via RET/JP)
    static constexpr uint16_t BP_EXIT = 0x0077;         // TR-DOS exit (RET to BASIC)

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
    TRDOSCommand _currentCommand = TRDOSCommand::UNKNOWN;
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
    
    // Private methods
    void emitEvent(TRDOSEvent&& event);
    void captureRawFDCEvent(const WD1793& fdc, Z80* cpu);
    void captureRawBreakpointEvent(uint16_t address, Z80* cpu);
    void handleTRDOSEntry(Z80* cpu);
    void handleCommandDispatch(uint16_t address, Z80* cpu);
    void handleTRDOSExit(Z80* cpu);
    TRDOSCommand identifyCommand(uint16_t address, Z80* cpu);
    std::string readFilenameFromMemory(uint16_t address);
};
