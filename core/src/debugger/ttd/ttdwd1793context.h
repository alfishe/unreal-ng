#pragma once

/// @file ttdwd1793context.h
/// @brief TTD serializer for the WD1793's command in flight beyond its own blob
/// (PeripheralId::Wd1793Context; WD1793::SaveTransferContext).
///
/// The BetaDisk blob (WD1793::TTDSaveState, 254 bytes) holds the registers, the
/// state machine and the counters, but not the queued command steps (closures)
/// nor where the transfer pointers point: a restore inside a command (an ID
/// search, a sector half read) used to end the command with Not Ready. This
/// blob carries them as tags and disk positions, so the command continues on
/// the same byte. Separate from the BetaDisk blob, whose layout older
/// recordings keep. Registered with the BetaDisk on every Beta machine
/// (ttdmachineperipherals.cpp; the Sprinter first, since phase S7).
///
/// Restored after the BetaDisk blob (the registry restores in ascending id
/// order), whose load empties the queue and the pointers.
///
/// Layout (v1): u1 version, then WD1793::kTransferContextSize bytes (wd1793.cpp).

#include "debugger/ttd/ttdserializable.h"

class WD1793;

namespace ttd
{

class TTDWd1793Context : public TTDSerializable
{
public:
    static constexpr uint8_t kVersion = 1;

    explicit TTDWd1793Context(WD1793& fdc) : _fdc(fdc) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "Wd1793Context"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::Wd1793Context; }
    uint64_t TTDHashState() const override;
    /// Restored after the controller's own blob, whose load empties the queue and the pointers
    TTDDeviceDescriptor TTDDescribe() const override;

private:
    WD1793& _fdc;
};

}  // namespace ttd
