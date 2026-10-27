#include "ttdwriteindex.h"

#include <algorithm>

#include "debugger/ttd/ttdcompression.h"

namespace ttd
{

void TTDWriteIndex::Append(const TTDWriteRecord& rec)
{
    _open.push_back(rec);
    ++_size;
    if (_open.size() == kWriteBlockRecords)
        Seal();
}

void TTDWriteIndex::Seal()
{
    if (_open.empty())
        return;
    Block b;
    b.firstT = _open.front().globalT;
    b.lastT = _open.back().globalT;
    b.count = static_cast<uint32_t>(_open.size());
    const std::vector<uint8_t> raw = EncodeWriteBlock(_open.data(), b.count);
    b.rawSize = static_cast<uint32_t>(raw.size());
    b.payload = codec::Compress(raw.data(), raw.size());
    b.payload.shrink_to_fit();
    _blocks.push_back(std::move(b));
    _open.clear();
}

const std::vector<TTDWriteRecord>& TTDWriteIndex::Decode(size_t block) const
{
    if (_cachedBlock != static_cast<int64_t>(block))
    {
        const Block& b = _blocks[block];
        std::vector<uint8_t> raw(b.rawSize);
        _cached.clear();
        if (codec::Decompress(b.payload, b.rawSize, raw.data()))
            DecodeWriteBlock(raw, b.count, _cached);
        _cachedBlock = static_cast<int64_t>(block);
    }
    return _cached;
}

std::optional<TTDWriteRecord> TTDWriteIndex::FindLastInRange(
    uint64_t afterT, uint64_t upToT, const std::function<bool(const TTDWriteRecord&)>& pred) const
{
    if (upToT <= afterT)
        return std::nullopt;
    bool pastRange = false;
    // The newest match in @p records within the range; pastRange once a record
    // at or before afterT is seen (nothing older can be in range)
    auto scan = [&](const std::vector<TTDWriteRecord>& records) -> std::optional<TTDWriteRecord> {
        for (size_t k = records.size(); k > 0; --k)
        {
            const TTDWriteRecord& r = records[k - 1];
            if (r.globalT > upToT)
                continue;
            if (r.globalT <= afterT)
            {
                pastRange = true;
                return std::nullopt;
            }
            if (pred(r))
                return r;
        }
        return std::nullopt;
    };
    // Newest first: the open block, then the sealed ones whose span meets the range
    if (auto r = scan(_open))
        return r;
    for (size_t b = _blocks.size(); b > 0 && !pastRange; --b)
    {
        const Block& block = _blocks[b - 1];
        if (block.lastT <= afterT)
            break;
        if (block.firstT > upToT)
            continue;
        if (auto r = scan(Decode(b - 1)))
            return r;
    }
    return std::nullopt;
}

void TTDWriteIndex::ForEach(const std::function<void(const TTDWriteRecord&)>& visit) const
{
    for (size_t b = 0; b < _blocks.size(); ++b)
        for (const TTDWriteRecord& r : Decode(b))
            visit(r);
    for (const TTDWriteRecord& r : _open)
        visit(r);
}

void TTDWriteIndex::Clear()
{
    _blocks.clear();
    _open.clear();
    _segments.clear();
    _size = 0;
    _droppedUpTo = 0;
    _cachedBlock = -1;
    _cached.clear();
}

void TTDWriteIndex::DropBefore(uint64_t globalT)
{
    size_t count = 0;
    uint64_t droppedUpTo = 0;
    while (count < _blocks.size() && _blocks[count].lastT < globalT)
    {
        droppedUpTo = _blocks[count].lastT;
        _size -= _blocks[count].count;
        ++count;
    }
    if (count == 0)
        return;
    _blocks.erase(_blocks.begin(), _blocks.begin() + static_cast<std::ptrdiff_t>(count));
    _cachedBlock = -1;
    _droppedUpTo = std::max(_droppedUpTo, droppedUpTo);
    SetSegments(std::move(_segments));
}

void TTDWriteIndex::Rebuild(const std::vector<TTDWriteRecord>& records)
{
    const uint64_t droppedUpTo = _droppedUpTo;
    std::vector<TTDJournalSegment> segments = std::move(_segments);
    Clear();
    _droppedUpTo = droppedUpTo;
    for (const TTDWriteRecord& r : records)
        Append(r);
    SetSegments(std::move(segments));
}

void TTDWriteIndex::DropAfter(uint64_t globalT)
{
    // The open block's newest records, then whole sealed blocks after the cut;
    // a block the cut falls into becomes the open block again, cut to size
    while (!_open.empty() && _open.back().globalT > globalT)
    {
        _open.pop_back();
        --_size;
    }
    if (_open.empty())
    {
        while (!_blocks.empty() && _blocks.back().firstT > globalT)
        {
            _size -= _blocks.back().count;
            _blocks.pop_back();
        }
        if (!_blocks.empty() && _blocks.back().lastT > globalT)
        {
            std::vector<TTDWriteRecord> records = Decode(_blocks.size() - 1);
            _size -= _blocks.back().count;
            _blocks.pop_back();
            for (const TTDWriteRecord& r : records)
                if (r.globalT <= globalT)
                {
                    _open.push_back(r);
                    ++_size;
                }
        }
    }
    _cachedBlock = -1;
    std::vector<TTDJournalSegment> kept;
    for (TTDJournalSegment s : _segments)
    {
        if (s.from >= globalT)
            continue;
        s.to = std::min(s.to, globalT);
        kept.push_back(s);
    }
    _segments = std::move(kept);
}

void TTDWriteIndex::SetSegments(std::vector<TTDJournalSegment> segments)
{
    // Records at the newest dropped time may be gone with it: covered only after it
    _segments.clear();
    for (TTDJournalSegment s : segments)
    {
        s.from = std::max(s.from, _droppedUpTo);
        if (s.to > s.from)
            _segments.push_back(s);
    }
}

size_t TTDWriteIndex::HeapBytes() const
{
    size_t bytes = _blocks.capacity() * sizeof(Block) + _open.capacity() * sizeof(TTDWriteRecord) +
                   _segments.capacity() * sizeof(TTDJournalSegment) + _cached.capacity() * sizeof(TTDWriteRecord);
    for (const Block& b : _blocks)
        bytes += b.payload.capacity();
    return bytes;
}

}  // namespace ttd
