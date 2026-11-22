#pragma once

#include <cmath>
#include <cstdint>

/// Does the display follow the frames (VRR)? One answer for every native presenter under policy auto, from what the
/// screen did and nothing else: the display's refreshes (vblanks, screen times) against the presents.
///
/// A range the system reports, a swap mode the driver offers or tearing being supported do not say it: a G-Sync
/// monitor follows only the foreground window (or only full screen), ProMotion puts the frames on its 120 Hz grid, an
/// IMMEDIATE swapchain exists on every fixed display. So a presenter starts on the refresh grid and takes VRR when the
/// refreshes are seen to follow the frames; it leaves VRR when they are seen on a grid the frames are not on.
///
/// Frames that come at the display's own rate (a 60 Hz machine on 60 Hz, 50.08 Hz on 50 Hz) tell nothing: the
/// refreshes are on the grid and with the frames at once. Nothing changes then: on the grid a frame of its refresh is
/// shown either way, while a tearing present on a fixed display would show its tear.
namespace vrrprobe
{
enum class Verdict : uint8_t
{
    Unknown,  ///< not enough to tell (no measurements, the frames at the display's rate, a mix of two grids)
    Follows,  ///< the refreshes come with the frames, off the mode's grid
    Grid      ///< the refreshes keep a grid of their own that the frames are not on
};

/// `vblankMs`: the refreshes' interval (median), `vblankLoMs` / `vblankHiMs` its spread (10th / 90th percentile; the
/// median twice when not kept); `presentMs`: the frames' interval; `refreshMs`: the mode's period.
/// A steady grid that is not the mode's is a grid too (a remote session's display reports its own rate while the
/// vblank wait hears the monitor the adapter drives)
inline Verdict Judge(double vblankMs, double vblankLoMs, double vblankHiMs, double presentMs, double refreshMs)
{
    if (vblankMs <= 0.0 || presentMs <= 0.0 || refreshMs <= 0.0)
        return Verdict::Unknown;
    const bool onGrid = std::fabs(vblankMs - refreshMs) < refreshMs * 0.03;
    const bool follows = std::fabs(vblankMs - presentMs) < presentMs * 0.03;
    const bool steady = vblankHiMs - vblankLoMs < vblankMs * 0.03;
    const bool presentsOffGrid = std::fabs(presentMs - vblankMs) > vblankMs * 0.10;
    if (follows && !onGrid)
        return Verdict::Follows;
    if ((onGrid || steady) && presentsOffGrid)
        return Verdict::Grid;
    return Verdict::Unknown;
}

/// The same from the steps between the screen times of the frames shown (Metal's presentedTime, a swapchain's
/// present feedback), where no vblank is heard between them: on a grid a frame stays for a whole number of the mode's
/// refreshes, and frames off the grid's rate get a mix of two counts (50 Hz on ProMotion's 120 Hz grid: 16.7 and
/// 25 ms, never 20 ms). `stepLoMs` / `stepMs` / `stepHiMs`: the 10th / 50th / 90th percentile of the steps
inline Verdict JudgeSteps(double stepLoMs, double stepMs, double stepHiMs, double presentMs, double refreshMs)
{
    if (stepLoMs <= 0.0 || stepMs <= 0.0 || stepHiMs <= 0.0 || presentMs <= 0.0 || refreshMs <= 0.0)
        return Verdict::Unknown;
    auto refreshes = [refreshMs](double ms) { return ms / refreshMs; };
    auto whole = [&](double ms) {
        const double n = refreshes(ms);
        return n > 0.5 && std::fabs(n - std::round(n)) < 0.03 * std::round(n);
    };
    // Frames at a whole number of refreshes are on the grid and with it at once: nothing to tell by
    if (whole(presentMs))
        return Verdict::Unknown;
    const bool steady = stepHiMs - stepLoMs < stepMs * 0.03;
    if (steady && std::fabs(stepMs - presentMs) < presentMs * 0.03)
        return Verdict::Follows;
    if (whole(stepLoMs) && whole(stepHiMs))
        return Verdict::Grid;
    return Verdict::Unknown;
}

/// A presenter's VRR state under policy auto: as it starts until the display is seen to follow or not, `Pending` until the
/// first verdict (or `kGiveUp` judgements without one: the frames come at the display's rate, nothing will tell).
/// While pending the window holds what a wrong guess would make it undo (a display mode switch)
class Probe
{
public:
    static constexpr int kGiveUp = 2;

    /// `engaged`: the state to start with. False where nothing says the display can follow (Windows: tearing
    /// supported says nothing); true where the system does (a refresh range of the screen), until seen otherwise
    void Reset(bool engaged = false)
    {
        _engaged = engaged;
        _pending = true;
        _unknown = 0;
    }

    /// A judgement; true when Engaged changed
    bool Feed(Verdict verdict)
    {
        if (verdict == Verdict::Unknown)
        {
            if (_pending && ++_unknown >= kGiveUp)
                _pending = false;
            return false;
        }
        _pending = false;
        _unknown = 0;
        const bool engaged = verdict == Verdict::Follows;
        if (engaged == _engaged)
            return false;
        _engaged = engaged;
        return true;
    }

    bool Engaged() const { return _engaged; }
    bool Pending() const { return _pending; }

private:
    bool _engaged = false;
    bool _pending = true;
    int _unknown = 0;
};
}  // namespace vrrprobe
