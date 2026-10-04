#pragma once

/// @file ethernetcard.h
/// @brief A frame-level network card on an expansion bus: a bus device (IIoBusDevice) that is also a switch port
/// (IEthernetPort) of the Ethernet gateway (network tdd §5.1, §7). The NE2000 boards (Ne2000Board) and the 3Com
/// EtherLink III 3C509B (EtherLink3) are such cards; NetworkManager fits any of them into a machine's slot the same
/// way, plugs it into the gateway and carries its state in the EthernetNics TTD blob.
///
///   machine bus slot --IIoBusDevice--> card --IEthernetPort / IEthernetLink--> EthernetGateway --> virtual network

#include <cstddef>
#include <cstdint>
#include <vector>

#include "emulator/io/iiobusdevice.h"
#include "emulator/io/network/ethernet/ethernetlink.h"

class IEthernetCard : public IIoBusDevice, public IEthernetPort
{
public:
    ~IEthernetCard() override = default;

    /// The wire the card is plugged into (the gateway); null = no cable
    virtual void SetLink(IEthernetLink* link) = 0;
    virtual IEthernetLink* Link() const = 0;

    // --- TTD (EthernetNics blob) -------------------------------------------------------------------------------
    /// The most bytes SaveCardState may write (the blob's size bound)
    virtual size_t CardStateBound() const = 0;
    /// The card's whole state (registers, buffers, EEPROM) as bytes
    virtual void SaveCardState(std::vector<uint8_t>& out) const = 0;
    /// False when the bytes are of another card, variant or version (nothing loaded)
    virtual bool LoadCardState(const uint8_t* src, size_t size) = 0;
};
