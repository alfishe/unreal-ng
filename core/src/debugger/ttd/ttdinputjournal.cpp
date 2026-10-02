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
    ev.netIndex = 0;
    _events.push_back(ev);
}

void TTDInputJournal::Record(TTDInputEvent ev, TTDNetInput net, const uint8_t* payload, uint32_t length)
{
    net.payloadOffset = static_cast<uint32_t>(_payload.size());
    net.payloadLength = payload ? length : 0;
    if (net.payloadLength)
        _payload.insert(_payload.end(), payload, payload + net.payloadLength);
    net.journalIndex = static_cast<uint32_t>(_net.size() + 1);
    _net.push_back(net);
    ev.netIndex = static_cast<uint32_t>(_net.size());
    _events.push_back(ev);
}

const TTDNetInput* TTDInputJournal::NetOf(const TTDInputEvent& ev) const
{
    if (ev.netIndex == 0 || ev.netIndex > _net.size())
        return nullptr;
    return &_net[ev.netIndex - 1];
}

const uint8_t* TTDInputJournal::PayloadOf(const TTDNetInput& net) const
{
    if (net.payloadLength == 0)
        return nullptr;
    const uint64_t end = static_cast<uint64_t>(net.payloadOffset) + net.payloadLength;
    if (end > _payload.size())
        return nullptr;
    return _payload.data() + net.payloadOffset;
}

void TTDInputJournal::Assign(std::vector<TTDInputEvent> events, std::vector<TTDNetInput> net,
                             std::vector<uint8_t> payload)
{
    _events = std::move(events);
    _net = std::move(net);
    _payload = std::move(payload);
    for (size_t i = 0; i < _net.size(); ++i)
        _net[i].journalIndex = static_cast<uint32_t>(i + 1);
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

    // Network records and bytes are in event order: keep them up to the last kept event's
    uint32_t keepNet = 0;
    for (const TTDInputEvent& ev : _events)
        keepNet = std::max(keepNet, ev.netIndex);
    _net.resize(std::min<size_t>(keepNet, _net.size()));
    size_t keepBytes = 0;
    for (const TTDNetInput& n : _net)
        keepBytes = std::max<size_t>(keepBytes, static_cast<size_t>(n.payloadOffset) + n.payloadLength);
    _payload.resize(std::min(keepBytes, _payload.size()));
}

void TTDInputJournal::DropBefore(const TTDTimePoint& t)
{
    auto it = std::find_if(_events.begin(), _events.end(), [&](const TTDInputEvent& ev) { return !(ev.time < t); });
    if (it == _events.begin())
        return;
    _events.erase(_events.begin(), it);

    // Network records are numbered from 1 in journal order (0 = none): drop the ones
    // before the first kept event's, renumber the rest, move their bytes down
    uint32_t firstNet = 0;
    for (const TTDInputEvent& ev : _events)
        if (ev.netIndex != 0 && (firstNet == 0 || ev.netIndex < firstNet))
            firstNet = ev.netIndex;
    const size_t dropNet = firstNet == 0 ? _net.size() : std::min<size_t>(firstNet - 1, _net.size());
    if (dropNet == 0)
        return;
    _net.erase(_net.begin(), _net.begin() + static_cast<std::ptrdiff_t>(dropNet));
    const size_t dropBytes = _net.empty() ? _payload.size() : std::min<size_t>(_net.front().payloadOffset, _payload.size());
    _payload.erase(_payload.begin(), _payload.begin() + static_cast<std::ptrdiff_t>(dropBytes));
    for (TTDNetInput& n : _net)
        n.payloadOffset -= static_cast<decltype(n.payloadOffset)>(dropBytes);
    for (TTDInputEvent& ev : _events)
        if (ev.netIndex != 0)
            ev.netIndex -= static_cast<uint32_t>(dropNet);
}

void TTDInputJournal::Clear()
{
    _events.clear();
    _net.clear();
    _payload.clear();
}

} // namespace ttd
