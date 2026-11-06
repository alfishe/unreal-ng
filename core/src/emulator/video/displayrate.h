#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// The display's refresh rate for the machine's ([VIDEO] DisplayRate; frame presenter design, section 4.6): a 48.83 or
/// 50 Hz machine on a fixed 60 Hz display beats (a frame held for two refreshes every fifth or sixth), and neither the
/// blend nor motion search is free of artefacts. Most monitors also offer 50 Hz: shown at a rate that is a multiple of
/// the machine's, every frame is on screen for the same time.
/// - keep: the display as it is;
/// - match: while the emulator is the active application, the display under its window runs the mode of the same
///   resolution whose rate fits the machine's best (DisplayRatePick); on focus loss, a window moving to another
///   display or the application quitting, the display gets its mode back (the platform also restores it should the
///   application end without that).
enum class DisplayRate : uint8_t
{
    Keep,
    Match
};

inline const char* DisplayRateName(DisplayRate rate)
{
    return rate == DisplayRate::Match ? "match" : "keep";
}

/// keep, match (any case). False for any other word
inline bool ParseDisplayRate(const char* text, DisplayRate& rate)
{
    std::string word = text ? text : "";
    for (char& c : word)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (DisplayRate r : {DisplayRate::Keep, DisplayRate::Match})
        if (word == DisplayRateName(r))
        {
            rate = r;
            return true;
        }
    return false;
}

/// How a display rate shows a machine's frames: each frame for `repeats` refreshes, off the machine's rate by `error`
/// (0.024 = 2.4 %: a 48.83 Hz machine on 50 Hz holds one frame for an extra refresh every 0.85 s)
struct DisplayRateFit
{
    double hz = 0.0;
    int repeats = 1;
    double error = 1.0;
};

inline DisplayRateFit FitDisplayRate(double displayHz, double machineHz)
{
    DisplayRateFit fit;
    fit.hz = displayHz;
    if (displayHz <= 0.0 || machineHz <= 0.0)
        return fit;
    fit.repeats = std::max(1, static_cast<int>(std::lround(displayHz / machineHz)));
    fit.error = std::fabs(displayHz / (fit.repeats * machineHz) - 1.0);
    return fit;
}

/// A display rate at most this far off the machine's (a multiple of it): 50 Hz for 48.83 (2.4 %) and 50.06 Hz,
/// 100 Hz for both; not 60 (23 %), 75 (23 %) or 120 (18 %)
inline constexpr double kDisplayRateMaxError = 0.05;

/// Of the display's rates at its current resolution, the one to switch to for the machine: the least error, equals the
/// lower rate; none (false) when no rate is within kDisplayRateMaxError or the current one fits as well. A current
/// rate within kDisplayRateMaxError stays unless the pick halves its error: a switch blanks the display, and a
/// machine at 49.0 Hz is 2 % off both 100 Hz (two refreshes) and 144 Hz (three), its measured rate swaying across
/// 49.0 picked them in turn (seen on a 144 Hz monitor: 100, 144, 100, ... every few seconds)
inline bool DisplayRatePick(double machineHz, double currentHz, const std::vector<double>& rates, DisplayRateFit& pick)
{
    bool found = false;
    for (double hz : rates)
    {
        const DisplayRateFit fit = FitDisplayRate(hz, machineHz);
        if (fit.error > kDisplayRateMaxError)
            continue;
        if (!found || fit.error < pick.error - 1e-9 || (std::fabs(fit.error - pick.error) <= 1e-9 && hz < pick.hz))
        {
            pick = fit;
            found = true;
        }
    }
    const double current = FitDisplayRate(currentHz, machineHz).error;
    if (current <= kDisplayRateMaxError)
        return found && pick.error < current / 2.0 - 1e-9;
    return found && pick.error < current - 1e-9;
}

/// View > Video Synchronization (smooth motion auto, present policy auto, display rate match): the first of these the
/// display allows, in effect
/// - vrr: the presenter shows each frame at its own time (a VRR / ProMotion display);
/// - display_rate: the display runs a rate fitting the machine's (switched to 50 or 100 Hz, or already such a rate):
///   every frame for the same number of refreshes, give or take kDisplayRateMaxError;
/// - interpolate: a fixed rate that beats with the machine's (60 Hz for 48.83 / 50): smooth motion draws each refresh.
/// off: synchronization not asked (smooth motion other than auto)
enum class VideoSync : uint8_t
{
    Off,
    Vrr,
    DisplayRate,
    Interpolate
};

