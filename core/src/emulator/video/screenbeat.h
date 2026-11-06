#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

/// What the screen did with the frames' times (frame presenter design, docs/inprogress/2026-10-26-frame-presenter,
/// sections 5 and 6.4): the frames shown outside the beat pattern and the screen-to-sound offset, from the screen
/// times a native presenter learns.
///
/// - **The beat pattern.** A frame stays on screen until the next one shows. On a display with refresh period V a
///   frame due `span` after the one before may stay floor or ceil of span / V refreshes: that is the arithmetic beat
///   of the two rates, unavoidable on a fixed display (50.08 Hz on 60 Hz: one frame in five for two refreshes). Any
///   other count is a frame outside it: a refresh missed, a frame held, a frame never shown. The span is the frames'
///   own target difference, so a machine at any rate, the DRC trim, an interpolated picture per refresh (span = V) or
///   a VRR display (V its shortest refresh: each frame its own span) are judged by the same rule (POC 037's
///   "irregular" count, tools/poc/037-frame-presenter/analyze.py).
/// - **The screen-to-sound offset.** The screen time minus the picture's target: the audio clock's time its sound is
///   heard (plus the lip-sync trim it was given), or the emulated clock's when no audio device runs. The audio clock
///   and the screen times are on the same host clock.
///
/// Not thread-safe: the presenter calls Shown and GetStats under its own stats lock.
class ScreenBeat
{
public:
    /// The offsets the percentiles cover
    static constexpr size_t kWindow = 600;
    /// Two pictures further apart than this on screen or in time break the sequence (a pause, a seek, an occluded
    /// window): nothing smooth was shown across it
    static constexpr int64_t kGapNs = 250'000'000;

    struct Stats
    {
        uint64_t frames = 0;   ///< pictures judged against the beat (each after the one before it on screen)
        uint64_t offBeat = 0;  ///< ... on screen for a refresh count outside floor / ceil of their span
        uint32_t window = 0;   ///< offsets the percentiles cover
        double offsetP50Ms = 0.0;  ///< screen time minus the picture's target
        double offsetP99Ms = 0.0;
    };

    /// A picture reached the screen: `picture` numbers the pictures (a frame's serial, or a count of the pictures
    /// smooth motion drew; the same number again is the same picture), `targetNs` its time (0: untimed, a repaint),
    /// `screenNs` when it showed, `refreshNs` the display's shortest refresh period
    void Shown(uint64_t picture, int64_t targetNs, int64_t screenNs, int64_t refreshNs)
    {
        if (targetNs <= 0 || screenNs <= 0 || picture == 0)
            return;
        if (picture == _lastPicture)
            return;
        AddOffset(static_cast<double>(screenNs - targetNs) / 1e6);
        if (_lastPicture != 0 && picture > _lastPicture && refreshNs > 0)
        {
            const int64_t onScreen = screenNs - _lastScreenNs;
            const int64_t span = targetNs - _lastTargetNs;
            if (span > 0 && onScreen >= 0 && span < kGapNs && onScreen < kGapNs)
            {
                // The pair's span over the refresh: floor / ceil of it is the beat (the frames between, if any, count
                // as never shown below)
                const double per = static_cast<double>(span) / static_cast<double>(refreshNs);
                const double lo = std::floor(per);
                const double hi = std::ceil(per);
                const double count = std::round(static_cast<double>(onScreen) / static_cast<double>(refreshNs));
                _stats.frames++;
                if (count < lo || count > hi || count == 0.0)
                    _stats.offBeat++;
                // Pictures between the two never showed: outside the beat where each is due a refresh or more (a
                // machine faster than the display skips some by its beat)
                const uint64_t skipped = picture - _lastPicture - 1;
                if (skipped > 0 && per / static_cast<double>(skipped + 1) >= 1.0)
                {
                    _stats.frames += skipped;
                    _stats.offBeat += skipped;
                }
            }
        }
        _lastPicture = picture;
        _lastTargetNs = targetNs;
        _lastScreenNs = screenNs;
    }

    /// The counters and the offset percentiles now
    Stats GetStats() const
    {
        Stats s = _stats;
        std::vector<double> offsets = _offsets;
        s.window = static_cast<uint32_t>(offsets.size());
        if (!offsets.empty())
        {
            std::sort(offsets.begin(), offsets.end());
            auto at = [&offsets](double q) {
                const size_t i = static_cast<size_t>(q * static_cast<double>(offsets.size() - 1) + 0.5);
                return offsets[std::min(i, offsets.size() - 1)];
            };
            s.offsetP50Ms = at(0.50);
            s.offsetP99Ms = at(0.99);
        }
        return s;
    }

    void Reset() { *this = ScreenBeat(); }

private:
    void AddOffset(double ms)
    {
        if (_offsets.size() < kWindow)
            _offsets.push_back(ms);
        else
            _offsets[_offsetNext] = ms;
        _offsetNext = (_offsetNext + 1) % kWindow;
    }

    Stats _stats;
    std::vector<double> _offsets;
    size_t _offsetNext = 0;
    uint64_t _lastPicture = 0;
    int64_t _lastTargetNs = 0;
    int64_t _lastScreenNs = 0;
};
