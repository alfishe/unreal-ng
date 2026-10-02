#pragma once

/// @file ttdregiontracker.h
/// @brief Which 4 KB pieces of a device's memory were written (engine regions, Phase 1, Step 6).
///
/// A device that owns memory recorded as an engine region (NeoGS RAM and
/// flash, MoonSound wave memory, General Sound RAM, ...) owns one tracker per
/// region and calls Mark from the paths that write that memory. The device
/// keeps a pointer to the tracker only while the engine records (Arm): when
/// nothing records, its write path pays one null-pointer check and nothing
/// else (performance guidelines: zero cost when off; checked by an A/B
/// benchmark per device).
/// Design: docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md §4.7.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ttdregion.h"

namespace ttd
{

class TTDRegionTracker
{
public:
    /// The memory this tracker watches (its device's; the size may be any
    /// multiple of a byte, the last piece may be partial)
    void Bind(uint8_t* memory, size_t bytes)
    {
        _memory = memory;
        _bytes = bytes;
        _dirty.assign((bytes + kTTDPieceSize - 1) / kTTDPieceSize, 0);
    }

    uint8_t* Memory() const { return _memory; }
    size_t Bytes() const { return _bytes; }
    uint32_t Pieces() const { return static_cast<uint32_t>(_dirty.size()); }

    /// A byte at @p offset was written
    void Mark(size_t offset) { _dirty[offset / kTTDPieceSize] = 1; }

    /// @p length bytes from @p offset were written
    void MarkRange(size_t offset, size_t length)
    {
        if (length == 0)
            return;
        const size_t last = (offset + length - 1) / kTTDPieceSize;
        for (size_t p = offset / kTTDPieceSize; p <= last && p < _dirty.size(); ++p)
            _dirty[p] = 1;
    }

    void MarkAll() { std::fill(_dirty.begin(), _dirty.end(), uint8_t(1)); }

    /// Append the pieces written since the last call to @p out and forget them
    void CollectAndClear(std::vector<uint32_t>& out)
    {
        for (size_t p = 0; p < _dirty.size(); ++p)
            if (_dirty[p])
            {
                out.push_back(static_cast<uint32_t>(p));
                _dirty[p] = 0;
            }
    }

    bool IsDirty(uint32_t piece) const { return piece < _dirty.size() && _dirty[piece] != 0; }

private:
    uint8_t* _memory = nullptr;
    size_t _bytes = 0;
    std::vector<uint8_t> _dirty;
};

/// One region a device offers to the engine
struct TTDDeviceRegion
{
    TTDRegionDesc desc;               ///< memory, pieces, bytes, restore hooks
    TTDRegionTracker* tracker = nullptr;
};

/// Implemented by a device whose memory the engine records as regions
class ITTDRegionSource
{
public:
    virtual ~ITTDRegionSource() = default;
    /// The device's regions, with their trackers bound to the live memory
    virtual void TTDRegions(std::vector<TTDDeviceRegion>& out) = 0;
    /// Start (true) or stop (false) marking writes in the trackers
    virtual void TTDArmRegions(bool on) = 0;
    /// Called before each capture: a device that tracks writes its own way
    /// (MoonSound's wave memory keeps a dirty bitmap) moves them into its trackers
    virtual void TTDBeforeCapture() {}
};

}  // namespace ttd
