#pragma once

/// @file ttdprofixtkbc.h
/// @brief TTD serializer for the ZX Profi PROFI-XT keyboard controller
/// (ProfiXtKbc): the MCS-48 running its firmware (RAM, registers, PSW, ports,
/// timer, interrupt state, PC, clock), the board around it (output latch, WAIT
/// flip-flop, reset line), the XT keyboard's wire (queued set-1 bytes, the frame
/// in flight, typematic repeat, held keys), the controller's time base, and the
/// table engine's matrix. The ROM image is configuration, not state: a blob of
/// the other engine is refused.
/// Design: docs/inprogress/2026-10-01-profi-v3-v5/design.md, section "Keyboard"

#include "debugger/ttd/ttdserializable.h"

class ProfiXtKbc;

namespace ttd
{

class TTDProfiXtKbc : public TTDSerializable
{
public:
    explicit TTDProfiXtKbc(ProfiXtKbc& kbc) : _kbc(kbc) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "ProfiXtKbc"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::ProfiXtKbc; }
    uint64_t TTDHashState() const override;

private:
    ProfiXtKbc& _kbc;
};

}  // namespace ttd
