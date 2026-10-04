#pragma once

#include "emulator/ports/models/sprinter/sprinterpldconfiguration.h"

/// The standard Sp2000 PLD configuration (the bitstream in the BIOS ROM), the
/// first configuration module (Sprinter tdd-ports-memory §6.1; the second is
/// Game, sprinterpldgame.h). It is the base every other module falls through to: the port codes of
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
    /// The Standard builds in the other shipped BIOS images (ROM page #C #0100, the same 59 215-byte layout; the
    /// BIOS 3.06 screen calls its build "Core 1K30 v3.05"): known, so the reports say Standard by the full hash
    static constexpr uint32_t kFullHash306Hf2 = 0xF9F42E59;
    static constexpr uint32_t kFullHash307Beta1 = 0x29641AB3;
    /// The cells #C0-#FF the bitstream brings: the embedded RAM's initial contents (BIOS-TT
    /// sprinter-computer-hard DCP.MIF, addresses #C0-#FF, the low byte; MAME machine_start port_default
    /// has the same values): #Cx 0, #Dx = #10-#1F, #Ex mostly #41 (#E9 = 5, #EA = 2, #EC = #FF, #EE = 0),
    /// #Fx = #00-#0F
    static constexpr uint8_t kCells[64] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
        0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x00, 0x05, 0x02, 0x41, 0xFF, 0x00, 0x00, 0x41,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    };

    SprinterPldStandard();

    const SprinterPldModuleDescriptor& Descriptor() const override { return _descriptor; }

    bool ReadCode(PortDecoder_Sprinter& decoder, uint8_t code, uint16_t port, uint8_t& value) override;
    bool WriteCode(PortDecoder_Sprinter& decoder, uint8_t code, uint16_t port, uint8_t value) override;
    bool UpdateBanks(SprinterMemory& memory, const SprinterPldState& pld) override;
    /// The standard picture (Sprinter tdd-video §3)
    const SprinterVideoRenderer* VideoRenderer() const override;
    /// The standard accelerator (Sprinter tdd-accel-sound-input §1): the decoder's instance
    SprinterAccelerator* Accelerator(PortDecoder_Sprinter& decoder) override;
    /// kCells
    bool InitialCells(uint8_t* cells) const override;

private:
    SprinterPldModuleDescriptor _descriptor;
};
