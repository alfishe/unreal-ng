#pragma once

/// @file ttdplus3fdc.h
/// @brief TTD serializer for the +3 floppy controller (uPD765A).
///
/// The controller belongs to Core; the port decoder hands the TTD registry this
/// owned forwarder so the +3's model state (#1FFD latch + FDC) comes from one
/// place. The drives and their disks are in the BetaDisk blob, as on every model.

#include "debugger/ttd/ttdserializable.h"

#include <cstddef>

class EmulatorContext;

namespace ttd
{
class TTDPlus3Fdc : public TTDSerializable
{
public:
    explicit TTDPlus3Fdc(EmulatorContext* context);

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "UPD765"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::Upd765; }
    uint64_t TTDHashState() const override;

private:
    EmulatorContext* _context;
};
} // namespace ttd
