#pragma once

/// @file ttdatm2kbc.h
/// @brief TTD serializer for the ATM Turbo 2+ keyboard controller (Atm2Kbc):
/// the MCS-51 running its firmware (internal RAM, SFRs, PC, clock, interrupt
/// and UART state), the board latches around it, the PS/2 keyboard model and
/// the controller's time base. The ROM image is configuration, not state: a
/// blob of another firmware is refused.
/// Design: docs/inprogress/2026-10-01-atm2-keyboard-controller/tdd-atm2-kbc.md §9

#include "debugger/ttd/ttdserializable.h"

class Atm2Kbc;

namespace ttd
{

class TTDAtm2Kbc : public TTDSerializable
{
public:
    explicit TTDAtm2Kbc(Atm2Kbc& kbc) : _kbc(kbc) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "Atm2Kbc"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::Atm2Kbc; }
    uint64_t TTDHashState() const override;
    /// The firmware image (V41, ...) is configuration: its fingerprint, so a
    /// restore on another image is reported as not bit-exact
    TTDDeviceDescriptor TTDDescribe() const override;

private:
    Atm2Kbc& _kbc;
};

}  // namespace ttd
