#pragma once

/// @file ttdatachannel.h
/// @brief TTD serializer for the IDE board (implementation-plan.md D4): the
/// channel's selected unit, both units' register file, transfer position,
/// buffer and ATAPI sense (AtaDeviceState), and the board adapter's latches.
///
/// The media are not here. Under the media manager's rule
/// (integration-ttd-snapshots.md §2) the media set is fixed while a recording
/// runs and every guest write is a replay barrier, so between barriers the
/// disks hold what they held and replay only needs the controller state.
///
/// Registered for every machine whose IDE board is enabled ([HDD] Scheme).

#include "debugger/ttd/ttdserializable.h"

class EmulatorContext;

namespace ttd
{

class TTDAtaChannel : public TTDSerializable
{
public:
    explicit TTDAtaChannel(EmulatorContext* context) : _context(context) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "AtaChannel"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::AtaChannel; }
    uint64_t TTDHashState() const override;

private:
    EmulatorContext* _context = nullptr;
};

}  // namespace ttd
