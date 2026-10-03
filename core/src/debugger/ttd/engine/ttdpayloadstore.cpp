#include "debugger/ttd/engine/ttdpayloadstore.h"

#include <cassert>

namespace ttd
{

TTDPayloadRef TTDPayloadStore::Store(const uint8_t* bytes, size_t size)
{
    uint32_t index;
    if (!_free.empty())
    {
        index = _free.back();
        _free.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>(_entries.size());
        _entries.emplace_back();
    }
    Entry& e = _entries[index];
    e.bytes.assign(bytes, bytes + size);
    e.refs = 1;
    ++_live;
    _liveBytes += size;
    return TTDPayloadRef{index + 1};
}

void TTDPayloadStore::AddRef(TTDPayloadRef ref)
{
    if (!ref)
        return;
    assert(ref.id <= _entries.size() && _entries[ref.id - 1].refs > 0);
    ++_entries[ref.id - 1].refs;
}

void TTDPayloadStore::Release(TTDPayloadRef ref)
{
    if (!ref)
        return;
    assert(ref.id <= _entries.size() && _entries[ref.id - 1].refs > 0);
    Entry& e = _entries[ref.id - 1];
    if (--e.refs > 0)
        return;
    _liveBytes -= e.bytes.size();
    --_live;
    e.bytes.clear();
    e.bytes.shrink_to_fit();
    _free.push_back(ref.id - 1);
}

const std::vector<uint8_t>& TTDPayloadStore::Bytes(TTDPayloadRef ref) const
{
    static const std::vector<uint8_t> kNone;
    if (!ref || ref.id > _entries.size() || _entries[ref.id - 1].refs == 0)
        return kNone;
    return _entries[ref.id - 1].bytes;
}

uint32_t TTDPayloadStore::RefCount(TTDPayloadRef ref) const
{
    return (!ref || ref.id > _entries.size()) ? 0 : _entries[ref.id - 1].refs;
}

void TTDPayloadStore::Clear()
{
    _entries.clear();
    _free.clear();
    _live = 0;
    _liveBytes = 0;
}

}  // namespace ttd
