#include "timinglog.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <ctime>
#include <filesystem>

namespace deckpoc
{

uint64_t NowNs()
{
    return SDL_GetTicksNS();
}

std::string LogPath(const std::string& poc, const std::string& ext)
{
    const char* home = SDL_getenv("HOME");
    if (!home)
        home = SDL_getenv("USERPROFILE");   // Windows
    std::filesystem::path dir = std::filesystem::path(home ? home : ".") / "steamdeck-poc-logs";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
    return (dir / (poc + "-" + stamp + "." + ext)).string();
}

CsvLog::~CsvLog()
{
    if (_file)
        std::fclose(_file);
}

bool CsvLog::Open(const std::string& path, const std::string& header)
{
    _file = std::fopen(path.c_str(), "w");
    if (!_file)
        return false;
    _path = path;
    std::fprintf(_file, "t_ns,%s\n", header.c_str());
    return true;
}

void CsvLog::Row(uint64_t tNs, const char* fmt, ...)
{
    if (!_file)
        return;
    std::fprintf(_file, "%llu,", static_cast<unsigned long long>(tNs));
    va_list args;
    va_start(args, fmt);
    std::vfprintf(_file, fmt, args);
    va_end(args);
    std::fputc('\n', _file);
}

void CsvLog::Flush()
{
    if (_file)
        std::fflush(_file);
}

IntervalStats ComputeSamples(std::vector<uint64_t> samplesNs)
{
    IntervalStats s;
    s.count = samplesNs.size();
    if (samplesNs.empty())
        return s;

    std::sort(samplesNs.begin(), samplesNs.end());
    auto at = [&](double q) { return samplesNs[static_cast<size_t>(q * static_cast<double>(samplesNs.size() - 1))] / 1e6; };

    double sum = 0;
    for (uint64_t v : samplesNs)
        sum += static_cast<double>(v);
    s.meanMs = sum / static_cast<double>(samplesNs.size()) / 1e6;

    double var = 0;
    for (uint64_t v : samplesNs)
    {
        const double d = static_cast<double>(v) / 1e6 - s.meanMs;
        var += d * d;
    }
    s.stddevMs = std::sqrt(var / static_cast<double>(samplesNs.size()));
    s.medianMs = at(0.5);
    s.p01Ms = at(0.01);
    s.p99Ms = at(0.99);
    s.minMs = samplesNs.front() / 1e6;
    s.maxMs = samplesNs.back() / 1e6;
    return s;
}

IntervalStats ComputeIntervals(const std::vector<uint64_t>& timestampsNs)
{
    std::vector<uint64_t> deltas;
    for (size_t i = 1; i < timestampsNs.size(); i++)
        deltas.push_back(timestampsNs[i] - timestampsNs[i - 1]);
    return ComputeSamples(std::move(deltas));
}

std::string Describe(const IntervalStats& s)
{
    char buf[256];
    std::snprintf(buf, sizeof buf, "n=%zu mean=%.3f median=%.3f p1=%.3f p99=%.3f min=%.3f max=%.3f sd=%.3f ms",
                  s.count, s.meanMs, s.medianMs, s.p01Ms, s.p99Ms, s.minMs, s.maxMs, s.stddevMs);
    return buf;
}

} // namespace deckpoc
