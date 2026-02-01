#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "debugger/disassembler/z80disasm.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/calltrace.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "stdafx.h"

class Z80;
class FeatureManager;

// Access types for memory regions
enum class AccessType : uint8_t
{
    Read = 0x01,
    Write = 0x02,
    Execute = 0x04,
    All = Read | Write | Execute
};

// Tracking modes for memory access
enum class TrackingMode : uint8_t
{
    Z80AddressSpace,  // Track accesses in Z80 64KB address space
    PhysicalMemory    // Track accesses in physical memory pages
};

// Event types for segmented tracking
enum class TrackingEvent : uint8_t
{
    Frame,      // Frame boundary (50Hz)
    Interrupt,  // Z80 interrupt
    Custom      // User-defined event
};

/// @brief Session state for profilers (memory tracking, call trace, opcode profiler)
/// @details Common state model for all profiling features:
///   - Stopped: Not capturing, data may or may not be present
///   - Capturing: Actively capturing data
///   - Paused: Capture paused, data retained
enum class ProfilerSessionState : uint8_t
{
    Stopped,    // Not capturing (feature may be enabled but session not started)
    Capturing,  // Actively capturing data
    Paused      // Capture paused, data retained
};

// Structure to hold tracking options for a monitored region
struct MonitoringOptions
{
    bool trackCallers = false;     // Track addresses of code that accesses this region
    bool trackDataFlow = false;    // Track data values read/written to this region
    uint32_t maxCallers = 100;     // Maximum number of unique caller addresses to track
    uint32_t maxDataValues = 100;  // Maximum number of unique data values to track
};

// Structure to hold access statistics for a memory location or port
struct AccessStats
{
    uint32_t readCount = 0;
    uint32_t writeCount = 0;
    uint32_t executeCount = 0;

    // Maps for tracking callers and data values if enabled
    std::unordered_map<uint16_t, uint32_t> callerAddresses;  // caller address -> access count
    std::unordered_map<uint8_t, uint32_t> dataValues;        // data value -> frequency
};

// Structure to define a monitored memory region
struct MonitoredRegion
{
    std::string name;           // User-friendly name for the region
    uint16_t startAddress;      // Start address of the region
    uint16_t length;            // Length of the region in bytes
    MonitoringOptions options;  // Tracking options for this region
    AccessStats stats;          // Access statistics for this region
    TrackingMode mode;          // Tracking mode for this region
};

// Structure to define a monitored I/O port
struct MonitoredPort
{
    std::string name;           // User-friendly name for the port
    uint16_t port;              // Port number
    MonitoringOptions options;  // Tracking options for this port
    AccessStats stats;          // Access statistics for this port
};

// Structure to hold segment information for time-based tracking
struct TrackingSegment
{
    std::string name;         // Name of the segment (e.g., "Frame 1", "Interrupt 5")
    TrackingEvent eventType;  // Type of event that defines this segment
    uint32_t eventId;         // ID of the event (e.g., frame number, interrupt number)

    // Maps to store region and port statistics for this segment
    std::unordered_map<std::string, AccessStats> regionStats;  // region name -> stats
    std::unordered_map<std::string, AccessStats> portStats;    // port name -> stats
};

class CallTraceBuffer;

// Class to track memory and port accesses
class MemoryAccessTracker
{
#ifdef _CODE_UNDER_TEST
    friend class MemoryAccessTrackerCUT;  // Allow CUT to access private members
#endif

    /// region <Fields>
private:
    // Reference to parent memory object
    Memory* _memory = nullptr;

    // Reference to emulator context
    EmulatorContext* _context = nullptr;

    // Feature-gate flags
    bool _feature_memorytracking_enabled = false;
    bool _feature_calltrace_enabled = false;

    // Session state for memory tracking
    ProfilerSessionState _memorySessionState = ProfilerSessionState::Stopped;
    ProfilerSessionState _calltraceSessionState = ProfilerSessionState::Stopped;

    // Lazy allocation flag - counters only allocated when tracking is enabled
    bool _isAllocated = false;

