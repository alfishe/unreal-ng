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
///   - cmos_addr is the DS12885 address latch - the CMOS contents themselves
///     are battery-backed configuration rather than per-frame machine state,
///     so they are deliberately NOT captured here (2.3 KB per checkpoint for
///     data the guest changes on the order of once a session).

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
    uint16_t pBD;               ///< ATM3 #xxBD (pBDl / pBDh union)
    uint8_t  pBE;               ///< ATM3 #xxBE
    uint8_t  pBF;               ///< ATM3 #xxBF (bit 0 = shaden, gates the FDC)
    uint8_t  aFE;               ///< ATM 4.50 system port
    uint8_t  aFB;               ///< ATM 4.50 system port
    uint8_t  atmMemSwapped;     ///< A5-A7 <-> A8-A10 address swap active
    uint8_t  cmos_addr;         ///< DS12885 address latch
};

static_assert(sizeof(AtmPagingState) == 44, "AtmPagingState layout changed");
static_assert(offsetof(AtmPagingState, cmos_addr) + 1 == sizeof(AtmPagingState),
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
