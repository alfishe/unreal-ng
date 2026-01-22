#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ianalyzer.h"
#include "emulator/memory/memory.h"

// Forward declarations
class EmulatorContext;
class BreakpointManager;
class FeatureManager;
class Z80;
class DebugManager;

/// Subscription handle for cleanup
using CallbackId = uint64_t;

/// Breakpoint identifier
using BreakpointId = uint32_t;

/// Hot path callback structure (raw function pointers for minimal overhead)
struct CPUStepCallback
{
    void (*fn)(void* ctx, Z80* cpu, uint16_t pc);
    void* context;
    std::string ownerId;
};

/// Memory access callback structure
struct MemoryCallback
{
    void (*fn)(void* ctx, uint16_t address, uint8_t value);
    void* context;
    std::string ownerId;
};

/// Manages analyzer lifecycle, event dispatching, and breakpoint ownership.
/// Uses hybrid dispatch: raw pointers for hot paths, std::function for warm paths, virtual for cold paths.
class AnalyzerManager
{
public:
    /// Owner ID used for all analyzer-owned breakpoints
    /// AnalyzerManager registers breakpoints with this ID, then dispatches hits internally
    static constexpr const char* OWNER_ID = "analyzer_manager";
    /// Constructor
    /// @param context Emulator context
    explicit AnalyzerManager(EmulatorContext* context);

    /// Destructor - deactivates all analyzers
    ~AnalyzerManager();

    /// Initialize manager after DebugManager construction is complete
    /// Must be called before using the manager
    /// @param debugManager Pointer to the parent DebugManager
    void init(DebugManager* debugManager);

    // Analyzer lifecycle

    /// Register an analyzer with the given ID
    /// @param id Unique identifier for the analyzer
    /// @param analyzer Analyzer instance (ownership transferred)
    void registerAnalyzer(const std::string& id, std::unique_ptr<IAnalyzer> analyzer);

    /// Unregister an analyzer (also deactivates if active)
    /// @param id Analyzer identifier
    void unregisterAnalyzer(const std::string& id);

    /// Get analyzer by ID
    /// @param id Analyzer identifier
    /// @return Pointer to analyzer or nullptr if not found
    IAnalyzer* getAnalyzer(const std::string& id);

    /// Get analyzer by ID with type checking
    /// @tparam T Analyzer type
    /// @param id Analyzer identifier
    /// @return Pointer to analyzer or nullptr if not found or wrong type
    template <typename T>
    T* getAnalyzer(const std::string& id)
    {
        return dynamic_cast<T*>(getAnalyzer(id));
    }

    // Activation control

    /// Activate an analyzer (calls onActivate)
    /// @param id Analyzer identifier
    /// @return True if activation succeeded
    bool activate(const std::string& id);

    /// Deactivate an analyzer (calls onDeactivate, cleans up subscriptions/breakpoints)
    /// @param id Analyzer identifier
    /// @return True if deactivation succeeded
    bool deactivate(const std::string& id);

    /// Activate all registered analyzers
    void activateAll();

    /// Deactivate all active analyzers
    void deactivateAll();

    /// Check if an analyzer is active
    /// @param id Analyzer identifier
    /// @return True if active
    bool isActive(const std::string& id) const;

    // Query

    /// Get list of all registered analyzer IDs
    /// @return Vector of analyzer IDs
    std::vector<std::string> getRegisteredAnalyzers() const;

    /// Get list of currently active analyzer IDs
    /// @return Vector of active analyzer IDs
    std::vector<std::string> getActiveAnalyzers() const;

    /// Check if an analyzer with given ID is registered
    /// @param id Analyzer identifier
    /// @return True if registered
    bool hasAnalyzer(const std::string& id) const;
    
    /// Check if any active analyzer owns a breakpoint at the given address
    /// Used by Z80 to determine if breakpoint should pause or just notify
    /// @param address The Z80 address to check
    /// @return True if an active analyzer owns a breakpoint at this address
    bool ownsBreakpointAtAddress(uint16_t address) const;
    
