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

#include <memory>

#include "debugger/ttd/ttdserializable.h"
#include "emulator/io/network/netstate.h"

class EmulatorContext;

namespace ttd
{

class TTDSerialPort : public TTDSerializable
{
public:
    explicit TTDSerialPort(EmulatorContext* context);

    size_t TTDStateSize() const override { return _size; }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "SerialPort"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::SerialPort; }
    uint64_t TTDHashState() const override;

private:
    EmulatorContext* _context = nullptr;
    size_t _size = netstate::kSerialPortShortSize;
    std::unique_ptr<netstate::SerialPort> _scratch;   ///< one buffer for save / load: no allocation per frame
};

}  // namespace ttd
