#include "debugger/ttd/ttdppi8255.h"

#include <cstring>

#include "emulator/io/ppi/ppi8255.h"

namespace ttd
{

size_t TTDPpi8255::TTDStateSize() const
{
    return sizeof(Ppi8255::State);
}

void TTDPpi8255::TTDSaveState(uint8_t* dst) const
{
    std::memcpy(dst, &_ppi.GetState(), sizeof(Ppi8255::State));
}

void TTDPpi8255::TTDLoadState(const uint8_t* src)
{
    Ppi8255::State state;
    std::memcpy(&state, src, sizeof(Ppi8255::State));
    _ppi.SetState(state);
}

}  // namespace ttd
