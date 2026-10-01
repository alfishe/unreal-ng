#pragma once

/// @file ttdatmpaging.h
/// @brief TTD serializer for ATM Turbo 2+ / ATM3 / ZX-Evo BaseConf state.
///
/// Per TDD 6.4: model-specific chipset state goes through the peripheral
/// registry, never into TTDChipsetState - the TTD framework knows no machine.
///
/// What lives here and why TTDChipsetState cannot carry it:
///   - pFFF7[8] IS the ATM memory map (|7ffd|rom|b7b6|b5..b0| per window). A
///     restore without it rebuilds paging from the standard 128K latches alone
///     and lands on entirely different memory.
///   - atmMemSwapped is the A5-A7 <-> A8-A10 address swap: not derivable from
///     any port value, and wrong in either direction silently corrupts every
///     subsequent access.
///   - aFF77 / aFE / aFB are the ATM 4.50 and mode latches; pBD / pBE / pBF the
///     ATM3 (ZX-Evo) ports, with pBF.0 (shaden) also gating the FDC on the bus.
///   - the ZX-Evo AVR's volatile state (extension type, EEPROM page, flags).
///     The clock chip itself - cells, address latch, time base - is its own
///     blob (PeripheralId::Ds12887, debugger/ttd/ttdds12887.h).
///   - atmPalette / atmPaletteRegs / atmBorderBright are the #FF palette RAM
///     and the 4th border bit: every ATM video mode renders through them.

#include "debugger/ttd/ttdserializable.h"

#include <cstddef>

class EmulatorContext;

namespace ttd {

/// @brief Packed ATM paging/port state.
///
/// Field order is chosen so the struct has no implicit padding: TTD blobs are
/// copied and hashed byte-wise, and unnamed padding would carry uninitialized
/// bytes into the stream and the divergence hash.
struct AtmPagingState
{
    uint32_t pFFF7[8];          ///< ATM 7.10 / ATM3 memory map (one entry per window)
    uint32_t aFF77;             ///< ATM mode latch
    uint32_t atmPalette[16];    ///< #FF palette RAM as rendered colors (ABGR)
    uint16_t pBD;               ///< ATM3 #xxBD (pBDl / pBDh union)
    uint8_t  pBE;               ///< ATM3 #xxBE
    uint8_t  pBF;               ///< ATM3 #xxBF (bit 0 = shaden, gates the FDC)
    uint8_t  aFE;               ///< ATM 4.50 system port
    uint8_t  aFB;               ///< ATM 4.50 system port
    uint8_t  atmMemSwapped;     ///< A5-A7 <-> A8-A10 address swap active
    uint8_t  reserved0;         ///< was the clock address latch (now in the Ds12887 blob), always 0
    uint8_t  atmPaletteRegs[16];///< raw #FF palette bytes (ATM3 #BE.0D readback)
    uint8_t  atmBorderBright;   ///< 4th border bit (~A3 of the last border write)
    uint8_t  evoFddMask;        ///< ZX-Evo #13BD virtual-drive mask
    uint8_t  evoAvrExtType;     ///< ZX-Evo AVR extension type (cells 0xF0-0xFF)
    uint8_t  evoAvrEepromPage;  ///< ZX-Evo AVR register A (EEPROM page)
    uint8_t  evoAvrFlags;       ///< ZX-Evo AVR: bit 0 EEPROM mode, bit 1 Caps LED, bit 2 tape-out
    uint8_t  evoInNmi;          ///< ZX-Evo NMI page (RAM #FF) mapped into #0000-#3FFF
    uint8_t  evoNmiEntry;       ///< ZX-Evo: the next accepted NMI is the board's own
    uint8_t  nmiAtIntPending;   ///< board NMI waiting for the frame INT
    uint8_t  evoTrdemu;         ///< ZX-Evo virtual TR-DOS: bit 0 page #FE in, bit 1 swap pending
    uint8_t  evoVgSys;        ///< ZX-Evo D5..D0 of the last OUT (#FF): VG93 system latch, drive in D1..D0
    uint8_t  evoWrProt;         ///< ZX-Evo `#xBF7` write protect, bit i = window i of map 0, bit 4 + i = of map 1
    uint8_t  evoTurboPending;   ///< ZX-Evo clock select waiting for the next M1 refresh
};

static_assert(sizeof(AtmPagingState) == 136, "AtmPagingState layout changed");
static_assert(offsetof(AtmPagingState, evoTurboPending) + 1 == sizeof(AtmPagingState),
              "AtmPagingState has implicit trailing padding");

/// @brief TTDSerializable implementation for ATM paging state.
class TTDAtmPaging : public TTDSerializable
{
public:
    explicit TTDAtmPaging(EmulatorContext* context);

    size_t TTDStateSize() const override { return sizeof(AtmPagingState); }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "AtmPaging"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::AtmPaging; }
    uint64_t TTDHashState() const override;

private:
    /// Fill a blob from live state. Shared by save and hash so the two can
    /// never disagree about what constitutes ATM state.
    AtmPagingState Snapshot() const;

    EmulatorContext* _context;
};

}  // namespace ttd
