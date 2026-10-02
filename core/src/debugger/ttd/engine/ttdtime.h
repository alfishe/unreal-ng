#pragma once

/// @file ttdtime.h
/// @brief Time and positions of the time-travel engine (TimeTravelEngine).
///
/// Machine time is the engine's single time line: main-CPU cycles in
/// top-clock units since the session start (engine decision D20). Frames
/// have no fixed length (D21): the frame table records where each frame
/// starts. A position names a branch, a frame and a point inside it (D15).
/// Design: docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md §4.2.

#include <cstdint>
#include <vector>

namespace ttd
{

/// Main-CPU cycles in top-clock units since the session start
using TTDMachineTime = uint64_t;

/// A point in a recorded history
struct TTDPosition
{
    uint32_t branch = 0;     ///< 0 = the trunk; other branches start where history was resumed or edited
    uint64_t frame = 0;      ///< frame number
    uint64_t tInFrame = 0;   ///< machine time since the frame's start

    bool operator==(const TTDPosition& o) const
    {
        return branch == o.branch && frame == o.frame && tInFrame == o.tInFrame;
    }
    bool operator!=(const TTDPosition& o) const { return !(*this == o); }
};

/// Where each recorded frame starts in machine time. Frames are appended in
/// increasing order; their lengths may differ (Sprinter 320 / 312 lines,
/// turbo switched at a frame boundary, RZX frames)
class TTDFrameTable
{
public:
    void Clear() { _entries.clear(); }

    /// Append the next frame. Refused when the frame number does not grow or
    /// its start lies before the previous frame's start
    bool Append(uint64_t frame, TTDMachineTime start)
    {
        if (!_entries.empty() && (frame <= _entries.back().frame || start < _entries.back().start))
            return false;
        _entries.push_back({frame, start});
        return true;
    }

    size_t Count() const { return _entries.size(); }
    bool Empty() const { return _entries.empty(); }
    uint64_t FirstFrame() const { return _entries.empty() ? 0 : _entries.front().frame; }
    uint64_t LastFrame() const { return _entries.empty() ? 0 : _entries.back().frame; }

    /// Index of @p frame in the table, or -1 when it is not recorded
    int64_t IndexOf(uint64_t frame) const;

    /// Start of @p frame; false when the frame is not recorded
    bool Start(uint64_t frame, TTDMachineTime& start) const;

    /// The recorded frame that contains machine time @p t: the last frame
    /// starting at or before it; false before the first frame
    bool FrameAt(TTDMachineTime t, uint64_t& frame) const;

    size_t HeapBytes() const { return _entries.capacity() * sizeof(Entry); }

private:
    struct Entry
    {
        uint64_t frame;
        TTDMachineTime start;
    };
    std::vector<Entry> _entries;
};

}  // namespace ttd
