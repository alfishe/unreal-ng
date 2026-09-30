#include "stdafx.h"

#include "ttdevoturbocache.h"

#include "emulator/memory/atm/evoturbooverlay.h"
#include "emulator/ports/models/portdecoder_atm3.h"

namespace ttd
{

namespace
{
    // Layout: [0] version, [1] flags (bit 0 code word valid, bit 1 data word valid),
    // [2..3] code word, [4..5] data word (little-endian, address >> 1)
    constexpr size_t kSize = 6;
    constexpr uint8_t kVersion = 1;
}  // namespace

size_t TTDEvoTurboCache::TTDStateSize() const
{
    return kSize;
}

void TTDEvoTurboCache::TTDSaveState(uint8_t* dst) const
{
    const EvoTurboOverlay::CacheState cache = _decoder.GetTurboCacheState();
    dst[0] = kVersion;
    dst[1] = static_cast<uint8_t>((cache.codeValid ? 0x01 : 0x00) | (cache.dataValid ? 0x02 : 0x00));
    dst[2] = static_cast<uint8_t>(cache.codeWord & 0xFF);
    dst[3] = static_cast<uint8_t>(cache.codeWord >> 8);
    dst[4] = static_cast<uint8_t>(cache.dataWord & 0xFF);
    dst[5] = static_cast<uint8_t>(cache.dataWord >> 8);
}

void TTDEvoTurboCache::TTDLoadState(const uint8_t* src)
{
    if (src[0] != kVersion)
        return;
    EvoTurboOverlay::CacheState cache;
    cache.codeValid = (src[1] & 0x01) != 0;
    cache.dataValid = (src[1] & 0x02) != 0;
    cache.codeWord = static_cast<uint16_t>(src[2] | (src[3] << 8));
    cache.dataWord = static_cast<uint16_t>(src[4] | (src[5] << 8));
    _decoder.SyncTurboWaits();
    _decoder.SetTurboCacheState(cache);
}

uint64_t TTDEvoTurboCache::TTDHashState() const
{
    uint8_t blob[kSize];
    TTDSaveState(blob);
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const uint8_t byte : blob)
    {
        h ^= byte;
        h *= 0x100000001b3ULL;
    }
    return h;
}

}  // namespace ttd