    /// Check if any active analyzer owns a page-specific breakpoint
    /// @param address The Z80 address to check
    /// @param page Page number (ROM 0-63, RAM 0-255)
    /// @param pageType Memory type (BANK_ROM, BANK_RAM, or BANK_CACHE)
    /// @return True if an active analyzer owns a breakpoint at this address/page
    bool ownsBreakpointAtAddress(uint16_t address, uint8_t page, MemoryBankModeEnum pageType) const;

    // Event subscription (called by analyzers during onActivate)

    // Hot path subscriptions (raw function pointers ~2-3ns per call)

    /// Subscribe to CPU step events (~3.5M/sec)
    /// @param fn Callback function pointer
    /// @param context User context pointer
    /// @param analyzerId Analyzer identifier (for ownership tracking)
    /// @return Subscription ID for cleanup
    CallbackId subscribeCPUStep(void (*fn)(void* ctx, Z80* cpu, uint16_t pc), void* context,
                                const std::string& analyzerId);

    /// Subscribe to memory read events (~1-3M/sec)
    /// @param fn Callback function pointer
    /// @param context User context pointer
    /// @param analyzerId Analyzer identifier (for ownership tracking)
    /// @return Subscription ID for cleanup
    CallbackId subscribeMemoryRead(void (*fn)(void* ctx, uint16_t addr, uint8_t val), void* context,
                                   const std::string& analyzerId);

    /// Subscribe to memory write events (~1-3M/sec)
    /// @param fn Callback function pointer
    /// @param context User context pointer
    /// @param analyzerId Analyzer identifier (for ownership tracking)
    /// @return Subscription ID for cleanup
    CallbackId subscribeMemoryWrite(void (*fn)(void* ctx, uint16_t addr, uint8_t val), void* context,
                                    const std::string& analyzerId);

    // Warm path subscriptions (std::function ~15-25ns per call)

    /// Subscribe to video line events (~15K/sec)
    /// @param callback Callback function
    /// @param analyzerId Analyzer identifier (for ownership tracking)
    /// @return Subscription ID for cleanup
    CallbackId subscribeVideoLine(std::function<void(uint16_t line)> callback, const std::string& analyzerId);

    /// Subscribe to audio sample events (~44K/sec)
    /// @param callback Callback function
    /// @param analyzerId Analyzer identifier (for ownership tracking)
    /// @return Subscription ID for cleanup
    CallbackId subscribeAudioSample(std::function<void(int16_t left, int16_t right)> callback,
                                    const std::string& analyzerId);

    // Breakpoint management with ownership

    /// Request an execution breakpoint (owned by analyzer, auto-cleaned on deactivate)
    /// @param address Breakpoint address
    /// @param analyzerId Analyzer identifier (for ownership tracking)
    /// @return Breakpoint ID
    BreakpointId requestExecutionBreakpoint(uint16_t address, const std::string& analyzerId);
    
    /// Request an execution breakpoint in a specific memory page (owned by analyzer)
    /// @param address Breakpoint address within the page
    /// @param page Page number (ROM 0-63, RAM 0-255)
    /// @param pageType Memory type (BANK_ROM, BANK_RAM, or BANK_CACHE)
    /// @param analyzerId Analyzer identifier (for ownership tracking)
    /// @return Breakpoint ID
    BreakpointId requestExecutionBreakpointInPage(uint16_t address, uint8_t page, 
                                                   MemoryBankModeEnum pageType,
                                                   const std::string& analyzerId);

    /// Request a memory watchpoint (owned by analyzer, auto-cleaned on deactivate)
    /// @param address Memory address
    /// @param onRead Trigger on read
    /// @param onWrite Trigger on write
    /// @param analyzerId Analyzer identifier (for ownership tracking)
    /// @return Breakpoint ID
    BreakpointId requestMemoryBreakpoint(uint16_t address, bool onRead, bool onWrite, const std::string& analyzerId);

    /// Release a breakpoint manually (usually automatic on deactivation)
    /// @param id Breakpoint ID
    void releaseBreakpoint(BreakpointId id);

    // Unsubscribe

    /// Unsubscribe from a specific event
    /// @param id Callback ID
    void unsubscribe(CallbackId id);

    /// Unsubscribe from all events for an analyzer (automatic on deactivation)
    /// @param analyzerId Analyzer identifier
    void unsubscribeAll(const std::string& analyzerId);

    // Dispatch methods (called by emulator main loop)

