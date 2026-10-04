#pragma once

/// @file ttdwriteindex.h
/// @brief The engine's write journal: memory writes in compressed blocks, and
/// the spans of the session it covers (D40, Phase 3 J6).
///
/// The journal is recorded on demand: switched on and off during a recording,
/// or built later by replay. Its segments say which spans of machine time it
/// covers; inside them "who wrote this address last" answers from it at once.
/// Records arrive in time order and are kept as v1's file blocks (2,048
/// records in columns, zstd level 1) instead of v1's raw 12-byte ring: about
/// 15-40 times smaller (E7).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "debugger/ttd/ttdwritejournal.h"

namespace ttd
{

/// A span of a session in which every memory write is in the write journal
/// (D40): machine times (GlobalT) after `from` up to and including `to`. The
/// journal is recorded on demand: switched on and off at any instruction, each
/// on-to-off span one segment
struct TTDJournalSegment
{
    uint64_t from = 0;
    uint64_t to = 0;
    bool operator==(const TTDJournalSegment& o) const { return from == o.from && to == o.to; }
};

class TTDWriteIndex
{
public:
    /// A memory write, after every write appended so far (time order)
    void Append(const TTDWriteRecord& rec);
    /// The spans the records cover, oldest first
    void SetSegments(std::vector<TTDJournalSegment> segments) { _segments = std::move(segments); }
    const std::vector<TTDJournalSegment>& Segments() const { return _segments; }

    uint64_t Size() const { return _size; }
    /// The newest record with afterT < globalT <= upToT matching @p pred
    std::optional<TTDWriteRecord> FindLastInRange(uint64_t afterT, uint64_t upToT,
                                                  const std::function<bool(const TTDWriteRecord&)>& pred) const;
    /// Every record in time order
    void ForEach(const std::function<void(const TTDWriteRecord&)>& visit) const;

    void Clear();
    /// Sealed blocks, their directory and the open block
    size_t HeapBytes() const;

private:
    struct Block
    {
        uint64_t firstT = 0;
        uint64_t lastT = 0;
        uint32_t count = 0;
        uint32_t rawSize = 0;
        std::vector<uint8_t> payload;   ///< columns (EncodeWriteBlock), zstd
    };
    void Seal();
    const std::vector<TTDWriteRecord>& Decode(size_t block) const;

    std::vector<Block> _blocks;
    std::vector<TTDWriteRecord> _open;   ///< the newest records, not sealed yet
    std::vector<TTDJournalSegment> _segments;
    uint64_t _size = 0;
    mutable int64_t _cachedBlock = -1;   ///< the last decoded block (queries scan backward)
    mutable std::vector<TTDWriteRecord> _cached;
};

}  // namespace ttd
