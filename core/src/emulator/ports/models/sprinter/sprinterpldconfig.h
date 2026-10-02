#pragma once

#include <cstdint>

#include "emulator/ports/models/sprinter/sprinterpldstate.h"

/// @file sprinterpldconfig.h
/// @brief The PLD configuration sink (Sprinter tdd-ports-memory §6, decision D2).
///
/// While the PLD is loading, every CPU memory write is one configuration clock:
/// the ROM's loader writes each bitstream byte 8 times, rotated right between
/// the writes (bit 0 first), so BIOS 3.04 needs 59 215 x 8 = 473 720 writes
/// (S0, docs/disasm/rom/sprinter/loader/README.md). The sink counts them and
/// keeps two hashes: MAME's hash of the first 4 096 writes (so MAME's constants
/// stay usable) and FNV-1a over the whole stream (exact identification). At the
/// count the load is over; a watchdog ends a load that never gets there.
///
/// Worked example: the first bitstream byte #FF gives writes 1-8 with the values
/// #FF (rotating #FF changes nothing); a byte #A5 gives #A5, #D2, #69, #B4, #5A,
/// #2D, #96, #4B - their D0 is 1, 0, 1, 0, 0, 1, 0, 1.
class SprinterPldConfig
{
public:
    /// 59 215 bitstream bytes x 8 single-bit writes (S0, statically from the BIOS 3.04 loader)
    static constexpr uint32_t kPldConfigurationWrites = 473720;
    /// MAME hashes this many writes (sprinter.cpp bootstrap_w)
    static constexpr uint32_t kHeadWrites = 4096;
    /// Frames a load may take before the watchdog ends it: the 3.04 loader needs
    /// ~94 frames at 3.5 MHz (113 T per byte), so 300 leaves a wide margin
    static constexpr uint32_t kWatchdogFrames = 300;
    /// FNV-1a offset basis / prime
    static constexpr uint32_t kFnvBasis = 0x811C9DC5u;
    static constexpr uint32_t kFnvPrime = 0x01000193u;

    /// Start a load: count and hashes cleared, the watchdog armed
    static void Begin(SprinterPldState& pld)
    {
        pld.configState = SprinterConfigState::Loading;
        pld.bitstreamCount = 0;
        pld.bitstreamHashHead = 0;
        pld.bitstreamHashFull = kFnvBasis;
        pld.loadWatchdog = kWatchdogFrames;
    }

    /// One configuration write. Returns true when it was the last one of the stream
    static bool OnWrite(SprinterPldState& pld, uint8_t value)
    {
        if (pld.bitstreamCount >= kPldConfigurationWrites)
            return false;  // after the end: ignored until the CPU reset

        if (pld.bitstreamCount < kHeadWrites)
            pld.bitstreamHashHead += static_cast<uint32_t>(value) << (8 * (pld.bitstreamCount % 4));
        pld.bitstreamHashFull = (pld.bitstreamHashFull ^ value) * kFnvPrime;

        return ++pld.bitstreamCount == kPldConfigurationWrites;
    }

    /// A frame ended while loading. Returns true when the watchdog expired
    static bool OnFrame(SprinterPldState& pld)
    {
        if (pld.loadWatchdog == 0)
            return false;
        return --pld.loadWatchdog == 0;
    }
};
