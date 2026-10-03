#pragma once

/// @file ttdethernetnics.h
/// @brief TTD serializer of the frame-level network cards (PeripheralId::EthernetNics = 44; network tdd §13): every
/// NE2000 in an expansion slot - its port key, the DP8390 registers, the remote / local DMA pointers, the packet RAM
/// (bytes until TTD v2 memory regions), the 93C46 EEPROM, the RTL8019AS page 3, the transmit in flight and the
/// station address. Registered while NetworkManager has slot cards; looks them up at every call.
///
/// Layout v1: u8 version, u8 card count, per card: u8 key length, the key ("isa2.eth"), Ne2000Board::SaveState;
/// then u32 length + the Ethernet gateway's tables (EthernetGateway::SaveState: ARP pairs, TCP connections with
/// their sequence numbers, timers and queued host bytes, UDP / ICMP flows, listeners, frames waiting for a card;
/// length 0 without a gateway). Variable size (the gateway's queues). A blob whose cards (keys, order, variants)
/// differ from the fitted ones is not loaded.

#include <vector>

#include "debugger/ttd/ttdserializable.h"

class EmulatorContext;

namespace ttd
{

class TTDEthernetNics : public TTDSerializable
{
public:
    static constexpr uint8_t kVersion = 1;

    explicit TTDEthernetNics(EmulatorContext* context) : _context(context) {}

    /// The bound a blob may reach: the cards and 16 MB of gateway tables
    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    bool TTDVariableSize() const override { return true; }
    void TTDSaveStateTo(std::vector<uint8_t>& out) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "EthernetNics"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::EthernetNics; }
    uint64_t TTDHashState() const override;

private:
    EmulatorContext* _context = nullptr;
};

}  // namespace ttd
