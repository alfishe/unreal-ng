#include "emulator/io/network/vnet/ethernetgateway.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace
{
constexpr uint16_t kTypeIpv4 = 0x0800;
constexpr uint16_t kTypeArp = 0x0806;
constexpr uint8_t kProtoIcmp = 1;
constexpr uint8_t kProtoTcp = 6;
constexpr uint8_t kProtoUdp = 17;
constexpr uint8_t kFin = 0x01, kSyn = 0x02, kRst = 0x04, kPsh = 0x08, kAck = 0x10;
constexpr uint32_t kBroadcastIp = 0xFFFFFFFFu;
constexpr uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr uint64_t kUdpIdleFrames = 3000;   ///< 60 s of emulated time
constexpr uint16_t kOurWindow = 8192;
constexpr uint8_t kStateVersion = 1;

uint16_t Get16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t Get32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
}
void Put16(std::vector<uint8_t>& v, size_t at, uint16_t x)
{
    v[at] = static_cast<uint8_t>(x >> 8);
    v[at + 1] = static_cast<uint8_t>(x);
}
void Put32(std::vector<uint8_t>& v, size_t at, uint32_t x)
{
    v[at] = static_cast<uint8_t>(x >> 24);
    v[at + 1] = static_cast<uint8_t>(x >> 16);
    v[at + 2] = static_cast<uint8_t>(x >> 8);
    v[at + 3] = static_cast<uint8_t>(x);
}

/// The Internet checksum (RFC 1071) over `length` bytes, with a starting sum (the pseudo header)
uint16_t Checksum(const uint8_t* data, size_t length, uint32_t sum = 0)
{
    for (size_t i = 0; i + 1 < length; i += 2)
        sum += static_cast<uint32_t>((data[i] << 8) | data[i + 1]);
    if (length & 1)
        sum += static_cast<uint32_t>(data[length - 1] << 8);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

uint32_t PseudoSum(uint32_t src, uint32_t dst, uint8_t protocol, size_t length)
{
    return (src >> 16) + (src & 0xFFFF) + (dst >> 16) + (dst & 0xFFFF) + protocol + static_cast<uint32_t>(length);
}

bool SeqLess(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) < 0; }
bool SeqLessEq(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) <= 0; }

bool IsBroadcastMac(const uint8_t* mac) { return std::memcmp(mac, kBroadcastMac, 6) == 0; }

std::string MacText(const uint8_t* mac)
{
    char text[24];
    std::snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return text;
}

// Little-endian state writer / reader
struct Out
{
    std::vector<uint8_t>& v;
    void U8(uint8_t x) { v.push_back(x); }
    void U16(uint16_t x)
    {
        U8(static_cast<uint8_t>(x));
        U8(static_cast<uint8_t>(x >> 8));
    }
    void U32(uint32_t x)
    {
        U16(static_cast<uint16_t>(x));
        U16(static_cast<uint16_t>(x >> 16));
    }
    void U64(uint64_t x)
    {
        U32(static_cast<uint32_t>(x));
        U32(static_cast<uint32_t>(x >> 32));
    }
    void Bytes(const uint8_t* p, size_t n) { v.insert(v.end(), p, p + n); }
};
struct In
{
    const uint8_t* p;
    size_t size;
    size_t at = 0;
    bool ok = true;
    bool Need(size_t n)
    {
        if (at + n > size)
            ok = false;
        return ok;
    }
    uint8_t U8() { return Need(1) ? p[at++] : 0; }
    uint16_t U16()
    {
        const uint16_t lo = U8();
        return static_cast<uint16_t>(lo | (U8() << 8));
    }
    uint32_t U32()
    {
        const uint32_t lo = U16();
        return lo | (static_cast<uint32_t>(U16()) << 16);
    }
    uint64_t U64()
    {
        const uint64_t lo = U32();
        return lo | (static_cast<uint64_t>(U32()) << 32);
    }
    void Bytes(uint8_t* dst, size_t n)
    {
        if (!Need(n))
            return;
        std::memcpy(dst, p + at, n);
        at += n;
    }
};
}  // namespace

EthernetGateway::EthernetGateway(VirtualNetwork& network, std::function<uint64_t()> frameCounter)
    : _network(network), _frameCounter(std::move(frameCounter))
{
}

EthernetGateway::~EthernetGateway()
{
    for (auto& [id, c] : _tcp)
        _network.Close(c.socket);
    for (auto& [id, f] : _udp)
        _network.Close(f.socket);
    for (auto& [id, f] : _icmp)
        _network.Close(f.socket);
    for (const Listener& l : _listeners)
        _network.Close(l.socket);
}

void EthernetGateway::Attach(IEthernetPort* port)
{
    if (!port)
        return;
    for (const PortQueue& q : _ports)
    {
        if (q.port == port)
            return;
    }
    _ports.push_back({port, {}});
    if (_listeners.empty())
        OpenListeners();
}

void EthernetGateway::Detach(IEthernetPort* port)
{
    _ports.erase(std::remove_if(_ports.begin(), _ports.end(), [port](const PortQueue& q) { return q.port == port; }),
                 _ports.end());
}

void EthernetGateway::OpenListeners()
{
    // Inbound: the guest is the server of every Forward= rule (the host listens on the rule's host port)
    for (const auto& [guestPort, hostPort] : _network.Config().forwards)
    {
        (void)hostPort;
        Listener l;
        l.id = NextId();
        l.socket = _network.Open(NetProto::Tcp, this, l.id);
        l.guestPort = guestPort;
        if (l.socket == VirtualNetwork::kNoSocket)
            continue;
        _network.Listen(l.socket, guestPort);
        _listeners.push_back(l);
    }
}

void EthernetGateway::ForgetConnections()
{
    _tcp.clear();
    _udp.clear();
    _icmp.clear();
    _listeners.clear();
    for (PortQueue& q : _ports)
        q.frames.clear();
    if (!_ports.empty())
        OpenListeners();
}

// ---------------------------------------------------------------------------
// Frames out
// ---------------------------------------------------------------------------

