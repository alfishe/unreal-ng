#pragma once

/// @file processstats.h
/// @brief The host process's own resource use, cross-platform: CPU time, resident and peak memory, thread count
/// (macOS: getrusage + mach task_info / task_threads; Linux: getrusage + /proc/self/status; Windows:
/// GetProcessTimes, GetProcessMemoryInfo, a Toolhelp thread walk). Process-wide, never per emulator instance.
///
/// SampleProcess() reads the counters once (tens of microseconds; the Windows thread walk is the dearest). CPU % needs
/// two samples: ProcessStatsSampler keeps the previous one and answers from a cache younger than its interval, so a
/// 50 Hz reader (a WebSocket topic) samples the OS at most twice a second.

#include <chrono>
#include <cstdint>
#include <mutex>

/// One reading of the process's counters
struct ProcessSample
{
    bool ok = false;               ///< the platform answered (false: every field is 0)
    uint64_t cpuTimeUs = 0;        ///< user + system CPU time of every thread since the process started
    uint64_t userTimeUs = 0;
    uint64_t systemTimeUs = 0;
    uint64_t residentBytes = 0;    ///< resident set (Windows: the working set)
    uint64_t peakResidentBytes = 0;  ///< the resident set's high-water mark (Windows: the peak working set)
    uint64_t virtualBytes = 0;     ///< virtual size (Windows: the private commit)
    uint32_t threads = 0;          ///< threads of the process now (0: not known)
};

/// The counters now
ProcessSample SampleProcess();

/// Logical processors of the host (1 when not known)
uint32_t HostLogicalCores();

/// What the monitoring planes show: a sample plus the CPU use over the last interval
struct ProcessStats
{
    ProcessSample sample;
    /// CPU use over the last interval, 100 = one logical core busy (may exceed 100 with several threads)
    double cpuPercent = 0;
    /// cpuPercent / logical cores: 100 = the whole host
    double cpuPercentOfHost = 0;
    uint32_t cores = 1;
    double uptimeSeconds = 0;     ///< since the sampler was created (the process start, for the process's own one)
    double intervalSeconds = 0;   ///< the window cpuPercent covers (0: the first sample, no CPU % yet)
    uint64_t samples = 0;         ///< OS readings taken so far
};

class ProcessStatsSampler
{
public:
    using Clock = std::chrono::steady_clock;
    static constexpr double kDefaultIntervalSeconds = 0.5;

    /// The process's sampler (created on first use: its uptime counts from then; the WebAPI touches it at start)
    static ProcessStatsSampler& Instance();

    explicit ProcessStatsSampler(double intervalSeconds = kDefaultIntervalSeconds);

    /// The cached stats when younger than the interval, else a new sample (thread-safe, a short lock)
    ProcessStats Read();
    /// The same at a given time with a given sample (tests: no OS, no clock)
    ProcessStats ReadAt(Clock::time_point now, const ProcessSample& sample);

private:
    double _interval;
    Clock::time_point _created;
    std::mutex _mutex;
    bool _have = false;
    Clock::time_point _sampledAt{};
    ProcessSample _last;   ///< the sample the current CPU % ends at
    ProcessStats _stats;
    uint32_t _cores = 1;
};
