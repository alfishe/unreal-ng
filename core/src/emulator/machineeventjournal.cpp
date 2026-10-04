#include "emulator/machineeventjournal.h"

#include <algorithm>

void MachineEventJournal::SetEnabled(bool on)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _enabled = on;
}

void MachineEventJournal::Clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _events.clear();
    _appended = 0;
    _dropped = 0;
    _rewound = 0;
}

void MachineEventJournal::NextEpoch()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _epoch++;
}

uint32_t MachineEventJournal::Epoch() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _epoch;
}

void MachineEventJournal::Append(MachineEvent event)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_enabled)
        return;
    event.epoch = _epoch;
    // Back in time in the same epoch (a TTD seek, then the machine ran live): the events after this
    // moment belong to a future that no longer happens
    while (!_events.empty())
    {
        const MachineEvent& last = _events.back();
        if (last.epoch != event.epoch || last.frame < event.frame || (last.frame == event.frame && last.t <= event.t))
            break;
        _events.pop_back();
        _rewound++;
    }
    event.seq = _nextSeq++;
    if (_events.size() >= kCapacity)
    {
        _events.pop_front();
        _dropped++;
    }
    _events.push_back(std::move(event));
    _appended++;
}

MachineEventJournal::Snapshot MachineEventJournal::Read(const Filter& filter) const
{
    Snapshot out;
    std::lock_guard<std::mutex> lock(_mutex);
    out.appended = _appended;
    out.dropped = _dropped;
    out.rewound = _rewound;
    out.held = _events.size();
    out.enabled = _enabled;
    out.epoch = _epoch;

    std::vector<const MachineEvent*> matches;
    for (const MachineEvent& e : _events)
    {
        if (e.seq <= filter.sinceSeq)
            continue;
        if (filter.frameFrom >= 0 && e.frame < static_cast<uint64_t>(filter.frameFrom))
            continue;
        if (filter.frameTo >= 0 && e.frame > static_cast<uint64_t>(filter.frameTo))
            continue;
        if (!filter.kinds.empty() &&
            std::find(filter.kinds.begin(), filter.kinds.end(), std::string(e.kind)) == filter.kinds.end())
            continue;
        matches.push_back(&e);
    }
    out.matched = matches.size();
    const size_t first = (filter.limit && matches.size() > filter.limit) ? matches.size() - filter.limit : 0;
    out.events.reserve(matches.size() - first);
    for (size_t i = first; i < matches.size(); i++)
        out.events.push_back(*matches[i]);
    return out;
}

std::vector<std::string> MachineEventJournal::SplitKinds(const std::string& text)
{
    std::vector<std::string> kinds;
    std::string current;
    for (char c : text)
    {
        if (c == ',' || c == ' ')
        {
            if (!current.empty())
                kinds.push_back(current);
            current.clear();
        }
        else
            current.push_back(c);
    }
    if (!current.empty())
        kinds.push_back(current);
    return kinds;
}
