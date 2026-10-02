#pragma once

#include "emulator/ports/models/sprinter/sprinterpldconfiguration.h"

/// The standard Sp2000 PLD configuration (the bitstream in the BIOS ROM), the
/// first and in v1 the only configuration module (Sprinter tdd-ports-memory
/// §6.1). It is the base every other module falls through to: the port codes of
/// hardware-reference §4.3, MAME's bank formula (tdd-ports-memory §5.1).
///
/// The decoder reaches the standard behavior only through this module, so the
/// extension point is exercised by real code from the first day.
class SprinterPldStandard : public SprinterPldConfiguration
{
public:
    /// BIOS 3.04 bitstream (ROM page #C #0100-#E84E, 59 215 bytes, 473 720 writes):
    /// hashes computed over the loader's writes (sprinterpldconfig.h)
    static constexpr uint32_t kFullHash304 = 0xFC0928F2;
    static constexpr uint32_t kHeadHash304 = 0x78EDDFC6;

    SprinterPldStandard();

    const SprinterPldModuleDescriptor& Descriptor() const override { return _descriptor; }

    bool ReadCode(PortDecoder_Sprinter& decoder, uint8_t code, uint16_t port, uint8_t& value) override;
    bool WriteCode(PortDecoder_Sprinter& decoder, uint8_t code, uint16_t port, uint8_t value) override;
    bool UpdateBanks(SprinterMemory& memory, const SprinterPldState& pld) override;
    /// The standard picture (Sprinter tdd-video §3)
    const SprinterVideoRenderer* VideoRenderer() const override;
    /// The standard accelerator (Sprinter tdd-accel-sound-input §1): the decoder's instance
    SprinterAccelerator* Accelerator(PortDecoder_Sprinter& decoder) override;

private:
    SprinterPldModuleDescriptor _descriptor;
};
