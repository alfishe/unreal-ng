#include "debugger/history/devicehistory.h"

#include <algorithm>

#include "common/filehelper.h"

namespace history
{

const char* ModeName(Mode mode)
{
    switch (mode)
    {
        case Mode::Ring:
            return "ring";
        case Mode::Capture:
            return "capture";
        default:
            return "off";
    }
}

bool ParseMode(const std::string& text, Mode& mode)
{
    if (text == "off")
        mode = Mode::Off;
    else if (text == "ring")
        mode = Mode::Ring;
    else if (text == "capture")
        mode = Mode::Capture;
    else
        return false;
    return true;
}

StateNode StatsNode(const IDeviceHistory& history)
{
    const Stats stats = history.GetStats();
    StateNode node = StateNode::Object();
    node["device"] = history.Device();
    node["what"] = history.What();
    node["mode"] = ModeName(stats.config.mode);
    node["limit"] = static_cast<int64_t>(stats.config.limit);
    node["written"] = static_cast<int64_t>(stats.written);
    node["kept"] = static_cast<int64_t>(stats.kept);
    node["dropped"] = static_cast<int64_t>(stats.dropped);
    node["paused"] = stats.paused;
    if (!stats.config.stream.empty())
    {
        StateNode stream = StateNode::Object();
        stream["path"] = stats.config.stream;
        stream["limit"] = static_cast<int64_t>(stats.config.streamLimit);
        stream["written"] = static_cast<int64_t>(stats.streamed);
        stream["error"] = stats.streamError;
        node["stream"] = std::move(stream);
    }
    return node;
}

void Registry::Add(IDeviceHistory* history)
{
    std::lock_guard lock(_mutex);
    _histories.push_back(history);
}

void Registry::Remove(IDeviceHistory* history)
{
    std::lock_guard lock(_mutex);
    _histories.erase(std::remove(_histories.begin(), _histories.end(), history), _histories.end());
}

StateNode Registry::List() const
{
    std::lock_guard lock(_mutex);
    StateNode list = StateNode::Array();
    for (const IDeviceHistory* history : _histories)
    {
        list.push(StatsNode(*history));
    }
    return list;
}

namespace detail
{

std::FILE* OpenStream(const std::string& path, std::string& error)
{
    std::FILE* file = FileHelper::OpenFile(path, "wb");  // UTF-8 paths on Windows too
    if (file == nullptr)
    {
        error = "cannot open the stream file " + path;
    }
    return file;
}

}  // namespace detail

}  // namespace history
