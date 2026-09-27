#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/sound/filters/filtervoicing.h"

/// Runtime owner of one stereo stream's voicing (FilterVoicing): what makes a
/// profile safe to change while music plays.
///
/// - request() may be called from any thread (GUI, WebAPI, scripting); the
///   stream switches at the start of the next process() call, i.e. at a frame
///   boundary on the emulation thread. requested() reports the request at
///   once, so a read right after a write shows the new value.
/// - A switch is click-free: the new filter is first warmed up ("pre-rolled")
///   over the last two frames of UNVOICED input kept in a small history, then
///   the switch frame crossfades linearly from the old to the new filter.
///   A bare reset() would pass the current level as a step through the
///   high-pass (tau ~2.5 ms) and the peak section (tau ~4.2 ms): ~525 LSB at
///   -6 dBFS; one frame of warm-up still leaves ~1.3 LSB, two leave < 0.01.
/// - With no valid history (first frame after reset(), a rate change or a gap
///   in processing) the switch starts from a cleared filter - rare, and at
///   points where the stream itself is discontinuous.
///
/// Design: docs/inprogress/2026-09-25-ay-tone-voicing/ay-tone-voicing-tdd.md §4.4
class VoicingStage
{
public:
    using Preset = FilterVoicing::Preset;
    static constexpr size_t HISTORY_FRAMES = 2;

    /// @param maxFrameSamples largest process() call in stereo sample pairs
    explicit VoicingStage(size_t maxFrameSamples);

    /// Re-derive for a new sample rate. Filter state is kept (see
    /// FilterVoicing::setup); the history is dropped - it holds old-rate samples
    void setup(double sampleRate);

    /// Select a profile at once, without a crossfade (construction, config
    /// load). Emulation thread / before processing starts
    void setPresetImmediate(Preset preset);

    /// Ask for a profile; any thread. Applied at the next process() call
    void request(Preset preset);

    /// The last requested profile (what the user / API asked for)
    Preset requested() const
    {
        return static_cast<Preset>(_requested.load(std::memory_order_acquire));
    }

    /// The profile the stream runs right now (tests, diagnostics)
    Preset active() const
    {
        return _filter.preset();
    }

    /// Clear the filter state and the history; a pending request is applied
    /// at once (nothing to crossfade from after a reset)
    void reset();

    /// Forget the history (a gap in processing: sound off, turbo without audio)
    void invalidateHistory();

    /// Process one frame of interleaved stereo int16 in place (emulation thread)
    void process(int16_t* interleaved, size_t frames);

    /// Frames of valid pre-roll history (0..HISTORY_FRAMES)
    size_t historyFrames() const
    {
        return _historyCount;
    }

    const FilterVoicing& filter() const
    {
        return _filter;
    }

private:
    void pushHistory(const int16_t* interleaved, size_t frames);
    void switchTo(Preset target, int16_t* interleaved, size_t frames);

    size_t _maxFrameSamples;
    double _sampleRate = 0.0;
    FilterVoicing _filter;
    std::atomic<uint8_t> _requested{static_cast<uint8_t>(Preset::Flat)};

    // Ring of the last HISTORY_FRAMES frames of unvoiced input
    std::array<std::vector<int16_t>, HISTORY_FRAMES> _history;
    std::array<size_t, HISTORY_FRAMES> _historyLength{};
    size_t _historyNewest = 0;
    size_t _historyCount = 0;
};
