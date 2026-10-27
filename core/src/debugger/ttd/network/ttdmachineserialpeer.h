#pragma once

/// @file ttdmachineserialpeer.h
/// @brief TTD serializer for the peer on a machine serial port that is no
/// 16550 on #xxEF (the ATM Turbo 2+ keyboard controller's RS-232: the UART
/// itself is in the controller's blob). The peer part of netstate::Com: the
/// loopback queue, a stream link and its received bytes by journal reference,
/// an ESP module. Variable size: what the fixed arrays do not hold follows as a
/// netstate::Tail (Com::reserved[0] = kHasTail), only when there is something in it.
///
/// One class for every machine port's peer: the first port (MachineSerialPeer), the second (MachineSerialPeer2: the
/// Next's UART 1, the Pi's line) and the joystick connector's (JoystickSerialPeer: NR #0B UART mode). Which one is
/// a constructor argument; the blob is the same netstate::Com for all.

#include <memory>

#include "debugger/ttd/ttdserializable.h"
#include "emulator/io/network/netstate.h"
#include "emulator/io/network/netstatetail.h"

class EmulatorContext;
class ISerialPeer;

namespace ttd
{

class TTDMachineSerialPeer : public TTDSerializable
{
public:
    /// The peer of a machine port: which of the context's peer pointers the serializer follows
    enum class Port : uint8_t
    {
        First,       ///< EmulatorContext::pMachineSerialPeer
        Second,      ///< EmulatorContext::pMachineSerialPeer2
        Joystick     ///< EmulatorContext::pJoystickSerialPeer
    };

    explicit TTDMachineSerialPeer(EmulatorContext* context, Port port = Port::First);

    /// The largest blob: the fixed part and a tail
    static constexpr size_t kMaxTailBytes = 16u << 20;
    size_t TTDStateSize() const override { return sizeof(netstate::Com) + kMaxTailBytes; }
    bool TTDVariableSize() const override { return true; }
    void TTDSaveStateTo(std::vector<uint8_t>& out) const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override;
    PeripheralId TTDPeripheralId() const override;

private:
    ISerialPeer* Peer() const;

    EmulatorContext* _context = nullptr;
    Port _port = Port::First;
    std::unique_ptr<netstate::Com> _scratch;
    mutable netstate::Tail _tail;
};

}  // namespace ttd
