#include "debugger/ttd/atm/ttdatmiobus.h"

#include <cstring>

#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_atm710.h"

namespace ttd
{

namespace
{
PortDecoder_ATM710* Decoder(EmulatorContext* context)
{
    return context ? dynamic_cast<PortDecoder_ATM710*>(context->pPortDecoder) : nullptr;
}
}  // namespace

void TTDAtmIoBus::TTDSaveState(uint8_t* dst) const
{
    std::memset(dst, 0, TTDStateSize());
    if (PortDecoder_ATM710* decoder = Decoder(_context))
        dst[0] = decoder->IoBusAddress();
}

void TTDAtmIoBus::TTDLoadState(const uint8_t* src)
{
    if (PortDecoder_ATM710* decoder = Decoder(_context))
        decoder->SetIoBusAddress(src[0]);
}

uint64_t TTDAtmIoBus::TTDHashState() const
{
    PortDecoder_ATM710* decoder = Decoder(_context);
    return decoder ? decoder->IoBusAddress() : 0;
}

}  // namespace ttd
