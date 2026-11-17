#pragma once

/// @file vdac2card.h
/// @brief The TS-Labs VDAC2 card on the TS-Conf IDE connector: an FT812
/// graphics controller (Bridgetek EVE2) behind the board's SPI master.
///
/// Design: docs/inprogress/2026-10-01-tsconf-vdac2/vdac2-integration-design.md
/// ("D-C") §5; hardware: vdac2-tdd.md §2; the chip itself is the eve-emu
/// library (eve-emu-architecture.md), built only with ENABLE_VDAC2.
///
/// What the card is, electrically (vdac2-tdd.md §2.1-§2.2):
///   - The FT812's SPI port hangs on the Z-Controller SPI bus of the Evo
///     together with the SD card. Its chip select is config bit D2 of #77,
///     active high in the register (the FPGA drives ftcs_n = ~D2,
///     [V] zports.v:296-311); the FPGA muxes MISO to the FT812 while it is
///     selected ([V] top.v:1173-1178). So the card is slot 1 of the hub,
///     mask #04, active high (ZControllerSpi::AttachDevice).
///   - The FT812 runs from its own 8 MHz crystal (CLKEXT); its system clock
///     is that times the multiplier the guest programs (CLKSEL), 48-80 MHz in
///     the TS-Labs mode table. It is not tied to the Evo's clocks.
///   - The card's CPLD switches the whole monitor signal between the Evo
///     and the FT812 by msel = V_CONFIG bit 2 ([C] cpld/top.v, vdac2-tdd.md
///     §2.1), and routes INT_N to the Evo's line interrupt while msel = 1.
///
///
/// The chip, its time, picture, capture and TTD are EveHostCard's.

#include <cstdint>
#include <functional>

#include "emulator/platforms/tsconf/evehostcard.h"

class EmulatorContext;

class Vdac2Card : public EveHostCard
{
public:
    /// The card's crystal on the FT812's X1 / X2 (vdac2-tdd.md §2.2)
    static constexpr uint32_t kCrystalHz = 8'000'000;

    /// Where the card sits on the Z-Controller hub (zports.v:296-311)
    static constexpr uint8_t kSpiSlot = 1;
    static constexpr uint8_t kSpiSelectMask = 0x04;  // #77 D2
    static constexpr bool kSpiSelectActiveHigh = true;

    /// `rasterInFrame` returns the raster tact inside the current frame
    /// (0..kFrameTacts) the owner has reached
    Vdac2Card(EmulatorContext* context, std::function<uint32_t()> rasterInFrame);

    /// The Evo's reset button. The card's FT812 has its own power-on reset
    /// and the TS-Conf reset line does not reach the IDE connector's chip
    /// (TO VERIFY on the card's schematic: PD_N wiring), so the chip keeps
    /// running; the SDK's ft_init always starts with PWRDOWN / ACTIVE /
    /// RST_PULSE anyway. The bus side is deselected by the hub's reset
    void Reset() override {}
};
