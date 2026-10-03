// The Ethernet gateway (ethernetgateway.h; network tdd §7, §15 T-NET-3; owner decision Q9 = A "test it well"): a
// guest station's frames through the switch and router to the virtual network (ScriptedHostNet behind it) and back:
// ARP, IPv4 checks (checksum, fragments, options), ICMP echo, UDP (DHCP by MAC, DNS, NAT flows), the TCP state
// machine (connect, refused, data both ways within MSS and window, retransmission, duplicates and out-of-order
// segments, FIN in both orders, RST, link reset), inbound forwards, a full card's ring, switching between two
// stations, determinism and the TTD state round trip

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/scriptedhostnet.h"
#include "common/network/dnsmessage.h"
#include "emulator/io/network/vnet/ethernetgateway.h"

namespace
{
constexpr uint32_t kRouter = NetIp(10, 0, 2, 2);
constexpr uint32_t kDns = NetIp(10, 0, 2, 3);
constexpr uint32_t kServer = NetIp(192, 0, 2, 10);
constexpr uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

uint16_t G16(const std::vector<uint8_t>& f, size_t at) { return static_cast<uint16_t>((f[at] << 8) | f[at + 1]); }
uint32_t G32(const std::vector<uint8_t>& f, size_t at)
{
    return (static_cast<uint32_t>(f[at]) << 24) | (static_cast<uint32_t>(f[at + 1]) << 16) | (static_cast<uint32_t>(f[at + 2]) << 8) |
           f[at + 3];
}
void P16(std::vector<uint8_t>& f, size_t at, uint16_t v)
{
    f[at] = static_cast<uint8_t>(v >> 8);
    f[at + 1] = static_cast<uint8_t>(v);
}
void P32(std::vector<uint8_t>& f, size_t at, uint32_t v)
{
    P16(f, at, static_cast<uint16_t>(v >> 16));
    P16(f, at + 2, static_cast<uint16_t>(v));
}
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
uint32_t Pseudo(uint32_t src, uint32_t dst, uint8_t proto, size_t len)
{
    return (src >> 16) + (src & 0xFFFF) + (dst >> 16) + (dst & 0xFFFF) + proto + static_cast<uint32_t>(len);
}

/// A station on the switch: what a card would do with frames (takes them while `room`)
class Station : public IEthernetPort
{
public:
    Station(std::string key, uint8_t last) : _key(std::move(key)) { mac[5] = last; }
    const std::string& PortKey() const override { return _key; }
    void StationMac(uint8_t out[6]) const override { std::memcpy(out, mac, 6); }
    bool Offer(const uint8_t* frame, size_t length) override
    {
        if (!room)
            return false;
        frames.emplace_back(frame, frame + length);
        return true;
    }
    uint8_t mac[6] = {0x02, 0x53, 0x50, 0x00, 0x00, 0x02};
    std::vector<std::vector<uint8_t>> frames;
    bool room = true;

private:
    std::string _key;
};

/// A guest's IPv4 packet in a frame to `dstMac`
std::vector<uint8_t> IpFrame(const uint8_t* srcMac, const uint8_t* dstMac, uint32_t src, uint32_t dst, uint8_t proto,
                             const std::vector<uint8_t>& payload, uint16_t fragment = 0)
{
    std::vector<uint8_t> f(14 + 20);
    std::memcpy(f.data(), dstMac, 6);
    std::memcpy(f.data() + 6, srcMac, 6);
    P16(f, 12, 0x0800);
    f[14] = 0x45;
    P16(f, 16, static_cast<uint16_t>(20 + payload.size()));
    P16(f, 20, fragment);
    f[22] = 64;
    f[23] = proto;
    P32(f, 26, src);
    P32(f, 30, dst);
    P16(f, 24, Sum(f.data() + 14, 20));
    f.insert(f.end(), payload.begin(), payload.end());
    if (f.size() < 60)
        f.resize(60, 0);
    return f;
}

std::vector<uint8_t> Udp(uint32_t src, uint16_t sport, uint32_t dst, uint16_t dport, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> u(8 + data.size());
    P16(u, 0, sport);
    P16(u, 2, dport);
    P16(u, 4, static_cast<uint16_t>(u.size()));
    std::memcpy(u.data() + 8, data.data(), data.size());
    P16(u, 6, Sum(u.data(), u.size(), Pseudo(src, dst, 17, u.size())));
    return u;
}

std::vector<uint8_t> Tcp(uint32_t src, uint16_t sport, uint32_t dst, uint16_t dport, uint32_t seq, uint32_t ack,
                         uint8_t flags, const std::string& data = {}, uint16_t window = 4096, uint16_t mss = 0)
{
    const size_t header = mss ? 24 : 20;
    std::vector<uint8_t> t(header + data.size());
    P16(t, 0, sport);
    P16(t, 2, dport);
    P32(t, 4, seq);
    P32(t, 8, ack);
    t[12] = static_cast<uint8_t>((header / 4) << 4);
    t[13] = flags;
    P16(t, 14, window);
    if (mss)
    {
        t[20] = 2;
        t[21] = 4;
        P16(t, 22, mss);
    }
    std::memcpy(t.data() + header, data.data(), data.size());
    P16(t, 16, Sum(t.data(), t.size(), Pseudo(src, dst, 6, t.size())));
    return t;
}

struct Segment
{
    uint32_t src = 0, dst = 0;
    uint16_t sport = 0, dport = 0;
    uint32_t seq = 0, ack = 0;
    uint8_t flags = 0;
    uint16_t window = 0, mss = 0;
    std::string data;
};

bool ParseTcp(const std::vector<uint8_t>& f, Segment& s)
{
    if (f.size() < 54 || G16(f, 12) != 0x0800 || f[23] != 6)
        return false;
    const size_t total = G16(f, 16);
    const uint8_t* t = f.data() + 34;
    const size_t length = total - 20;
    s.src = G32(f, 26);
    s.dst = G32(f, 30);
    EXPECT_EQ(Sum(f.data() + 14, 20), 0) << "IPv4 header checksum";
    EXPECT_EQ(Sum(t, length, Pseudo(s.src, s.dst, 6, length)), 0) << "TCP checksum";
    s.sport = static_cast<uint16_t>((t[0] << 8) | t[1]);
    s.dport = static_cast<uint16_t>((t[2] << 8) | t[3]);
    s.seq = G32(f, 38);
    s.ack = G32(f, 42);
    s.flags = t[13];
    s.window = static_cast<uint16_t>((t[14] << 8) | t[15]);
    const size_t offset = static_cast<size_t>(t[12] >> 4) * 4;
    if (offset == 24 && t[20] == 2)
        s.mss = static_cast<uint16_t>((t[22] << 8) | t[23]);
    s.data.assign(t + offset, t + length);
    return true;
}

constexpr uint8_t kFin = 1, kSyn = 2, kRst = 4, kPsh = 8, kAck = 16;
}  // namespace

