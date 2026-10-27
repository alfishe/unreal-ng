#include "stdafx.h"

#include "sparsememorydisk.h"

#include <algorithm>
#include <atomic>
#include <cstring>

namespace
{
    bool AllZero(const uint8_t* p, size_t n)
    {
        return std::all_of(p, p + n, [](uint8_t b) { return b == 0; });
    }
}  // namespace

SparseMemoryDisk::SparseMemoryDisk(uint64_t sectors)
    : _sectors(sectors), _leaves(static_cast<size_t>(((sectors + kChunkSectors - 1) / kChunkSectors + kChunksPerLeaf - 1) / kChunksPerLeaf))
{
    // A fresh identity per disk: two blank cards are not the same medium
    static std::atomic<uint64_t> next{1};
    _contentId = 0x5350415253450000ULL ^ (next.fetch_add(1) * 0x9E3779B97F4A7C15ULL);
}

uint8_t* SparseMemoryDisk::Chunk(uint64_t index) const
{
    const Leaf* leaf = _leaves[static_cast<size_t>(index / kChunksPerLeaf)].get();
    return leaf ? (*leaf)[static_cast<size_t>(index % kChunksPerLeaf)].get() : nullptr;
}

uint64_t SparseMemoryDisk::TableBytes() const
{
    uint64_t bytes = _leaves.size() * sizeof(_leaves[0]);
    for (const auto& leaf : _leaves)
        if (leaf)
            bytes += sizeof(Leaf);
    return bytes;
}

bool SparseMemoryDisk::ReadSector(uint64_t lba, uint8_t* dst)
{
    if (lba >= _sectors)
        return false;
    const uint8_t* chunk = Chunk(lba / kChunkSectors);
    if (!chunk)
        std::memset(dst, 0, kSectorSize);
    else
        std::memcpy(dst, chunk + (lba % kChunkSectors) * kSectorSize, kSectorSize);
    return true;
}

bool SparseMemoryDisk::WriteSector(uint64_t lba, const uint8_t* src)
{
    if (lba >= _sectors)
        return false;
    const uint64_t index = lba / kChunkSectors;
    const bool zero = AllZero(src, kSectorSize);
    std::unique_ptr<Leaf>& leaf = _leaves[static_cast<size_t>(index / kChunksPerLeaf)];
    if (!leaf)
    {
        if (zero)
            return true;  // it reads zeros already
        leaf = std::make_unique<Leaf>();
    }
    std::unique_ptr<uint8_t[]>& chunk = (*leaf)[static_cast<size_t>(index % kChunksPerLeaf)];
    if (!chunk)
    {
        if (zero)
            return true;
        chunk.reset(new uint8_t[kChunkSectors * kSectorSize]());
        _stored++;
    }
    std::memcpy(chunk.get() + (lba % kChunkSectors) * kSectorSize, src, kSectorSize);
    if (zero && AllZero(chunk.get(), kChunkSectors * kSectorSize))
    {
        chunk.reset();
        _stored--;
    }
    return true;
}

uint64_t SparseMemoryDisk::ZeroRun(uint64_t lba)
{
    if (lba >= _sectors)
        return 0;
    uint64_t index = lba / kChunkSectors;
    if (Chunk(index))
        return 0;
    const uint64_t chunks = (_sectors + kChunkSectors - 1) / kChunkSectors;
    while (index < chunks)
    {
        if (!_leaves[static_cast<size_t>(index / kChunksPerLeaf)])
        {
            index = (index / kChunksPerLeaf + 1) * kChunksPerLeaf;  // a whole GiB never written
            continue;
        }
        if (Chunk(index))
            break;
        index++;
    }
    return std::min<uint64_t>(_sectors, index * kChunkSectors) - lba;
}
