// libopl4 — default wave memory implementation (core TDD §6).
#include "opl4/wavememory.h"

#include <algorithm>
#include <cstring>

namespace opl4
{

void WaveMemory::Configure(uint32_t romSizeBytes, uint32_t ramSizeBytes)
{
    // Clamp so the configured window always fits the 22-bit space.
    const uint32_t space = kAddressMask + 1;
    if (romSizeBytes > space)
        romSizeBytes = space;
    if (static_cast<uint64_t>(romSizeBytes) + ramSizeBytes > space)
        ramSizeBytes = space - romSizeBytes;

    _romEnd = romSizeBytes;
    _ramEnd = romSizeBytes + ramSizeBytes;

    // Exactly rom+ram bytes, value-initialized (zero fill on miss, D10 of
    // the integration doc). Never a fixed 4 MiB block.
    _data = std::make_unique<uint8_t[]>(_ramEnd);
    _dirtyPages = std::make_unique<uint8_t[]>(kMaxDirtyPages);
    ClearDirty();
}

void WaveMemory::ClearDirty()
{
    std::memset(_dirtyPages.get(), 0, kMaxDirtyPages);
}

uint32_t WaveMemory::DirtyPageCount() const
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < kMaxDirtyPages; i++)
        count += (_dirtyPages[i] != 0) ? 1 : 0;
    return count;
}

void WaveMemory::WriteSram(uint32_t addr, const uint8_t* src, uint32_t len)
{
    // Bulk restore path: byte-for-byte through Write so the dirty bitmap
    // stays consistent with single writes; the host clears it after capture.
    for (uint32_t i = 0; i < len; i++)
        Write(addr + i, src[i]);
}

} // namespace opl4