inline const char* VideoSyncName(VideoSync sync)
{
    switch (sync)
    {
        case VideoSync::Vrr: return "vrr";
        case VideoSync::DisplayRate: return "display_rate";
        case VideoSync::Interpolate: return "interpolate";
        case VideoSync::Off: break;
    }
    return "off";
}

/// The step in effect for a display `vrr` (frames each at its own time) at `displayHz` and a machine at `machineHz`.
/// Not steady yet (`machineHz` 0: start, pause, turbo): `last`, the step before, or interpolate when there was none
inline VideoSync PickVideoSync(bool vrr, double displayHz, double machineHz, VideoSync last = VideoSync::Off)
{
    if (vrr)
        return VideoSync::Vrr;
    if (machineHz <= 0.0)
        return last == VideoSync::Off ? VideoSync::Interpolate : last;
    return FitDisplayRate(displayHz, machineHz).error <= kDisplayRateMaxError ? VideoSync::DisplayRate
                                                                              : VideoSync::Interpolate;
}

/// The machine's frame rate. A frame paced to real time (the audio or the emulated clock) brings the machine's
/// nominal period (`periodNs`, from its frame length and clock: a Pentagon's 71 680 T at 3.5 MHz is 48.83 Hz): that
/// is the rate. The targets themselves wander by a few percent (the audio clock's steps, a remote session's audio),
/// so measuring them gives a rate a little off that a 1 % agreement check rarely confirms. Without a period (a screen
/// no frame clock times: an external picture's source not timed yet) the rate is the median of the last target
/// periods once they agree. A frame timed by its latch (turbo, a run not paced to real time) says nothing about the
/// machine's rate: it breaks the agreement and the rate stays the last one (PushUnpaced). Once a paced frame came, a
/// frame without a period counts as such
class FrameRateMeter
{
public:
    static constexpr size_t kPeriods = 25;
    static constexpr double kSpread = 0.01;  ///< the middle half of the periods within 1 % of the median

    /// A new frame's target time and, when it is paced to real time, the machine's nominal period; returns true
    /// when the rate changed
    bool Push(int64_t targetNs, int64_t periodNs = 0)
    {
        if (periodNs > 0)
        {
            _paced = true;
            _count = 0;
            _last = targetNs;
            return Set(std::round(1e9 / static_cast<double>(periodNs) * 100.0) / 100.0, 0.005);
        }
        if (_paced)
            return PushUnpaced();
        if (_last != 0 && targetNs > _last)
        {
            _periods[_next] = targetNs - _last;
            _next = (_next + 1) % kPeriods;
            _count = std::min(_count + 1, kPeriods);
        }
        _last = targetNs;
        if (_count < kPeriods)
            return false;
        // No allocation per frame: the ring sorted in a copy on the stack
        std::array<int64_t, kPeriods> sorted = _periods;
        std::sort(sorted.begin(), sorted.end());
        const double median = static_cast<double>(sorted[kPeriods / 2]);
        const double low = static_cast<double>(sorted[kPeriods / 4]);
        const double high = static_cast<double>(sorted[kPeriods * 3 / 4]);
        if (median <= 0.0 || high - low > median * kSpread)
            return false;
        // The rate at 0.01 Hz: the audio clock's trim moves the periods by a few ppm
        return Set(std::round(1e9 / median * 100.0) / 100.0, 0.05);
    }

    /// A frame not paced to real time (timed by its latch: turbo): the periods so far no longer agree with what comes
    /// next; the rate stays. Never a change
    bool PushUnpaced()
    {
        _count = 0;
        _last = 0;
        return false;
    }

    double Hz() const { return _hz; }
    void Reset()
    {
        _count = 0;
        _next = 0;
        _last = 0;
        _hz = 0.0;
        _paced = false;
    }

private:
    /// A measured rate moves by less than `minChange` without being a change: no churn from the trim
    bool Set(double hz, double minChange)
    {
        if (std::fabs(hz - _hz) < minChange)
            return false;
        _hz = hz;
        return true;
    }

    std::array<int64_t, kPeriods> _periods{};  // a ring: the last kPeriods target periods
    size_t _next = 0;
    size_t _count = 0;
    int64_t _last = 0;
    double _hz = 0.0;
    bool _paced = false;  // a paced frame came since the reset
};
