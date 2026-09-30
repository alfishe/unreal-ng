#include "ttdtsconfstate.h"

#include <cstring>

#include "emulator/ports/models/portdecoder_tsconf.h"

namespace ttd {

size_t TTDTsConfState::TTDStateSize() const
{
    return sizeof(TsConfState);
}

void TTDTsConfState::TTDSaveState(uint8_t* dst) const
{
    if (dst)
        std::memcpy(dst, &_decoder.GetState(), sizeof(TsConfState));
}

void TTDTsConfState::TTDLoadState(const uint8_t* src)
{
    if (!src)
        return;

    std::memcpy(&_decoder.GetState(), src, sizeof(TsConfState));
    _decoder.ApplyState();
}

uint64_t TTDTsConfState::TTDHashState() const
{
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&_decoder.GetState());
    uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a offset basis
    for (size_t i = 0; i < sizeof(TsConfState); ++i)
    {
        h ^= static_cast<uint64_t>(bytes[i]);
        h *= 0x100000001b3ULL;           // FNV-1a prime
    }
    return h;
}

}  // namespace ttd