class EthernetGateway_Test : public ::testing::Test
{
protected:
    void SetUp() override { Build(); }

    void Build(VirtualNetworkConfig config = VirtualNetworkConfig())
    {
        config.hosts["pinned.test"] = NetIp(192, 0, 2, 50);
        _gateway.reset();
        auto host = std::make_unique<ScriptedHostNet>();
        _host = host.get();
        _net = std::make_unique<VirtualNetwork>(nullptr, std::move(host), config);
        _gateway = std::make_unique<EthernetGateway>(*_net, [this]() { return _frame; });
        _gateway->Attach(&_card);
    }

    /// One frame boundary as NetworkManager runs it: the gateway's own work, then the virtual network's answers
    /// (which send what they cause at once)
    void Boundary()
    {
        ++_frame;
        _gateway->OnFrame();
        _net->Pump();
    }

    void Send(const std::vector<uint8_t>& frame) { _gateway->Transmit(_card, frame.data(), frame.size()); }
    void SendIp(uint32_t dst, uint8_t proto, const std::vector<uint8_t>& payload)
    {
        Send(IpFrame(_card.mac, EthernetGateway::kRouterMac, _ip, dst, proto, payload));
    }
    void SendTcp(uint32_t seq, uint32_t ack, uint8_t flags, const std::string& data = {}, uint16_t window = 4096, uint16_t mss = 0)
    {
        SendIp(kServer, 6, Tcp(_ip, 1025, kServer, 80, seq, ack, flags, data, window, mss));
    }

    std::vector<Segment> TakeSegments()
    {
        std::vector<Segment> out;
        for (const auto& f : _card.frames)
        {
            Segment s;
            if (ParseTcp(f, s))
                out.push_back(s);
        }
        _card.frames.clear();
        return out;
    }

