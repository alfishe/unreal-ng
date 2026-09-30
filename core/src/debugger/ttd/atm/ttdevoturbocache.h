#pragma once

/// @file ttdevoturbocache.h
/// @brief TTD serializer for the ZX-Evo's DRAM cache at 14 MHz (EvoTurboOverlay): the code and data words and
/// their valid flags. Whether a read waits depends on them, so a seek must restore them for replay to keep its
/// timing (docs/inprogress/2026-09-29-machine-waits/tdd.md section 3).
///
/// Restoring also installs or removes the overlay for the restored clock rate: the chipset state (the rate) is
/// restored before the peripherals, as a plain field copy that does not run the decoder.

#include "debugger/ttd/ttdserializable.h"

class PortDecoder_ATM3;

namespace ttd
{

class TTDEvoTurboCache : public TTDSerializable
{
public:
    explicit TTDEvoTurboCache(PortDecoder_ATM3& decoder) : _decoder(decoder) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "EvoTurboCache"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::EvoTurboCache; }
    uint64_t TTDHashState() const override;

private:
    PortDecoder_ATM3& _decoder;
};

}  // namespace ttd
