#pragma once

/// @file reporttap.h
/// @brief The raw input reports of one device for the test panel (ui-ux.md §3.4): a ring of the newest reports, kept
/// only while someone reads them. A backend calls Record from its report callback; with no reader that costs one
/// relaxed atomic load (Armed). Reading (RawReports) starts the tap, kIdleMs after the last read it stops again. Host-only: the
/// reports never reach the machine, a replay or a snapshot.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "hostbackend.h"

namespace controllers
{

class ReportTap
{
public:
    static constexpr size_t kReports = 16;     ///< the ring
    static constexpr size_t kBytes = 64;       ///< kept of each report (a DualSense Bluetooth report is 78)
    static constexpr uint64_t kIdleMs = 3000;  ///< the tap stops this long after the last read

    /// Someone has read and the tap has not stopped since: the report callback checks this before it takes the time
    bool Armed() const
    {
        return _readMs.load(std::memory_order_relaxed) != 0;
    }
    /// From the report callback; past kIdleMs after the last read it disarms the tap
    void Record(const uint8_t* report, size_t size, uint64_t nowMs);
    /// The newest reports, oldest first; starts (or keeps) the tap
    std::vector<RawReport> Read(uint64_t nowMs);
    /// Someone read within kIdleMs
    bool Tapping(uint64_t nowMs) const;

private:
    std::atomic<uint64_t> _readMs{0};  ///< the last read + 1; 0 = never read
    std::mutex _mutex;
    std::array<RawReport, kReports> _ring;
    size_t _next = 0;
    size_t _count = 0;
};

/// "20 00 1c 00 ...": the bytes kept, two hex digits each
std::string RawReportHex(const RawReport& report);

}  // namespace controllers