    /// The guest's SYN, the host's Connected, SYN-ACK, the guest's ACK: an established connection; returns the ISN
    uint32_t Connect(uint16_t mss = 0)
    {
        SendTcp(1000, 0, kSyn, {}, 4096, mss);
        Boundary();
        const auto segs = TakeSegments();
        EXPECT_EQ(segs.size(), 1u);
        if (segs.empty())
            return 0;
        EXPECT_EQ(segs[0].flags, kSyn | kAck);
        EXPECT_EQ(segs[0].ack, 1001u);
        const uint32_t iss = segs[0].seq;
        SendTcp(1001, iss + 1, kAck);
        return iss;
    }

    uint64_t _frame = 0;
    uint32_t _ip = NetIp(10, 0, 2, 15);
    Station _card{"isa2.eth", 0x02};
    ScriptedHostNet* _host = nullptr;
    std::unique_ptr<VirtualNetwork> _net;
    std::unique_ptr<EthernetGateway> _gateway;
};

// --- ARP ------------------------------------------------------------------------------------------------------

TEST_F(EthernetGateway_Test, Arp_RouterAnswersForTheWorldNotForStations)
{
    auto request = [&](uint32_t sender, uint32_t target) {
        std::vector<uint8_t> f(60, 0);
        std::memcpy(f.data(), kBroadcast, 6);
        std::memcpy(f.data() + 6, _card.mac, 6);
        P16(f, 12, 0x0806);
        P16(f, 14, 1);
        P16(f, 16, 0x0800);
        f[18] = 6;
        f[19] = 4;
        P16(f, 20, 1);
        std::memcpy(f.data() + 22, _card.mac, 6);
        P32(f, 28, sender);
        P32(f, 38, target);
        Send(f);
    };
    request(_ip, kRouter);
    request(_ip, kDns);
    request(_ip, kServer);
    request(_ip, _ip);                  // a duplicate-address check: silent
    request(0, _ip);                    // an ARP probe (sender 0.0.0.0): silent
    EXPECT_TRUE(_card.frames.empty()) << "answers leave at the frame boundary";
    Boundary();
    ASSERT_EQ(_card.frames.size(), 3u);
    for (size_t i = 0; i < 3; ++i)
    {
        const auto& f = _card.frames[i];
        EXPECT_EQ(G16(f, 12), 0x0806);
        EXPECT_EQ(G16(f, 20), 2) << "reply";
        EXPECT_EQ(std::memcmp(f.data() + 22, EthernetGateway::kRouterMac, 6), 0);
        EXPECT_EQ(std::memcmp(f.data(), _card.mac, 6), 0);
        EXPECT_EQ(G32(f, 38), _ip);
    }
    EXPECT_EQ(G32(_card.frames[0], 28), kRouter);
    EXPECT_EQ(G32(_card.frames[2], 28), kServer);
    EXPECT_GE(_card.frames[0].size(), 60u) << "padded";
}

// --- IPv4 checks ------------------------------------------------------------------------------------------------

TEST_F(EthernetGateway_Test, Ipv4_BadChecksumFragmentsAndOptionsAreDroppedAndCounted)
{
    std::vector<uint8_t> f = IpFrame(_card.mac, EthernetGateway::kRouterMac, _ip, kServer, 17, Udp(_ip, 1, kServer, 7, {1}));
    f[24] ^= 0x55;   // header checksum
    Send(f);
    Send(IpFrame(_card.mac, EthernetGateway::kRouterMac, _ip, kServer, 17, Udp(_ip, 1, kServer, 7, {1}), 0x2000));   // MF
    Send(IpFrame(_card.mac, EthernetGateway::kRouterMac, _ip, kServer, 17, Udp(_ip, 1, kServer, 7, {1}), 0x0010));   // offset
    std::vector<uint8_t> udp = Udp(_ip, 1, kServer, 7, {1, 2});
    udp[8] ^= 1;   // UDP checksum
    SendIp(kServer, 17, udp);
    SendIp(kServer, 47, {0, 0, 0, 0});   // GRE: not routed
    const auto& c = _gateway->GetCounters();
    EXPECT_EQ(c.badChecksum, 2u);
    EXPECT_EQ(c.fragments, 2u);
    EXPECT_EQ(c.unsupported, 1u);
    EXPECT_TRUE(_host->commands.empty() || _host->Last("udp") == nullptr) << "nothing reached the host";
}

