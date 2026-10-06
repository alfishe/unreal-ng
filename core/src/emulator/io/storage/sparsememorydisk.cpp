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

SparseMemoryDisk::SparseMemoryDisk(uint64_t sectors) : _sectors(sectors), _chunks(static_cast<size_t>((sectors + kChunkSectors - 1) / kChunkSectors))
{
    // A fresh identity per disk: two blank cards are not the same medium
    static std::atomic<uint64_t> next{1};
    _contentId = 0x5350415253450000ULL ^ (next.fetch_add(1) * 0x9E3779B97F4A7C15ULL);
}

bool SparseMemoryDisk::ReadSector(uint64_t lba, uint8_t* dst)
{
    if (lba >= _sectors)
        return false;
    const uint8_t* chunk = _chunks[static_cast<size_t>(lba / kChunkSectors)].get();
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
    std::unique_ptr<uint8_t[]>& chunk = _chunks[static_cast<size_t>(lba / kChunkSectors)];
    const bool zero = AllZero(src, kSectorSize);
    if (!chunk)
    {
        if (zero)
            return true;  // it reads zeros already
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
    size_t index = static_cast<size_t>(lba / kChunkSectors);
    if (_chunks[index])
        return 0;
    while (index < _chunks.size() && !_chunks[index])
        index++;
    return std::min<uint64_t>(_sectors, static_cast<uint64_t>(index) * kChunkSectors) - lba;
}