    /// Dispatch CPU step event to all subscribers
    /// @param cpu CPU state
    /// @param pc Program counter
    void dispatchCPUStep(Z80* cpu, uint16_t pc);

    /// Dispatch memory read event to all subscribers
    /// @param addr Memory address
    /// @param val Value read
    void dispatchMemoryRead(uint16_t addr, uint8_t val);

    /// Dispatch memory write event to all subscribers
    /// @param addr Memory address
    /// @param val Value written
    void dispatchMemoryWrite(uint16_t addr, uint8_t val);

    /// Dispatch video line event to all subscribers
    /// @param line Scanline number
    void dispatchVideoLine(uint16_t line);

    /// Dispatch audio sample event to all subscribers
    /// @param left Left channel sample
    /// @param right Right channel sample
    void dispatchAudioSample(int16_t left, int16_t right);

    /// Dispatch frame start event to all active analyzers
    void dispatchFrameStart();

    /// Dispatch frame end event to all active analyzers
    void dispatchFrameEnd();

    /// Dispatch breakpoint hit event to owning analyzers
    /// @param addr Breakpoint address
    /// @param bpId Breakpoint ID
    /// @param cpu CPU state at breakpoint
    void dispatchBreakpointHit(uint16_t addr, BreakpointId bpId, Z80* cpu);

    // Feature toggle

    /// Enable/disable all analyzer dispatching (master toggle)
    /// @param enabled True to enable, false to disable
    void setEnabled(bool enabled);

    /// Check if analyzer dispatching is enabled
    /// @return True if enabled
    bool isEnabled() const;

private:
    EmulatorContext* _context;
    BreakpointManager* _breakpointManager;
    FeatureManager* _featureManager;

    // Analyzer storage
    std::unordered_map<std::string, std::unique_ptr<IAnalyzer>> _analyzers;
    std::unordered_set<std::string> _activeAnalyzers;

    // Hot path callback storage
    std::vector<CPUStepCallback> _cpuStepCallbacks;
    std::vector<MemoryCallback> _memoryReadCallbacks;
    std::vector<MemoryCallback> _memoryWriteCallbacks;

    // Warm path callback storage
    std::vector<std::pair<std::function<void(uint16_t)>, std::string>> _videoLineCallbacks;
    std::vector<std::pair<std::function<void(int16_t, int16_t)>, std::string>> _audioCallbacks;

    // Breakpoint ownership tracking
    std::unordered_map<BreakpointId, std::string> _breakpointOwners;                  // bpId -> analyzerId
    std::unordered_map<BreakpointId, uint32_t> _breakpointKeys;                       // bpId -> composite key (pageType|page|address)
    std::unordered_map<std::string, std::vector<BreakpointId>> _analyzerBreakpoints;  // analyzerId -> [bpIds]
    std::unordered_set<uint16_t> _ownedAddresses;                                     // Set of addresses for O(1) address-only lookup
    std::unordered_set<uint32_t> _ownedKeys;                                          // Set of composite keys for O(1) page-specific lookup
    
    // Helper to create composite breakpoint key: (pageType << 24) | (page << 16) | address
    // For non-page-specific breakpoints, uses BANK_INVALID (0xFF) as pageType
    static constexpr uint32_t makeBreakpointKey(uint16_t address, uint8_t page = 0, MemoryBankModeEnum pageType = BANK_INVALID)
    {
        return (static_cast<uint32_t>(pageType) << 24) | (static_cast<uint32_t>(page) << 16) | address;
    }

    // Subscription tracking for cleanup
    std::unordered_map<CallbackId, std::string> _subscriptionOwners;                  // callbackId -> analyzerId
    std::unordered_map<std::string, std::vector<CallbackId>> _analyzerSubscriptions;  // analyzerId -> [callbackIds]

    // Master toggle
    bool _enabled = false;

    // ID generators
    CallbackId _nextCallbackId = 1;

    // Helper methods

    /// Remove all breakpoints owned by an analyzer
    /// @param analyzerId Analyzer identifier
    void removeAllBreakpointsForAnalyzer(const std::string& analyzerId);

    /// Remove all subscriptions for an analyzer
    /// @param analyzerId Analyzer identifier
    void removeAllSubscriptionsForAnalyzer(const std::string& analyzerId);
};