    // Tracking mode
    TrackingMode _currentMode = TrackingMode::Z80AddressSpace;

    // Monitored regions and ports
    std::vector<MonitoredRegion> _monitoredRegions;
    std::vector<MonitoredPort> _monitoredPorts;

    // Fast lookup maps for regions and ports
    std::unordered_map<uint16_t, std::vector<size_t>> _addressToRegionMap;  // address -> indices in _monitoredRegions
    std::unordered_map<uint16_t, size_t> _portToIndexMap;                   // port -> index in _monitoredPorts

    // Segmented tracking
    bool _segmentTrackingEnabled = false;
    std::vector<TrackingSegment> _segments;
    TrackingSegment* _currentSegment = nullptr;

    // Global counters for Z80 address space (64KB)
    std::vector<uint32_t> _z80ReadCounters;     // Size: 64KB
    std::vector<uint32_t> _z80WriteCounters;    // Size: 64KB
    std::vector<uint32_t> _z80ExecuteCounters;  // Size: 64KB

    // Global counters for physical memory pages
    std::vector<uint32_t> _physReadCounters;     // Size: PAGE_SIZE * MAX_PAGES
    std::vector<uint32_t> _physWriteCounters;    // Size: PAGE_SIZE * MAX_PAGES
    std::vector<uint32_t> _physExecuteCounters;  // Size: PAGE_SIZE * MAX_PAGES

    // Page-level aggregated counters
    std::vector<uint32_t> _pageReadCounters;     // Size: MAX_PAGES
    std::vector<uint32_t> _pageWriteCounters;    // Size: MAX_PAGES
    std::vector<uint32_t> _pageExecuteCounters;  // Size: MAX_PAGES

    // Bank-level flags (for Z80 address space)
    uint8_t _z80BankReadMarks = 0;
    uint8_t _z80BankWriteMarks = 0;
    uint8_t _z80BankExecuteMarks = 0;

    // Page-level flags
    std::vector<uint8_t> _pageReadMarks;     // Size: MAX_PAGES / 8
    std::vector<uint8_t> _pageWriteMarks;    // Size: MAX_PAGES / 8
    std::vector<uint8_t> _pageExecuteMarks;  // Size: MAX_PAGES / 8

    std::unique_ptr<CallTraceBuffer> _callTraceBuffer;
    std::unique_ptr<Z80Disassembler> _disassembler;

    // HALT detection fields
    uint16_t _lastExecutedAddress = 0xFFFF;
    uint32_t _haltExecutionCount = 0;
    static constexpr uint32_t MAX_HALT_EXECUTIONS = 1;  // Only count the first HALT execution
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    MemoryAccessTracker() = delete;  // Disable default constructor
    MemoryAccessTracker(Memory* memory, EmulatorContext* context);
    virtual ~MemoryAccessTracker();
    /// endregion </Constructors / Destructors>

    /// region <Initialization>
public:
    // Initialize the tracker with the specified mode
    void Initialize(TrackingMode mode = TrackingMode::Z80AddressSpace);

    // Reset all counters and statistics
    void ResetCounters();

    // Set the current tracking mode
    void SetTrackingMode(TrackingMode mode);

    // Get the current tracking mode
    TrackingMode GetTrackingMode() const;

    // Update feature cache (call when features change at runtime)
    void UpdateFeatureCache();

    /// region <Session Control - Memory Tracking>
    /// @brief Start a memory tracking session (clears previous data)
    void StartMemorySession();
    
    /// @brief Pause memory tracking session (retains data)
    void PauseMemorySession();
    
    /// @brief Resume a paused memory tracking session
    void ResumeMemorySession();
    
    /// @brief Stop memory tracking session (retains data until cleared)
    void StopMemorySession();
    
    /// @brief Clear all memory tracking data
    void ClearMemoryData();
    
    /// @brief Get current memory tracking session state
    ProfilerSessionState GetMemorySessionState() const;
    
