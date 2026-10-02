#pragma once

/// @file ttdframeinput.h
/// @brief What the engine is fed at each frame boundary.
///
/// Both producers hand the engine the same frame input: live capture from
/// the running emulator, and the v1 file feeder used for verification. The
/// engine never needs to know which one feeds it.
/// Design: docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md §4.1-4.2.

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "debugger/ttd/ttdcheckpoint.h"
#include "ttdtime.h"

namespace ttd
{

/// One 4 KB piece whose content changed since the previous frame
struct TTDChangedPiece
{
    uint32_t region = 0;          ///< index into the session's region list
    uint32_t piece = 0;           ///< piece index inside the region
    const uint8_t* bytes = nullptr;  ///< kTTDPieceSize bytes of new content, valid during the call
};

struct TTDFrameInput
{
    TTDPosition position;         ///< frame boundary being captured (tInFrame = 0)
    TTDMachineTime start = 0;     ///< the frame's start in machine time
    TTDCpuState cpu;
    TTDChipsetState chipset;
    /// Device state blobs, keyed by peripheral id, as v1 stores them
    /// (Phase 2 replaces this with the device registry)
    const std::unordered_map<uint8_t, std::vector<uint8_t>>* deviceBlobs = nullptr;
    std::vector<TTDChangedPiece> changed;
};

}  // namespace ttd
