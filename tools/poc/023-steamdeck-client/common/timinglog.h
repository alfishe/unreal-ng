#pragma once

// Timing helpers shared by the Steam Deck POCs: a CSV event log under ~/steamdeck-poc-logs/
// (Game Mode has no visible stdout) and simple interval statistics.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace deckpoc
{

/// Monotonic nanoseconds (SDL_GetTicksNS)
uint64_t NowNs();

/// ~/steamdeck-poc-logs/<poc>-<yyyymmdd-hhmmss>.<ext>; creates the folder
std::string LogPath(const std::string& poc, const std::string& ext);

class CsvLog
{
public:
    CsvLog() = default;
    ~CsvLog();
    CsvLog(const CsvLog&) = delete;
    CsvLog& operator=(const CsvLog&) = delete;

    bool Open(const std::string& path, const std::string& header);
    /// One line: "<t_ns>,<fields>"
    void Row(uint64_t tNs, const char* fmt, ...);
    void Flush();
    const std::string& Path() const { return _path; }

private:
    std::FILE* _file = nullptr;
    std::string _path;
};

struct IntervalStats
{
    size_t count = 0;
    double meanMs = 0;
    double medianMs = 0;
    double p01Ms = 0;
    double p99Ms = 0;
    double minMs = 0;
    double maxMs = 0;
    double stddevMs = 0;
};

/// Statistics over intervals (in ns) between consecutive timestamps
IntervalStats ComputeIntervals(const std::vector<uint64_t>& timestampsNs);
/// Statistics over plain samples (in ns)
IntervalStats ComputeSamples(std::vector<uint64_t> samplesNs);

std::string Describe(const IntervalStats& s);

} // namespace deckpoc
