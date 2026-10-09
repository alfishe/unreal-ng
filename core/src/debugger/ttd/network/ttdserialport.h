#pragma once

/// @file ttdserialport.h
/// @brief TTD serializer for the serial port on #xxEF (network TDD §7): the
/// 16550's registers and FIFOs (the ZX-Evo AVR firmware's or a ZX-WiFi
/// card's) and its peer - the loopback queue, a stream's link and its
/// received bytes by journal reference, an ESP module.
///
/// The blob size is chosen when the recording starts (the port's set is fixed
/// while TTD records): the full netstate::SerialPort with a peer, only the
/// header and the UART without one (a ZX-Evo whose COM port is unconnected).
/// With a peer the blob is variable: what the fixed arrays do not hold (a long
/// echo queue, many received runs, received bytes the journal does not have)
/// follows as a netstate::Tail, only when there is something in it.
///
/// The same serializer saves any other 16550 + peer the machine has under its
/// own id: the ATM2IOESP card on the ATM Turbo 2+ INTERNAL I/O connector.

#include <functional>
#include <memory>
#include <string>

#include "debugger/ttd/ttdserializable.h"
#include "emulator/io/network/netstate.h"
#include "emulator/io/network/netstatetail.h"

class ComPort;
class EmulatorContext;

namespace ttd
{

class TTDSerialPort : public TTDSerializable
{
public:
    /// The #xxEF port (EmulatorContext::pComPort)
    explicit TTDSerialPort(EmulatorContext* context);
    /// Another 16550 + peer: `port` finds it at save / load time
    TTDSerialPort(EmulatorContext* context, std::function<ComPort*()> port, PeripheralId id, std::string name);

    /// The largest blob: the fixed part and a tail
    static constexpr size_t kMaxTailBytes = 16u << 20;
    size_t TTDStateSize() const override { return HasPeer() ? _size + kMaxTailBytes : _size; }
    bool TTDVariableSize() const override { return HasPeer(); }
    void TTDSaveStateTo(std::vector<uint8_t>& out) const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return _name; }
    PeripheralId TTDPeripheralId() const override { return _id; }
    /// One kind, SerialPort, whatever v1 id the port records under; the
    /// instance tells the ports of one machine apart: the machine's #xxEF port
    /// ("uart"), the ATM2IOESP card's ("atm2ioesp.uart"), the TS AVR's ZiFi
    /// line ("zifi.uart"), the UART cards in expansion slots 1 / 2 and their
    /// second channels ("slot1.uart", "slot1.uart2", "slot2.uart", "slot2.uart2":
    /// a Sprinter with SprinterESP and a modem, or SprinterSerial's two ports)
    TTDDeviceDescriptor TTDDescribe() const override
    {
        TTDDeviceDescriptor d = TTDSerializable::TTDDescribe();
        d.type = TTDDeviceType::SerialPort;
        d.instance = InstanceOf(_id);
        return d;
    }
    static const char* InstanceOf(PeripheralId id)
    {
        switch (id)
        {
        case PeripheralId::Atm2IoEsp: return "atm2ioesp.uart";
        case PeripheralId::ZiFiLine: return "zifi.uart";
        case PeripheralId::SlotSerial1: return "slot1.uart";
        case PeripheralId::SlotSerial1B: return "slot1.uart2";
        case PeripheralId::SlotSerial2: return "slot2.uart";
        case PeripheralId::SlotSerial2B: return "slot2.uart2";
        default: return "uart";
        }
    }
    uint64_t TTDHashState() const override;

private:
    bool HasPeer() const { return _size == sizeof(netstate::SerialPort); }

    EmulatorContext* _context = nullptr;
    std::function<ComPort*()> _port;
    PeripheralId _id = PeripheralId::SerialPort;
    std::string _name = "SerialPort";
    size_t _size = netstate::kSerialPortShortSize;
    std::unique_ptr<netstate::SerialPort> _scratch;   ///< one buffer for save / load: no allocation per frame
    mutable netstate::Tail _tail;                      ///< the same for the tail
};

}  // namespace ttd
