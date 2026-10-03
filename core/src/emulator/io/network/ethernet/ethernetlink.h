#pragma once

/// @file ethernetlink.h
/// @brief The wire between a frame-level network card (NE2000, later the 3C509B) and the network it is plugged
/// into: the virtual network's Ethernet gateway (network tdd §6.1, §7). Frames carry no preamble and no CRC: the
/// card adds and checks those itself.
///
///   card --Transmit(port, frame)--> IEthernetLink (the gateway, a switch with a router behind it)
///   card <--Offer(frame)----------- IEthernetLink, at the frame boundary; false = no room now, offered again later
///
/// Both run on the emulator thread.

#include <cstddef>
#include <cstdint>
#include <string>

/// The card side: what a switch port talks to
class IEthernetPort
{
public:
    virtual ~IEthernetPort() = default;
    /// A stable name for reports, TTD and automation ("isa2.eth")
    virtual const std::string& PortKey() const = 0;
    /// The station address the card was built with (its PROM / EEPROM)
    virtual void StationMac(uint8_t out[6]) const = 0;
    /// A frame arrives on the wire. False: the card cannot store it now (its receive ring is full) - the switch
    /// keeps it. True: taken (stored, or dropped by the card's own rules: stopped, filtered)
    virtual bool Offer(const uint8_t* frame, size_t length) = 0;
};

/// The network side, seen from a card
class IEthernetLink
{
public:
    virtual ~IEthernetLink() = default;
    /// The card put a frame on the wire
    virtual void Transmit(IEthernetPort& from, const uint8_t* frame, size_t length) = 0;
};
