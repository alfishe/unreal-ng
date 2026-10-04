#include "debugger/ttd/ttdpit8253.h"

#include <cstring>

#include "emulator/io/timer/pit8253.h"

namespace ttd
{

size_t TTDPit8253::TTDStateSize() const
{
    return sizeof(Pit8253::State);
}

void TTDPit8253::TTDSaveState(uint8_t* dst) const
{
    std::memcpy(dst, &_chip.GetState(), sizeof(Pit8253::State));
}

void TTDPit8253::TTDLoadState(const uint8_t* src)
{
    Pit8253::State state;
    std::memcpy(&state, src, sizeof(Pit8253::State));
    _chip.SetState(state);
}

}  // namespace ttd
