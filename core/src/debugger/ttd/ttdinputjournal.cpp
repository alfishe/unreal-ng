/// @file ttdinputjournal.cpp
/// @brief TTD input event journal — implementation.
///
/// Per parent TDD §5 row #1 and §5.1. See ttdinputjournal.h for the
/// threading model and design rationale.

#include "ttdinputjournal.h"

#include <algorithm>

#include "emulator/io/keyboard/keyboard.h"  // Keyboard, ZXKeysEnum
#include "emulator/io/mouse/mouse.h"        // Mouse

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
    return InjectDueEvents(&keyboard, nullptr, now);
}

bool TTDInputJournal::Apply(const TTDInputEvent& ev, Keyboard* keyboard, Mouse* mouse)
{
    switch (ev.kind)
    {
        case TTDInputKind::Key:
            if (!keyboard)
                return false;
            {
                // ZXKeysEnum is `enum ZXKeysEnum : uint8_t` (unscoped, explicit
                // underlying type). static_cast is the canonical conversion from
                // the underlying integer type back to the enum.
                const auto key = static_cast<ZXKeysEnum>(ev.key);
                if (ev.pressed)
                    keyboard->PressKey(key);
                else
                    keyboard->ReleaseKey(key);
            }
            break;

        case TTDInputKind::KeyboardReset:
            if (!keyboard)
                return false;
            keyboard->Reset();
            break;

        case TTDInputKind::MouseMove:
            if (!mouse)
                return false;
            mouse->Move(ev.dx, ev.dy);
            break;

        case TTDInputKind::MouseButtons:
            if (!mouse)
                return false;
            mouse->SetButtons(ev.buttonMask);
            break;

        case TTDInputKind::MouseWheel:
            if (!mouse)
                return false;
            mouse->SetWheel(ev.wheelSteps);
            break;

        case TTDInputKind::MouseCounters:
            if (!mouse)
                return false;
            mouse->SetCounters(static_cast<uint8_t>(ev.dx), static_cast<uint8_t>(ev.dy));
            break;
    }
    return true;
}

size_t TTDInputJournal::InjectDueEvents(Keyboard* keyboard, Mouse* mouse, const TTDTimePoint& now)
{
    size_t injected = 0;
    for (const auto& ev : _events)
    {
        if (!(ev.time == now))
            continue;

        if (Apply(ev, keyboard, mouse))
            ++injected;
    }
    return injected;
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
