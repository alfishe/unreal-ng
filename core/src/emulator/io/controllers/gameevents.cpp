#include "gameevents.h"

#include <algorithm>

namespace controllers
{

void GameEventLog::Define(const std::vector<GameEventInfo>& events, const std::string& profile)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _state = Snapshot{};
    _state.defined = !events.empty();
    _state.profile = _state.defined ? profile : std::string();
    const size_t count = std::min<size_t>(events.size(), GameEventFrame::kMax);
    for (size_t index = 0; index < count; index++)
    {
        Event event;
        event.name = events[index].name;
        event.level = events[index].level;
        _state.events.push_back(std::move(event));
    }
    _revision++;
}

void GameEventLog::Clear()
{
    Define({}, {});
}

void GameEventLog::Record(const GameEventFrame& frame)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_state.defined)
        return;
    _state.frame = frame.frame;
    bool changed = false;
    for (size_t index = 0; index < _state.events.size(); index++)
    {
        Event& event = _state.events[index];
        const bool fired = (frame.fired >> index) & 1u;
        if (event.current != frame.level[index] || event.value != frame.value[index])
            changed = true;
        event.current = frame.level[index];
        event.value = frame.value[index];
        if (!fired)
            continue;
        event.fires++;
        event.lastFrame = frame.frame;
        if (event.level)
            continue;  // a level above 0 for a while is no stream of events
        changed = true;
        if (_state.recent.size() == kRecent)
            _state.recent.erase(_state.recent.begin());
        _state.recent.push_back({++_state.seq, frame.frame, event.name});
    }
    if (changed)
        _revision++;
}

GameEventLog::Snapshot GameEventLog::Read() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _state;
}

uint64_t GameEventLog::Revision() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _revision;
}

}  // namespace controllers
