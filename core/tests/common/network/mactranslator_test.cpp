// The network bridge's MAC translation on a Wi-Fi host adapter (network SN6b, mactranslator.h): the card's frames leave
// with the host adapter's MAC (Ethernet source, ARP sender), a DHCP request asks for a broadcast answer; the LAN's
// answers come back to the card by its IPv4 address (learned from its own packets and from the DHCP ACK); the host's
// own traffic stays out; the capture filter keeps only what can be for a guest

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "common/network/mactranslator.h"

namespace
{
const MacTranslator::Mac kHost{0x9C, 0x76, 0x0E, 0x30, 0x18, 0x2E};
const MacTranslator::Mac kCard{0x02, 0x53, 0x50, 0x00, 0x00, 0x02};
const MacTranslator::Mac kRouter{0x74, 0xAC, 0xB9, 0x1B, 0xDE, 0xA4};
const MacTranslator::Mac kBroadcast{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr uint32_t kCardIp = 0xAC101E7A;    // 172.16.30.122
constexpr uint32_t kHostIp = 0xAC100D37;    // 172.16.13.55
constexpr uint32_t kRouterIp = 0xAC101001;  // 172.16.16.1

void Put16(std::vector<uint8_t>& f, size_t at, uint16_t v)
{
    f[at] = static_cast<uint8_t>(v >> 8);
    f[at + 1] = static_cast<uint8_t>(v);
}
void Put32(std::vector<uint8_t>& f, size_t at, uint32_t v)
{
    Put16(f, at, static_cast<uint16_t>(v >> 16));
    Put16(f, at + 2, static_cast<uint16_t>(v));
}
std::vector<uint8_t> Eth(const MacTranslator::Mac& dst, const MacTranslator::Mac& src, uint16_t type, size_t payload)
{
    std::vector<uint8_t> f(14 + payload, 0);
    std::memcpy(f.data(), dst.data(), 6);
    std::memcpy(f.data() + 6, src.data(), 6);
    Put16(f, 12, type);
    return f;
}
std::vector<uint8_t> Arp(const MacTranslator::Mac& dst, const MacTranslator::Mac& src, uint16_t op, const MacTranslator::Mac& sha,
                         uint32_t spa, const MacTranslator::Mac& tha, uint32_t tpa)
{
    std::vector<uint8_t> f = Eth(dst, src, 0x0806, 28);
    Put16(f, 14, 1);
    Put16(f, 16, 0x0800);
    f[18] = 6;
    f[19] = 4;
    Put16(f, 20, op);
    std::memcpy(f.data() + 22, sha.data(), 6);
    Put32(f, 28, spa);
    std::memcpy(f.data() + 32, tha.data(), 6);
    Put32(f, 38, tpa);
    return f;
}
std::vector<uint8_t> Ip(const MacTranslator::Mac& dst, const MacTranslator::Mac& src, uint32_t sip, uint32_t dip, uint8_t proto, size_t payload)
{
    std::vector<uint8_t> f = Eth(dst, src, 0x0800, 20 + payload);
    f[14] = 0x45;
    Put16(f, 16, static_cast<uint16_t>(20 + payload));
    f[23] = proto;
    Put32(f, 26, sip);
    Put32(f, 30, dip);
    return f;
}
std::vector<uint8_t> Dhcp(const MacTranslator::Mac& dst, const MacTranslator::Mac& src, uint32_t sip, uint32_t dip, uint16_t sport,
                          uint16_t dport, uint8_t op, uint32_t yiaddr, const MacTranslator::Mac& chaddr)
{
    std::vector<uint8_t> f = Ip(dst, src, sip, dip, 17, 8 + 240);
    Put16(f, 34, sport);
    Put16(f, 36, dport);
    Put16(f, 38, 8 + 240);
    Put16(f, 40, 0x1234);   // a checksum the translator must clear
    f[42] = op;
    Put32(f, 42 + 16, yiaddr);
    std::memcpy(f.data() + 42 + 28, chaddr.data(), 6);
    return f;
}
}  // namespace

TEST(MacTranslator_Test, DhcpThenTrafficBothWays)
{
    MacTranslator t(kHost);
    t.SetCards({kCard});

    // The card's DISCOVER: source MAC = the host's, BROADCAST flag set, UDP checksum cleared, chaddr kept
    const std::vector<uint8_t> discover = Dhcp(kBroadcast, kCard, 0, 0xFFFFFFFF, 68, 67, 1, 0, kCard);
    const std::vector<uint8_t> out = t.Outbound(discover.data(), discover.size());
    EXPECT_EQ(0, std::memcmp(out.data() + 6, kHost.data(), 6));
    EXPECT_EQ(out[42 + 10] & 0x80, 0x80) << "BROADCAST flag";
    EXPECT_EQ(out[40], 0);
    EXPECT_EQ(out[41], 0);
    EXPECT_EQ(0, std::memcmp(out.data() + 42 + 28, kCard.data(), 6)) << "chaddr stays the card's";
    EXPECT_TRUE(t.Guests().empty()) << "0.0.0.0 is no address";

    // The router's ACK, broadcast: the card learns 172.16.30.122
    std::vector<uint8_t> ack = Dhcp(kBroadcast, kRouter, kRouterIp, 0xFFFFFFFF, 67, 68, 2, kCardIp, kCard);
    ASSERT_TRUE(t.Inbound(ack));
    ASSERT_EQ(t.Guests().count(kCardIp), 1u);
    EXPECT_EQ(t.GuestIps(), std::vector<uint32_t>{kCardIp});

    // The card's ARP for the router: sender MAC = the host's
    const std::vector<uint8_t> who = Arp(kBroadcast, kCard, 1, kCard, kCardIp, {}, kRouterIp);
    const std::vector<uint8_t> whoOut = t.Outbound(who.data(), who.size());
    EXPECT_EQ(0, std::memcmp(whoOut.data() + 22, kHost.data(), 6));
    // The router's reply goes to the host MAC: the card's MAC put back, in Ethernet and ARP
    std::vector<uint8_t> reply = Arp(kHost, kRouter, 2, kRouter, kRouterIp, kHost, kCardIp);
    ASSERT_TRUE(t.Inbound(reply));
    EXPECT_EQ(0, std::memcmp(reply.data(), kCard.data(), 6));
    EXPECT_EQ(0, std::memcmp(reply.data() + 32, kCard.data(), 6));

    // An echo reply for the card's address: to the card; one for the host's own address: dropped
    std::vector<uint8_t> echo = Ip(kHost, kRouter, kRouterIp, kCardIp, 1, 8);
    ASSERT_TRUE(t.Inbound(echo));
    EXPECT_EQ(0, std::memcmp(echo.data(), kCard.data(), 6));
    std::vector<uint8_t> hostOwn = Ip(kHost, kRouter, kRouterIp, kHostIp, 6, 20);
    EXPECT_FALSE(t.Inbound(hostOwn)) << "the host's own traffic";
    std::vector<uint8_t> other = Ip(kRouter, kHost, kHostIp, kRouterIp, 6, 20);
    EXPECT_FALSE(t.Inbound(other)) << "a unicast for someone else";

    // A broadcast ARP asking for the card: through, with the target MAC as it is
    std::vector<uint8_t> ask = Arp(kBroadcast, kRouter, 1, kRouter, kRouterIp, {}, kCardIp);
    EXPECT_TRUE(t.Inbound(ask));
    EXPECT_EQ(0, std::memcmp(ask.data(), kBroadcast.data(), 6));
}

TEST(MacTranslator_Test, CaptureFilterKeepsWhatCanBeForAGuest)
{
    const std::vector<uint32_t> guests{kCardIp};
    auto wants = [&guests](const std::vector<uint8_t>& f) { return MacTranslator::WantsInbound(f.data(), f.size(), kHost, guests); };
    EXPECT_TRUE(wants(Ip(kHost, kRouter, kRouterIp, kCardIp, 1, 8)));
    EXPECT_FALSE(wants(Ip(kHost, kRouter, kRouterIp, kHostIp, 6, 20))) << "the host's own: the bulk of a Wi-Fi capture";
    EXPECT_TRUE(wants(Arp(kHost, kRouter, 2, kRouter, kRouterIp, kHost, kCardIp)));
    EXPECT_FALSE(wants(Arp(kHost, kRouter, 2, kRouter, kRouterIp, kHost, kHostIp)));
    EXPECT_TRUE(wants(Eth(kBroadcast, kRouter, 0x0800, 40)));
    EXPECT_FALSE(wants(Eth(kBroadcast, kHost, 0x0800, 40))) << "the host's (or our) own broadcast, echoed";
    EXPECT_FALSE(wants(Ip(kRouter, kRouter, kRouterIp, kCardIp, 1, 8))) << "not to the host MAC";
}
