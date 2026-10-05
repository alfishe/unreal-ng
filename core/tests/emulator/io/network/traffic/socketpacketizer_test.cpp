// Socket operations as packets for Wireshark (network #91 T2, socketpacketizer.h): a TCP conversation gets a
// handshake, data segments whose sequence numbers chain, FINs and RSTs; every IPv4 / TCP / UDP checksum is right; the
// data of the segments put back together equals what was sent; UDP and ICMP keep their bytes

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "emulator/io/network/traffic/socketpacketizer.h"

namespace
{
uint16_t G16(const std::vector<uint8_t>& f, size_t at) { return static_cast<uint16_t>((f[at] << 8) | f[at + 1]); }
uint32_t G32(const std::vector<uint8_t>& f, size_t at) { return (static_cast<uint32_t>(G16(f, at)) << 16) | G16(f, at + 2); }
uint16_t Sum(const uint8_t* d, size_t n, uint32_t s = 0)
{
    for (size_t i = 0; i + 1 < n; i += 2)
        s += static_cast<uint32_t>((d[i] << 8) | d[i + 1]);
    if (n & 1)
        s += static_cast<uint32_t>(d[n - 1] << 8);
    while (s >> 16)
        s = (s & 0xFFFF) + (s >> 16);
    return static_cast<uint16_t>(~s);
}

TrafficRecord Op(const std::string& op, bool out, NetProto proto = NetProto::Tcp, const std::string& data = {})
{
    TrafficRecord r;
    r.kind = TrafficRecord::Kind::Socket;
    r.adapter = "zxnetusb";
    r.socket = 3;
    r.proto = proto;
    r.op = op;
    r.out = out;
    r.peer = NetEndpoint{NetIp(93, 184, 216, 34), 80};
    r.bytes.assign(data.begin(), data.end());
    return r;
}

struct Segment
{
    uint32_t src, dst, seq, ack;
    uint8_t flags;
    std::string data;
};
Segment Parse(const std::vector<uint8_t>& f)
{
    EXPECT_EQ(G16(f, 12), 0x0800);
    EXPECT_EQ(Sum(f.data() + 14, 20), 0) << "IPv4 header checksum";
    const size_t total = G16(f, 16);
    const uint32_t src = G32(f, 26), dst = G32(f, 30);
    const uint8_t* t = f.data() + 34;
    const size_t n = total - 20;
    const uint32_t pseudo = (src >> 16) + (src & 0xFFFF) + (dst >> 16) + (dst & 0xFFFF) + 6 + static_cast<uint32_t>(n);
    EXPECT_EQ(Sum(t, n, pseudo), 0) << "TCP checksum";
    return Segment{src, dst, G32(f, 38), G32(f, 42), t[13], std::string(reinterpret_cast<const char*>(t + 20), n - 20)};
}
}  // namespace

TEST(SocketPacketizer_Test, TcpConversationChainsAndReassembles)
{
    SocketPacketizer p;
    const uint32_t me = p.AdapterAddress("zxnetusb");
    EXPECT_EQ(me, NetIp(10, 0, 2, 15));
    std::vector<Segment> s;
    auto add = [&](const TrafficRecord& r) {
        for (const auto& f : p.Packets(r))
            s.push_back(Parse(f));
    };
    add(Op("connect", true));
    add(Op("connected", false));
    const std::string request = "GET / HTTP/1.0\r\n\r\n";
    add(Op("send", true, NetProto::Tcp, request));
    const std::string body(3000, 'x');   // more than one segment
    add(Op("data", false, NetProto::Tcp, body));
    add(Op("peer-closed", false));
    add(Op("close", true));

    ASSERT_EQ(s.size(), 1u + 2u + 1u + 3u + 1u + 1u);
    EXPECT_EQ(s[0].flags, 0x02) << "SYN";
    EXPECT_EQ(s[0].src, me);
    EXPECT_EQ(s[1].flags, 0x12) << "SYN-ACK from the peer";
    EXPECT_EQ(s[1].ack, s[0].seq + 1);
    EXPECT_EQ(s[2].flags, 0x10);
    EXPECT_EQ(s[3].data, request);
    EXPECT_EQ(s[3].seq, s[0].seq + 1);
    std::string got;
    for (size_t i = 4; i < 7; ++i)
    {
        EXPECT_EQ(s[i].seq, s[1].seq + 1 + got.size()) << "segments chain";
        got += s[i].data;
    }
    EXPECT_EQ(got, body);
    EXPECT_EQ(s[7].flags, 0x11) << "FIN-ACK from the peer";
    EXPECT_EQ(s[8].flags, 0x11) << "FIN-ACK from the adapter";
    EXPECT_EQ(s[8].ack, s[7].seq + 1);

    EXPECT_TRUE(p.Packets(Op("close", true)).empty()) << "closed: nothing more on the wire";
    EXPECT_EQ(Parse(p.Packets(Op("reset", false))[0]).flags, 0x14) << "RST-ACK";
}

TEST(SocketPacketizer_Test, UdpIcmpAndWhatGivesNoPacket)
{
    SocketPacketizer p;
    const std::string query = "\x12\x34 dns";
    std::vector<std::vector<uint8_t>> out = p.Packets(Op("sendto", true, NetProto::Udp, query));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0][23], 17);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(out[0].data() + 42), query.size()), query);
    out = p.Packets(Op("datagram", false, NetProto::Udp, "answer"));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(G32(out[0], 26), NetIp(93, 184, 216, 34)) << "from the peer";

    const std::string echo = std::string("\x08\x00\x00\x00", 4) + "ping";
    out = p.Packets(Op("echo", true, NetProto::Icmp, echo));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0][23], 1);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(out[0].data() + 34), echo.size()), echo);

    EXPECT_TRUE(p.Packets(Op("listen", true)).empty());
    EXPECT_TRUE(p.Packets(Op("data", false, NetProto::Serial, "bytes")).empty()) << "a serial line is no IP";
    TrafficRecord frame;
    EXPECT_TRUE(p.Packets(frame).empty()) << "frames are not its business";
    EXPECT_EQ(p.AdapterAddress("com.esp"), NetIp(10, 0, 2, 16)) << "one address per adapter";
}