// --- ICMP -------------------------------------------------------------------------------------------------------

TEST_F(EthernetGateway_Test, Icmp_RouterAndHostAnswerPings)
{
    _host->SetPingable(kServer);
    std::vector<uint8_t> echo = {8, 0, 0, 0, 0x12, 0x34, 0, 1, 'p', 'i', 'n', 'g'};
    P16(echo, 2, Sum(echo.data(), echo.size()));
    SendIp(kRouter, 1, echo);
    SendIp(kServer, 1, echo);
    SendIp(NetIp(192, 0, 2, 99), 1, echo);   // nobody answers
    Boundary();
    ASSERT_EQ(_card.frames.size(), 2u);
    for (const auto& f : _card.frames)
    {
        EXPECT_EQ(f[23], 1);
        EXPECT_EQ(f[34], 0) << "echo reply";
        EXPECT_EQ(G32(f, 30), _ip);
        EXPECT_EQ(Sum(f.data() + 34, G16(f, 16) - 20), 0) << "ICMP checksum";
        EXPECT_EQ(std::string(f.begin() + 42, f.begin() + 46), "ping");
    }
    EXPECT_EQ(G32(_card.frames[0], 26), kRouter);
    EXPECT_EQ(G32(_card.frames[1], 26), kServer);
}

// --- UDP: DHCP, DNS, NAT ------------------------------------------------------------------------------------------

TEST_F(EthernetGateway_Test, Dhcp_LeaseByTheCardsMacThroughDhcpServer)
{
    std::vector<uint8_t> dhcp(244, 0);
    dhcp[0] = 1;
    dhcp[1] = 1;
    dhcp[2] = 6;
    P32(dhcp, 4, 0xCAFEBABE);
    std::memcpy(dhcp.data() + 28, _card.mac, 6);
    dhcp[236] = 99;
    dhcp[237] = 130;
    dhcp[238] = 83;
    dhcp[239] = 99;
    dhcp[240] = 53;
    dhcp[241] = 1;
    dhcp[242] = 1;   // DISCOVER
    dhcp[243] = 255;
    Send(IpFrame(_card.mac, kBroadcast, 0, 0xFFFFFFFF, 17, Udp(0, 68, 0xFFFFFFFF, 67, dhcp)));
    Boundary();
    ASSERT_EQ(_card.frames.size(), 1u);
    const auto& offer = _card.frames[0];
    EXPECT_EQ(std::memcmp(offer.data(), kBroadcast, 6), 0) << "a broadcast, as slirp";
    EXPECT_EQ(G32(offer, 26), kRouter);
    EXPECT_EQ(G32(offer, 30), 0xFFFFFFFFu);
    EXPECT_EQ(G16(offer, 34), 67);
    EXPECT_EQ(G16(offer, 36), 68);
    EXPECT_EQ(G32(offer, 42 + 16), NetIp(10, 0, 2, 15)) << "yiaddr";
    // The same lease the virtual network keeps for this MAC (shared with every adapter)
    DhcpServer::Mac key{};
    std::memcpy(key.data(), _card.mac, 6);
    ASSERT_EQ(_net->Dhcp().Leases().count(key), 1u);
    EXPECT_EQ(_gateway->GuestAddress(), NetIp(10, 0, 2, 15));
}

TEST_F(EthernetGateway_Test, Dns_HostsTableAndHostResolver)
{
    _host->AddName("example.test", kServer);
    for (const char* name : {"pinned.test", "example.test", "missing.test"})
    {
        const std::vector<uint8_t> q = dns::BuildQuery(0x4242, name);
        SendIp(kDns, 17, Udp(_ip, 1053, kDns, 53, q));
        Boundary();
    }
    ASSERT_EQ(_card.frames.size(), 3u);
    const uint32_t expected[] = {NetIp(192, 0, 2, 50), kServer};
    for (size_t i = 0; i < 3; ++i)
    {
        const auto& f = _card.frames[i];
        EXPECT_EQ(G32(f, 26), kDns);
        EXPECT_EQ(G16(f, 36), 1053);
        std::vector<uint32_t> addresses;
        uint8_t rcode = 0;
        ASSERT_TRUE(dns::ParseAnswer(f.data() + 42, G16(f, 38) - 8, 0x4242, addresses, rcode));
        if (i < 2)
        {
            ASSERT_EQ(addresses.size(), 1u);
            EXPECT_EQ(addresses[0], expected[i]);
        }
        else
            EXPECT_EQ(rcode, dns::kRcodeNxDomain);
    }
}

