#include "common/network/mactranslator.h"

#include <algorithm>
#include <cstring>

namespace
{
constexpr uint16_t kTypeIpv4 = 0x0800;
constexpr uint16_t kTypeArp = 0x0806;
constexpr size_t kEth = 14;

uint16_t Get16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t Get32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

/// The IPv4 header's length, 0 when the frame is no IPv4 packet we can read
size_t Ipv4Header(const uint8_t* frame, size_t length)
{
    if (length < kEth + 20 || Get16(frame + 12) != kTypeIpv4 || (frame[kEth] >> 4) != 4)
        return 0;
    const size_t ihl = static_cast<size_t>(frame[kEth] & 0x0F) * 4;
    return (ihl >= 20 && kEth + ihl <= length) ? ihl : 0;
}

/// The UDP payload of a DHCP / BOOTP packet (from `clientPort` to `serverPort`), nullptr otherwise
uint8_t* Dhcp(uint8_t* frame, size_t length, uint16_t srcPort, uint16_t dstPort, size_t* udpAt)
{
    const size_t ihl = Ipv4Header(frame, length);
    if (!ihl || frame[kEth + 9] != 17)
        return nullptr;
    const size_t udp = kEth + ihl;
    if (udp + 8 + 240 > length || Get16(frame + udp) != srcPort || Get16(frame + udp + 2) != dstPort)
        return nullptr;
    if (udpAt)
        *udpAt = udp;
    return frame + udp + 8;
}
}  // namespace

bool MacTranslator::IsCard(const uint8_t* mac) const
{
    return std::any_of(_cards.begin(), _cards.end(), [mac](const Mac& m) { return std::memcmp(m.data(), mac, 6) == 0; });
}

void MacTranslator::Learn(uint32_t ip, const uint8_t* mac)
{
    if (ip == 0 || ip == 0xFFFFFFFFu)
        return;
    Mac m{};
    std::memcpy(m.data(), mac, 6);
    _guests[ip] = m;
}

std::vector<uint32_t> MacTranslator::GuestIps() const
{
    std::vector<uint32_t> out;
    for (const auto& [ip, mac] : _guests)
        out.push_back(ip);
    return out;
}

std::vector<uint8_t> MacTranslator::Outbound(const uint8_t* frame, size_t length)
{
    std::vector<uint8_t> f(frame, frame + length);
    if (length < kEth)
        return f;
    uint8_t card[6];
    std::memcpy(card, f.data() + 6, 6);
    std::memcpy(f.data() + 6, _host.data(), 6);
    const uint16_t type = Get16(f.data() + 12);
    if (type == kTypeArp && length >= kEth + 28 && f[kEth + 4] == 6 && f[kEth + 5] == 4)
    {
        // ARP sender: the card's address as the LAN must see it
        Learn(Get32(f.data() + kEth + 14), card);
        std::memcpy(f.data() + kEth + 8, _host.data(), 6);
    }
    else if (const size_t ihl = Ipv4Header(f.data(), length))
    {
        Learn(Get32(f.data() + kEth + 12), card);
        size_t udp = 0;
        if (uint8_t* dhcp = Dhcp(f.data(), length, 68, 67, &udp))
        {
            dhcp[10] |= 0x80;   // flags: BROADCAST - the answer cannot come to the card's own MAC
            f[udp + 6] = 0;     // UDP checksum: optional in IPv4, and now stale
            f[udp + 7] = 0;
        }
        (void)ihl;
    }
    return f;
}

bool MacTranslator::Inbound(std::vector<uint8_t>& f)
{
    const size_t length = f.size();
    if (length < kEth)
        return false;
    const bool group = (f[0] & 1) != 0;
    const uint16_t type = Get16(f.data() + 12);

    // A DHCP answer gives a card its address: learn it before the frame is judged
    if (Dhcp(f.data(), length, 67, 68, nullptr))
    {
        const uint8_t* dhcp = f.data() + kEth + Ipv4Header(f.data(), length) + 8;
        const uint8_t* chaddr = dhcp + 28;
        if (dhcp[0] == 2 && IsCard(chaddr))
            Learn(Get32(dhcp + 16), chaddr);
    }

    auto cardFor = [this](uint32_t ip) -> const Mac* {
        auto it = _guests.find(ip);
        return it == _guests.end() ? nullptr : &it->second;
    };

    if (type == kTypeArp && length >= kEth + 28 && f[kEth + 4] == 6 && f[kEth + 5] == 4)
    {
        const Mac* card = cardFor(Get32(f.data() + kEth + 24));
        if (card)
        {
            // An ARP for a card's address: the card's MAC as the target
            if (!group)
                std::memcpy(f.data(), card->data(), 6);
            if (std::memcmp(f.data() + kEth + 18, _host.data(), 6) == 0)
                std::memcpy(f.data() + kEth + 18, card->data(), 6);
            return true;
        }
        return group;
    }
    if (group)
        return true;
    if (std::memcmp(f.data(), _host.data(), 6) != 0)
        return false;
    if (Ipv4Header(f.data(), length))
    {
        if (const Mac* card = cardFor(Get32(f.data() + kEth + 16)))
        {
            std::memcpy(f.data(), card->data(), 6);
            return true;
        }
    }
    return false;   // the host's own traffic
}

bool MacTranslator::WantsInbound(const uint8_t* f, size_t length, const Mac& host, const std::vector<uint32_t>& guestIps)
{
    if (length < kEth)
        return false;
    if (std::memcmp(f + 6, host.data(), 6) == 0)
        return false;   // the host's own frame (or ours, echoed)
    if (f[0] & 1)
        return true;
    if (std::memcmp(f, host.data(), 6) != 0)
        return false;
    auto guest = [&guestIps](uint32_t ip) { return std::find(guestIps.begin(), guestIps.end(), ip) != guestIps.end(); };
    const uint16_t type = Get16(f + 12);
    if (type == kTypeArp)
        return length >= kEth + 28 && guest(Get32(f + kEth + 24));
    if (Ipv4Header(f, length))
        return guest(Get32(f + kEth + 16));
    return false;
}
