#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "emulator/memory/memoryaccesstracker.h"  // For ProfilerSessionState

class EmulatorContext;

/// @brief Tracks I/O port read/write activity with per-port caller tracking.
/// @details Separate from MemoryAccessTracker (which handles memory R/W/X).
///          Receives port events from the I/O dispatch layer.
///          Gated by Features::kPortTracking.
///
///          Data model: lazy allocation per-port. Only ports that are actually
///          accessed get entries. On a typical ZX Spectrum program, only 5–15
///          ports are used, so memory footprint stays very small (~1–5KB).
class PortTracker
{
public:
    explicit PortTracker(EmulatorContext* context);
    ~PortTracker();

    // === Event Recording (called from I/O dispatch hot path) ===

    /// Record a port read event.
    /// @param port     Full 16-bit port address (Z80 uses all 16 bits for decoding)
    /// @param value    Byte value read from the port
    /// @param callerPC PC of the IN instruction that triggered this read
    void TrackRead(uint16_t port, uint8_t value, uint16_t callerPC);

    /// Record a port write event.
    /// @param port     Full 16-bit port address
    /// @param value    Byte value written to the port
    /// @param callerPC PC of the OUT instruction that triggered this write
    void TrackWrite(uint16_t port, uint8_t value, uint16_t callerPC);

    // === Query API (used by classifiers and reporting) ===

    /// Get total write count for a specific I/O port.
    uint32_t GetWriteCount(uint16_t port) const;

    /// Get total read count for a specific I/O port.
    uint32_t GetReadCount(uint16_t port) const;

    /// Get the set of PCs that wrote to a specific I/O port.
    /// @return Map: caller_PC → write_count. Empty map if port has no writes.
    const std::unordered_map<uint16_t, uint32_t>& GetWriteCallers(uint16_t port) const;

    /// Get the set of PCs that read from a specific I/O port.
    /// @return Map: caller_PC → read_count. Empty map if port has no reads.
    const std::unordered_map<uint16_t, uint32_t>& GetReadCallers(uint16_t port) const;

    /// Get list of all ports that had any activity.
    std::vector<uint16_t> GetActivePorts() const;

    /// Check if a specific port has any recorded activity.
    bool HasActivity(uint16_t port) const;

    /// Per-port summary for reports / CLI.
    struct PortSummary
    {
        uint16_t port;
        uint32_t readCount;
        uint32_t writeCount;
        uint32_t uniqueReadCallers;
        uint32_t uniqueWriteCallers;
    };

    /// Get summaries for all active ports, sorted by port number.
    std::vector<PortSummary> GetPortSummaries() const;

    /// Get summary for a specific port.
    /// @return PortSummary with all zeros if port has no activity.
    PortSummary GetPortSummary(uint16_t port) const;

    /// Get total number of tracked ports.
    size_t GetActivePortCount() const;

    // === Data Value Tracking ===

    /// Get the distribution of values written to a specific port.
    /// @return Map: value → write_count. Empty if no writes.
    const std::unordered_map<uint8_t, uint32_t>& GetWriteValues(uint16_t port) const;

    /// Get the distribution of values read from a specific port.
    /// @return Map: value → read_count. Empty if no reads.
    const std::unordered_map<uint8_t, uint32_t>& GetReadValues(uint16_t port) const;

    // === Lifecycle ===

    /// Reset all tracked data (clears all port entries).
    void Reset();

    /// Check if tracking is active (session is Capturing).
    bool IsActive() const;

    // === Session Control (mirrors MemoryAccessTracker pattern) ===

    /// Start a new tracking session (clears previous data).
    void StartSession();

    /// Pause tracking (retains data).
    void PauseSession();

    /// Resume a paused session.
    void ResumeSession();

    /// Stop tracking (retains data until cleared).
    void StopSession();

    /// Get current session state.
    ProfilerSessionState GetSessionState() const;

    // === Reporting ===

    /// Generate a human-readable port activity report.
    std::string GenerateReport() const;

private:
    [[maybe_unused]] EmulatorContext* _context;
    ProfilerSessionState _sessionState = ProfilerSessionState::Stopped;

    /// Per-port tracking data. Lazy allocation — only ports that are
    /// actually accessed get entries.
    struct PortData
    {
        uint32_t readCount = 0;
        uint32_t writeCount = 0;
        std::unordered_map<uint16_t, uint32_t> readCallers;   // caller_PC → count
        std::unordered_map<uint16_t, uint32_t> writeCallers;  // caller_PC → count
        std::unordered_map<uint8_t, uint32_t> readValues;     // value → count
        std::unordered_map<uint8_t, uint32_t> writeValues;    // value → count
    };
    std::unordered_map<uint16_t, PortData> _ports;

    /// Sentinel empty maps returned by const-ref getters when port has no data.
    static const std::unordered_map<uint16_t, uint32_t> _emptyCallerMap;
    static const std::unordered_map<uint8_t, uint32_t> _emptyValueMap;
};