void EthernetGateway::Record(bool toCard, const std::string& port, const std::vector<uint8_t>& bytes)
{
    CapturedFrame f;
    f.frame = _frameCounter ? _frameCounter() : 0;
    f.index = _captureIndex++;
    f.toCard = toCard;
    f.port = port;
    f.bytes = bytes;
    _capture.push_back(std::move(f));
    while (_capture.size() > kCaptureLength)
        _capture.pop_front();
}

std::vector<uint8_t> EthernetGateway::Ethernet(const uint8_t* dst, const uint8_t* src, uint16_t type,
                                               const std::vector<uint8_t>& payload) const
{
    std::vector<uint8_t> f(14);
    std::memcpy(f.data(), dst, 6);
    std::memcpy(f.data() + 6, src, 6);
    Put16(f, 12, type);
    f.insert(f.end(), payload.begin(), payload.end());
    if (f.size() < 60)
        f.resize(60, 0);   // the sender pads to the Ethernet minimum
    return f;
}

void EthernetGateway::Queue(const uint8_t* dstMac, const std::vector<uint8_t>& frame, IEthernetPort* only)
{
    for (PortQueue& q : _ports)
    {
        if (only && q.port != only)
            continue;
        if (!only && !IsBroadcastMac(dstMac) && !(dstMac[0] & 1))
        {
            uint8_t mac[6];
            q.port->StationMac(mac);
            if (std::memcmp(mac, dstMac, 6) != 0)
                continue;
        }
        if (q.frames.size() >= kMaxQueuedFrames)
        {
            q.frames.pop_front();
            ++_counters.queueDrops;
        }
        q.frames.push_back(frame);
    }
}

std::vector<uint8_t> EthernetGateway::Ipv4(uint32_t src, uint32_t dst, uint8_t protocol, const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> p(20);
    p[0] = 0x45;
    Put16(p, 2, static_cast<uint16_t>(20 + payload.size()));
    Put16(p, 4, _ipId++);
    p[8] = 64;
    p[9] = protocol;
    Put32(p, 12, src);
    Put32(p, 16, dst);
    Put16(p, 10, Checksum(p.data(), 20));
    p.insert(p.end(), payload.begin(), payload.end());
    return p;
}

void EthernetGateway::SendIp(const uint8_t* dstMac, uint32_t src, uint32_t dst, uint8_t protocol,
                             const std::vector<uint8_t>& payload)
{
    Queue(dstMac, Ethernet(dstMac, kRouterMac, kTypeIpv4, Ipv4(src, dst, protocol, payload)));
}

void EthernetGateway::SendUdp(const uint8_t* dstMac, uint32_t src, uint16_t srcPort, uint32_t dst, uint16_t dstPort,
                              const uint8_t* data, size_t length)
{
    std::vector<uint8_t> u(8 + length);
    Put16(u, 0, srcPort);
    Put16(u, 2, dstPort);
    Put16(u, 4, static_cast<uint16_t>(u.size()));
    if (length)
        std::memcpy(u.data() + 8, data, length);
    uint16_t sum = Checksum(u.data(), u.size(), PseudoSum(src, dst, kProtoUdp, u.size()));
    Put16(u, 6, sum == 0 ? 0xFFFF : sum);
    SendIp(dstMac, src, dst, kProtoUdp, u);
}

void EthernetGateway::SendTcp(TcpConn& c, uint8_t flags, uint32_t seq, const uint8_t* data, size_t length, bool withMss)
{
    const size_t header = withMss ? 24 : 20;
    std::vector<uint8_t> t(header + length);
    Put16(t, 0, c.remotePort);
    Put16(t, 2, c.guestPort);
    Put32(t, 4, seq);
    Put32(t, 8, (flags & kAck) ? c.rcvNxt : 0);
    t[12] = static_cast<uint8_t>((header / 4) << 4);
    t[13] = flags;
    Put16(t, 14, kOurWindow);
    if (withMss)
    {
        t[20] = 2;
        t[21] = 4;
        Put16(t, 22, 1460);
    }
    if (length)
        std::memcpy(t.data() + header, data, length);
    Put16(t, 16, Checksum(t.data(), t.size(), PseudoSum(c.remoteIp, c.guestIp, kProtoTcp, t.size())));
    SendIp(c.guestMac, c.remoteIp, c.guestIp, kProtoTcp, t);
    c.lastSendFrame = _frameCounter ? _frameCounter() : 0;
}

void EthernetGateway::SendRst(const uint8_t* dstMac, uint32_t src, uint16_t srcPort, uint32_t dst, uint16_t dstPort,
                              uint32_t seq, uint32_t ack, bool withAck)
{
    std::vector<uint8_t> t(20);
    Put16(t, 0, srcPort);
    Put16(t, 2, dstPort);
    Put32(t, 4, seq);
    Put32(t, 8, withAck ? ack : 0);
    t[12] = 0x50;
    t[13] = static_cast<uint8_t>(kRst | (withAck ? kAck : 0));
    Put16(t, 16, Checksum(t.data(), t.size(), PseudoSum(src, dst, kProtoTcp, t.size())));
    ++_counters.resets;
    SendIp(dstMac, src, dst, kProtoTcp, t);
}

// ---------------------------------------------------------------------------
// Frames in
// ---------------------------------------------------------------------------

void EthernetGateway::Learn(const uint8_t* mac, uint32_t ip)
{
    if (ip == 0 || ip == kBroadcastIp || IsBroadcastMac(mac) || (mac[0] & 1))
        return;
    for (Station& s : _stations)
    {
        if (std::memcmp(s.mac, mac, 6) == 0)
        {
            s.ip = ip;
            return;
        }
    }
    Station s;
    std::memcpy(s.mac, mac, 6);
    s.ip = ip;
    _stations.push_back(s);
}

bool EthernetGateway::IsStationIp(uint32_t ip) const
{
    for (const Station& s : _stations)
    {
        if (s.ip == ip)
            return true;
    }
    return false;
}

