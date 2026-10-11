#include "mediaundolog.h"

void MediaUndoLog::Reset(uint64_t head)
{
    _entries.clear();
    _newest.clear();
    _newestVersion = 0;
    _bytes = 0;
    _floor = head;
    _top = head;
}

void MediaUndoLog::DropAbove(uint64_t head)
{
    while (!_entries.empty() && _entries.back().version > head)
    {
        _bytes -= SizeOf(_entries.back());
        _entries.pop_back();
    }
    if (_newestVersion > head)
    {
        _newest.clear();
        _newestVersion = 0;
    }
    if (_top > head)
        _top = head;
}

void MediaUndoLog::Reach(uint64_t version)
{
    if (version > _top)
        _top = version;
}

bool MediaUndoLog::Has(uint64_t version, Unit unit, uint64_t index) const
{
    if (version == _newestVersion)
        return _newest.count({unit, index}) != 0;
    for (auto it = _entries.rbegin(); it != _entries.rend() && it->version >= version; ++it)
        if (it->version == version && it->unit == unit && it->index == index)
            return true;
    return false;
}

void MediaUndoLog::Keep(uint64_t version, uint64_t frame, Unit unit, uint64_t index, Image before,
                        std::optional<Image> after)
{
    if (version != _newestVersion)
    {
        _newest.clear();
        _newestVersion = version;
    }
    _newest.insert({unit, index});
    Entry entry;
    entry.version = version;
    entry.frame = frame;
    entry.unit = unit;
    entry.index = index;
    entry.before = std::move(before);
    entry.after = std::move(after);
    _bytes += SizeOf(entry);
    _entries.push_back(std::move(entry));
    Reach(version);
    Trim();
}

bool MediaUndoLog::Reaches(uint64_t head, uint64_t target) const
{
    if (target == head)
        return true;
    if (target < head)
        return target >= _floor;
    if (target > _top)
        return false;
    for (const Entry& e : _entries)
        if (e.version > head && e.version <= target && !e.after)
            return false;
    return true;
}

bool MediaUndoLog::Move(uint64_t head, uint64_t target, IUnits& units)
{
    if (target == head)
        return true;
    bool ok = true;
    if (target < head)
    {
        if (target < _floor)
            return false;
        // Newest first: a unit changed in several versions ends as it was before the oldest of them
        for (auto it = _entries.rbegin(); it != _entries.rend() && it->version > target; ++it)
        {
            if (it->version > head)
                continue;  // went back already
            Image now;
            if (units.Read(it->unit, it->index, now))
            {
                _bytes -= SizeOf(*it);
                it->after = std::move(now);
                _bytes += SizeOf(*it);
            }
            else
                ok = false;
            ok = units.Write(it->unit, it->index, it->before) && ok;
        }
        return ok;  // the budget is kept at the next change: trimming now could cut below the target
    }
    // Forward: every version on the way went back before, so each knows what its units became
    if (target > _top)
        return false;
    for (const Entry& e : _entries)
        if (e.version > head && e.version <= target && !e.after)
            return false;
    for (const Entry& e : _entries)
        if (e.version > head && e.version <= target)
            ok = units.Write(e.unit, e.index, *e.after) && ok;
    return ok;
}

std::optional<bool> MediaUndoLog::SwitchAt(uint64_t head, uint64_t frame) const
{
    const Entry* found = nullptr;
    for (const Entry& e : _entries)
    {
        if (e.version <= head)
            continue;
        if (e.version > head + 1)
            break;
        if (found || e.unit != Unit::Protect || e.frame != frame || !e.after)
            return std::nullopt;
        found = &e;
    }
    if (!found)
        return std::nullopt;
    return std::get<bool>(*found->after);
}

size_t MediaUndoLog::SizeOf(const Image& image)
{
    if (const auto* bytes = std::get_if<std::vector<uint8_t>>(&image))
        return bytes->size();
    if (const auto* track = std::get_if<unrealdisk::DiskImage::RawTrack>(&image))
        return track->rawSize() + track->clockBitmap().size() + track->weakBitmap().size();
    return 1;
}

size_t MediaUndoLog::SizeOf(const Entry& entry) const
{
    return sizeof(Entry) + SizeOf(entry.before) + (entry.after ? SizeOf(*entry.after) : 0);
}

void MediaUndoLog::Trim()
{
    while (_bytes > _budget && !_entries.empty())
    {
        const uint64_t oldest = _entries.front().version;
        while (!_entries.empty() && _entries.front().version == oldest)
        {
            _bytes -= SizeOf(_entries.front());
            _entries.pop_front();
        }
        _floor = oldest;
        if (oldest == _newestVersion)
        {
            _newest.clear();
            _newestVersion = 0;
        }
    }
}
