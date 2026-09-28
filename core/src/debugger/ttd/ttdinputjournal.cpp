/// @file ttdinputjournal.cpp
/// @brief TTD input event journal — implementation.
///
/// Per parent TDD §5 row #1 and §5.1. See ttdinputjournal.h for the
/// threading model and design rationale.

#include "ttdinputjournal.h"

#include <algorithm>

namespace ttd {

// ---------------------------------------------------------------------------
// Capture path
// ---------------------------------------------------------------------------

void TTDInputJournal::Record(TTDInputEvent ev)
{
    _events.push_back(ev);
}

// ---------------------------------------------------------------------------
// Replay path
// ---------------------------------------------------------------------------

TTDTimePoint TTDInputJournal::PeekNextEventTimeOnOrAfter(const TTDTimePoint& from) const
{
    // Linear scan is fine: the journal is small (a few hundred to a few
    // thousand events for a typical session), and this is called at most
    // once per RunTStates batch during replay, not per instruction.
    for (const auto& ev : _events)
    {
        if (!(ev.time < from))
            return ev.time;
    }
    return TTDTimePoint{};
}

size_t TTDInputJournal::FirstIndexAtOrAfter(const TTDTimePoint& t) const
{
    // The journal is sorted by time (appended in application order)
    auto it = std::lower_bound(_events.begin(), _events.end(), t,
                               [](const TTDInputEvent& ev, const TTDTimePoint& at) { return ev.time < at; });
    return static_cast<size_t>(it - _events.begin());
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void TTDInputJournal::DropAfter(const TTDTimePoint& t)
{
    // Keep events with time <= t (use partition point: find first event
    // strictly greater than t, then erase from there to end).
    auto it = std::find_if(_events.begin(), _events.end(),
                           [&](const TTDInputEvent& ev) { return t < ev.time; });
    _events.erase(it, _events.end());
}

void TTDInputJournal::Clear()
{
    _events.clear();
}

} // namespace ttd
