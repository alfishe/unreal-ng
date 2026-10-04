#pragma once

/// @file ttdplus3paging.h
/// @brief TTD serializer for the +2A/+3 #1FFD paging latch.
///
/// #1FFD picks the high bit of the ROM number (bit 2) and the all-RAM special
/// paging modes (bits 0-2); bit 3 is the disk motor, bit 4 the printer strobe.
/// It is +2A/+3-specific, so the model-agnostic TTDChipsetState does not carry
/// it: without this blob a restore would land in the wrong ROM or RAM layout.
/// #7FFD, including its paging-lock bit, is in the chipset state already.

#include "debugger/ttd/ttdserializable.h"

#include <cstddef>

class EmulatorContext;

namespace ttd
{
/// Explicit filler: TTD blobs are copied and hashed byte-wise
struct Plus3PagingState
{
    uint8_t p1FFD;
    /// The +2A/+3 gate array's latch of the last contended memory byte
    /// (UlaContention::GetLatchedByte): what an unattached port read returns,
    /// software probes it. Recordings made before it read 0 here and keep the
    /// live latch (flag below clear)
    uint8_t floatingBus;
    uint8_t flags;             ///< bit 0: floatingBus is valid
    uint8_t reserved;
};
static_assert(sizeof(Plus3PagingState) == 4, "Plus3PagingState must be 4 bytes");

class TTDPlus3Paging : public TTDSerializable
{
public:
    explicit TTDPlus3Paging(EmulatorContext* context);

    size_t TTDStateSize() const override { return sizeof(Plus3PagingState); }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "Plus3Paging"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::Plus3Paging; }
    uint64_t TTDHashState() const override;

private:
    Plus3PagingState Snapshot() const;

    EmulatorContext* _context;
};
} // namespace ttd
