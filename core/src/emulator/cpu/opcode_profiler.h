#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// Forward declaration for ProfilerSessionState
#include "emulator/memory/memoryaccesstracker.h"

class EmulatorContext;

/// @brief Trace entry capturing a single opcode execution for forensics
struct OpcodeTraceEntry
{
    uint16_t pc;       // Program counter where opcode executed
    uint16_t prefix;   // Prefix code (0=none, 0xCB, 0xDD, 0xED, 0xFD, 0xDDCB, 0xFDCB)
    uint8_t opcode;    // Opcode byte (0-255)
    uint8_t flags;     // F register value at execution
    uint8_t a;         // A register value at execution
    uint8_t reserved;  // Alignment padding
    uint32_t frame;    // Frame number
    uint32_t tState;   // T-state within frame
};
static_assert(sizeof(OpcodeTraceEntry) == 16, "OpcodeTraceEntry should be 16 bytes");

/// @brief Counter entry for reporting opcode execution counts
struct OpcodeCounter
{
    uint16_t prefix;      // Prefix code
    uint8_t opcode;       // Opcode byte
    uint64_t count;       // Execution count
    std::string mnemonic; // Human-readable mnemonic (populated on retrieval)
};

/// @brief Profiler status information
struct ProfilerStatus
{
    bool capturing;        // Currently capturing
    uint64_t totalExecutions;  // Total opcode executions
    uint32_t traceSize;    // Current trace buffer entries
    uint32_t traceCapacity;// Maximum trace buffer capacity
};

/// @brief Z80 Opcode Profiler for execution statistics and crash forensics
///
/// Two-tier architecture:
/// - Tier 1: Statistical counters for all 1792 opcode variants
/// - Tier 2: Sequential trace ring buffer for last N executed instructions
///
/// Usage:
/// 1. Enable via FeatureManager: feature opcodeprofiler on
/// 2. Start session: profiler.Start()
/// 3. Run emulation
/// 4. Retrieve data: GetTopOpcodes(), GetRecentTrace()
/// 5. Clear and repeat as needed
class OpcodeProfiler
{
public:
    /// @brief Opcode index ranges by prefix
    /// Total: 1792 unique opcodes (256 * 7 prefix groups)
    static constexpr size_t COUNTER_TABLE_SIZE = 1792;
    static constexpr size_t DEFAULT_TRACE_SIZE = 10'000;

    // Prefix index offsets
    static constexpr uint16_t PREFIX_NONE = 0x0000;
    static constexpr uint16_t PREFIX_CB   = 0x00CB;
    static constexpr uint16_t PREFIX_DD   = 0x00DD;
    static constexpr uint16_t PREFIX_ED   = 0x00ED;
    static constexpr uint16_t PREFIX_FD   = 0x00FD;
    static constexpr uint16_t PREFIX_DDCB = 0xDDCB;
    static constexpr uint16_t PREFIX_FDCB = 0xFDCB;

public:
    explicit OpcodeProfiler(EmulatorContext* context);
    ~OpcodeProfiler() = default;

    // Non-copyable
    OpcodeProfiler(const OpcodeProfiler&) = delete;
    OpcodeProfiler& operator=(const OpcodeProfiler&) = delete;

    /// region <Session Control>
    
    /// @brief Start capture session, clears previous data
    void Start();

    /// @brief Pause capturing, data retained
    void Pause();
    
    /// @brief Resume a paused capture session
    void Resume();

    /// @brief Stop capturing, data remains accessible
    void Stop();

    /// @brief Reset all counters and trace buffer
    void Clear();

    /// @brief Check if currently capturing
    bool IsCapturing() const { return _sessionState == ProfilerSessionState::Capturing; }
    
    /// @brief Get current session state
    ProfilerSessionState GetSessionState() const { return _sessionState; }

    /// endregion </Session Control>

    /// region <Data Collection - Hot Path>

    /// @brief Log opcode execution (called from Z80Step hot path)
    /// @param pc Program counter
    /// @param prefix Prefix bytes (0=none)
    /// @param opcode Opcode byte
    /// @param flags F register
    /// @param a A register
    /// @param frame Frame number
    /// @param tState T-state within frame
    void LogExecution(uint16_t pc, uint16_t prefix, uint8_t opcode,
                      uint8_t flags, uint8_t a, uint32_t frame, uint32_t tState);

    /// endregion </Data Collection - Hot Path>

    /// region <Data Retrieval>

    /// @brief Get profiler status
    ProfilerStatus GetStatus() const;

    /// @brief Get total execution count
    uint64_t GetTotalExecutions() const;

    /// @brief Get execution count for specific opcode
    uint64_t GetCount(uint16_t prefix, uint8_t opcode) const;

    /// @brief Get top N opcodes by execution count
    std::vector<OpcodeCounter> GetTopOpcodes(size_t limit = 50) const;

    /// @brief Get counters for specific prefix group
    std::vector<OpcodeCounter> GetByPrefix(uint16_t prefix) const;

    /// @brief Get last N trace entries (newest first)
    std::vector<OpcodeTraceEntry> GetRecentTrace(size_t count = 100) const;

    /// endregion </Data Retrieval>

    /// region <Export>

    /// @brief Save profiler data to YAML file
    bool SaveToFile(const std::string& path) const;

    /// endregion </Export>

private:
    /// @brief Convert prefix + opcode to counter array index (0-1791)
    static size_t ToIndex(uint16_t prefix, uint8_t opcode);

    /// @brief Get prefix group offset in counter array
    static size_t GetPrefixOffset(uint16_t prefix);

    /// @brief Get mnemonic for opcode (uses disassembler)
    std::string GetMnemonic(uint16_t prefix, uint8_t opcode) const;

private:
    EmulatorContext* _context;

    // Capture state
    std::atomic<ProfilerSessionState> _sessionState{ProfilerSessionState::Stopped};

    // Tier 1: Statistical counters (1792 entries, ~14KB)
    std::array<std::atomic<uint64_t>, COUNTER_TABLE_SIZE> _counters{};

    // Tier 2: Ring buffer trace (~160KB at 10K entries)
    std::vector<OpcodeTraceEntry> _traceBuffer;
    std::atomic<size_t> _traceHead{0};
    size_t _traceCount{0};  // Actual entries (up to capacity)

    mutable std::mutex _mutex;  // For thread-safe retrieval
};
