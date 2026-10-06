#include "ttdtime.h"

#include <algorithm>
#include <iterator>

namespace ttd
{

int64_t TTDFrameTable::IndexOf(uint64_t frame) const
{
    const auto it = std::lower_bound(_entries.begin(), _entries.end(), frame,
                                     [](const Entry& e, uint64_t f) { return e.frame < f; });
    if (it == _entries.end() || it->frame != frame)
        return -1;
    return static_cast<int64_t>(_base + static_cast<size_t>(it - _entries.begin()));
}

int64_t TTDFrameTable::IndexAtOrBefore(uint64_t frame) const
{
    const auto it = std::upper_bound(_entries.begin(), _entries.end(), frame,
                                     [](uint64_t f, const Entry& e) { return f < e.frame; });
    if (it == _entries.begin())
        return -1;
    return static_cast<int64_t>(_base + static_cast<size_t>(it - _entries.begin()) - 1);
}

bool TTDFrameTable::Start(uint64_t frame, TTDMachineTime& start) const
{
    const int64_t idx = IndexOf(frame);
    if (idx < 0)
        return false;
    start = _entries[static_cast<size_t>(idx) - _base].start;
    return true;
}

bool TTDFrameTable::FrameAt(TTDMachineTime t, uint64_t& frame) const
{
    // The first entry starting after t; the frame before it contains t
    const auto it = std::upper_bound(_entries.begin(), _entries.end(), t,
                                     [](TTDMachineTime v, const Entry& e) { return v < e.start; });
    if (it == _entries.begin())
        return false;
    frame = std::prev(it)->frame;
    return true;
}

}  // namespace ttd
