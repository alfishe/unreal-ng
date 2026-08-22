/// @file ttd_input_journal.cpp
/// @brief TTD input event journal — implementation.
///
/// Per parent TDD §5 row #1 and §5.1. See ttd_input_journal.h for the
/// threading model and design rationale.

#include "ttd_input_journal.h"

#include <algorithm>

#include "emulator/io/keyboard/keyboard.h"  // Keyboard, ZXKeysEnum

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

size_t TTDInputJournal::InjectDueEvents(Keyboard& keyboard, const TTDTimePoint& now)
{
    size_t injected = 0;
    for (const auto& ev : _events)
    {
        if (ev.time == now)
        {
            // ZXKeysEnum is `enum ZXKeysEnum : uint8_t` (unscoped, explicit
            // underlying type). static_cast is the canonical conversion from
            // the underlying integer type back to the enum.
            const auto key = static_cast<ZXKeysEnum>(ev.key);
            if (ev.pressed)
                keyboard.PressKey(key);
            else
                keyboard.ReleaseKey(key);
            ++injected;
        }
    }
    return injected;
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
