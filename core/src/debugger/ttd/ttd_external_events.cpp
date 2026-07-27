#include "ttd_external_events.h"

#include <algorithm>
#include <cstring>

namespace ttd {

void TTDExternalEventJournal::Record(TTDExternalEvent ev)
{
    std::lock_guard<std::mutex> lk(_mutex);
    _events.push_back(ev);
}

const TTDExternalEvent* TTDExternalEventJournal::FirstMarkerInInterval(
    const TTDTimePoint& from,
    const TTDTimePoint& to) const
{
    std::lock_guard<std::mutex> lk(_mutex);

    // Linear scan — the journal is small (typically < 1000 events) and we
    // exit on the first match. A binary search via std::lower_bound would
    // be O(log N) but adds the subtle "first marker with time > from AND
    // time <= to" predicate. The performance gain is irrelevant at this
    // scale; clarity wins.
    //
    // NOTE: returned pointer is only valid while the caller holds no
    // Record/DropAfter/Clear mutation; a concurrent Record could
    // reallocate the buffer and invalidate it. Seek-engine callers
    // hold the emulator pause, so no concurrent Record is possible —
    // but the lock still needs to be held during this scan to defend
    // against WebAPI edits that didn't pause.
    for (const auto& ev : _events)
    {
        // Strictly inside (from, to] means: ev.time > from AND ev.time <= to.
        // TTDTimePoint::operator< gives lexical (frame, tInFrame) ordering.
        if (from < ev.time && !(to < ev.time))
            return &ev;
    }
    return nullptr;
}

void TTDExternalEventJournal::DropAfter(const TTDTimePoint& t)
{
    std::lock_guard<std::mutex> lk(_mutex);

    // Same shape as TTDInputJournal::DropAfter: find the first event with
    // time > t and erase from there to the end. Events at exactly t are
    // kept (they're "part of the past" — Item 5 contract).
    auto it = std::find_if(_events.begin(), _events.end(),
                           [&](const TTDExternalEvent& ev) { return t < ev.time; });
    _events.erase(it, _events.end());
}

void TTDExternalEventJournal::Clear()
{
    std::lock_guard<std::mutex> lk(_mutex);
    _events.clear();
}

const char* TTDExternalEventKindToString(TTDExternalEventKind kind)
{
    switch (kind)
    {
        case TTDExternalEventKind::TapeControl:   return "tape_control";
        case TTDExternalEventKind::DiskWrite:     return "disk_write";
        case TTDExternalEventKind::DebuggerEdit:  return "debugger_edit";
        case TTDExternalEventKind::HardwareReset: return "hardware_reset";
        case TTDExternalEventKind::Other:         return "other";
    }
    return "unknown";
}

} // namespace ttd
