#pragma once

/// @file ttdethernetnics.h
/// @brief TTD serializer of the frame-level network cards (PeripheralId::EthernetNics = 45; network tdd §13): every
/// Ethernet card in an expansion slot (IEthernetCard: the NE2000 boards - DP8390 registers, DMA pointers, packet RAM,
/// 93C46 EEPROM, RTL8019AS page 3, the transmit in flight, the station address; the 3C509B - its ID sequence state,
/// windows, both FIFOs with their packets, TX status stack, statistics, the EEPROM image) and the Ethernet gateway.
/// Registered while NetworkManager has slot cards; looks them up at every call.
///
/// Layout v2 (network SN5): u8 version 2, u8 card count, per card: u8 key length, the key ("isa2.eth"), u8 kind length,
/// the kind ("ne2000" | "el3c509b"), u32 state length, the card's state (IEthernetCard::SaveCardState); then u32
/// length + the Ethernet gateway's tables (EthernetGateway::SaveState: ARP pairs, TCP connections with their sequence
/// numbers, timers and queued host bytes, UDP / ICMP flows, listeners, frames waiting for a card; length 0 without a
/// gateway). Variable size. Layout v1 (SN1-SN4, NE2000 only) still loads: per card the key, then the fixed
/// Ne2000Board::SaveState. A blob whose cards (keys, kinds, order, variants) differ from the fitted ones is not loaded.

#include <vector>

#include "debugger/ttd/ttdserializable.h"

class EmulatorContext;

namespace ttd
{

class TTDEthernetNics : public TTDSerializable
{
public:
    static constexpr uint8_t kVersion = 2;

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