TEST_F(EthernetGateway_Test, Udp_NatFlowAnswersComeBackFromTheSender)
{
    _host->AddUdpEcho({kServer, 7});
    SendIp(kServer, 17, Udp(_ip, 4000, kServer, 7, {'a', 'b', 'c'}));
    Boundary();
    ASSERT_EQ(_card.frames.size(), 1u);
    const auto& f = _card.frames[0];
    EXPECT_EQ(G32(f, 26), kServer);
    EXPECT_EQ(G16(f, 34), 7);
    EXPECT_EQ(G16(f, 36), 4000);
    EXPECT_EQ(Sum(f.data() + 34, G16(f, 38), Pseudo(kServer, _ip, 17, G16(f, 38))), 0) << "UDP checksum";
    EXPECT_EQ(std::string(f.begin() + 42, f.begin() + 45), "abc");
    // An idle flow closes after 60 s of emulated time
    _frame += 3001;
    Boundary();
    EXPECT_NE(_host->Last("close"), nullptr);
}

// --- TCP --------------------------------------------------------------------------------------------------------

TEST_F(EthernetGateway_Test, Tcp_ConnectAndAnHttpFetch)
{
    std::vector<uint8_t> body(3000);
    for (size_t i = 0; i < body.size(); ++i)
        body[i] = static_cast<uint8_t>(i);
    _host->AddHttp({kServer, 80}, {{"/f.bin", body}});
    const uint32_t iss = Connect(512);
    const std::string get = "GET /f.bin HTTP/1.0\r\n\r\n";
    SendTcp(1001, iss + 1, kAck | kPsh, get);
    // The request reached the host server; the guest's data is acknowledged at once
    auto acks = TakeSegments();
    ASSERT_EQ(acks.size(), 0u) << "ACKs are frames: they leave at the boundary";
    Boundary();
    std::string received;
    uint32_t next = iss + 1;
    bool fin = false;
    for (int round = 0; round < 20 && !fin; ++round)
    {
        for (const Segment& s : TakeSegments())
        {
            EXPECT_LE(s.data.size(), 512u) << "never beyond the guest's MSS";
            if (!s.data.empty())
            {
                EXPECT_EQ(s.seq, next);
                received += s.data;
                next += static_cast<uint32_t>(s.data.size());
            }
            if (s.flags & kFin)
            {
                EXPECT_EQ(s.seq, next);
                fin = true;
                next += 1;
            }
        }
        SendTcp(1001 + static_cast<uint32_t>(get.size()), next, kAck);
        Boundary();
    }
    ASSERT_TRUE(fin);
    const size_t head = received.find("\r\n\r\n");
    ASSERT_NE(head, std::string::npos);
    EXPECT_EQ(std::vector<uint8_t>(received.begin() + static_cast<std::ptrdiff_t>(head + 4), received.end()), body);
    // The guest closes too: the connection is gone
    SendTcp(1001 + static_cast<uint32_t>(get.size()), next, kFin | kAck);
    Boundary();
    EXPECT_TRUE(_gateway->Describe().find("tcp")->items.empty());
}

TEST_F(EthernetGateway_Test, Tcp_RefusedIsAReset)
{
    SendTcp(1000, 0, kSyn);
    Boundary();
    const auto segs = TakeSegments();
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_EQ(segs[0].flags, kRst | kAck);
    EXPECT_EQ(segs[0].ack, 1001u);
    EXPECT_EQ(_gateway->GetCounters().tcpRefused, 1u);
}

