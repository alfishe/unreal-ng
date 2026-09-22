#pragma once

/// @file ttdprofipaging.h
/// @brief TTD serializer for ZX Profi 1024 model state.
///
/// Per TDD 6.4: model-specific chipset state goes through the peripheral
/// registry, never into TTDChipsetState.
///
/// What lives here and why TTDChipsetState cannot carry it:
///   - pDFFD is the Profi extended paging / mode latch (RAM high bits, SCO, WOROM,
///     CPM, SCR, DS80). A restore without it rebuilds paging from the 128K latches
///     alone and lands on different memory, in the wrong video mode.
///   - profiPalette[16] is the hi-res palette RAM written by OUT (#xx7E); it is not
///     derivable from any latch.
/// The DOS latch (CF_TRDOS) and #7FFD/#FE already travel in TTDChipsetState.

#include "debugger/ttd/ttdserializable.h"

#include <cstddef>

class EmulatorContext;

namespace ttd {

/// @brief Packed Profi model state (no implicit padding: blobs are copied and hashed byte-wise).
struct ProfiPagingState
{
    uint8_t pDFFD;              ///< #DFFD latch
    uint8_t profiPalette[16];   ///< hi-res palette, raw GGGRRRBB per entry
};

static_assert(sizeof(ProfiPagingState) == 17, "ProfiPagingState layout changed");

/// @brief TTDSerializable implementation for Profi model state.
class TTDProfiPaging : public TTDSerializable
{
public:
    explicit TTDProfiPaging(EmulatorContext* context);

    size_t TTDStateSize() const override { return sizeof(ProfiPagingState); }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "ProfiPaging"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::ProfiPaging; }
    uint64_t TTDHashState() const override;

private:
    /// Shared by save and hash so the two can never disagree.
    ProfiPagingState Snapshot() const;

    EmulatorContext* _context;
};

}  // namespace ttd
