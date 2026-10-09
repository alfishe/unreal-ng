#pragma once

/// @file ttdmachineserialpeer.h
/// @brief TTD serializer for the peer on a machine serial port that is no
/// 16550 on #xxEF (the ATM Turbo 2+ keyboard controller's RS-232: the UART
/// itself is in the controller's blob). The peer part of netstate::Com: the
/// loopback queue, a stream link and its received bytes by journal reference,
/// an ESP module. Variable size: what the fixed arrays do not hold follows as a
/// netstate::Tail (Com::reserved[0] = kHasTail), only when there is something in it.

#include <memory>

#include "debugger/ttd/ttdserializable.h"
#include "emulator/io/network/netstate.h"
#include "emulator/io/network/netstatetail.h"

class EmulatorContext;

namespace ttd
{

class TTDMachineSerialPeer : public TTDSerializable
{
public:
    explicit TTDMachineSerialPeer(EmulatorContext* context);

    /// The largest blob: the fixed part and a tail
    static constexpr size_t kMaxTailBytes = 16u << 20;
    size_t TTDStateSize() const override { return sizeof(netstate::Com) + kMaxTailBytes; }
    bool TTDVariableSize() const override { return true; }
    void TTDSaveStateTo(std::vector<uint8_t>& out) const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "MachineSerialPeer"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::MachineSerialPeer; }

private:
    EmulatorContext* _context = nullptr;
    std::unique_ptr<netstate::Com> _scratch;
    mutable netstate::Tail _tail;
};

}  // namespace ttd
