#pragma once

#include <algorithm>
#include <cstdint>

/// The audio ring's setpoint under the feature lowlatency, adapted to what the host can hold.
///
/// It starts above the floor (a fresh host has not shown its jitter yet: the first gap came within a minute on a loaded
/// Steam Deck at 10 ms) and reads the device's ring statistics
/// (AudioDeviceDescriptor: the least occupancy per one-second window, the gap count):
/// - a gap raises it by kRaiseOnGapMs at once;
/// - a window whose least occupancy fell below kGuardMs raises it by kRaiseOnLowMs (the gap's warning);
/// - after a raise, kHoldoffWindows windows raise nothing more: the ring needs that long to fill to the new setpoint
///   through the +-0.5 % trim, and its low troughs meanwhile are not news;
/// - kLowerAfterWindows windows in a row with the least occupancy at kGuardMs + kLowerMarginMs or more lower it by
///   kLowerMs, never below the floor (from the start a quiet host reaches the floor in about 2.5 minutes; 10 quiet seconds per step brought back gaps: a loaded Steam Deck had 1-2 per 10 min and the setpoint swung up to 27 ms).
/// The ceiling is the default setpoint: a host that cannot hold even that gets the default behaviour.
/// Runs on the emulation thread (one Update per frame); the target is read through the owner's atomic.
class LowLatencyTarget
{
public:
    static constexpr double kGuardMs = 2.0;
    static constexpr double kRaiseOnGapMs = 5.0;
    static constexpr double kRaiseOnLowMs = 2.0;
    static constexpr double kLowerMs = 1.0;
    static constexpr double kLowerMarginMs = 4.0;
    static constexpr uint32_t kHoldoffWindows = 3;
    static constexpr uint32_t kLowerAfterWindows = 30;
    static constexpr uint32_t kNoTrough = UINT32_MAX;

    /// startMs is clamped to [floorMs, ceilingMs]
    LowLatencyTarget(double startMs, double floorMs, double ceilingMs)
        : _start(std::clamp(startMs, floorMs, ceilingMs)), _floor(floorMs), _ceiling(ceilingMs), _target(_start)
    {
    }

    /// One step. active: the feature is on and the device reports; windows / troughFrames / underruns: the device's
    /// counters now (troughFrames of the last closed window, kNoTrough before the first); rate: device frames a second.
    /// Inactive: the target rests at the start and the counters are only followed, so turning on starts afresh
    double Update(bool active, uint64_t windows, uint32_t troughFrames, uint64_t underruns, uint32_t rate)
    {
        if (!active || !_seeded)
        {
            _lastWindows = windows;
            _lastUnderruns = underruns;
            _healthy = 0;
            _holdoff = 0;
            _seeded = active;
            if (!active)
                _target = _start;
            return _target;
        }

        const bool gap = underruns > _lastUnderruns;
        const bool window = windows > _lastWindows;
        _lastUnderruns = underruns;
        _lastWindows = windows;

        if (window && _holdoff > 0)
            _holdoff--;

        if (gap)
        {
            Raise(kRaiseOnGapMs);
            return _target;
        }
        if (!window || troughFrames == kNoTrough || rate == 0)
            return _target;

        const double troughMs = static_cast<double>(troughFrames) * 1000.0 / static_cast<double>(rate);
        if (troughMs < kGuardMs)
        {
            Raise(kRaiseOnLowMs);
        }
        else if (troughMs >= kGuardMs + kLowerMarginMs)
        {
            if (++_healthy >= kLowerAfterWindows)
            {
                _target = std::max(_floor, _target - kLowerMs);
                _healthy = 0;
            }
        }
        else
        {
            _healthy = 0;  // close to the guard: hold
        }
        return _target;
    }

    double Target() const { return _target; }
    uint64_t Raises() const { return _raises; }

private:
    void Raise(double ms)
    {
        _healthy = 0;
        if (_holdoff > 0)
            return;
        const double raised = std::min(_ceiling, _target + ms);
        if (raised > _target)
            _raises++;
        _target = raised;
        _holdoff = kHoldoffWindows + 1;  // counted down at each window from the next one: kHoldoffWindows blocked
    }

    double _start;
    double _floor;
    double _ceiling;
    double _target;
    uint64_t _lastWindows = 0;
    uint64_t _lastUnderruns = 0;
    uint64_t _raises = 0;
    uint32_t _healthy = 0;
    uint32_t _holdoff = 0;
    bool _seeded = false;
};