TEST_F(EthernetGateway_Test, Tcp_WindowLimitsWhatIsInFlight)
{
    _host->AddBanner({kServer, 80}, std::string(5000, 'x'));
    SendTcp(1000, 0, kSyn, {}, 1000);   // a 1000-byte window
    Boundary();
    auto segs = TakeSegments();
    ASSERT_EQ(segs.size(), 1u);
    const uint32_t iss = segs[0].seq;
    SendTcp(1001, iss + 1, kAck, {}, 1000);
    Boundary();
    segs = TakeSegments();
    size_t inFlight = 0;
    for (const Segment& s : segs)
        inFlight += s.data.size();
    EXPECT_EQ(inFlight, 1000u) << "the guest's window, no more";
    SendTcp(1001, iss + 1 + 1000, kAck, {}, 0);   // a zero window
    Boundary();
    EXPECT_TRUE(TakeSegments().empty());
    SendTcp(1001, iss + 1 + 1000, kAck, {}, 2000);   // the window opens
    Boundary();
    inFlight = 0;
    for (const Segment& s : TakeSegments())
        inFlight += s.data.size();
    EXPECT_EQ(inFlight, 2000u);
}

TEST_F(EthernetGateway_Test, Tcp_RetransmitsAfterSilenceThenResets)
{
    _host->AddBanner({kServer, 80}, "hello");
    const uint32_t iss = Connect();
    Boundary();
    auto segs = TakeSegments();
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_EQ(segs[0].data, "hello");
    // No ACK: after 25 frames the same bytes again, then after 50, 100, ...
    for (int i = 0; i < 24; ++i)
        Boundary();
    EXPECT_TRUE(TakeSegments().empty());
    Boundary();
    segs = TakeSegments();
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_EQ(segs[0].seq, iss + 1);
    EXPECT_EQ(segs[0].data, "hello");
    EXPECT_EQ(_gateway->GetCounters().retransmits, 1u);
    // An ACK of it stops the timer
    SendTcp(1001, iss + 6, kAck);
    for (int i = 0; i < 200; ++i)
        Boundary();
    EXPECT_TRUE(TakeSegments().empty());

    // A guest that never answers again: 5 retries, then RST
    EthernetGateway_Test::Build();
    _host->AddBanner({kServer, 80}, "x");
    Connect();
    int resets = 0;
    for (int i = 0; i < 2000 && !resets; ++i)
    {
        Boundary();
        for (const Segment& s : TakeSegments())
            resets += (s.flags & kRst) ? 1 : 0;
    }
    EXPECT_EQ(resets, 1);
    EXPECT_EQ(_gateway->GetCounters().retransmits, 5u);
}

TEST_F(EthernetGateway_Test, Tcp_DuplicatesAndOutOfOrderSegments)
{
    _host->AddEcho({kServer, 80});
    const uint32_t iss = Connect();
    SendTcp(1001, iss + 1, kAck | kPsh, "ab");
    SendTcp(1001, iss + 1, kAck | kPsh, "ab");   // a duplicate: acknowledged again, not sent twice
    SendTcp(1010, iss + 1, kAck | kPsh, "zz");   // out of order: dropped, a duplicate ACK
    Boundary();
    auto segs = TakeSegments();
    int acksFor1003 = 0;
    std::string echoed;
    for (const Segment& s : segs)
    {
        if (s.data.empty() && s.ack == 1003)
            ++acksFor1003;
        echoed += s.data;
    }
    EXPECT_EQ(acksFor1003, 3);
    EXPECT_EQ(echoed, "ab") << "the host got 'ab' once";
    const std::vector<uint8_t> sent = _host->Received(_host->Last("connect")->socket);
    EXPECT_EQ(std::string(sent.begin(), sent.end()), "ab");
}

TEST_F(EthernetGateway_Test, Tcp_FinFromTheHostFirst)
{
    _host->AddHttp({kServer, 80}, {});
    const uint32_t iss = Connect();
    const std::string get = "GET /x HTTP/1.0\r\n\r\n";
    const uint32_t after = 1001 + static_cast<uint32_t>(get.size());
    SendTcp(1001, iss + 1, kAck | kPsh, get);
    Boundary();
    auto segs = TakeSegments();
    ASSERT_FALSE(segs.empty());
    uint32_t next = iss + 1;
    bool fin = false;
    for (const Segment& s : segs)
    {
        next += static_cast<uint32_t>(s.data.size());
        if (s.flags & kFin)
            fin = true;
    }
    EXPECT_TRUE(fin) << "404 and the host's FIN follow the data";
    SendTcp(after, next + 1, kAck);
    SendTcp(after, next + 1, kFin | kAck);
    Boundary();
    segs = TakeSegments();
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_EQ(segs[0].ack, after + 1) << "the guest's FIN is acknowledged";
    EXPECT_TRUE(_gateway->Describe().find("tcp")->items.empty());
}