uint32_t EthernetGateway::GuestAddress() const
{
    for (const PortQueue& q : _ports)
    {
        uint8_t mac[6];
        q.port->StationMac(mac);
        DhcpServer::Mac key{};
        std::memcpy(key.data(), mac, 6);
        auto lease = _network.Dhcp().Leases().find(key);
        if (lease != _network.Dhcp().Leases().end())
            return lease->second;
    }
    if (!_stations.empty())
        return _stations.back().ip;
    return _network.Config().firstLease;
}

void EthernetGateway::Transmit(IEthernetPort& from, const uint8_t* frame, size_t length)
{
    ++_counters.framesFromCards;
    Record(false, from.PortKey(), std::vector<uint8_t>(frame, frame + length));
    if (length < 14)
    {
        ++_counters.runts;
        return;
    }
    if (length < 60)
        ++_counters.runts;   // counted; a router still reads what it can
    const uint8_t* dst = frame;
    const bool toRouter = std::memcmp(dst, kRouterMac, 6) == 0;
    const bool group = (dst[0] & 1) != 0;

    // The switch: frames for other stations (or everyone) reach the other cards as they are
    if (!toRouter)
    {
        for (PortQueue& q : _ports)
        {
            if (q.port == &from)
                continue;
            uint8_t mac[6];
            q.port->StationMac(mac);
            if (group || std::memcmp(mac, dst, 6) == 0)
            {
                q.frames.emplace_back(frame, frame + length);
                ++_counters.switched;
            }
        }
    }
    if (!toRouter && !group)
        return;   // a unicast for someone else on the segment (or nobody): not the router's

    const uint16_t type = Get16(frame + 12);
    if (type == kTypeArp)
        HandleArp(from, frame, length);
    else if (type == kTypeIpv4)
        HandleIpv4(from, frame, length);
    else
        ++_counters.unsupported;
}

void EthernetGateway::HandleArp(IEthernetPort& from, const uint8_t* frame, size_t length)
{
    (void)from;
    if (length < 42)
        return;
    const uint8_t* a = frame + 14;
    if (Get16(a) != 1 || Get16(a + 2) != kTypeIpv4 || a[4] != 6 || a[5] != 4)
        return;
    const uint16_t op = Get16(a + 6);
    const uint8_t* sha = a + 8;
    const uint32_t spa = Get32(a + 14);
    const uint32_t tpa = Get32(a + 24);
    Learn(sha, spa);
    if (op != 1)
        return;
    // Silent for the sender's own address (a duplicate check) and for another station on the switch (it answers)
    if (tpa == spa || tpa == 0 || IsStationIp(tpa))
        return;
    std::vector<uint8_t> reply(28);
    Put16(reply, 0, 1);
    Put16(reply, 2, kTypeIpv4);
    reply[4] = 6;
    reply[5] = 4;
    Put16(reply, 6, 2);
    std::memcpy(reply.data() + 8, kRouterMac, 6);
    Put32(reply, 14, tpa);
    std::memcpy(reply.data() + 18, sha, 6);
    Put32(reply, 24, spa);
    ++_counters.arpReplies;
    Queue(sha, Ethernet(sha, kRouterMac, kTypeArp, reply));
}

void EthernetGateway::HandleIpv4(IEthernetPort& from, const uint8_t* frame, size_t length)
{
    (void)from;
    if (length < 34)
        return;
    const uint8_t* ip = frame + 14;
    const size_t ihl = static_cast<size_t>(ip[0] & 0x0F) * 4;
    const size_t total = Get16(ip + 2);
    if ((ip[0] >> 4) != 4 || ihl < 20 || total < ihl || 14 + total > length)
    {
        ++_counters.unsupported;
        return;
    }
    if (Checksum(ip, ihl) != 0)
    {
        ++_counters.badChecksum;
        return;
    }
    const uint16_t fragment = Get16(ip + 6);
    if ((fragment & 0x2000) || (fragment & 0x1FFF))
    {
        ++_counters.fragments;   // no fragment reassembly: MTU 1500 everywhere
        return;
    }
    if (ihl > 20)
    {
        ++_counters.unsupported;   // IP options
        return;
    }
    const uint32_t src = Get32(ip + 12);
    const uint32_t dst = Get32(ip + 16);
    const uint8_t* srcMac = frame + 6;
    if (src != 0)
        Learn(srcMac, src);
    const uint8_t* payload = ip + ihl;
    const size_t payloadLength = total - ihl;
    switch (ip[9])
    {
        case kProtoUdp: HandleUdp(srcMac, src, dst, payload, payloadLength); break;
        case kProtoIcmp: HandleIcmp(srcMac, src, dst, payload, payloadLength); break;
        case kProtoTcp: HandleTcp(srcMac, src, dst, payload, payloadLength); break;
        default: ++_counters.unsupported; break;
    }
}

void EthernetGateway::HandleUdp(const uint8_t* srcMac, uint32_t srcIp, uint32_t dstIp, const uint8_t* udp, size_t length)
{
    if (length < 8 || Get16(udp + 4) < 8 || Get16(udp + 4) > length)
        return;
    const size_t udpLength = Get16(udp + 4);
    if (Get16(udp + 6) != 0 && Checksum(udp, udpLength, PseudoSum(srcIp, dstIp, kProtoUdp, udpLength)) != 0)
    {
        ++_counters.badChecksum;
        return;
    }
    const uint16_t srcPort = Get16(udp);
    const uint16_t dstPort = Get16(udp + 2);
    if (dstPort == 67)
        ++_counters.dhcp;
    else if (dstPort == 53)
        ++_counters.dns;
    else
        ++_counters.udp;

    UdpFlow* flow = nullptr;
    for (auto& [id, f] : _udp)
    {
        if (f.guestIp == srcIp && f.guestPort == srcPort)
            flow = &f;
    }
    if (!flow)
    {
        UdpFlow f;
        f.id = NextId();
        f.socket = _network.Open(NetProto::Udp, this, f.id);
        if (f.socket == VirtualNetwork::kNoSocket)
            return;
        f.guestIp = srcIp;
        f.guestPort = srcPort;
        flow = &(_udp[f.id] = f);
    }
    std::memcpy(flow->guestMac, srcMac, 6);
    flow->lastUsedFrame = _frameCounter ? _frameCounter() : 0;
    _network.SendTo(flow->socket, srcPort, NetEndpoint{dstIp, dstPort}, udp + 8, static_cast<uint32_t>(udpLength - 8));
}

