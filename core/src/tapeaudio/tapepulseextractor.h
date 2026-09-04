#pragma once

#include "stdafx.h"

#include "tapeaudio/tapeaudioconfig.h"

#include <cstdint>
#include <span>
#include <vector>

/// region <Documentation>

/// Audio samples → tape pulse track (tape-audio-bridge design §6.1/§6.2).
///
/// A Schmitt trigger with a hysteresis band relative to the measured signal
/// range recovers edge positions; sample deltas become T-state half-periods.
/// The output is the import direction's interchange format — the exact
/// inverse of what MaterializePulses() produces for render: signal
/// half-periods below the pause threshold, silence gaps as single entries of
/// gapMs * 3500 T-states (the same convention the loaders' $20 hold-edges
/// and tapepulsegen's pause split use).
///
/// Robustness contract: DC offset and arbitrary input gain are handled by
/// centering on (min+max)/2 and scaling the band to (max-min)/2; noise below
/// the hysteresis band produces no edges; runt pulses below minSignalT are
/// merged into the following edge instead of emitted.

/// endregion </Documentation>

struct TapePulseExtractorOptions
{
    /// Schmitt band as a fraction of the signal half-range. 0.2: the classic
    /// cassette-decoder margin — noise up to ±20% of signal never toggles.
    double hysteresis = 0.2;

    /// Runt filter in T-states: half-periods below this are treated as
    /// noise and merged into the next edge (design §6.2).
    uint32_t minSignalT = 50;

    /// Half-periods at/above this many ms are silence gaps, never signal.
    uint32_t gapThresholdMs = TapeAudio::PAUSE_HOLD_THRESHOLD_MS;
};

struct TapePulseTrack
{
    /// T-states per half-period in edge order. Signal entries are below
    /// gapThresholdMs * 3500; each gap entry is one stretch of silence.
    std::vector<uint32_t> entries;
    uint32_t sampleRate = 0;
    size_t signalEdges = 0;       // entries below the gap threshold
    size_t gapCount = 0;          // entries at/above it
    float signalRange = 0.0f;     // measured (max-min)/2 — diagnostics/preview
};

class TapePulseExtractor
{
public:
    /// `samples` is mono float PCM [-1, 1]; stereo sources are downmixed by
    /// the caller (first channel — antiphase channels must not cancel a tape
    /// signal). Returns an empty track for silence-only or flat input.
    static TapePulseTrack Extract(std::span<const float> samples, uint32_t sampleRate,
                                  const TapePulseExtractorOptions& options = {});
};