    /// @brief Check if memory tracking is actively capturing
    bool IsMemoryCapturing() const;
    /// endregion </Session Control - Memory Tracking>

    /// region <Session Control - Call Trace>
    /// @brief Start a call trace session (clears previous data)
    void StartCalltraceSession();
    
    /// @brief Pause call trace session (retains data)
    void PauseCalltraceSession();
    
    /// @brief Resume a paused call trace session
    void ResumeCalltraceSession();
    
    /// @brief Stop call trace session (retains data until cleared)
    void StopCalltraceSession();
    
    /// @brief Clear all call trace data
    void ClearCalltraceData();
    
    /// @brief Get current call trace session state
    ProfilerSessionState GetCalltraceSessionState() const;
    
    /// @brief Check if call trace is actively capturing
    bool IsCalltraceCapturing() const;
    /// endregion </Session Control - Call Trace>
    /// endregion </Initialization>

    /// region <Region and Port Monitoring>
public:
    // Add a monitored memory region with the specified options
    bool AddMonitoredRegion(const std::string& name, uint16_t startAddress, uint16_t length,
                            const MonitoringOptions& options = MonitoringOptions());

    // Add a monitored I/O port with the specified options
    bool AddMonitoredPort(const std::string& name, uint16_t port,
                          const MonitoringOptions& options = MonitoringOptions());

    // Remove a monitored memory region by name
    bool RemoveMonitoredRegion(const std::string& name);

    // Remove a monitored I/O port by name
    bool RemoveMonitoredPort(const std::string& name);

    // Get statistics for a monitored memory region
    const AccessStats* GetRegionStats(const std::string& name) const;

    // Get statistics for a monitored I/O port
    const AccessStats* GetPortStats(const std::string& name) const;
    /// endregion </Region and Port Monitoring>

    /// region <Segmented Tracking>
public:
    // Enable or disable segmented tracking
    void EnableSegmentTracking(bool enable);

    // Start a new tracking segment
    void StartSegment(const std::string& name, TrackingEvent eventType, uint32_t eventId);

    // End the current tracking segment
    void EndSegment();

    // Get statistics for a specific segment
    const TrackingSegment* GetSegment(const std::string& name) const;

    // Get all tracking segments
    const std::vector<TrackingSegment>& GetAllSegments() const;
    /// endregion </Segmented Tracking>

    /// region <HALT Detection>
public:
    // Reset HALT detection when PC changes (called from Z80 when PC changes)
    void ResetHaltDetection(uint16_t newPC);
    /// endregion </HALT Detection>

    /// region <Access Tracking>
public:
    // Track memory read access
    void TrackMemoryRead(uint16_t address, uint8_t value, uint16_t callerAddress);

    // Track memory write access
    void TrackMemoryWrite(uint16_t address, uint8_t value, uint16_t callerAddress);

    // Track memory execute access
    void TrackMemoryExecute(uint16_t address, uint16_t callerAddress);

    // Track port read access
    void TrackPortRead(uint16_t port, uint8_t value, uint16_t callerAddress);

    // Track port write access
    void TrackPortWrite(uint16_t port, uint8_t value, uint16_t callerAddress);
    /// endregion </Access Tracking>

    /// region <Statistics and Reporting>
public:
    // Get total access count for a Z80 bank
    uint32_t GetZ80BankTotalAccessCount(uint8_t bank) const;

    // Get read access count for a Z80 bank
    uint32_t GetZ80BankReadAccessCount(uint8_t bank) const;

    // Get write access count for a Z80 bank
    uint32_t GetZ80BankWriteAccessCount(uint8_t bank) const;

    // Get execute access count for a Z80 bank
    uint32_t GetZ80BankExecuteAccessCount(uint8_t bank) const;

    // Get total access count for a physical memory page
    uint32_t GetPageTotalAccessCount(uint16_t page) const;

    // Get read access count for a physical memory page
    uint32_t GetPageReadAccessCount(uint16_t page) const;

    // Get write access count for a physical memory page
    uint32_t GetPageWriteAccessCount(uint16_t page) const;

