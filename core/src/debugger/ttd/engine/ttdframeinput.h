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

/// One device's raw state for a frame (what TTDSaveState wrote, or its state
/// without region memory)
struct TTDDeviceStateInput
{
    uint8_t id = 0;                  ///< the device's v1 id (TTDDeviceDescriptor::legacyId)
    const uint8_t* bytes = nullptr;  ///< valid during the call
    size_t size = 0;
};

struct TTDFrameInput
{
    TTDPosition position;         ///< frame boundary being captured (tInFrame = 0)
    TTDMachineTime start = 0;     ///< the frame's start in machine time
    TTDCpuState cpu;
    TTDChipsetState chipset;
    /// Device states, raw (live capture). When empty, deviceBlobs is read
    std::vector<TTDDeviceStateInput> deviceStates;
    /// Device states as v1 stores them, wrapped and compressed, keyed by v1 id
    /// (the v1 file feeder); decoded by the engine
    const std::unordered_map<uint8_t, std::vector<uint8_t>>* deviceBlobs = nullptr;
    std::vector<TTDChangedPiece> changed;
    /// Raw device-state bytes the capture serialized for the blobs above
    /// (counted work only; the engine does not read them)
    uint64_t deviceStateBytes = 0;
};

}  // namespace ttd
