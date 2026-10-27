#include "stdafx.h"

#include "ttdevoflash.h"

#include "emulator/memory/atm/evoflash.h"

namespace ttd
{

/// Blob (EvoFlash::kStateSize, 33 bytes): u1 version 1, then Flash29F040B::saveState (32 bytes): u1 mode, u1 reserved,
/// u1 flags (bit 0 erasing, 1 writable, 2 modified, 3 AMD), u1 DQ6 toggle, u1 DQ7 source, u1 erase sectors,
/// u1 program value, s8 erase window end, s8 busy end, u3 program offset, reserved to 32

size_t TTDEvoFlash::TTDStateSize() const
{
    return EvoFlash::kStateSize;
}

void TTDEvoFlash::TTDSaveState(uint8_t* dst) const
{
    if (dst)
        _flash.SaveState(dst);
}

void TTDEvoFlash::TTDLoadState(const uint8_t* src)
{
    if (src)
        _flash.LoadState(src);
}

uint64_t TTDEvoFlash::TTDHashState() const
{
    uint8_t blob[EvoFlash::kStateSize];
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
