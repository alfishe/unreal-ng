#pragma once

#include "debugger/analyzers/ianalyzer.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Forward declarations
class AnalyzerManager;
class Z80;

/// CoverageAnalyzer: executed-address coverage profiler
///
/// Subscribes to CPU step events and records which Z80 addresses have been
/// executed into a 64Kbit map (one bit per address). Intended automation
/// flow: start coverage, run the program, stop, then read covered ranges
/// and uncovered gaps to locate dead code.
///
/// Thread-safety: the map is written from the stepping context (emulation
/// thread or WebAPI step handlers) without locking. Bits are monotonic —
/// only ever set, never cleared — so a concurrent reader observes either 0
/// or 1, never torn data, which is safe for coverage semantics. clear()
/// should be called while the emulator is paused for a clean reset.
class CoverageAnalyzer : public IAnalyzer
{
public:
    CoverageAnalyzer();
    ~CoverageAnalyzer() override;

    // IAnalyzer interface
    std::string getName() const override { return "CoverageAnalyzer"; }
    std::string getUUID() const override { return _uuid; }

    void onActivate(AnalyzerManager* manager) override;
    void onDeactivate() override;

    /// region <Query API (cold path — called from WebAPI)>

    /// Reset all recorded coverage and counters
    void clear();

    /// Number of distinct executed addresses (0..65536)
    size_t getExecutedCount() const;

    /// Number of distinct executed addresses within [start, end]
    size_t getExecutedCountInRange(uint16_t start, uint16_t end) const;

    /// Whether the given address has been executed
    bool isExecuted(uint16_t address) const;

    /// Merge consecutive executed addresses into compact [start, end] ranges
    /// @param maxRanges Truncate the result after this many ranges (0 = unlimited)
    std::vector<std::pair<uint16_t, uint16_t>> getExecutedRanges(size_t maxRanges = 0) const;

    /// Inverse coverage within [start, end]: address gaps never executed
    /// @param maxGaps Truncate the result after this many gaps (0 = unlimited)
    std::vector<std::pair<uint16_t, uint16_t>> getGaps(uint16_t start, uint16_t end, size_t maxGaps = 0) const;

    /// Instructions dispatched since activation or last clear
    uint64_t getInstructionCount() const { return _instructions.load(std::memory_order_relaxed); }

    /// Whether the analyzer is currently recording
    bool isRecording() const { return _recording.load(std::memory_order_relaxed); }

    /// endregion </Query API>

private:
    /// Hot-path CPU step callback (raw function pointer, no capture)
    static void onCPUStep(void* context, Z80* cpu, uint16_t pc);

    std::string _uuid;
    std::atomic<bool> _recording{false};

    /// 64Kbit executed-address map: one bit per Z80 address, MSB-first within each byte
    uint8_t _coverageMap[65536 / 8];

    std::atomic<uint64_t> _instructions{0};
};