    // Get execute access count for a physical memory page
    uint32_t GetPageExecuteAccessCount(uint16_t page) const;

    // Generate a report of all monitored regions and their statistics
    std::string GenerateRegionReport() const;

    // Generate a report of all monitored ports and their statistics
    std::string GeneratePortReport() const;

    // Generate a report of all segments and their statistics
    std::string GenerateSegmentReport() const;

    // Check if there's any activity (read/write/execute) in the specified address range
    // @param start Start address (inclusive)
    // @param end End address (inclusive)
    // @return True if any access was detected in the range
    bool HasActivity(uint16_t start, uint16_t end) const;

    /// Save memory access data
    /// @param outputPath Output file or directory path
    /// @param format Output format ("yaml" only supported for now)
    /// @param singleFile If true, saves everything in one file; if false, uses separate files in a subfolder
    /// @param filterPages Optional list of specific pages to include (empty = all)
    /// @return true if successful, false on error
    std::string SaveAccessData(const std::string& outputPath, const std::string& format = "yaml",
                               bool singleFile = false, const std::vector<std::string>& filterPages = {});

    /// endregion </Statistics and Reporting>

    /// region <Helper Methods>
private:
    // Update the address to region map after adding or removing a region
    void UpdateAddressToRegionMap();

    // Get the physical memory offset for a Z80 address
    size_t GetPhysicalOffsetForZ80Address(uint16_t address) const;

    // Get the page number for a physical memory offset
    uint16_t GetPageForPhysicalOffset(size_t offset) const;

    // Update region statistics for a memory access
    void UpdateRegionStats(uint16_t address, uint8_t value, uint16_t callerAddress, AccessType accessType);

    // Update port statistics for a port access
    void UpdatePortStats(uint16_t port, uint8_t value, uint16_t callerAddress, AccessType accessType);

    // Update segment statistics if segment tracking is enabled
    void UpdateSegmentStats(const std::string& name, const AccessStats& stats, bool isPort);

    // Add a value to the data flow tracking map with limit enforcement
    void AddToDataFlowMap(std::unordered_map<uint8_t, uint32_t>& map, uint8_t value, uint32_t maxEntries);

    // Add a caller to the caller tracking map with limit enforcement
    void AddToCallerMap(std::unordered_map<uint16_t, uint32_t>& map, uint16_t callerAddress, uint32_t maxEntries);

    // Helper methods for saving access data
    void SaveMemoryLayout(std::ostream& out);
    void SavePageSummaries(std::ostream& out, const std::vector<std::string>& filterPages);
    bool SaveDetailedAccessData(const std::filesystem::path& dirPath, const std::vector<std::string>& filterPages);
    void SaveDetailedAccessData(std::ostream& out, const std::vector<std::string>& filterPages);
    void SaveSinglePageAccessData(std::ostream& out, uint16_t page, const std::string& indent = "");
    std::string GetBankPageName(uint8_t bank) const;

    // Allocate counter vectors (lazy allocation - called when tracking is first enabled)
    void AllocateCounters();

    // Deallocate counter vectors (called when tracking is disabled to free memory)
    void DeallocateCounters();
    /// endregion </Helper Methods>

public:
    // Call trace API
    CallTraceBuffer* GetCallTraceBuffer();
    void LogControlFlowEvent(const Z80ControlFlowEvent& event);
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties for unit testing
//
#ifdef _CODE_UNDER_TEST

class MemoryAccessTrackerCUT : public MemoryAccessTracker
{
public:
    MemoryAccessTrackerCUT(Memory* memory, EmulatorContext* context) : MemoryAccessTracker(memory, context) {};

public:
    using MemoryAccessTracker::_feature_memorytracking_enabled;
    using MemoryAccessTracker::_isAllocated;
    using MemoryAccessTracker::_z80ExecuteCounters;
    using MemoryAccessTracker::_z80ReadCounters;
    using MemoryAccessTracker::_z80WriteCounters;
};

#endif  // _CODE_UNDER_TEST