TEST_F(EthernetGateway_Test, Tcp_FinFromTheGuestFirst)
{
    _host->AddEcho({kServer, 80});
    const uint32_t iss = Connect();
    SendTcp(1001, iss + 1, kFin | kAck);
    Boundary();
    EXPECT_NE(_host->Last("shutdown"), nullptr) << "the host side half-closes";
    auto segs = TakeSegments();
    // The ACK of the FIN, then the echo server closes: our FIN
    bool ackOfFin = false, fin = false;
    uint32_t finSeq = 0;
    for (int round = 0; round < 3; ++round)
    {
        for (const Segment& s : segs)
        {
            ackOfFin |= s.ack == 1002;
            if (s.flags & kFin)
            {
                fin = true;
                finSeq = s.seq;
            }
        }
        Boundary();
        segs = TakeSegments();
    }
    EXPECT_TRUE(ackOfFin);
    ASSERT_TRUE(fin);
    SendTcp(1002, finSeq + 1, kAck);
    EXPECT_TRUE(_gateway->Describe().find("tcp")->items.empty());
}

TEST_F(EthernetGateway_Test, Tcp_RstFromEitherSide)
{
    _host->AddEcho({kServer, 80});
    uint32_t iss = Connect();
    SendTcp(1001, iss + 1, kRst);
    EXPECT_TRUE(_gateway->Describe().find("tcp")->items.empty());
    EXPECT_NE(_host->Last("close"), nullptr);

    iss = Connect();
    const uint16_t socket = _host->Last("connect")->socket;
    _host->Push(NetEventType::Reset, socket, NetEventStatus::Error, {kServer, 80});
    Boundary();
    const auto segs = TakeSegments();
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_TRUE(segs[0].flags & kRst);
    EXPECT_TRUE(_gateway->Describe().find("tcp")->items.empty());
    // A segment for a connection that no longer exists: RST
    SendTcp(1001, iss + 1, kAck | kPsh, "late");
    Boundary();
    const auto late = TakeSegments();
    ASSERT_EQ(late.size(), 1u);
    EXPECT_TRUE(late[0].flags & kRst);
    EXPECT_EQ(late[0].seq, iss + 1);
}

TEST_F(EthernetGateway_Test, Tcp_LinkResetResetsTheGuest)
{
    _host->AddEcho({kServer, 80});
    Connect();
    _net->ApplyLinkReset();   // TTD left the recorded past: the host connections are gone
    Boundary();
    const auto segs = TakeSegments();
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_TRUE(segs[0].flags & kRst);
}

TEST_F(EthernetGateway_Test, Tcp_InboundForwardMakesTheGuestAServer)
{
    VirtualNetworkConfig config;
    config.forwards[23] = 2323;
    Build(config);
    const ScriptedHostNet::Command* listen = _host->Last("listen");
    ASSERT_NE(listen, nullptr);
    EXPECT_EQ(listen->endpoint.port, 2323);
    _gateway->Attach(&_card);
    _host->ConnectClient(listen->socket, 0x8001, {NetIp(127, 0, 0, 1), 50000});
    Boundary();
    auto segs = TakeSegments();
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_EQ(segs[0].flags, kSyn);
    EXPECT_EQ(segs[0].dport, 23);
    EXPECT_EQ(segs[0].dst, NetIp(10, 0, 2, 15)) << "the guest's address";
    const uint32_t iss = segs[0].seq;
    // The guest's server answers
    SendIp(NetIp(127, 0, 0, 1), 6, Tcp(_ip, 23, NetIp(127, 0, 0, 1), 50000, 7000, iss + 1, kSyn | kAck));
    Boundary();
    segs = TakeSegments();
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_EQ(segs[0].flags, kAck);
    _host->SendFromClient(0x8001, "login: ");
    Boundary();
    segs = TakeSegments();
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_EQ(segs[0].data, "login: ");
    SendIp(NetIp(127, 0, 0, 1), 6, Tcp(_ip, 23, NetIp(127, 0, 0, 1), 50000, 7001, iss + 8, kAck | kPsh, "z80"));
    EXPECT_EQ(std::string(_host->Received(0x8001).begin(), _host->Received(0x8001).end()), "");
    const ScriptedHostNet::Command* sent = _host->Last("send");
    ASSERT_NE(sent, nullptr);
    EXPECT_EQ(std::string(sent->data.begin(), sent->data.end()), "z80");
}

