#include "ttdreftable.h"

#include <cassert>
#include <new>

namespace ttd
{

uint32_t TTDRefTables::DefaultBlockPieces(uint32_t pieces)
{
    if (pieces <= 64)
        return 8;      // up to 256 KB: blocks of 2 pages
    if (pieces <= 256)
        return 16;     // up to 1 MB: blocks of 4 pages
    return 32;         // larger: blocks of 8 pages (E3)
}

TTDRefTables::Block* TTDRefTables::NewBlock(uint32_t count)
{
    const size_t bytes = sizeof(Block) + size_t(count) * sizeof(TTDPieceId);
    Block* b = static_cast<Block*>(::operator new(bytes));
    b->refcount = 1;
    b->count = count;
    for (uint32_t i = 0; i < count; ++i)
        b->Ids()[i] = TTDPieceStore::kNone;
    _liveBlocks++;
    _bytes += bytes;
    return b;
}

void TTDRefTables::FreeBlock(Block* block)
{
    _bytes -= sizeof(Block) + size_t(block->count) * sizeof(TTDPieceId);
    _liveBlocks--;
    ::operator delete(block);
}

void TTDRefTables::ReleaseBlock(Block* block)
{
    assert(block->refcount > 0);
    if (--block->refcount > 0)
        return;
    for (uint32_t i = 0; i < block->count; ++i)
        if (block->Ids()[i] != TTDPieceStore::kNone)
            _store.Release(block->Ids()[i]);
    FreeBlock(block);
}

TTDRefTables::Table* TTDRefTables::NewTable(uint32_t pieces, uint32_t blockPieces)
{
    const uint32_t blockCount = (pieces + blockPieces - 1) / blockPieces;
    const size_t bytes = sizeof(Table) + size_t(blockCount) * sizeof(Block*);
    Table* t = static_cast<Table*>(::operator new(bytes));
    t->refcount = 1;
    t->pieces = pieces;
    t->blockPieces = blockPieces;
    t->blockCount = blockCount;
    _liveTables++;
    _bytes += bytes;
    return t;
}

TTDRefTables::Table* TTDRefTables::Create(uint32_t pieces, uint32_t blockPieces)
{
    assert(blockPieces > 0);
    Table* t = NewTable(pieces, blockPieces);
    for (uint32_t i = 0; i < t->blockCount; ++i)
        t->Blocks()[i] = nullptr;
    return t;
}

void TTDRefTables::Release(Table* table)
{
    assert(table->refcount > 0);
    if (--table->refcount > 0)
        return;
    for (uint32_t i = 0; i < table->blockCount; ++i)
        if (Block* b = table->Blocks()[i])
            ReleaseBlock(b);
    _bytes -= sizeof(Table) + size_t(table->blockCount) * sizeof(Block*);
    _liveTables--;
    ::operator delete(table);
}

TTDRefTables::Table* TTDRefTables::Derive(Table* parent)
{
    Table* t = NewTable(parent->pieces, parent->blockPieces);
    for (uint32_t i = 0; i < t->blockCount; ++i)
    {
        Block* b = parent->Blocks()[i];
        t->Blocks()[i] = b;
        if (b)
            b->refcount++;
    }
    return t;
}

void TTDRefTables::Set(Table* table, uint32_t piece, TTDPieceId id)
{
    assert(piece < table->pieces);
    Block*& slot = table->Blocks()[piece / table->blockPieces];
    if (!slot)
        slot = NewBlock(table->blockPieces);
    else if (slot->refcount > 1)
    {
        // Shared with an older table: clone it, with its own references
        Block* clone = NewBlock(slot->count);
        for (uint32_t i = 0; i < slot->count; ++i)
        {
            clone->Ids()[i] = slot->Ids()[i];
            if (clone->Ids()[i] != TTDPieceStore::kNone)
                _store.AddRef(clone->Ids()[i]);
        }
        slot->refcount--;
        slot = clone;
    }
    TTDPieceId& cell = slot->Ids()[piece % table->blockPieces];
    if (cell != TTDPieceStore::kNone)
        _store.Release(cell);
    cell = id;
}

bool TTDRefTables::OwnsAnyBlock(const Table* table, const Table* parent) const
{
    for (uint32_t i = 0; i < table->blockCount; ++i)
        if (table->Blocks()[i] != parent->Blocks()[i])
            return true;
    return false;
}

}  // namespace ttd
