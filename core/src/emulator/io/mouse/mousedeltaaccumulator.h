#pragma once

#include <cmath>
#include <cstdint>

/// Host pointer travel -> whole emulated pixels (Kempston Mouse design §5.1-§5.2).
///
/// Rule: one pixel of host pointer travel across the displayed image equals one
/// pixel of emulated screen travel, whatever the monitor DPI, window size or
/// upscale. Both inputs are therefore taken in the same unit - physical pixels:
///
///   emulated = hostPhysicalDelta / physicalPixelsPerEmulatedPixel * scale
///
/// where physicalPixelsPerEmulatedPixel = drawn image size in physical pixels /
/// source framebuffer (or cropped viewport) size. A front end that works in
/// logical units multiplies both by the device pixel ratio, which cancels.
///
/// Front-end independent (no Qt): the same arithmetic serves Qt and SDL, and is
/// unit-tested in core-tests.
class MouseDeltaAccumulator
{
public:
    struct Steps
    {
        int dx = 0;
        int dy = 0;
    };

    void Reset()
    {
        _accX = 0.0;
        _accY = 0.0;
    }

    /// @param hostDx, hostDy                  host pointer travel, physical pixels (screen axes: +y down)
    /// @param physPerEmuX, physPerEmuY        physical pixels covered by one emulated pixel on screen
    /// @param scale                           user sensitivity, 2^mousescale (1.0 = identity)
    /// @return whole emulated pixels to apply now, screen axes (+y down); the fraction is carried
    Steps Feed(double hostDx, double hostDy, double physPerEmuX, double physPerEmuY, double scale = 1.0)
    {
        // A degenerate geometry (widget not laid out yet) must not produce inf/NaN motion
        if (!(physPerEmuX > 0.0) || !(physPerEmuY > 0.0) || !std::isfinite(hostDx) || !std::isfinite(hostDy))
            return {};

        _accX += hostDx / physPerEmuX * scale;
        _accY += hostDy / physPerEmuY * scale;

        // Truncate toward zero and keep the remainder: a slow drag at high upscale
        // produces sub-pixel deltas that must still add up to motion (§5.2 traps 1 and 3)
        Steps steps;
        steps.dx = static_cast<int>(_accX);
        steps.dy = static_cast<int>(_accY);
        _accX -= steps.dx;
        _accY -= steps.dy;
        return steps;
    }

    double RemainderX() const { return _accX; }
    double RemainderY() const { return _accY; }

private:
    double _accX = 0.0;
    double _accY = 0.0;
};

/// Host wheel rotation -> whole notches. Qt/Win32 report 120 units per notch;
/// high-resolution wheels and trackpads deliver fractions of that, which must
/// accumulate instead of truncating to zero on every event.
class MouseWheelAccumulator
{
public:
    static constexpr int kUnitsPerNotch = 120;

    void Reset() { _units = 0; }

    /// @param units angle delta, + = away from the user
    /// @return whole notches to apply now; the remainder is carried
    int Feed(int units)
    {
        _units += units;
        const int notches = _units / kUnitsPerNotch;  // truncates toward zero for both signs
        _units -= notches * kUnitsPerNotch;
        return notches;
    }

private:
    int _units = 0;
};
