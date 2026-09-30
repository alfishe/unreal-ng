#pragma once

/// @file ttdzxnetusb.h
/// @brief TTD serializer for the network adapter set (network adapters TDD
/// §6.3, option A): the ZXNETUSB card ports, the W5300 registers and socket
/// states, bytes written but not sent, and references into the TTD journal
/// for every received byte the chip holds; plus the virtual network's
/// guest-side tables; the COM port (UART registers and FIFOs, its peer's
/// queues, received bytes by journal reference). The loader takes the
/// received bytes from the journal.
///
/// Registered for every machine while a card or a COM port is fitted. Looks
/// the devices up at each call, so a refit between checkpoints is seen.

#include <memory>

#include "debugger/ttd/ttdserializable.h"
#include "emulator/io/network/netstate.h"

class EmulatorContext;

namespace ttd
{

class TTDZxNetUsb : public TTDSerializable
{
public:
    explicit TTDZxNetUsb(EmulatorContext* context);

    size_t TTDStateSize() const override { return sizeof(netstate::Adapters); }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "ZxNetUsb"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::ZxNetUsb; }
    uint64_t TTDHashState() const override;

private:
    EmulatorContext* _context = nullptr;
    std::unique_ptr<netstate::Adapters> _scratch;   ///< one buffer for save / load: no allocation per frame
};

}  // namespace ttd
