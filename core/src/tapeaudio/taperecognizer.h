#pragma once

#include "stdafx.h"

#include "emulator/io/tape/tapetypes.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

/// region <Documentation>

/// Pulse track → recognized tape blocks (tape-audio-bridge design §6.3).
///
/// Three-stage recognition, mirroring how the ROM itself loads: a pilot run
/// plus sync pair frames the block, the data region decodes as bit pairs
/// (each ZX bit is two equal half-periods) against measured medians, and XOR
/// checksum framing validates the byte payload. Blocks whose measured
/// timings sit within tolerance of the ROM constants become $10-equivalents;
/// self-consistent but non-ROM timings become $11-equivalents; anything the
/// bit decoder cannot frame degrades honestly to pulse-level blocks ($12
/// tone / $13 pulse sequence), never to fabricated bytes.
///
/// Input convention: TapePulseExtractor's track — signal half-periods in
/// T-states with silence gaps as entries >= 5 ms. Gaps immediately after a
/// decoded byte block are consumed as that block's pause (the $10/$11 pause
/// field); gaps elsewhere become $20-equivalent PauseGap entries.

/// endregion </Documentation>

enum class RecognizedBlockKind : uint8_t
{
    StandardBlock,   // pilot+sync+bits near ROM timings → TZX $10
    TurboBlock,      // bit-decodable, non-ROM (or pilotless) timings → TZX $11
    PureTone,        // tone-only run → TZX $12
    PulseSequence,   // undecodable signal, faithful pulse copy → TZX $13
    PauseGap         // silence stretch → TZX $20
};

struct RecognizedBlock
{
    RecognizedBlockKind kind = RecognizedBlockKind::PauseGap;

    // Standard/Turbo: flag + payload + checksum exactly as decoded
    std::vector<uint8_t> data;

    // Turbo: full measured profile (profile=Custom, bitsInLastByte set);
    // Standard: only pauseMs is meaningful (ROM constants implied)
    TapeTimingProfile timing;

    uint32_t tonePeriod = 0;          // PureTone
    uint32_t tonePulses = 0;

    std::vector<uint32_t> pulses;     // PulseSequence (≤ 255 u16-sized entries)

    uint32_t pauseMs = 0;             // PauseGap / consumed trailing pause
    bool checksumValid = false;       // Standard/Turbo XOR framing result
    std::vector<std::string> notes;   // recognition anomalies, surfaced by import
};

class TapeRecognizer
{
public:
    /// Pure function over the pulse track. Always terminates; every input
    /// entry is accounted for by exactly one output block.
    static std::vector<RecognizedBlock> Recognize(std::span<const uint32_t> track);
};
