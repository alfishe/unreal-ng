#include "debugger/ttd/engine/ttdmediajournal.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace ttd
{

void TTDMediaJournal::Clear()
{
    _mode = Mode::Off;
    _cursor = 0;
    _divergences = 0;
    _records.clear();
    _data.clear();
    _slots.clear();
}

uint16_t TTDMediaJournal::SlotIndex(const std::string& slot)
{
    for (size_t i = 0; i < _slots.size(); ++i)
        if (_slots[i] == slot)
            return static_cast<uint16_t>(i);
    _slots.push_back(slot);
    return static_cast<uint16_t>(_slots.size() - 1);
}

void TTDMediaJournal::Append(uint64_t frame, uint32_t tInFrame, const std::string& slot, uint64_t lba,
                             const uint8_t* bytes, size_t size)
{
    Record r;
    r.frame = frame;
    r.tInFrame = tInFrame;
    r.slot = SlotIndex(slot);
    r.lba = lba;
    r.offset = _data.size();
    r.size = static_cast<uint32_t>(size);
    _data.insert(_data.end(), bytes, bytes + size);
    _records.push_back(r);
}

void TTDMediaJournal::TruncateTo(uint64_t count)
{
    if (count >= _records.size())
        return;
    _data.resize(static_cast<size_t>(_records[static_cast<size_t>(count)].offset));
    _records.resize(static_cast<size_t>(count));
    _cursor = std::min<uint64_t>(_cursor, count);
}

uint64_t TTDMediaJournal::CountUpTo(uint64_t frame, uint32_t tInFrame) const
{
    const auto it = std::upper_bound(_records.begin(), _records.end(), std::make_pair(frame, tInFrame),
                                     [](const std::pair<uint64_t, uint32_t>& at, const Record& r) {
                                         return at.first < r.frame || (at.first == r.frame && at.second < r.tInFrame);
                                     });
    return static_cast<uint64_t>(it - _records.begin());
}

bool TTDMediaJournal::PlayNext(const std::string& slot, uint64_t lba, uint8_t* out, size_t size)
{
    if (_mode != Mode::Play || _cursor >= _records.size())
        return false;
    const Record& r = _records[_cursor];
    if (r.lba != lba || r.size != size || _slots[r.slot] != slot)
    {
        ++_divergences;
        return false;
    }
    std::memcpy(out, _data.data() + r.offset, size);
    ++_cursor;
    return true;
}

size_t TTDMediaJournal::HeapBytes() const
{
    size_t bytes = _records.capacity() * sizeof(Record) + _data.capacity();
    for (const std::string& s : _slots)
        bytes += s.capacity();
    return bytes;
}

}  // namespace ttd
