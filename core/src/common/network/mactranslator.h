#pragma once

/// @file mactranslator.h
/// @brief MAC address translation for the network bridge on a Wi-Fi host adapter (network SN6b, sn6-bridge-design.md
/// §7): an access point accepts frames only from the MAC addresses associated with it, so the cards' frames leave with
/// the host adapter's address, and the LAN's frames for the host address are given back to the card whose IPv4
/// address they carry - the way VirtualBox bridges over Wi-Fi. IPv4 (with ARP and DHCP) works; other protocols do not.
///
///   card -> LAN (Outbound): Ethernet source = host MAC; ARP sender MAC = host MAC; a DHCP request gets the BROADCAST
///                           flag (the server cannot send to the card's own MAC) and a zero UDP checksum (optional in
///                           IPv4); the card's IPv4 source address is learned (address -> card MAC)
///   LAN -> card (Inbound):  a broadcast / multicast stays as it is; a unicast for the host MAC goes to the card whose
///                           learned address it carries (IPv4 destination, ARP target), with the card's MAC put back
///                           (Ethernet destination, ARP target MAC); anything else is the host's own traffic: dropped.
///                           A DHCP ACK teaches the address it gives (yiaddr -> chaddr)
///
/// Runs on the emulator thread; the capture thread only filters with WantsInbound.

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

class MacTranslator
{
public:
    using Mac = std::array<uint8_t, 6>;

    explicit MacTranslator(const Mac& hostMac) : _host(hostMac) {}

    const Mac& HostMac() const { return _host; }
    /// The cards on the switch (their DHCP chaddr is trusted only for these)
    void SetCards(const std::vector<Mac>& cards) { _cards = cards; }

    /// A card's frame, made fit for the Wi-Fi LAN
    std::vector<uint8_t> Outbound(const uint8_t* frame, size_t length);
    /// A LAN frame for a card, with the card's addresses put back; false: not for any card (drop it)
    bool Inbound(std::vector<uint8_t>& frame);

    /// The learned card addresses (IPv4 host byte order -> card MAC)
    const std::map<uint32_t, Mac>& Guests() const { return _guests; }
    std::vector<uint32_t> GuestIps() const;

    /// The capture thread's filter: group frames (not the host's own), and unicasts to the host MAC that carry a
    /// guest address (IPv4 destination or ARP target)
    static bool WantsInbound(const uint8_t* frame, size_t length, const Mac& host, const std::vector<uint32_t>& guestIps);

private:
    bool IsCard(const uint8_t* mac) const;
    void Learn(uint32_t ip, const uint8_t* mac);

    Mac _host{};
    std::vector<Mac> _cards;
    std::map<uint32_t, Mac> _guests;
};
