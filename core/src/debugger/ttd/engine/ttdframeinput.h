#pragma once

/// @file ttdframeinput.h
/// @brief What the engine is fed at each frame boundary.
///
/// Live capture from the running emulator hands the engine one frame input
/// per boundary; tests build the same input by hand.
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

/// Bytes of memory the picture is read from (TTDFrameInput::screen)
struct TTDScreenRange
{
    uint32_t region = 0;  ///< index into the session's region list
    uint32_t begin = 0;   ///< the first byte, in the region
    uint32_t end = 0;     ///< one past the last

    bool operator==(const TTDScreenRange& o) const { return region == o.region && begin == o.begin && end == o.end; }
};

/// One device's raw state for a frame (what TTDSaveState wrote, or its state
/// without region memory)
struct TTDDeviceStateInput
{
    uint32_t device = 0;             ///< the device's index in the session's device table
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
    /// Device states as blobs (PeripheralBlobHeader, compressed), keyed by
    /// legacy id; decoded by the engine. Only tests set it (the v1 file feeder
    /// that did went with v1)
    const std::unordered_map<uint8_t, std::vector<uint8_t>>* deviceBlobs = nullptr;
    std::vector<TTDChangedPiece> changed;
    /// Raw device-state bytes the capture serialized for the blobs above
    /// (counted work only; the engine does not read them)
    uint64_t deviceStateBytes = 0;
    /// Maskable interrupts the CPU accepted while halted, since it was made (Z80::interruptsAcceptedHalted); with
    /// haltKnown false the capture did not say (a file feeder)
    uint64_t haltedInterrupts = 0;
    bool haltKnown = false;
    /// The bytes the picture is read from at this boundary (Screen::DescribeDisplayedMemory), sorted by region and
    /// begin, merged; null: the capture did not say (a file feeder, a machine without a screen)
    const std::vector<TTDScreenRange>* screen = nullptr;
};

}  // namespace ttd