void EthernetGateway::HandleIcmp(const uint8_t* srcMac, uint32_t srcIp, uint32_t dstIp, const uint8_t* icmp, size_t length)
{
    if (length < 8)
        return;
    if (Checksum(icmp, length) != 0)
    {
        ++_counters.badChecksum;
        return;
    }
    if (icmp[0] != 8)
    {
        ++_counters.unsupported;   // only echo requests leave the switch
        return;
    }
    ++_counters.icmp;
    IcmpFlow* flow = nullptr;
    for (auto& [id, f] : _icmp)
    {
        if (f.guestIp == srcIp)
            flow = &f;
    }
    if (!flow)
    {
        IcmpFlow f;
        f.id = NextId();
        f.socket = _network.Open(NetProto::Icmp, this, f.id);
        if (f.socket == VirtualNetwork::kNoSocket)
            return;
        f.guestIp = srcIp;
        flow = &(_icmp[f.id] = f);
    }
    std::memcpy(flow->guestMac, srcMac, 6);
    _network.SendTo(flow->socket, 0, NetEndpoint{dstIp, 0}, icmp, static_cast<uint32_t>(length));
}

EthernetGateway::TcpConn* EthernetGateway::FindConn(uint32_t guestIp, uint16_t guestPort, uint32_t remoteIp, uint16_t remotePort)
{
    for (auto& [id, c] : _tcp)
    {
        if (c.guestIp == guestIp && c.guestPort == guestPort && c.remoteIp == remoteIp && c.remotePort == remotePort)
            return &c;
    }
    return nullptr;
}

EthernetGateway::TcpConn* EthernetGateway::FindConnById(uint32_t id)
{
    auto it = _tcp.find(id);
    return it == _tcp.end() ? nullptr : &it->second;
}

void EthernetGateway::CloseConn(uint32_t id, bool closeSocket)
{
    auto it = _tcp.find(id);
    if (it == _tcp.end())
        return;
    if (closeSocket)
        _network.Close(it->second.socket);
    _tcp.erase(it);
}

void EthernetGateway::HandleTcp(const uint8_t* srcMac, uint32_t srcIp, uint32_t dstIp, const uint8_t* tcp, size_t length)
{
    if (length < 20)
        return;
    const size_t offset = static_cast<size_t>(tcp[12] >> 4) * 4;
    if (offset < 20 || offset > length)
        return;
    if (Checksum(tcp, length, PseudoSum(srcIp, dstIp, kProtoTcp, length)) != 0)
    {
        ++_counters.badChecksum;
        return;
    }
    const uint16_t srcPort = Get16(tcp);
    const uint16_t dstPort = Get16(tcp + 2);
    const uint32_t seq = Get32(tcp + 4);
    const uint32_t ack = Get32(tcp + 8);
    const uint8_t flags = tcp[13];
    const uint16_t window = Get16(tcp + 14);
    const uint8_t* data = tcp + offset;
    const size_t dataLength = length - offset;
    uint16_t mss = 536;
    for (size_t at = 20; at + 1 < offset;)
    {
        const uint8_t kind = tcp[at];
        if (kind == 0)
            break;
        if (kind == 1)
        {
            ++at;
            continue;
        }
        const uint8_t len = tcp[at + 1];
        if (len < 2 || at + len > offset)
            break;
        if (kind == 2 && len == 4)
            mss = Get16(tcp + at + 2);
        at += len;
    }

    TcpConn* c = FindConn(srcIp, srcPort, dstIp, dstPort);
    if (!c)
    {
        if (flags & kRst)
            return;
        if ((flags & kSyn) && !(flags & kAck))
        {
            TcpConn conn;
            conn.id = NextId();
            conn.socket = _network.Open(NetProto::Tcp, this, conn.id);
            if (conn.socket == VirtualNetwork::kNoSocket)
                return;
            conn.guestIp = srcIp;
            conn.guestPort = srcPort;
            conn.remoteIp = dstIp;
            conn.remotePort = dstPort;
            std::memcpy(conn.guestMac, srcMac, 6);
            conn.rcvNxt = seq + 1;
            conn.guestWindow = window;
            conn.guestMss = mss ? mss : 536;
            // A deterministic ISN: the four addresses and a counter
            conn.iss = (srcIp * 2654435761u) ^ (dstIp * 40503u) ^ (static_cast<uint32_t>(srcPort) << 16) ^ dstPort ^
                       (++_issCounter * 0x9E3779B9u);
            conn.sndUna = conn.sndNxt = conn.iss;
            conn.state = TcpState::Connecting;
            ++_counters.tcpConnections;
            const uint32_t id = conn.id;
            const uint16_t socket = conn.socket;
            _tcp[id] = std::move(conn);
            _network.Connect(socket, NetEndpoint{dstIp, dstPort});
            return;
        }
        // Nothing open here: RST (RFC 793 "reset generation")
        const uint32_t rstSeq = (flags & kAck) ? ack : 0;
        const uint32_t rstAck = seq + static_cast<uint32_t>(dataLength) + ((flags & kSyn) ? 1 : 0) + ((flags & kFin) ? 1 : 0);
        SendRst(srcMac, dstIp, dstPort, srcIp, srcPort, rstSeq, rstAck, !(flags & kAck));
        return;
    }

    std::memcpy(c->guestMac, srcMac, 6);
    if (flags & kRst)
    {
        CloseConn(c->id, true);
        return;
    }

    switch (c->state)
    {
        case TcpState::Connecting:
            return;   // the host has not answered yet: a retransmitted SYN waits with the first
        case TcpState::SynSentToGuest:
            if ((flags & kSyn) && (flags & kAck) && ack == c->iss + 1)
            {
                c->rcvNxt = seq + 1;
                c->sndUna = c->sndNxt = c->iss + 1;
                c->guestWindow = window;
                c->guestMss = mss ? mss : 536;
                c->state = TcpState::Established;
                c->retries = 0;
                c->rto = kRtoFrames;
                SendTcp(*c, kAck, c->sndNxt, nullptr, 0);
            }
            return;
        case TcpState::SynAckSent:
            if (flags & kSyn)
            {
                SendTcp(*c, kSyn | kAck, c->iss, nullptr, 0, true);   // our SYN-ACK was lost to the guest
                return;
            }
            if ((flags & kAck) && ack == c->iss + 1)
            {
                c->state = TcpState::Established;
                c->sndUna = c->iss + 1;
                c->retries = 0;
                c->rto = kRtoFrames;
            }
            else
                return;
            break;
        default:
            break;
    }

    // Acknowledgment of our bytes (and FIN)
    if (flags & kAck)
    {
        if (SeqLess(c->sndUna, ack) && SeqLessEq(ack, c->sndNxt))
        {
            uint32_t acked = ack - c->sndUna;
            const uint32_t dataAcked = std::min<uint32_t>(acked, static_cast<uint32_t>(c->queue.size()));
            c->queue.erase(c->queue.begin(), c->queue.begin() + dataAcked);
            c->sndUna = ack;
            c->retries = 0;
            c->rto = kRtoFrames;
            c->lastSendFrame = _frameCounter ? _frameCounter() : 0;
        }
        c->guestWindow = window;
    }

    // The guest's bytes
    if (dataLength || (flags & kFin))
    {
        if (seq == c->rcvNxt)
        {
            if (dataLength)
            {
                _network.Send(c->socket, data, static_cast<uint32_t>(dataLength));
                c->rcvNxt += static_cast<uint32_t>(dataLength);
            }
            if ((flags & kFin) && !c->guestFin)
            {
                c->guestFin = true;
                c->rcvNxt += 1;
                _network.ShutdownWrite(c->socket);
            }
        }
        // A duplicate or a segment out of order: (re-)acknowledge what arrived in order; the guest sends again
        SendTcp(*c, kAck, c->sndNxt, nullptr, 0);
    }

    if (c->guestFin && c->finSent && c->sndUna == c->sndNxt)
        CloseConn(c->id, true);
}

