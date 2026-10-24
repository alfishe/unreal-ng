#include "hid/reporttap.h"

#include <algorithm>

namespace controllers
{

bool ReportTap::Tapping(uint64_t nowMs) const
{
    const uint64_t read = _readMs.load(std::memory_order_relaxed);
    return read != 0 && nowMs + 1 <= read + kIdleMs;
}

void ReportTap::Record(const uint8_t* report, size_t size, uint64_t nowMs)
{
    if (report == nullptr)
        return;
    if (!Tapping(nowMs))
    {
        // Idle: disarm, unless a read came in between
        uint64_t read = _readMs.load(std::memory_order_relaxed);
        if (read != 0 && nowMs + 1 > read + kIdleMs)
            _readMs.compare_exchange_strong(read, 0, std::memory_order_relaxed);
        return;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    RawReport& slot = _ring[_next];
    slot.timeMs = nowMs;
    slot.size = size;
    slot.bytes.assign(report, report + std::min(size, kBytes));
    _next = (_next + 1) % kReports;
    _count = std::min(_count + 1, kReports);
}

std::string RawReportHex(const RawReport& report)
{
    static const char kDigits[] = "0123456789abcdef";
    std::string text;
    text.reserve(report.bytes.size() * 3);
    for (const uint8_t byte : report.bytes)
    {
        if (!text.empty())
            text += ' ';
        text += kDigits[byte >> 4];
        text += kDigits[byte & 0x0F];
    }
    return text;
}

std::vector<RawReport> ReportTap::Read(uint64_t nowMs)
{
    _readMs.store(nowMs + 1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<RawReport> reports;
    reports.reserve(_count);
    for (size_t i = 0; i < _count; i++)
        reports.push_back(_ring[(_next + kReports - _count + i) % kReports]);
    return reports;
}

}  // namespace controllers
