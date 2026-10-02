#include "sprinterpldstandard.h"

#include "emulator/memory/sprinter/sprintermemory.h"
#include "emulator/ports/models/portdecoder_sprinter.h"

SprinterPldStandard::SprinterPldStandard()
{
    _descriptor.name = "Standard";
    _descriptor.fullHash = kFullHash304;
    _descriptor.headHash = kHeadHash304;
}

bool SprinterPldStandard::ReadCode(PortDecoder_Sprinter& decoder, uint8_t code, uint16_t port, uint8_t& value)
{
    value = decoder.StandardReadCode(code, port);
    return true;
}

bool SprinterPldStandard::WriteCode(PortDecoder_Sprinter& decoder, uint8_t code, uint16_t port, uint8_t value)
{
    decoder.StandardWriteCode(code, port, value);
    return true;
}

bool SprinterPldStandard::UpdateBanks(SprinterMemory& memory, const SprinterPldState& pld)
{
    memory.StandardUpdateBanks(pld);
    return true;
}
