#pragma once

#include "stdafx.h"

#include "emulator/io/tape/tapetypes.h"

#include <cstdint>
#include <vector>

/// region <Documentation>

/// Pure tape pulse materialization — the shared, emulator-free half of the
/// engine's bitstream generator (tape-audio-bridge design §4.3).
///
/// Tape::generateBitstream() produces a block's edge series inside the live
/// engine; the tape-audio renderer and importer need the identical output
/// without an EmulatorContext. Everything pulse-generating lives here and the
/// engine delegates to it, so there is exactly one encoding of ROM/turbo/pure
/// timing in the tree (single source of truth — the engine tests are the
/// regression gate for byte-identical output).
///
/// Pause convention: GenerateHalfPeriods() emits the SIGNAL half-periods only.
/// The engine (Tape::generateBitstream) appends the pause as one trailing
/// hold-edge of pauseMs * 3500 T-states — its playback cursor semantics rely
/// on that shape — while MaterializePulses() returns the pause separately
/// (pauseAfterMs) so offline consumers can treat gaps structurally.

/// endregion </Documentation>

namespace TapePulseGen
{
    /// Result of materializing one block (design §4.1: the pulse timeline is
    /// the interchange format between render and import).
    struct MaterializedPulses
    {
        bool playable = false;                 // false: control/empty block with no signal
        std::vector<uint32_t> halfPeriods;     // signal edges, WITHOUT the pause hold-edge
        uint32_t pauseAfterMs = 0;             // silence following the signal
        uint64_t totalTStates = 0;             // signal + pause — engine totalBitstreamLength semantics
        bool romStandardTiming = false;        // true: representation 1 (ROM constants)
    };

    /// The parameterized bit encoder — the exact math of the engine path,
    /// factored out verbatim. Writes `pilot * count + sync pair + data bits`
    /// half-periods into outHalfPeriods (no pause edge) and returns the
    /// signal total in T-states (also no pause).
    uint64_t GenerateHalfPeriods(std::span<const uint8_t> data,
                                 uint32_t pilotHalfPeriod, uint32_t sync1, uint32_t sync2,
                                 uint32_t zeroHalfPeriod, uint32_t oneHalfPeriod,
                                 uint64_t pilotPulses, uint8_t bitsInLastByte,
                                 std::vector<uint32_t>& outHalfPeriods);

    /// Whole-block dispatch, mirroring Tape::generateBitstreamForStandardBlock
    /// one representation at a time (design §5.7):
    ///   - data empty            -> loader-supplied train as-is (representation 3);
    ///                              a trailing pause hold-edge is split into pauseAfterMs
    ///   - block.timing present  -> profile pass-through (representation 2, turbo/$14)
    ///   - otherwise             -> ROM constants, header/data pilot, 1000 ms pause
    ///                              (representation 1)
    ///
    /// `descriptor` is optional: when present, a representation-1 block whose
    /// descriptor carries a non-zero pauseMs (TZX $10 pause hint) uses that
    /// pause instead of the 1000 ms TAP default — the engine ignores the hint,
    /// offline render honors it (design §5.4).
    MaterializedPulses MaterializePulses(const TapeBlock& block, const TapeBlockDescriptor* descriptor = nullptr);
}
