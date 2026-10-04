#include "sprinterpldstandard.h"

#include <cstring>

#include "emulator/memory/sprinter/sprintermemory.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/video/sprinter/sprintervideorenderer.h"

SprinterPldStandard::SprinterPldStandard()
{
    _descriptor.name = "Standard";
    _descriptor.fullHash = kFullHash304;
    _descriptor.headHash = kHeadHash304;
    _descriptor.streams = {{kFullHash304, "BIOS 3.04 ROM page #C (= LDConf's STREAM.304)"},
                           {kFullHash306Hf2, "BIOS 3.06 Hotfix 2 ROM page #C"},
                           {kFullHash307Beta1, "BIOS 3.07 BETA 1 ROM page #C"}};
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

SprinterAccelerator* SprinterPldStandard::Accelerator(PortDecoder_Sprinter& decoder)
{
    return &decoder.StandardAccelerator();
}

const SprinterVideoRenderer* SprinterPldStandard::VideoRenderer() const
{
    return &SprinterVideoRenderer::Standard();
}

bool SprinterPldStandard::InitialCells(uint8_t* cells) const
{
    std::memcpy(cells, kCells, sizeof(kCells));
    return true;
}
