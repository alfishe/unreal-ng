#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

/// The native presenters' adaptive lead (NativePresenter::LeadNs; frame presenter design, section 6.3): the margin the
/// window draws a frame ahead of its time. A present shown a refresh late widens it by a step when it was handed over
/// less than a refresh before its time (the window was late: more lead helps); one handed over earlier and still late
/// was held by the system (the compositor a refresh behind under load), and more lead would only queue more presents.
/// A long run on time narrows it by one, within the bounds. The first vsync a present can make is less than a refresh after the time
/// asked (smooth motion asks half a refresh before its vsync), so more than a refresh (plus 1 ms of jitter) is a
/// refresh late.
///
/// Shown from the presenter's completion thread, Margin from the drawing one.
class PresentLead
{
public:
    static constexpr int64_t kMinNs = 4'000'000;
    static constexpr int64_t kMaxNs = 20'000'000;
    static constexpr int64_t kStepNs = 1'000'000;
    static constexpr uint32_t kOnTimeRun = 600;
    static constexpr int64_t kJitterNs = 1'000'000;

    explicit PresentLead(int64_t startNs) : _marginNs(std::clamp(startNs, kMinNs, kMaxNs)) {}

    /// The margin now; `refreshPeriodNs` (the window's) is what Shown measures a refresh by
    int64_t Margin(int64_t refreshPeriodNs)
    {
        _periodNs.store(refreshPeriodNs, std::memory_order_relaxed);
        return _marginNs.load(std::memory_order_relaxed);
    }

    /// A present on screen `errorNs` after the time asked for; `present` its number (1 on, in the order handed over;
    /// 0: not counted), LastLate's and LastOnTime's; `aheadNs` how long before the time asked it was handed over (< 0:
    /// not known, a late one widens)
    void Shown(int64_t errorNs, uint64_t present = 0, int64_t aheadNs = -1)
    {
        const int64_t period = _periodNs.load(std::memory_order_relaxed);
        if (period <= 0)
            return;
        const int64_t margin = _marginNs.load(std::memory_order_relaxed);
        if (errorNs > period + kJitterNs)
        {
            _late.fetch_add(1, std::memory_order_relaxed);
            if (present > _lastLate.load(std::memory_order_relaxed))
            {
                _lastLate.store(present, std::memory_order_relaxed);
                _lastLateLag.store(static_cast<uint32_t>(errorNs / period), std::memory_order_relaxed);
            }
            _onTimeRun.store(0, std::memory_order_relaxed);
            if (aheadNs < period)
                _marginNs.store(std::min(margin + kStepNs, kMaxNs), std::memory_order_relaxed);
            return;
        }
        if (present > _lastOnTime.load(std::memory_order_relaxed))
            _lastOnTime.store(present, std::memory_order_relaxed);
        if (_onTimeRun.fetch_add(1, std::memory_order_relaxed) + 1 >= kOnTimeRun)
        {
            _onTimeRun.store(0, std::memory_order_relaxed);
            _marginNs.store(std::max(margin - kStepNs, kMinNs), std::memory_order_relaxed);
        }
    }

    uint64_t Late() const { return _late.load(std::memory_order_relaxed); }
    /// The number of the newest present shown a refresh late (0: none)
    uint64_t LastLate() const { return _lastLate.load(std::memory_order_relaxed); }
    /// LastLate's lag in refreshes (1: shown at the refresh after its own)
    uint32_t LastLateLag() const { return _lastLateLag.load(std::memory_order_relaxed); }
    /// The number of the newest present shown on time (0: none)
    uint64_t LastOnTime() const { return _lastOnTime.load(std::memory_order_relaxed); }
    int64_t MarginNs() const { return _marginNs.load(std::memory_order_relaxed); }
    int64_t PeriodNs() const { return _periodNs.load(std::memory_order_relaxed); }

private:
    std::atomic<int64_t> _marginNs;
    std::atomic<int64_t> _periodNs{0};
    std::atomic<uint64_t> _late{0};
    std::atomic<uint64_t> _lastLate{0};
    std::atomic<uint32_t> _lastLateLag{0};
    std::atomic<uint64_t> _lastOnTime{0};
    std::atomic<uint32_t> _onTimeRun{0};
};
