#pragma once

#include <algorithm>
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
/// lower rate; none (false) when no rate is within kDisplayRateMaxError or the current one fits as well
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
    return found && pick.error < FitDisplayRate(currentHz, machineHz).error - 1e-9;
}

/// The machine's frame rate from its frames' target times (steady clock, ns): the median of the last periods once
/// they agree. A pause, turbo or a seek breaks the agreement; the rate stays the last steady one
class FrameRateMeter
{
public:
    static constexpr size_t kPeriods = 25;
    static constexpr double kSpread = 0.01;  ///< the middle half of the periods within 1 % of the median

    /// A new frame's target time; returns true when the steady rate changed
    bool Push(int64_t targetNs)
    {
        if (_last != 0 && targetNs > _last)
        {
            _periods.push_back(targetNs - _last);
            if (_periods.size() > kPeriods)
                _periods.erase(_periods.begin());
        }
        _last = targetNs;
        if (_periods.size() < kPeriods)
            return false;
        std::vector<int64_t> sorted = _periods;
        std::sort(sorted.begin(), sorted.end());
        const double median = static_cast<double>(sorted[sorted.size() / 2]);
        const double low = static_cast<double>(sorted[sorted.size() / 4]);
        const double high = static_cast<double>(sorted[sorted.size() * 3 / 4]);
        if (median <= 0.0 || high - low > median * kSpread)
            return false;
        // The rate at 0.01 Hz: the audio clock's trim moves the periods by a few ppm
        const double hz = std::round(1e9 / median * 100.0) / 100.0;
        if (std::fabs(hz - _hz) < 0.05)
            return false;
        _hz = hz;
        return true;
    }

    double Hz() const { return _hz; }
    void Reset()
    {
        _periods.clear();
        _last = 0;
        _hz = 0.0;
    }

private:
    std::vector<int64_t> _periods;
    int64_t _last = 0;
    double _hz = 0.0;
};
