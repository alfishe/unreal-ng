#include "operationlog.h"

#include <utility>

namespace coordinator
{
uint64_t OperationLog::Append(OperationLogEntry entry)
{
    std::lock_guard<std::mutex> lock(_mutex);
    entry.seq = ++_seq;
    const uint64_t seq = entry.seq;
    if (entry.kind == OpKind::ToolEdit && !_ring.empty())
    {
        OperationLogEntry& newest = _ring[(_next + kCapacity - 1) % kCapacity % _ring.size()];
        if (newest.kind == entry.kind && newest.source == entry.source && newest.mode == entry.mode &&
            newest.status == entry.status && newest.effect == entry.effect && newest.summary == entry.summary &&
            newest.detail == entry.detail)
        {
            newest.seq = seq;
            newest.time = entry.time;
            ++newest.repeats;
            return seq;
        }
    }
    if (_ring.size() < kCapacity)
        _ring.push_back(std::move(entry));
    else
        _ring[_next] = std::move(entry);
    _next = (_next + 1) % kCapacity;
    return seq;
}

std::vector<OperationLogEntry> OperationLog::Entries(uint64_t since) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<OperationLogEntry> out;
    out.reserve(_ring.size());
    // Oldest first: until the ring is full it starts at 0, then at the slot written next
    const size_t start = _ring.size() < kCapacity ? 0 : _next;
    for (size_t i = 0; i < _ring.size(); ++i)
    {
        const OperationLogEntry& e = _ring[(start + i) % _ring.size()];
        if (e.seq > since)
            out.push_back(e);
    }
    return out;
}

uint64_t OperationLog::LastSeq() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _seq;
}
}  // namespace coordinator
