#include "stdafx.h"

#include "ttdevoavrvolatile.h"

#include "emulator/memory/atm/evoavr.h"

namespace ttd
{

/// Blob: version, ext type, EEPROM page, flags (EvoAvr::GetVolatileState)

void TTDEvoAvrVolatile::TTDSaveState(uint8_t* dst) const
{
    if (!dst)
        return;
    dst[0] = kVersion;
    _avr.GetVolatileState(dst[1], dst[2], dst[3]);
}

void TTDEvoAvrVolatile::TTDLoadState(const uint8_t* src)
{
    if (!src || src[0] != kVersion)
        return;
    _avr.SetVolatileState(src[1], src[2], src[3]);
}

uint64_t TTDEvoAvrVolatile::TTDHashState() const
{
    uint8_t blob[4];
    TTDSaveState(blob);
    uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a
    for (uint8_t byte : blob)
    {
        h ^= byte;
        h *= 0x100000001b3ULL;
    }
    return h;
}

}  // namespace ttd
