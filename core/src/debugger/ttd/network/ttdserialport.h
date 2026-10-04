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
///
/// The same serializer saves any other 16550 + peer the machine has under its
/// own id: the ATM2IOESP card on the ATM Turbo 2+ INTERNAL I/O connector.

#include <functional>
#include <memory>
#include <string>

#include "debugger/ttd/ttdserializable.h"
#include "emulator/io/network/netstate.h"

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

    size_t TTDStateSize() const override { return _size; }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return _name; }
    PeripheralId TTDPeripheralId() const override { return _id; }
    /// One kind, SerialPort, whatever v1 id the port records under: the
    /// machine's #xxEF port ("uart"), the ATM2IOESP card's ("atm2ioesp.uart"),
    /// the TS AVR's ZiFi line ("zifi.uart")
    TTDDeviceDescriptor TTDDescribe() const override
    {
        TTDDeviceDescriptor d = TTDSerializable::TTDDescribe();
        d.type = TTDDeviceType::SerialPort;
        d.instance = _id == PeripheralId::Atm2IoEsp ? "atm2ioesp.uart" : _id == PeripheralId::ZiFiLine ? "zifi.uart" : "uart";
        return d;
    }
    uint64_t TTDHashState() const override;

private:
    EmulatorContext* _context = nullptr;
    std::function<ComPort*()> _port;
    PeripheralId _id = PeripheralId::SerialPort;
    std::string _name = "SerialPort";
    size_t _size = netstate::kSerialPortShortSize;
    std::unique_ptr<netstate::SerialPort> _scratch;   ///< one buffer for save / load: no allocation per frame
};

}  // namespace ttd
