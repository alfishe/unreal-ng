#pragma once

#include <cstdint>
#include <cstdlib>

/// Pointer travel from absolute cursor samples, with re-centering - the
/// platform-independent half of the Windows capture backend
/// (platform/windows/mousecapture_windows.cpp), unit-tested on every platform.
///
/// The travel is the difference between consecutive samples, not the offset from
/// the centre: over RDP (and other remote sessions) the client sends absolute
/// positions of its own cursor, which follows the server's SetCursorPos only a
/// round trip later. Measured against the centre, every sample of that round trip
/// reported the whole distance again and the guest pointer jerked around.
///
/// The cursor is put back to the centre only when it strays beyond the warp zone,
/// and for a while after a warp a jump of about the warp vector (to the centre: our
/// own warp, or the remote client catching up) or of its inverse (a sample the
/// client sent before it saw the warp) is the warp itself, not travel: dropped.
/// Locally the first sample after a warp is the centre and nothing needs dropping.
///
/// Remote mode (an RDP session): no warps at all. The mstsc client does not move its
/// own cursor when the server warps, so every client sample stayed offset from the
/// centre by wherever the capture click was - a jump on both axes each time, even
/// for a straight horizontal move. Remote, the travel is only the difference between
/// samples (the client's own pointer speed and acceleration), the first sample sets
/// the baseline, and the clip keeps the cursor over the picture - as virtual machines
/// do over RDP. The cost: the travel stops at the edge of the clip.
class CursorTravel
{
public:
    struct Point
    {
        int x = 0;
        int y = 0;
    };

    struct Step
    {
        int dx = 0;
        int dy = 0;
        bool warp = false;  ///< the caller puts the cursor back to the centre now
    };

    /// After a warp, jumps of the warp vector are artifacts for this long
    static constexpr int64_t kArtifactWindowMs = 500;
    /// No second warp this soon: a remote client still sending pre-warp samples would be warped again and again
    static constexpr int64_t kRewarpHoldMs = 250;

    /// @param centre where the cursor is now and returns to
    /// @param zone the cursor stays this far from the centre (each axis) before it is warped back
    /// @param remote a remote session: never warp, the first sample is the baseline
    void Start(Point centre, Point zone, bool remote = false)
    {
        _centre = centre;
        _zone = Point{zone.x > 16 ? zone.x : 16, zone.y > 16 ? zone.y : 16};
        _last = centre;
        _warpAt = -1;
        _remote = remote;
        _haveBaseline = !remote;
    }

    /// One cursor sample (physical pixels) at `nowMs` (any monotonic clock)
    Step Feed(Point position, int64_t nowMs)
    {
        Step step;
        if (!_haveBaseline)
        {
            _last = position;
            _haveBaseline = true;
            return step;
        }
        step.dx = position.x - _last.x;
        step.dy = position.y - _last.y;
        _last = position;

        const bool afterWarp = _warpAt >= 0 && nowMs - _warpAt <= kArtifactWindowMs;
        if (afterWarp && (IsAbout(step.dx, step.dy, _warp.x, _warp.y) || IsAbout(step.dx, step.dy, -_warp.x, -_warp.y)))
        {
            step.dx = 0;
            step.dy = 0;
        }

        const bool strayed = std::abs(position.x - _centre.x) > _zone.x || std::abs(position.y - _centre.y) > _zone.y;
        if (strayed && !_remote && (_warpAt < 0 || nowMs - _warpAt >= kRewarpHoldMs))
        {
            _warp = Point{_centre.x - position.x, _centre.y - position.y};
            _warpAt = nowMs;
            _last = _centre;
            step.warp = true;
        }
        return step;
    }

private:
    /// (dx, dy) is the vector (wx, wy) give or take a quarter of its length (at least 4 pixels)
    static bool IsAbout(int dx, int dy, int wx, int wy)
    {
        const int span = std::abs(wx) > std::abs(wy) ? std::abs(wx) : std::abs(wy);
        const int tolerance = span / 4 > 4 ? span / 4 : 4;
        return std::abs(dx - wx) <= tolerance && std::abs(dy - wy) <= tolerance;
    }

    Point _centre;
    Point _zone;
    Point _last;
    Point _warp;           // centre - position at the last warp
    int64_t _warpAt = -1;  // clock of the last warp; -1 = none yet
    bool _remote = false;
    bool _haveBaseline = true;
};
