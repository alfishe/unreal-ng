#pragma once

#include "stdafx.h"

#include <cstddef>
#include <cstdint>

/// region <Documentation>

/// Shared constants of the tape ↔ audio bridge (tape-audio-bridge design
/// §4.1/§5.2/§6.3). One header, no dependencies — both directions and every
/// surface (CLI/WebAPI/UI) quote the same numbers.

/// endregion </Documentation>

namespace TapeAudio
{
    /// Z80 nominal clock for tape timing on every supported model. All
    /// Spectrum targets run the CPU at 3.5 MHz; a single named constant keeps
    /// a future exception one edit away (design R5).
    inline constexpr uint32_t TSTATE_HZ = 3'500'000;

    /// T-states per millisecond — the pause hold-edge unit both the engine
    /// and the loaders use (1 ms == 3500 T-states).
    inline constexpr uint32_t TSTATES_PER_MS = TSTATE_HZ / 1000;

    /// Half-periods at or above this many milliseconds are pause hold-edges,
    /// never signal (no real pulse reaches 5 ms; TZX $13 caps at ~18.7 ms but
    /// those are mid-train signal, not trailing holds — see tapepulsegen).
    inline constexpr uint32_t PAUSE_HOLD_THRESHOLD_MS = 5;

    /// Community-standard render rate (Fuse tape2wav / hardware decks).
    inline constexpr uint32_t DEFAULT_SAMPLE_RATE = 44100;

    /// Bipolar square amplitude as a fraction of full scale (±0.8 FS).
    inline constexpr double DEFAULT_AMPLITUDE = 0.8;

    /// PCM streaming granularity into the encoders — ~100 ms at 44.1 kHz;
    /// the whole file is never buffered (design §9).
    inline constexpr size_t PCM_CHUNK_SAMPLES = 4410;
}