// ---------------------------------------------------------------------------
// The virtual network's answers
// ---------------------------------------------------------------------------

void EthernetGateway::OnNetEvent(uint32_t cookie, NetEventType type, NetEventStatus status, const NetEndpoint& peer,
                                 const uint8_t* data, uint32_t length, uint32_t source)
{
    // What an answer causes leaves at once: the TCP bytes it allows, then every frame the cards take now. So the
    // effect does not depend on where in the frame-boundary work the answer is applied (live: inside the virtual
    // network's Pump; TTD replay: from the journal at the first instruction after the boundary)
    HandleNetEvent(cookie, type, status, peer, data, length, source);
    if (TcpConn* c = FindConnById(cookie))
        PumpTcp(*c);
    Deliver();
}

void EthernetGateway::HandleNetEvent(uint32_t cookie, NetEventType type, NetEventStatus status, const NetEndpoint& peer,
                                     const uint8_t* data, uint32_t length, uint32_t source)
{
    (void)status;
    (void)source;
    if (auto u = _udp.find(cookie); u != _udp.end())
    {
        if (type != NetEventType::Datagram)
            return;
        UdpFlow& f = u->second;
        const bool dhcp = peer.port == 67;
        if (dhcp && length >= 240)
        {
            // Learn the lease the server handed out (ACK): the guest's address
            uint32_t yiaddr = Get32(data + 16);
            for (size_t at = 240; at + 2 < length && data[at] != 255; at += 2u + data[at + 1])
            {
                if (data[at] == 53 && data[at + 2] == 5)
                    Learn(data + 28, yiaddr);
            }
            // As slirp: the reply is broadcast (the client has no address yet)
            SendUdp(kBroadcastMac, peer.addr, peer.port, kBroadcastIp, f.guestPort, data, length);
            return;
        }
        SendUdp(f.guestMac, peer.addr, peer.port, f.guestIp, f.guestPort, data, length);
        f.lastUsedFrame = _frameCounter ? _frameCounter() : 0;
        return;
    }
    if (auto i = _icmp.find(cookie); i != _icmp.end())
    {
        if (type == NetEventType::EchoReply && length >= 8)
            SendIp(i->second.guestMac, peer.addr, i->second.guestIp, kProtoIcmp, std::vector<uint8_t>(data, data + length));
        return;
    }
    for (size_t n = 0; n < _listeners.size(); ++n)
    {
        if (_listeners[n].id != cookie)
            continue;
        if (type != NetEventType::Accepted)
            return;
        // The listening socket is now the connection to the host client: a SYN to the guest's server
        TcpConn conn;
        conn.id = NextId();
        conn.socket = _listeners[n].socket;
        conn.guestIp = GuestAddress();
        conn.guestPort = _listeners[n].guestPort;
        conn.remoteIp = peer.addr;
        conn.remotePort = peer.port;
        bool known = false;
        for (const Station& s : _stations)
        {
            if (s.ip == conn.guestIp)
            {
                std::memcpy(conn.guestMac, s.mac, 6);
                known = true;
            }
        }
        if (!known && !_ports.empty())
            _ports.front().port->StationMac(conn.guestMac);
        conn.iss = (peer.addr * 2654435761u) ^ (static_cast<uint32_t>(peer.port) << 16) ^ conn.guestPort ^
                   (++_issCounter * 0x9E3779B9u);
        conn.sndUna = conn.iss;
        conn.sndNxt = conn.iss + 1;
        conn.state = TcpState::SynSentToGuest;
        conn.guestWindow = 0;
        ++_counters.tcpConnections;
        _network.Rebind(conn.socket, this, conn.id);
        const uint32_t id = conn.id;
        _tcp[id] = std::move(conn);
        SendTcp(_tcp[id], kSyn, _tcp[id].iss, nullptr, 0, true);
        // A new listener for the next client
        Listener next;
        next.id = NextId();
        next.guestPort = _listeners[n].guestPort;
        next.socket = _network.Open(NetProto::Tcp, this, next.id);
        if (next.socket != VirtualNetwork::kNoSocket)
            _network.Listen(next.socket, next.guestPort);
        _listeners[n] = next;
        return;
    }

    TcpConn* c = FindConnById(cookie);
    if (!c)
        return;
    switch (type)
    {
        case NetEventType::Connected:
            c->state = TcpState::SynAckSent;
            c->sndUna = c->iss;
            c->sndNxt = c->iss + 1;
            c->retries = 0;
            c->rto = kRtoFrames;
            SendTcp(*c, kSyn | kAck, c->iss, nullptr, 0, true);
            break;
        case NetEventType::ConnectFailed:
            ++_counters.tcpRefused;
            SendRst(c->guestMac, c->remoteIp, c->remotePort, c->guestIp, c->guestPort, 0, c->rcvNxt, true);
            CloseConn(c->id, true);
            break;
        case NetEventType::Data:
            if (c->queue.size() + length > kMaxTcpQueue)
            {
                // The guest does not read: a reset is all a router can do without a host pause (open item)
                SendRst(c->guestMac, c->remoteIp, c->remotePort, c->guestIp, c->guestPort, c->sndNxt, c->rcvNxt, true);
                CloseConn(c->id, true);
                break;
            }
            c->queue.insert(c->queue.end(), data, data + length);
            break;
        case NetEventType::PeerClosed:
            c->hostFin = true;
            break;
        case NetEventType::Reset:
            SendRst(c->guestMac, c->remoteIp, c->remotePort, c->guestIp, c->guestPort, c->sndNxt, c->rcvNxt, true);
            CloseConn(c->id, true);
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// The frame boundary
// ---------------------------------------------------------------------------

void EthernetGateway::PumpTcp(TcpConn& c)
{
    const uint64_t now = _frameCounter ? _frameCounter() : 0;
    // Retransmission: nothing acknowledged for rto frames - go back to the oldest unacknowledged byte
    if (c.sndUna != c.sndNxt && now - c.lastSendFrame >= c.rto)
    {
        if (c.retries >= kMaxRetries)
        {
            SendRst(c.guestMac, c.remoteIp, c.remotePort, c.guestIp, c.guestPort, c.sndNxt, c.rcvNxt, true);
            CloseConn(c.id, true);
            return;
        }
        ++c.retries;
        c.rto *= 2;
        ++_counters.retransmits;
        if (c.state == TcpState::SynAckSent)
        {
            SendTcp(c, kSyn | kAck, c.iss, nullptr, 0, true);
            return;
        }
        if (c.state == TcpState::SynSentToGuest)
        {
            SendTcp(c, kSyn, c.iss, nullptr, 0, true);
            return;
        }
        c.sndNxt = c.sndUna;
        c.finSent = false;
    }
    if (c.state != TcpState::Established)
        return;

    uint32_t sent = c.sndNxt - c.sndUna - (c.finSent ? 1u : 0u);
    const uint16_t mss = c.guestMss ? c.guestMss : 536;
    while (sent < c.queue.size() && !c.finSent)
    {
        const uint32_t inFlight = c.sndNxt - c.sndUna;
        if (inFlight >= c.guestWindow)
            break;
        const uint32_t room = c.guestWindow - inFlight;
        const uint32_t n = std::min<uint32_t>({static_cast<uint32_t>(mss), room, static_cast<uint32_t>(c.queue.size() - sent)});
        std::vector<uint8_t> chunk(c.queue.begin() + sent, c.queue.begin() + sent + n);
        SendTcp(c, kAck | kPsh, c.sndNxt, chunk.data(), chunk.size());
        c.sndNxt += n;
        sent += n;
    }
    if (c.hostFin && !c.finSent && sent == c.queue.size())
    {
        SendTcp(c, kFin | kAck, c.sndNxt, nullptr, 0);
        c.sndNxt += 1;
        c.finSent = true;
    }
}

void EthernetGateway::OnFrame()
{
    // TCP output and retransmissions (a conn may close inside: collect the ids first)
    std::vector<uint32_t> ids;
    ids.reserve(_tcp.size());
    for (const auto& [id, c] : _tcp)
        ids.push_back(id);
    for (uint32_t id : ids)
    {
        if (TcpConn* c = FindConnById(id))
            PumpTcp(*c);
    }

    // Idle UDP flows close after 60 s of emulated time
    const uint64_t now = _frameCounter ? _frameCounter() : 0;
    for (auto it = _udp.begin(); it != _udp.end();)
    {
        if (now - it->second.lastUsedFrame > kUdpIdleFrames)
        {
            _network.Close(it->second.socket);
            it = _udp.erase(it);
        }
        else
            ++it;
    }

    Deliver();
}

void EthernetGateway::Deliver()
{
    // Everything for the cards, in order, while each card's ring takes it
    for (PortQueue& q : _ports)
    {
        while (!q.frames.empty())
        {
            if (!q.port->Offer(q.frames.front().data(), q.frames.front().size()))
            {
                ++_counters.ringFullWaits;
                break;
            }
            Record(true, q.port->PortKey(), q.frames.front());
            ++_counters.framesToCards;
            q.frames.pop_front();
        }
    }
}

bool EthernetGateway::Inject(const std::string& portKey, const std::vector<uint8_t>& frame)
{
    for (PortQueue& q : _ports)
    {
        if (q.port->PortKey() == portKey)
        {
            q.frames.push_back(frame);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Reports
// ---------------------------------------------------------------------------

std::vector<uint8_t> EthernetGateway::CapturePcap(const std::string& portKey) const
{
    std::vector<uint8_t> out;
    Out o{out};
    o.U32(0xA1B2C3D4u);   // pcap, microsecond timestamps, little-endian
    o.U16(2);
    o.U16(4);
    o.U32(0);
    o.U32(0);
    o.U32(65535);
    o.U32(1);             // LINKTYPE_ETHERNET
    for (const CapturedFrame& f : _capture)
    {
        if (!portKey.empty() && f.port != portKey)
            continue;
        const uint64_t us = f.frame * 20000 + (f.index % 1000);   // the machine frame (20 ms) keeps the order
        o.U32(static_cast<uint32_t>(us / 1000000));
        o.U32(static_cast<uint32_t>(us % 1000000));
        o.U32(static_cast<uint32_t>(f.bytes.size()));
        o.U32(static_cast<uint32_t>(f.bytes.size()));
        o.Bytes(f.bytes.data(), f.bytes.size());
    }
    return out;
}

StateNode EthernetGateway::Describe() const
{
    StateNode ret = StateNode::Object();
    ret["mode"] = "nat";
    ret["router_mac"] = MacText(kRouterMac);
    ret["router_ip"] = NetIpToString(_network.Config().gateway);
    ret["dns_ip"] = NetIpToString(_network.Config().dnsServer);
    StateNode ports = StateNode::Array();
    for (const PortQueue& q : _ports)
    {
        StateNode p = StateNode::Object();
        uint8_t mac[6];
        q.port->StationMac(mac);
        p["port"] = q.port->PortKey();
        p["mac"] = MacText(mac);
        p["queued_frames"] = static_cast<uint64_t>(q.frames.size());
        DhcpServer::Mac key{};
        std::memcpy(key.data(), mac, 6);
        auto lease = _network.Dhcp().Leases().find(key);
        p["dhcp_lease"] = lease == _network.Dhcp().Leases().end() ? std::string("none") : NetIpToString(lease->second);
        ports.push(std::move(p));
    }
    ret["ports"] = ports;
    StateNode arp = StateNode::Array();
    for (const Station& s : _stations)
    {
        StateNode n = StateNode::Object();
        n["mac"] = MacText(s.mac);
        n["ip"] = NetIpToString(s.ip);
        arp.push(std::move(n));
    }
    ret["arp"] = arp;
    StateNode tcp = StateNode::Array();
    static const char* const kStates[] = {"connecting", "syn-ack-sent", "established", "syn-sent-to-guest", "closed"};
    for (const auto& [id, c] : _tcp)
    {
        StateNode n = StateNode::Object();
        n["guest"] = NetIpToString(c.guestIp) + ":" + std::to_string(c.guestPort);
        n["remote"] = NetIpToString(c.remoteIp) + ":" + std::to_string(c.remotePort);
        n["state"] = kStates[static_cast<int>(c.state)];
        n["socket"] = static_cast<int>(c.socket);
        n["queued_bytes"] = static_cast<uint64_t>(c.queue.size());
        n["unacked"] = static_cast<uint64_t>(c.sndNxt - c.sndUna);
        n["guest_window"] = static_cast<int>(c.guestWindow);
        n["guest_mss"] = static_cast<int>(c.guestMss);
        n["guest_fin"] = c.guestFin;
        n["host_fin"] = c.hostFin;
        n["retries"] = static_cast<int>(c.retries);
        tcp.push(std::move(n));
    }
    ret["tcp"] = tcp;
    StateNode udp = StateNode::Array();
    for (const auto& [id, f] : _udp)
    {
        StateNode n = StateNode::Object();
        n["guest"] = NetIpToString(f.guestIp) + ":" + std::to_string(f.guestPort);
        n["socket"] = static_cast<int>(f.socket);
        udp.push(std::move(n));
    }
    ret["udp"] = udp;
    StateNode listeners = StateNode::Array();
    for (const Listener& l : _listeners)
        listeners.push(StateNode(static_cast<int>(l.guestPort)));
    ret["forwarded_guest_ports"] = listeners;
    StateNode c = StateNode::Object();
    c["frames_from_cards"] = _counters.framesFromCards;
    c["frames_to_cards"] = _counters.framesToCards;
    c["switched"] = _counters.switched;
    c["arp_replies"] = _counters.arpReplies;
    c["dhcp"] = _counters.dhcp;
    c["dns"] = _counters.dns;
    c["udp"] = _counters.udp;
    c["icmp"] = _counters.icmp;
    c["tcp_connections"] = _counters.tcpConnections;
    c["tcp_refused"] = _counters.tcpRefused;
    c["retransmits"] = _counters.retransmits;
    c["resets"] = _counters.resets;
    c["unsupported"] = _counters.unsupported;
    c["fragments"] = _counters.fragments;
    c["bad_checksum"] = _counters.badChecksum;
    c["runts"] = _counters.runts;
    c["queue_drops"] = _counters.queueDrops;
    c["ring_full_waits"] = _counters.ringFullWaits;
    ret["counters"] = c;
    ret["captured_frames"] = static_cast<uint64_t>(_capture.size());
    return ret;
}

// ---------------------------------------------------------------------------
// TTD
// ---------------------------------------------------------------------------

std::vector<uint8_t> EthernetGateway::SaveState() const
{
    std::vector<uint8_t> v;
    Out o{v};
    o.U8(kStateVersion);
    o.U32(_nextId);
    o.U16(_ipId);
    o.U32(_issCounter);
    const uint64_t* counters = reinterpret_cast<const uint64_t*>(&_counters);
    for (size_t i = 0; i < sizeof(Counters) / sizeof(uint64_t); ++i)
        o.U64(counters[i]);
    o.U16(static_cast<uint16_t>(_stations.size()));
    for (const Station& s : _stations)
    {
        o.Bytes(s.mac, 6);
        o.U32(s.ip);
    }
    o.U16(static_cast<uint16_t>(_tcp.size()));
    for (const auto& [id, c] : _tcp)
    {
        o.U32(c.id);
        o.U16(c.socket);
        o.U8(static_cast<uint8_t>(c.state));
        o.U32(c.guestIp);
        o.U32(c.remoteIp);
        o.U16(c.guestPort);
        o.U16(c.remotePort);
        o.Bytes(c.guestMac, 6);
        o.U32(c.iss);
        o.U32(c.sndUna);
        o.U32(c.sndNxt);
        o.U32(c.rcvNxt);
        o.U16(c.guestWindow);
        o.U16(c.guestMss);
        o.U8(static_cast<uint8_t>((c.guestFin ? 1 : 0) | (c.hostFin ? 2 : 0) | (c.finSent ? 4 : 0)));
        o.U64(c.lastSendFrame);
        o.U32(c.rto);
        o.U8(c.retries);
        o.U32(static_cast<uint32_t>(c.queue.size()));
        for (uint8_t b : c.queue)
            o.U8(b);
    }
    o.U16(static_cast<uint16_t>(_udp.size()));
    for (const auto& [id, f] : _udp)
    {
        o.U32(f.id);
        o.U16(f.socket);
        o.U32(f.guestIp);
        o.U16(f.guestPort);
        o.Bytes(f.guestMac, 6);
        o.U64(f.lastUsedFrame);
    }
    o.U16(static_cast<uint16_t>(_icmp.size()));
    for (const auto& [id, f] : _icmp)
    {
        o.U32(f.id);
        o.U16(f.socket);
        o.U32(f.guestIp);
        o.Bytes(f.guestMac, 6);
    }
    o.U16(static_cast<uint16_t>(_listeners.size()));
    for (const Listener& l : _listeners)
    {
        o.U32(l.id);
        o.U16(l.socket);
        o.U16(l.guestPort);
    }
    o.U16(static_cast<uint16_t>(_ports.size()));
    for (const PortQueue& q : _ports)
    {
        const std::string& key = q.port->PortKey();
        o.U8(static_cast<uint8_t>(key.size()));
        o.Bytes(reinterpret_cast<const uint8_t*>(key.data()), key.size());
        o.U32(static_cast<uint32_t>(q.frames.size()));
        for (const auto& f : q.frames)
        {
            o.U16(static_cast<uint16_t>(f.size()));
            o.Bytes(f.data(), f.size());
        }
    }
    return v;
}

bool EthernetGateway::LoadState(const uint8_t* data, size_t size)
{
    In in{data, size};
    if (in.U8() != kStateVersion)
        return false;
    EthernetGateway::Counters counters{};
    std::vector<Station> stations;
    std::map<uint32_t, TcpConn> tcp;
    std::map<uint32_t, UdpFlow> udp;
    std::map<uint32_t, IcmpFlow> icmp;
    std::vector<Listener> listeners;
    const uint32_t nextId = in.U32();
    const uint16_t ipId = in.U16();
    const uint32_t issCounter = in.U32();
    uint64_t* c64 = reinterpret_cast<uint64_t*>(&counters);
    for (size_t i = 0; i < sizeof(Counters) / sizeof(uint64_t); ++i)
        c64[i] = in.U64();
    for (uint16_t n = in.U16(); n && in.ok; --n)
    {
        Station s;
        in.Bytes(s.mac, 6);
        s.ip = in.U32();
        stations.push_back(s);
    }
    for (uint16_t n = in.U16(); n && in.ok; --n)
    {
        TcpConn c;
        c.id = in.U32();
        c.socket = in.U16();
        c.state = static_cast<TcpState>(in.U8());
        c.guestIp = in.U32();
        c.remoteIp = in.U32();
        c.guestPort = in.U16();
        c.remotePort = in.U16();
        in.Bytes(c.guestMac, 6);
        c.iss = in.U32();
        c.sndUna = in.U32();
        c.sndNxt = in.U32();
        c.rcvNxt = in.U32();
        c.guestWindow = in.U16();
        c.guestMss = in.U16();
        const uint8_t fl = in.U8();
        c.guestFin = (fl & 1) != 0;
        c.hostFin = (fl & 2) != 0;
        c.finSent = (fl & 4) != 0;
        c.lastSendFrame = in.U64();
        c.rto = in.U32();
        c.retries = in.U8();
        const uint32_t q = in.U32();
        if (!in.Need(q))
            return false;
        c.queue.assign(data + in.at, data + in.at + q);
        in.at += q;
        tcp[c.id] = std::move(c);
    }
    for (uint16_t n = in.U16(); n && in.ok; --n)
    {
        UdpFlow f;
        f.id = in.U32();
        f.socket = in.U16();
        f.guestIp = in.U32();
        f.guestPort = in.U16();
        in.Bytes(f.guestMac, 6);
        f.lastUsedFrame = in.U64();
        udp[f.id] = f;
    }
    for (uint16_t n = in.U16(); n && in.ok; --n)
    {
        IcmpFlow f;
        f.id = in.U32();
        f.socket = in.U16();
        f.guestIp = in.U32();
        in.Bytes(f.guestMac, 6);
        icmp[f.id] = f;
    }
    for (uint16_t n = in.U16(); n && in.ok; --n)
    {
        Listener l;
        l.id = in.U32();
        l.socket = in.U16();
        l.guestPort = in.U16();
        listeners.push_back(l);
    }
    std::map<std::string, std::deque<std::vector<uint8_t>>> queues;
    for (uint16_t n = in.U16(); n && in.ok; --n)
    {
        const uint8_t keyLength = in.U8();
        std::string key(keyLength, '\0');
        in.Bytes(reinterpret_cast<uint8_t*>(key.data()), keyLength);
        auto& frames = queues[key];
        for (uint32_t f = in.U32(); f && in.ok; --f)
        {
            const uint16_t length = in.U16();
            if (!in.Need(length))
                return false;
            frames.emplace_back(data + in.at, data + in.at + length);
            in.at += length;
        }
    }
    if (!in.ok)
        return false;
    _nextId = nextId;
    _ipId = ipId;
    _issCounter = issCounter;
    _counters = counters;
    _stations = std::move(stations);
    _tcp = std::move(tcp);
    _udp = std::move(udp);
    _icmp = std::move(icmp);
    _listeners = std::move(listeners);
    for (PortQueue& q : _ports)
        q.frames = queues[q.port->PortKey()];
    return true;
}
