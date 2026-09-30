#include "stdafx.h"

#include "ttdevops2.h"

#include <cstring>

#include "emulator/memory/atm/evoavr.h"

namespace ttd
{

size_t TTDEvoPs2::TTDStateSize() const
{
    return sizeof(EvoAvr::Ps2State);
}

void TTDEvoPs2::TTDSaveState(uint8_t* dst) const
{
    std::memcpy(dst, &_avr.GetPs2State(), sizeof(EvoAvr::Ps2State));
}

void TTDEvoPs2::TTDLoadState(const uint8_t* src)
{
    EvoAvr::Ps2State state{};
    std::memcpy(&state, src, sizeof(state));
    _avr.SetPs2State(state);
}

uint64_t TTDEvoPs2::TTDHashState() const
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(&_avr.GetPs2State());
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < sizeof(EvoAvr::Ps2State); i++)
    {
        h ^= bytes[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

}  // namespace ttd
