#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "emulator/video/windowview.h"

/// @brief The software screen's timing: it has no screen times, so each new frame is timed when the window paints it.
///
/// A paint of a frame not painted before counts once: presented and shown (the window's paint is all it knows),
/// the frames skipped between two painted ones are dropped, and paint time minus the time asked for (the frame's
/// target; the latch under low-latency video) goes into the last kWindow samples. Repaints of the same frame (an
/// expose, smooth motion's in-between pictures) are not counted. A serial that goes back (a reset, a new machine)
/// starts the sequence again. Painted runs on the GUI thread, Fill on whichever thread reads the report.
class PaintTiming
{
public:
    static constexpr size_t kWindow = 600;  ///< the paints the percentiles cover (as the native presenters)

    /// The frame `serial` (0: a source without serials, not counted), due at `targetNs`, painted at `paintNs`
    void Painted(uint64_t serial, int64_t targetNs, int64_t paintNs)
    {
        if (serial == 0 || targetNs <= 0)
            return;
        std::lock_guard<std::mutex> lock(_mutex);
        if (serial == _lastSerial)
            return;
        if (_lastSerial != 0 && serial > _lastSerial)
            _dropped += serial - _lastSerial - 1;
        _lastSerial = serial;
        _painted++;
        const double ms = static_cast<double>(paintNs - targetNs) / 1e6;
        if (_errors.size() < kWindow)
            _errors.push_back(ms);
        else
            _errors[_next] = ms;
        _next = (_next + 1) % kWindow;
    }

    void Reset()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _lastSerial = 0;
        _painted = 0;
        _dropped = 0;
        _errors.clear();
        _next = 0;
    }

    /// presented, shown, dropped, error p50 / p99 / max and timedAt "paint" (nothing while no frame was painted)
    void Fill(WindowPresentStats& out) const
    {
        std::vector<double> errors;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_painted == 0)
                return;
            out.presented = out.shown = _painted;
            out.dropped = _dropped;
            errors = _errors;
        }
        std::sort(errors.begin(), errors.end());
        const auto at = [&errors](double q) {
            const size_t i = static_cast<size_t>(q * static_cast<double>(errors.size() - 1) + 0.5);
            return errors[std::min(i, errors.size() - 1)];
        };
        out.errorP50Ms = at(0.50);
        out.errorP99Ms = at(0.99);
        out.errorMaxMs = errors.back();
        out.timedAt = "paint";
    }

private:
    mutable std::mutex _mutex;
    uint64_t _lastSerial = 0;
    uint64_t _painted = 0;
    uint64_t _dropped = 0;
    std::vector<double> _errors;  ///< ms, the last kWindow paints
    size_t _next = 0;
};
