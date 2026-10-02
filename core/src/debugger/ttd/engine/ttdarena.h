#pragma once

/// @file ttdarena.h
/// @brief Where the engine's compressed pieces live.
///
/// Payloads are carved from 1 MB chunks by bumping a pointer, at their exact
/// size, so a 60-byte piece costs 60 bytes and no heap header of its own
/// (median stored pieces are 39-93 bytes). A chunk counts its live bytes and
/// is returned when the last payload in it is released. A payload is named
/// by {chunk, offset, size}, never by an address: in Phase 4 a chunk can be
/// written to the session file and released, and the same reference then
/// names the bytes in the file (engine decision D28).
/// Design: docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md §4.3.

#include <cstdint>
#include <memory>
#include <vector>

namespace ttd
{

struct TTDArenaRef
{
    uint32_t chunk = kNone;
    uint32_t offset = 0;
    uint32_t size = 0;

    static constexpr uint32_t kNone = 0xFFFFFFFFu;
    bool Empty() const { return chunk == kNone; }
};

class TTDArena
{
public:
    static constexpr uint32_t kChunkBytes = 1u << 20;

    /// Copy @p size bytes into the arena. Payloads larger than a chunk get a
    /// chunk of their own
    TTDArenaRef Store(const uint8_t* bytes, uint32_t size);

    /// The stored bytes of @p ref (valid until the payload is released)
    const uint8_t* Data(const TTDArenaRef& ref) const { return _chunks[ref.chunk]->bytes.get() + ref.offset; }

    /// Release a payload; its chunk is returned when nothing in it is live
    void Release(const TTDArenaRef& ref);

    size_t LiveBytes() const { return _liveBytes; }
    /// Heap the arena holds: every allocated chunk
    size_t HeapBytes() const;
    size_t ChunkCount() const;

    void Clear();

private:
    struct Chunk
    {
        std::unique_ptr<uint8_t[]> bytes;
        uint32_t capacity = 0;
        uint32_t used = 0;
        uint32_t live = 0;
    };

    std::vector<std::unique_ptr<Chunk>> _chunks;   ///< null when returned
    std::vector<uint32_t> _freeIndices;            ///< returned chunk slots to reuse
    uint32_t _current = TTDArenaRef::kNone;        ///< chunk being filled
    size_t _liveBytes = 0;
};

}  // namespace ttd