// --- The switch and the cards ------------------------------------------------------------------------------------

TEST_F(EthernetGateway_Test, Switch_TwoStationsAndAFullRing)
{
    Station other("isa1.eth", 0x01);
    _gateway->Attach(&other);
    // A unicast from one station to the other: switched, the router ignores it
    std::vector<uint8_t> f(60, 0x11);
    std::memcpy(f.data(), other.mac, 6);
    std::memcpy(f.data() + 6, _card.mac, 6);
    P16(f, 12, 0x88B5);
    Send(f);
    Boundary();
    ASSERT_EQ(other.frames.size(), 1u);
    EXPECT_EQ(other.frames[0], f);
    EXPECT_TRUE(_card.frames.empty());
    EXPECT_EQ(_gateway->GetCounters().switched, 1u);

    // A card whose ring is full keeps its frames queued until it takes them, in order
    _card.room = false;
    std::vector<uint8_t> echo = {8, 0, 0, 0, 0, 1, 0, 1};
    P16(echo, 2, Sum(echo.data(), echo.size()));
    SendIp(kRouter, 1, echo);
    SendIp(kRouter, 1, echo);
    Boundary();
    Boundary();
    EXPECT_TRUE(_card.frames.empty());
    EXPECT_GE(_gateway->GetCounters().ringFullWaits, 2u);
    _card.room = true;
    Boundary();
    EXPECT_EQ(_card.frames.size(), 2u);
}

TEST_F(EthernetGateway_Test, Capture_PcapHasEveryFrameBothWays)
{
    std::vector<uint8_t> echo = {8, 0, 0, 0, 0, 1, 0, 1};
    P16(echo, 2, Sum(echo.data(), echo.size()));
    SendIp(kRouter, 1, echo);
    Boundary();
    ASSERT_EQ(_gateway->Capture().size(), 2u);
    EXPECT_FALSE(_gateway->Capture()[0].toCard);
    EXPECT_TRUE(_gateway->Capture()[1].toCard);
    const std::vector<uint8_t> pcap = _gateway->CapturePcap();
    ASSERT_GE(pcap.size(), 24u + 2 * 16u + 2 * 60u);
    EXPECT_EQ(pcap[0], 0xD4);
    EXPECT_EQ(pcap[3], 0xA1);
    EXPECT_EQ(pcap[20], 1) << "LINKTYPE_ETHERNET";
}

// --- Determinism and TTD -------------------------------------------------------------------------------------------

TEST_F(EthernetGateway_Test, SameInputsGiveIdenticalFrames)
{
    auto run = [&]() {
        Build();
        _host->AddHttp({kServer, 80}, {{"/a", std::vector<uint8_t>(1500, 0x5A)}});
        const uint32_t iss = Connect();
        SendTcp(1001, iss + 1, kAck | kPsh, "GET /a HTTP/1.0\r\n\r\n");
        std::vector<std::vector<uint8_t>> all;
        for (int i = 0; i < 5; ++i)
        {
            Boundary();
            all.insert(all.end(), _card.frames.begin(), _card.frames.end());
            _card.frames.clear();
        }
        return all;
    };
    const auto first = run();
    _frame = 0;
    const auto second = run();
    EXPECT_FALSE(first.empty());
    EXPECT_EQ(first, second);
}

TEST_F(EthernetGateway_Test, State_RoundTripRestoresConnectionsAndQueues)
{
    _host->AddBanner({kServer, 80}, std::string(3000, 'q'));
    const uint32_t iss = Connect(536);
    Boundary();
    const std::vector<uint8_t> saved = _gateway->SaveState();
    const auto afterSave = TakeSegments();
    ASSERT_FALSE(afterSave.empty());

    EthernetGateway restored(*_net, [this]() { return _frame; });
    Station card2("isa2.eth", 0x02);
    restored.Attach(&card2);
    ASSERT_TRUE(restored.LoadState(saved.data(), saved.size()));
    EXPECT_EQ(restored.SaveState(), saved);
    const StateNode report = restored.Describe();
    ASSERT_EQ(report.find("tcp")->items.size(), 1u);
    EXPECT_EQ(report.find("tcp")->items[0].find("state")->s, "established");
    (void)iss;
}
