#pragma once

/// @file ttdmachineserialpeer.h
/// @brief TTD serializer for the peer on a machine serial port that is no
/// 16550 on #xxEF (the ATM Turbo 2+ keyboard controller's RS-232: the UART
/// itself is in the controller's blob). The peer part of netstate::Com: the
/// loopback queue, a stream link and its received bytes by journal reference,
/// an ESP module.

#include <memory>

#include "debugger/ttd/ttdserializable.h"
#include "emulator/io/network/netstate.h"

class EmulatorContext;

namespace ttd
{

class TTDMachineSerialPeer : public TTDSerializable
{
public:
    explicit TTDMachineSerialPeer(EmulatorContext* context);

    size_t TTDStateSize() const override { return sizeof(netstate::Com); }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "MachineSerialPeer"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::MachineSerialPeer; }

private:
    EmulatorContext* _context = nullptr;
    std::unique_ptr<netstate::Com> _scratch;
};

}  // namespace ttd
