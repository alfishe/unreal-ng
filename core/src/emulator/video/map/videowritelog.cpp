#include "videowritelog.h"

namespace videomap
{
VideoLatches VideoFrameLog::StateAt(uint32_t t) const
{
    VideoLatches state = start;
    for (const VideoWrite& w : writes)
    {
        if (w.t > t)
            break;
        state = w.latches;
    }
    return state;
}

void VideoWriteLog::BeginFrame(uint64_t frame, const VideoLatches& start)
{
    _previous = std::move(_current);
    {
        std::lock_guard<std::mutex> lock(_publishMutex);
        _published = _previous;
    }
    _current = VideoFrameLog{};
    _current.frame = frame;
    _current.valid = true;
    _current.start = start;
    _current.writes.reserve(16);
}

void VideoWriteLog::Record(uint32_t t, const VideoLatches& now, uint16_t pc)
{
    if (!_current.valid)
        return;
    const VideoLatches& last = _current.writes.empty() ? _current.start : _current.writes.back().latches;
    if (now == last)
        return;
    if (_current.writes.size() >= kCapacity)
    {
        _current.partial = true;
        return;
    }
    _current.writes.push_back({t, pc, now});
}

void VideoWriteLog::RecordTable(VideoTable table, uint32_t t, uint32_t address, uint16_t pc)
{
    if (!_current.valid || table >= VideoTable::Count)
        return;
    VideoTableWrites& w = _current.tables[static_cast<size_t>(table)];
    if (w.count == 0)
    {
        w.firstT = t;
        w.firstAddress = address;
        w.firstPc = pc;
    }
    w.count++;
    w.lastT = t;
    w.lastAddress = address;
    w.lastPc = pc;
}

VideoFrameLog VideoWriteLog::Published() const
{
    std::lock_guard<std::mutex> lock(_publishMutex);
    return _published;
}
} // namespace videomap
