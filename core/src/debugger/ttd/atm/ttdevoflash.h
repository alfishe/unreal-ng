#pragma once

/// @file ttdevoflash.h
/// @brief TTD serializer for the ZX-Evo's ROM chip as a flash (PeripheralId::EvoFlash; TS-Conf and the ATM3).
///
/// The blob is the chip's command state: mode (read array, unlock steps, autoselect, program / erase set-up, the
/// sector-erase window, busy, failed), the DQ6 toggle, the DQ7 source, the sectors being erased, the pending byte
/// program and the ends of the erase window and of the running operation in base clock t-states. The 512 KB array
/// is the engine region EvoFlash (18); the overlay that routes CPU accesses to the chip is derived and follows after
/// a load (EvoFlash::Sync).

#include "debugger/ttd/ttdserializable.h"

class EvoFlash;

namespace ttd
{

class TTDEvoFlash : public TTDSerializable
{
public:
    explicit TTDEvoFlash(EvoFlash& flash) : _flash(flash) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "EvoFlash"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::EvoFlash; }
    uint64_t TTDHashState() const override;

private:
    EvoFlash& _flash;
};

}  // namespace ttd
