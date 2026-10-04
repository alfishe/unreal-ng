#include "ttdarena.h"

#include <cassert>
#include <cstring>

namespace ttd
{

TTDArenaRef TTDArena::Store(const uint8_t* bytes, uint32_t size)
{
    TTDArenaRef ref;
    if (size == 0)
        return ref;

    const bool fits = _current != TTDArenaRef::kNone && _chunks[_current] &&
                      _chunks[_current]->capacity - _chunks[_current]->used >= size;
    if (!fits)
    {
        auto chunk = std::make_unique<Chunk>();
        chunk->capacity = size > _nextChunkBytes ? size : _nextChunkBytes;
        if (size <= kChunkBytes && _nextChunkBytes < kChunkBytes)
            _nextChunkBytes *= 2;
        chunk->bytes.reset(new uint8_t[chunk->capacity]);
        uint32_t index;
        if (!_freeIndices.empty())
        {
            index = _freeIndices.back();
            _freeIndices.pop_back();
            _chunks[index] = std::move(chunk);
        }
        else
        {
            index = static_cast<uint32_t>(_chunks.size());
            _chunks.push_back(std::move(chunk));
        }
        // A payload bigger than a chunk does not become the chunk being filled
        if (size <= kChunkBytes)
            _current = index;
        Chunk& c = *_chunks[index];
        std::memcpy(c.bytes.get(), bytes, size);
        c.used = size;
        c.live = size;
        _liveBytes += size;
        ref.chunk = index;
        ref.offset = 0;
        ref.size = size;
        return ref;
    }

    Chunk& c = *_chunks[_current];
    std::memcpy(c.bytes.get() + c.used, bytes, size);
    ref.chunk = _current;
    ref.offset = c.used;
    ref.size = size;
    c.used += size;
    c.live += size;
    _liveBytes += size;
    return ref;
}

void TTDArena::Release(const TTDArenaRef& ref)
{
    if (ref.Empty())
        return;
    assert(ref.chunk < _chunks.size() && _chunks[ref.chunk]);
    Chunk& c = *_chunks[ref.chunk];
    assert(c.live >= ref.size);
    c.live -= ref.size;
    _liveBytes -= ref.size;
    if (c.live == 0)
    {
        if (ref.chunk == _current)
        {
            c.used = 0;   // the chunk being filled starts over instead of being returned
            return;
        }
        _chunks[ref.chunk].reset();
        _freeIndices.push_back(ref.chunk);
    }
}

size_t TTDArena::HeapBytes() const
{
    size_t total = _chunks.capacity() * sizeof(_chunks[0]) + _freeIndices.capacity() * sizeof(uint32_t);
    for (const auto& c : _chunks)
        if (c)
            total += sizeof(Chunk) + c->capacity;
    return total;
}

size_t TTDArena::ChunkCount() const
{
    size_t n = 0;
    for (const auto& c : _chunks)
        n += c ? 1 : 0;
    return n;
}

void TTDArena::Clear()
{
    _chunks.clear();
    _chunks.shrink_to_fit();
    _freeIndices.clear();
    _current = TTDArenaRef::kNone;
    _liveBytes = 0;
    _nextChunkBytes = kFirstChunkBytes;
}

}  // namespace ttd
