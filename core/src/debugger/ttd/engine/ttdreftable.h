#pragma once

/// @file ttdreftable.h
/// @brief Copy-on-write reference table: which stored version holds each piece of a region.
///
/// Two levels, both shared (POC 011 experiment E3; engine decision D2):
/// - a block maps a fixed number of pieces to their versions (the block size
///   is set per region: 32 pieces = 8 pages of 16 KB for large memories,
///   fewer for small ones, see DefaultBlockPieces);
/// - a region table points to its blocks.
/// A checkpoint holds one region table per region. Recording a frame clones
/// only the blocks in which a piece got a new version, and the region table
/// only when one of its blocks changed; a region with no change keeps the
/// previous checkpoint's table, so an unchanged frame costs one pointer per
/// region whatever the installed memory (PR-10). A block holds one reference
/// to each version it lists; a table holds one reference to each block; a
/// checkpoint holds one reference to each table. A block whose pieces were
/// never seen is not allocated. Blocks and tables are single allocations of
/// their exact size.
/// Design: docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md §4.4.

#include <cstdint>

#include "ttdpiecestore.h"

namespace ttd
{

class TTDRefTables
{
public:
    /// A block: header, then `count` piece ids
    struct Block
    {
        uint32_t refcount;
        uint32_t count;
        TTDPieceId* Ids() { return reinterpret_cast<TTDPieceId*>(this + 1); }
        const TTDPieceId* Ids() const { return reinterpret_cast<const TTDPieceId*>(this + 1); }
    };

    /// A region table: header, then `blockCount` block pointers (null = no
    /// piece of that block seen yet)
    struct Table
    {
        uint32_t refcount;
        uint32_t pieces;
        uint32_t blockPieces;
        uint32_t blockCount;
        Block** Blocks() { return reinterpret_cast<Block**>(this + 1); }
        Block* const* Blocks() const { return reinterpret_cast<Block* const*>(this + 1); }
    };

    /// The block size for a region of @p pieces when none is given: small
    /// memories get small blocks, so a frame that changes one piece copies a
    /// few ids instead of the whole region's map
    static uint32_t DefaultBlockPieces(uint32_t pieces);

    explicit TTDRefTables(TTDPieceStore& store) : _store(store) {}
    TTDRefTables(const TTDRefTables&) = delete;
    TTDRefTables& operator=(const TTDRefTables&) = delete;

    /// A new table with every piece absent (one reference, the caller's)
    Table* Create(uint32_t pieces, uint32_t blockPieces);

    void AddRef(Table* table) { table->refcount++; }
    /// Drop one reference; the last one frees the table, its blocks' references
    /// and, through them, the versions' references
    void Release(Table* table);

    /// The version of @p piece, or TTDPieceStore::kNone when not seen
    TTDPieceId Get(const Table* table, uint32_t piece) const
    {
        const Block* b = table->Blocks()[piece / table->blockPieces];
        return b ? b->Ids()[piece % table->blockPieces] : TTDPieceStore::kNone;
    }

    /// An editable successor of @p parent for one frame: shares every block.
    /// The caller owns its one reference
    Table* Derive(Table* parent);

    /// Set @p piece of @p table (a table from Create or Derive, not yet shared)
    /// to @p id, taking over the caller's reference to @p id. The block is
    /// cloned first when other tables share it
    void Set(Table* table, uint32_t piece, TTDPieceId id);

    /// Whether @p table has a block of its own, not shared with @p parent
    bool OwnsAnyBlock(const Table* table, const Table* parent) const;

    size_t LiveTables() const { return _liveTables; }
    size_t LiveBlocks() const { return _liveBlocks; }
    /// Bytes of every live table and block
    size_t HeapBytes() const { return _bytes; }

private:
    Block* NewBlock(uint32_t count);
    void FreeBlock(Block* block);
    Table* NewTable(uint32_t pieces, uint32_t blockPieces);
    void ReleaseBlock(Block* block);

    TTDPieceStore& _store;
    size_t _liveTables = 0;
    size_t _liveBlocks = 0;
    size_t _bytes = 0;
};

}  // namespace ttd
