#pragma once

#include <algorithm>
#include <cstdint>

/// @brief When the software screen paints: at each frame's time, and not when a frame comes while that paint waits;
/// under smooth motion a refresh after each paint, and when a frame comes too.
///
/// The screen asks for a frame by time: a frame that comes is not shown before its time (the audio clock's target, a
/// few frames on), so painting when it comes shows the frame on screen again. A paint after which a later frame waits
/// schedules the next one at that frame's target; while it waits, a frame that comes paints nothing (half the paints
/// and 40 % of the process's CPU on a Pentagon demo, the paint a ms or two later).
///
/// Smooth motion paints a refresh after each paint and when a frame comes, as before: a paint once a refresh on a grid
/// of refreshes drew fewer pictures (59 a second, not 92) for the same CPU (the interpolation costs it, not the
/// paints), and found each new frame 3.6 ms later at the median, 13 ms at p99 (A/B on an M3 Ultra, 4K at 60 Hz).
/// Wholly on the GUI thread.
class PaintPacer
{
public:
    static constexpr int64_t kMaxWaitMs = 100;  ///< the longest wait for a frame's target

    /// A frame came (or the picture changed otherwise): true = paint now, false = the paint waiting shows it
    bool PaintNow() const { return !_scheduled || _smooth; }

    /// After a paint that left a later frame waiting (`nextTargetNs`): the wait in ms for the next paint, or -1 when one
    /// already waits. `smooth`: a refresh (`periodNs`) on, whole ms
    int64_t Schedule(int64_t nowNs, bool smooth, int64_t periodNs, int64_t nextTargetNs)
    {
        if (_scheduled)
            return -1;
        _scheduled = true;
        _smooth = smooth && periodNs > 0;
        if (_smooth)
            return std::max<int64_t>(1, periodNs / 1'000'000);
        return std::clamp<int64_t>((nextTargetNs - nowNs + 999'999) / 1'000'000, 1, kMaxWaitMs);
    }

    /// The waiting paint is due: it paints now
    void Fired() { _scheduled = false; }

    /// No frames any more (the source detached): nothing waits
    void Reset()
    {
        _scheduled = false;
        _smooth = false;
    }

    bool Scheduled() const { return _scheduled; }

private:
    bool _scheduled = false;
    bool _smooth = false;  ///< the waiting paint is smooth motion's: a frame that comes paints all the same
};
