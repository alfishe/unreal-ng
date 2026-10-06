#include "stdafx.h"

#include "ttdevoavrvolatile.h"

#include "emulator/memory/atm/evoavr.h"

namespace ttd
{

/// Blob: version, ext type, EEPROM page, flags (EvoAvr::GetVolatileState), then the /WAIT ports' main-loop resume
/// and EEPROM-ready AVR cycles (EvoAvr::GetWaitState), little-endian u8 each

namespace
{
void PutU64(uint8_t* dst, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        dst[i] = static_cast<uint8_t>(v >> (8 * i));
}

uint64_t GetU64(const uint8_t* src)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(src[i]) << (8 * i);
    return v;
}
}  // namespace

void TTDEvoAvrVolatile::TTDSaveState(uint8_t* dst) const
{
    if (!dst)
        return;
    dst[0] = kVersion;
    _avr.GetVolatileState(dst[1], dst[2], dst[3]);
    const EvoAvrWait::State wait = _avr.GetWaitState();
    PutU64(dst + 4, wait.loopResume);
    PutU64(dst + 12, wait.eepromReadyAt);
}

void TTDEvoAvrVolatile::TTDLoadState(const uint8_t* src)
{
    if (!src || src[0] != kVersion)
        return;
    _avr.SetVolatileState(src[1], src[2], src[3]);
    EvoAvrWait::State wait{};
    wait.loopResume = GetU64(src + 4);
    wait.eepromReadyAt = GetU64(src + 12);
    _avr.SetWaitState(wait);
}

uint64_t TTDEvoAvrVolatile::TTDHashState() const
{
    uint8_t blob[kStateSize];
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
