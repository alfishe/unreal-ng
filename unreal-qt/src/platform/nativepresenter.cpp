#include "platform/nativepresenter.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>

#if !defined(__APPLE__) && !defined(_WIN32) && !defined(__linux__)
// No native presenter here: the window presents with GL
std::unique_ptr<NativePresenter> NativePresenter::Create(void*)
{
    return nullptr;
}
#endif

#ifndef __linux__
int NativePresenter::PresentFeedback()
{
#if defined(__APPLE__) || defined(_WIN32)
    return 1;
#else
    return 0;
#endif
}
#endif

namespace presentstats
{
void Ring::Add(double value)
{
    if (_values.size() < kWindow)
        _values.push_back(value);
    else
        _values[_next] = value;
    _next = (_next + 1) % kWindow;
}

namespace
{
double SortedAt(const std::vector<double>& sorted, double q)
{
    if (sorted.empty())
        return 0.0;
    const size_t i = static_cast<size_t>(q * static_cast<double>(sorted.size() - 1) + 0.5);
    return sorted[std::min(i, sorted.size() - 1)];
}
}  // namespace

double Percentile(std::vector<double> values, double q)
{
    std::sort(values.begin(), values.end());
    return SortedAt(values, q);
}

void Fill(NativePresenter::Stats& s, std::vector<double> errors, std::vector<double> gpuWaits,
          std::vector<double> drawableWaits)
{
    std::sort(errors.begin(), errors.end());
    std::sort(gpuWaits.begin(), gpuWaits.end());
    std::sort(drawableWaits.begin(), drawableWaits.end());
    s.window = static_cast<uint32_t>(errors.size());
    s.errorP50 = SortedAt(errors, 0.50);
    s.errorP99 = SortedAt(errors, 0.99);
    s.errorMax = errors.empty() ? 0.0 : errors.back();
    s.gpuWaitP50 = SortedAt(gpuWaits, 0.50);
    s.gpuWaitMax = gpuWaits.empty() ? 0.0 : gpuWaits.back();
    s.drawableWaitP50 = SortedAt(drawableWaits, 0.50);
    s.drawableWaitMax = drawableWaits.empty() ? 0.0 : drawableWaits.back();
}

Trace::Trace()
{
    const char* path = std::getenv("UNREAL_PRESENT_TRACE");
    if (!path || !*path)
        return;
    static std::atomic<bool> started{false};
    _file = std::fopen(path, started.exchange(true) ? "a" : "w");
}

Trace::~Trace()
{
    if (_file)
        std::fclose(_file);
}

void Trace::Write(const NativePresenter::TraceInfo& info, int64_t atNs, int64_t submittedNs, int64_t screenNs,
                  int64_t drawableWaitNs)
{
    if (!_file)
        return;
    std::fprintf(_file, "%llu %lld %lld %lld %lld %lld %lld %lld\n", static_cast<unsigned long long>(info.serial),
                 static_cast<long long>(atNs), static_cast<long long>(submittedNs), static_cast<long long>(screenNs),
                 static_cast<long long>(info.latchNs), static_cast<long long>(info.plannedNs),
                 static_cast<long long>(info.wokeNs), static_cast<long long>(drawableWaitNs));
}
}  // namespace presentstats
