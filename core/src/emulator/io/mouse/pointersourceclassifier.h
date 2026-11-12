#pragma once

#include <cmath>
#include <cstdint>

/// Tells a real mouse from a touchscreen when both reach the application as the same absolute pointer.
///
/// Why it exists: the Steam Deck's gamescope composes the touchscreen as pointer motion plus a button press (no touch
/// events), and Xwayland puts every pointer, the touchscreen's and a USB / Bluetooth mouse's, on one X11 device, so
/// neither the event type nor the device id says which one moved. The two do behave differently, and that is what
/// this class reads (nothing about the device):
///   - a touch never moves the pointer without a button: the pointer jumps to the finger once (a single "hover" move,
///     often a leap across the screen) and the contact starts, then moves with the button held;
///   - a mouse hovers: a run of small steps with no button held, and a press comes where the pointer already is.
///
/// Rules, each event judged on its own so a mouse and a finger can alternate in one session:
///   - Hover moves (no button): a run of at least kMouseHoverMoves of them, none a leap, is a mouse. The first ones
///     are held back and delivered with the confirming one (a finger's landing never moves the guest).
///   - A press that follows a leap (the pointer jumping to the finger) is a touch; so is a leap while the button is
///     held (the press arrived first, the pointer catches up with the finger afterwards).
///   - A right or middle button, or the wheel, is a mouse (a touch only has the left button).
/// While the source is a mouse, a leap in a hover is held for kDeferMs: if a press follows it was a finger landing and
/// the leap is dropped, otherwise it was a fast flick and it is delivered.
///
/// Front-end independent (no Qt), unit-tested with the controller. Coordinates are logical pixels (any consistent
/// space: global works), time is the caller's concern (the deferred leap is flushed by the caller's timer).
class PointerSourceClassifier
{
public:
    enum class Source
    {
        Touch,  ///< the default, and what a lone press with no mouse evidence is: the pad behaviour
        Mouse,
    };

    /// A single move this far (in one event) is a pointer jumping to a finger, not a hand moving a mouse
    static constexpr double kLeapPx = 120.0;
    /// A step this far with the button held is the pointer catching up with a finger whose press came first (the
    /// limit of TouchPointerTracker); a dragging hand, even through a stalled event queue, stays under it
    static constexpr double kMaxStepPx = 250.0;
    /// Hover moves in a row (no press between) that make the source a mouse
    static constexpr int kMouseHoverMoves = 2;
    /// A leap of a mouse source is held this long (ms) waiting for the press of a finger
    static constexpr int kDeferMs = 60;

    struct Travel
    {
        double dx = 0.0;
        double dy = 0.0;
        /// Drag only: a mouse source's drag leapt, so a finger landed with its press delivered first. The source is
        /// a touch now and the contact starts here: the caller releases what the press pressed
        bool landed = false;

        bool Empty() const { return dx == 0.0 && dy == 0.0; }
    };

    Source GetSource() const { return _source; }
    bool HasDeferred() const { return _deferred; }

    void Reset() { *this = PointerSourceClassifier(); }

    /// The source is known for certain (Qt synthesized the event from a touch, the input device is a touchscreen)
    void ForceTouch()
    {
        _source = Source::Touch;
        _hoverRun = 0;
        _heldDx = _heldDy = 0.0;
        _deferred = false;
    }

    /// The pointer is somewhere else without having travelled (after a warp): no step from the old place
    void Rebase(double x, double y)
    {
        _x = x;
        _y = y;
        _haveLast = true;
        _hoverRun = 0;
        _heldDx = _heldDy = 0.0;
    }

    /// The pointer moved with no button held: the travel the guest should see now (empty for a touch)
    Travel Hover(double x, double y)
    {
        const double dx = _haveLast ? x - _x : 0.0;
        const double dy = _haveLast ? y - _y : 0.0;
        _x = x;
        _y = y;
        _haveLast = true;
        const bool leap = std::hypot(dx, dy) > kLeapPx;

        Travel travel;
        if (_source == Source::Touch)
        {
            if (leap)
            {
                // A finger landing: not travel, not a mouse
                _hoverRun = 0;
                _heldDx = _heldDy = 0.0;
                return travel;
            }
            _heldDx += dx;
            _heldDy += dy;
            if (++_hoverRun < kMouseHoverMoves)
                return travel;
            _source = Source::Mouse;
            travel.dx = _heldDx;
            travel.dy = _heldDy;
            _heldDx = _heldDy = 0.0;
            return travel;
        }

        // A mouse: whatever was held back before this move was a fast flick after all
        travel.dx = _deferredDx;
        travel.dy = _deferredDy;
        _deferred = false;
        _deferredDx = _deferredDy = 0.0;
        if (leap)
        {
            _deferred = true;
            _deferredDx = dx;
            _deferredDy = dy;
        }
        else
        {
            travel.dx += dx;
            travel.dy += dy;
        }
        return travel;
    }

    /// The held-back leap of a mouse source (its timer ran out): the travel to deliver
    Travel TakeDeferred()
    {
        Travel travel;
        if (_deferred)
        {
            travel.dx = _deferredDx;
            travel.dy = _deferredDy;
        }
        _deferred = false;
        _deferredDx = _deferredDy = 0.0;
        return travel;
    }

    /// A button goes down at (x, y): the source in force for this press
    Source Press(bool left, double x, double y)
    {
        _x = x;
        _y = y;
        _haveLast = true;
        _hoverRun = 0;
        _heldDx = _heldDy = 0.0;
        if (!left)
            _source = Source::Mouse;
        else if (_deferred)
        {
            // The pointer leapt and the button went down right after: a finger landed
            _source = Source::Touch;
            _deferred = false;
            _deferredDx = _deferredDy = 0.0;
        }
        return _source;
    }

    /// The pointer moved with a button held. A mouse source: the travel (a step too big to be a hand is dropped).
    /// A touch source: nothing (the contact's own tracker measures it); only the position is followed
    Travel Drag(double x, double y)
    {
        const double dx = _haveLast ? x - _x : 0.0;
        const double dy = _haveLast ? y - _y : 0.0;
        _x = x;
        _y = y;
        _haveLast = true;

        Travel travel;
        if (_source != Source::Mouse)
            return travel;
        const double step = std::hypot(dx, dy);
        if (step > kMaxStepPx)
        {
            // A hand does not drag this fast; the press came first and the pointer caught up with the finger
            _source = Source::Touch;
            _hoverRun = 0;
            travel.landed = true;
            return travel;
        }
        travel.dx = dx;
        travel.dy = dy;
        return travel;
    }

    /// The wheel turned: only a mouse has one
    void Wheel() { _source = Source::Mouse; }

private:
    Source _source = Source::Touch;
    bool _haveLast = false;
    double _x = 0.0;
    double _y = 0.0;
    int _hoverRun = 0;
    double _heldDx = 0.0;
    double _heldDy = 0.0;
    bool _deferred = false;
    double _deferredDx = 0.0;
    double _deferredDy = 0.0;
};
