#include "debugger/ttd/ttdusart8251.h"

#include <cstring>

#include "emulator/io/serial/usart8251.h"

namespace ttd
{

size_t TTDUsart8251::TTDStateSize() const
{
    return sizeof(Usart8251::State);
}

void TTDUsart8251::TTDSaveState(uint8_t* dst) const
{
    std::memcpy(dst, &_chip.GetState(), sizeof(Usart8251::State));
}

void TTDUsart8251::TTDLoadState(const uint8_t* src)
{
    Usart8251::State state;
    std::memcpy(&state, src, sizeof(Usart8251::State));
    _chip.SetState(state);
}

}  // namespace ttd
