#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

/// The native presenters' adaptive lead (NativePresenter::LeadNs; frame presenter design, section 6.3): the margin the
/// window draws a frame ahead of its time. A present shown a refresh late widens it by a step when it was handed over
/// less than a refresh before its time (the window was late: more lead helps); one handed over earlier and still late
/// was held by the system (the compositor a refresh behind under load), and more lead would only queue more presents.
/// A long run on time narrows it by one, within the bounds. The first vsync a present can make is less than a refresh after the time
/// asked (smooth motion asks half a refresh before its vsync), so more than a refresh (plus 1 ms of jitter) is a
/// refresh late. Only presents with a time of their own count: one at once (low-latency video, a repaint) shows at the
/// first refresh it makes, its submit somewhere in a refresh; judged by the submit its "late" was the compositor's
/// deadline and the backend's submit point (Vulkan's before the GPU work, GL's after it; Steam Deck, 2026-11-22), and
/// the margin it widened is not used at once anyway.
///
/// The screen times of some systems come a constant little after the vblank (gamescope's are its composition's:
/// 1.5-2 ms on the Steam Deck, where frames shown at the first refresh after their time read 0.3 to 18.7 ms at
/// 16.7 ms a refresh, 4 % of them "late", and the margin sat at its bound: smooth motion then drew every other
/// refresh). That offset is measured, the low end of the errors of the presents on time (their 5th percentile over
/// the last kOffsetWindow, up to kMaxOffsetNs), and a refresh late is a refresh more than it.
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
    static constexpr int64_t kMaxOffsetNs = 3'000'000;
    static constexpr size_t kOffsetWindow = 64;
    /// A present shown this long after its time was held by a gap (sleep, a display off), not drawn late
    static constexpr int64_t kGapNs = 100'000'000;

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
        const bool late = errorNs > period + OffsetNs() + kJitterNs;
        NoteLag(present, late && errorNs <= kGapNs ? static_cast<uint32_t>(errorNs / period) : 0, late,
                errorNs > kGapNs);
        if (late)
        {
            _late.fetch_add(1, std::memory_order_relaxed);
            // Shown seconds after its time: the display was asleep or off, the system held the presents (a gap in
            // time, not a window drawing late). Counted, the margin and the run on time left as they were
            if (errorNs > kGapNs)
                return;
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
        NoteOnTime(errorNs);
        if (present > _lastOnTime.load(std::memory_order_relaxed))
            _lastOnTime.store(present, std::memory_order_relaxed);
        if (_onTimeRun.fetch_add(1, std::memory_order_relaxed) + 1 >= kOnTimeRun)
        {
            _onTimeRun.store(0, std::memory_order_relaxed);
            _marginNs.store(std::max(margin - kStepNs, kMinNs), std::memory_order_relaxed);
        }
    }

    uint64_t Late() const { return _late.load(std::memory_order_relaxed); }
    /// Presents shown at another lag (in refreshes after their own) than the present before them: the screen held a
    /// picture a refresh more, or skipped one. A run all a refresh late, one every refresh, is no step: the compositor
    /// a refresh behind for a while shows every picture evenly, a refresh later (the owner's Mac, 2026-11-27: whole
    /// runs of 40 s with every present 25 ms after its time, at 8.5 ms in the next run of the same build), and smooth
    /// motion's pictures move as evenly as on time. Its first present late and the one back on time are a step each.
    /// A present without a number, one not after the present before it or one held by a gap: a step when late
    uint64_t Steps() const { return _steps.load(std::memory_order_relaxed); }
    /// The number of the newest present shown a refresh late (0: none)
    uint64_t LastLate() const { return _lastLate.load(std::memory_order_relaxed); }
    /// LastLate's lag in refreshes (1: shown at the refresh after its own)
    uint32_t LastLateLag() const { return _lastLateLag.load(std::memory_order_relaxed); }
    /// The number of the newest present shown on time (0: none)
    uint64_t LastOnTime() const { return _lastOnTime.load(std::memory_order_relaxed); }
    int64_t MarginNs() const { return _marginNs.load(std::memory_order_relaxed); }
    /// The screen times' constant offset after the vblank, as measured (kMaxOffsetNs until kOffsetWindow presents came
    /// on time: the first presents are not judged tighter than the system may turn out to be)
    int64_t OffsetNs() const { return _offsetNs.load(std::memory_order_relaxed); }
    int64_t PeriodNs() const { return _periodNs.load(std::memory_order_relaxed); }

private:
    /// Shown's thread: a present's lag (0: on time) against the one before it
    void NoteLag(uint64_t present, uint32_t lag, bool late, bool gap)
    {
        const bool follows = present != 0 && !gap && _lagPresent != 0 && present == _lagPresent + 1;
        if (follows ? lag != _lag : late)
            _steps.fetch_add(1, std::memory_order_relaxed);
        _lagPresent = gap ? 0 : present;
        _lag = lag;
    }
    /// Shown's thread: a present on time, its error into the window the offset is taken from
    void NoteOnTime(int64_t errorNs)
    {
        _errors[_errorNext] = std::max<int64_t>(errorNs, 0);
        _errorNext = (_errorNext + 1) % kOffsetWindow;
        if (_errorCount < kOffsetWindow)
        {
            _errorCount++;
            return;
        }
        if (_errorNext % 8 != 0)  // every eighth present is often enough
            return;
        std::array<int64_t, kOffsetWindow> sorted = _errors;
        std::nth_element(sorted.begin(), sorted.begin() + kOffsetWindow / 20, sorted.end());
        _offsetNs.store(std::min(sorted[kOffsetWindow / 20], kMaxOffsetNs), std::memory_order_relaxed);
    }

    std::array<int64_t, kOffsetWindow> _errors{};  ///< Shown's thread
    size_t _errorNext = 0;
    size_t _errorCount = 0;
    std::atomic<int64_t> _offsetNs{kMaxOffsetNs};  ///< the most allowed until it is measured
    std::atomic<int64_t> _marginNs;
    std::atomic<int64_t> _periodNs{0};
    std::atomic<uint64_t> _late{0};
    std::atomic<uint64_t> _steps{0};
    uint64_t _lagPresent = 0;  ///< Shown's thread: the present the lag is of (0: none to follow)
    uint32_t _lag = 0;
    std::atomic<uint64_t> _lastLate{0};
    std::atomic<uint32_t> _lastLateLag{0};
    std::atomic<uint64_t> _lastOnTime{0};
    std::atomic<uint32_t> _onTimeRun{0};
};
