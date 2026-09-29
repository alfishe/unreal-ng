#include "stdafx.h"

#include "memorydisk.h"

#include <atomic>
#include <cstring>

namespace
{
    /// Every memory disk is a distinct medium
    std::atomic<uint64_t> g_nextMemoryDiskId{1};
}  // namespace

MemoryDisk::MemoryDisk(uint64_t sectors, bool writable)
    : _sectors(sectors),
      _writable(writable),
      _data(new uint8_t[static_cast<size_t>(sectors * kSectorSize)]()),
      _contentId(0x4D454D0000000000ULL | g_nextMemoryDiskId.fetch_add(1))  // "MEM" + serial
{
}

bool MemoryDisk::ReadSector(uint64_t lba, uint8_t* dst)
{
    if (lba >= _sectors)
        return false;
    std::memcpy(dst, _data.get() + lba * kSectorSize, kSectorSize);
    return true;
}

bool MemoryDisk::WriteSector(uint64_t lba, const uint8_t* src)
{
    if (!_writable || lba >= _sectors)
        return false;
    std::memcpy(_data.get() + lba * kSectorSize, src, kSectorSize);
    return true;
}
