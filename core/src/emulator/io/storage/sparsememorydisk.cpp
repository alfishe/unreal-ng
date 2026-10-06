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

SparseMemoryDisk::SparseMemoryDisk(uint64_t sectors) : _sectors(sectors)
{
    // A fresh identity per disk: two blank cards are not the same medium
    static std::atomic<uint64_t> next{1};
    _contentId = 0x5350415253450000ULL ^ (next.fetch_add(1) * 0x9E3779B97F4A7C15ULL);
}

bool SparseMemoryDisk::ReadSector(uint64_t lba, uint8_t* dst)
{
    if (lba >= _sectors)
        return false;
    const auto it = _chunks.find(lba / kChunkSectors);
    if (it == _chunks.end())
        std::memset(dst, 0, kSectorSize);
    else
        std::memcpy(dst, it->second.get() + (lba % kChunkSectors) * kSectorSize, kSectorSize);
    return true;
}

bool SparseMemoryDisk::WriteSector(uint64_t lba, const uint8_t* src)
{
    if (lba >= _sectors)
        return false;
    const uint64_t index = lba / kChunkSectors;
    auto it = _chunks.find(index);
    const bool zero = AllZero(src, kSectorSize);
    if (it == _chunks.end())
    {
        if (zero)
            return true;  // it reads zeros already
        it = _chunks.emplace(index, std::unique_ptr<uint8_t[]>(new uint8_t[kChunkSectors * kSectorSize]())).first;
    }
    std::memcpy(it->second.get() + (lba % kChunkSectors) * kSectorSize, src, kSectorSize);
    if (zero && AllZero(it->second.get(), kChunkSectors * kSectorSize))
        _chunks.erase(it);
    return true;
}

uint64_t SparseMemoryDisk::ZeroRun(uint64_t lba)
{
    if (lba >= _sectors)
        return 0;
    const uint64_t index = lba / kChunkSectors;
    const auto next = _chunks.lower_bound(index);
    if (next != _chunks.end() && next->first == index)
        return 0;
    const uint64_t end = next == _chunks.end() ? _sectors : std::min(_sectors, next->first * kChunkSectors);
    return end - lba;
}
