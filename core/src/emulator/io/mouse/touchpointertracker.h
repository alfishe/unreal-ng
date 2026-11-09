#pragma once

#include <cmath>
#include <cstdint>

/// A touchscreen as a relative pointer, the way a laptop's pad is one: the finger's travel since its last position
/// moves the cursor, where the finger lands or rests means nothing, a short tap is a click.
///
/// Why it exists: a touchscreen reaches the application as an ABSOLUTE pointer (the Steam Deck's gamescope composes it
/// as pointer motion plus a button press: no touch events). The capture backend that warps the pointer to the centre
/// of the window and reads the travel from it works for a mouse only; a finger stays where it is, and every touch or
/// drag reads as "finger minus centre", a leap across the screen. Here the position is taken from the last event of
/// the same contact, never from a warp.
///
/// Front-end independent (no Qt), unit-tested in core-tests. Coordinates are logical pixels, time is milliseconds.
class TouchPointerTracker
{
public:
    /// A contact whose first move is this far from where it went down, or any move this far from the last one, is a
    /// report catching up with the touch (the pointer jumps to the finger around the press), not travel: it moves the
    /// anchor, not the cursor
    static constexpr double kMaxStepPx = 250.0;
    /// A contact that stays within this radius of where it went down and ends within kTapMaxMs is a tap
    static constexpr double kTapSlopPx = 14.0;
    static constexpr int64_t kTapMaxMs = 400;

    struct Travel
    {
        double dx = 0.0;
        double dy = 0.0;
    };

    bool IsDown() const { return _down; }

    void Reset() { *this = TouchPointerTracker(); }

    /// The finger goes down (the button press)
    void Down(double x, double y, int64_t nowMs)
    {
        _down = true;
        _moved = false;
        _x = _startX = x;
        _y = _startY = y;
        _startMs = nowMs;
    }

    /// The finger moves: the travel since its last position, zero when it is not down or the step is a catch-up
    Travel Move(double x, double y)
    {
        if (!_down)
            return {};
        const double dx = x - _x;
        const double dy = y - _y;
        _x = x;
        _y = y;
        if (std::hypot(dx, dy) > kMaxStepPx)
        {
            // The pointer jumped to the finger: anchor there, and count the jump as no travel at all
            _startX = x;
            _startY = y;
            return {};
        }
        if (!_moved && std::hypot(x - _startX, y - _startY) > kTapSlopPx)
            _moved = true;
        return {dx, dy};
    }

    /// The finger lifts: true when the contact was a tap (a click)
    bool Up(int64_t nowMs)
    {
        const bool tap = _down && !_moved && nowMs - _startMs <= kTapMaxMs;
        _down = false;
        _moved = false;
        return tap;
    }

private:
    bool _down = false;
    bool _moved = false;
    double _x = 0.0;
    double _y = 0.0;
    double _startX = 0.0;
    double _startY = 0.0;
    int64_t _startMs = 0;
};
