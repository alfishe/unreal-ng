#include "emulator/io/network/traffic/socketpacketizer.h"

#include <algorithm>
#include <cstring>

namespace
{
constexpr uint8_t kFin = 0x01, kSyn = 0x02, kRst = 0x04, kPsh = 0x08, kAck = 0x10;
constexpr size_t kMss = 1460;

void Put16(std::vector<uint8_t>& v, size_t at, uint16_t x)
{
    v[at] = static_cast<uint8_t>(x >> 8);
    v[at + 1] = static_cast<uint8_t>(x);
}
void Put32(std::vector<uint8_t>& v, size_t at, uint32_t x)
{
    Put16(v, at, static_cast<uint16_t>(x >> 16));
    Put16(v, at + 2, static_cast<uint16_t>(x));
}
uint16_t Checksum(const uint8_t* d, size_t n, uint32_t sum = 0)
{
    for (size_t i = 0; i + 1 < n; i += 2)
        sum += static_cast<uint32_t>((d[i] << 8) | d[i + 1]);
    if (n & 1)
        sum += static_cast<uint32_t>(d[n - 1] << 8);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<uint16_t>(~sum);
}
uint32_t Pseudo(uint32_t src, uint32_t dst, uint8_t proto, size_t length)
{
    return (src >> 16) + (src & 0xFFFF) + (dst >> 16) + (dst & 0xFFFF) + proto + static_cast<uint32_t>(length);
}

/// Ethernet + IPv4 around a transport payload; the MACs say which side sent it (02:..:01 the adapter, 02:..:02 the
/// network)
std::vector<uint8_t> Ip(bool fromAdapter, uint32_t src, uint32_t dst, uint8_t proto, const std::vector<uint8_t>& payload)
{
    static const uint8_t adapterMac[6] = {0x02, 0x00, 0x5E, 0x00, 0x00, 0x01};
    static const uint8_t networkMac[6] = {0x02, 0x00, 0x5E, 0x00, 0x00, 0x02};
    std::vector<uint8_t> f(14 + 20 + payload.size());
    std::memcpy(f.data(), fromAdapter ? networkMac : adapterMac, 6);
    std::memcpy(f.data() + 6, fromAdapter ? adapterMac : networkMac, 6);
    Put16(f, 12, 0x0800);
    f[14] = 0x45;
    Put16(f, 16, static_cast<uint16_t>(20 + payload.size()));
    f[22] = 64;
    f[23] = proto;
    Put32(f, 26, src);
    Put32(f, 30, dst);
    Put16(f, 24, Checksum(f.data() + 14, 20));
    std::copy(payload.begin(), payload.end(), f.begin() + 34);
    return f;
}

std::vector<uint8_t> Tcp(bool fromAdapter, uint32_t src, uint16_t sport, uint32_t dst, uint16_t dport, uint32_t seq,
                         uint32_t ack, uint8_t flags, const uint8_t* data, size_t length)
{
    std::vector<uint8_t> t(20 + length);
    Put16(t, 0, sport);
    Put16(t, 2, dport);
    Put32(t, 4, seq);
    Put32(t, 8, ack);
    t[12] = 0x50;
    t[13] = flags;
    Put16(t, 14, 65535);
    if (length)
        std::memcpy(t.data() + 20, data, length);
    Put16(t, 16, Checksum(t.data(), t.size(), Pseudo(src, dst, 6, t.size())));
    return Ip(fromAdapter, src, dst, 6, t);
}

std::vector<uint8_t> Udp(bool fromAdapter, uint32_t src, uint16_t sport, uint32_t dst, uint16_t dport, const uint8_t* data,
                         size_t length)
{
    std::vector<uint8_t> u(8 + length);
    Put16(u, 0, sport);
    Put16(u, 2, dport);
    Put16(u, 4, static_cast<uint16_t>(u.size()));
    if (length)
        std::memcpy(u.data() + 8, data, length);
    uint16_t sum = Checksum(u.data(), u.size(), Pseudo(src, dst, 17, u.size()));
    Put16(u, 6, sum ? sum : 0xFFFF);
    return Ip(fromAdapter, src, dst, 17, u);
}
}  // namespace

void SocketPacketizer::Reset()
{
    _conns.clear();
    _addresses.clear();
}

uint32_t SocketPacketizer::AdapterAddress(const std::string& adapter)
{
    auto it = _addresses.find(adapter);
    if (it != _addresses.end())
        return it->second;
    const uint32_t address = NetIp(10, 0, 2, 15) + static_cast<uint32_t>(_addresses.size());
    _addresses.emplace(adapter, address);
    return address;
}

std::vector<std::vector<uint8_t>> SocketPacketizer::Packets(const TrafficRecord& r)
{
    std::vector<std::vector<uint8_t>> out;
    if (r.kind != TrafficRecord::Kind::Socket || r.proto == NetProto::Serial)
        return out;
    const uint32_t me = AdapterAddress(r.adapter);
    const uint32_t peer = r.peer.addr;
    const uint16_t myPort = r.localPort ? r.localPort : static_cast<uint16_t>(49152 + (r.socket & 0x3FFF));
    const uint16_t peerPort = r.peer.port;
    const std::string& op = r.op;
    const uint8_t* data = r.bytes.empty() ? nullptr : r.bytes.data();
    const size_t length = r.bytes.size();

    if (r.proto == NetProto::Udp)
    {
        if (op == "sendto")
            out.push_back(Udp(true, me, myPort, peer, peerPort, data, length));
        else if (op == "datagram")
            out.push_back(Udp(false, peer, peerPort, me, myPort, data, length));
        return out;
    }
    if (r.proto == NetProto::Icmp)
    {
        if (op == "echo" && length)
            out.push_back(Ip(true, me, peer, 1, r.bytes));
        else if (op == "echo-reply" && length)
            out.push_back(Ip(false, peer, me, 1, r.bytes));
        return out;
    }

    // TCP: a conversation opens with connect / accepted; other operations of a socket the tap never saw open (the ring
    // started in the middle) continue on fresh sequence numbers
    const auto key = std::make_pair(r.adapter, r.socket);
    if (op == "connect" || op == "connect-tls" || op == "accepted")
        _conns[key] = Conn{};
    else if (op == "close" && !_conns.count(key))
        return out;   // a listener or a socket that never connected: nothing on the wire
    Conn& c = _conns[key];
    auto fromMe = [&](uint8_t flags, const uint8_t* d, size_t n) {
        out.push_back(Tcp(true, me, myPort, peer, peerPort, c.mySeq, c.peerSeq, flags, d, n));
        c.mySeq += static_cast<uint32_t>(n) + ((flags & (kSyn | kFin)) ? 1 : 0);
    };
    auto fromPeer = [&](uint8_t flags, const uint8_t* d, size_t n) {
        out.push_back(Tcp(false, peer, peerPort, me, myPort, c.peerSeq, c.mySeq, flags, d, n));
        c.peerSeq += static_cast<uint32_t>(n) + ((flags & (kSyn | kFin)) ? 1 : 0);
    };
    if (op == "connect" || op == "connect-tls")
    {
        out.push_back(Tcp(true, me, myPort, peer, peerPort, c.mySeq, 0, kSyn, nullptr, 0));
        c.mySeq += 1;
    }
    else if (op == "connected")
    {
        fromPeer(kSyn | kAck, nullptr, 0);
        fromMe(kAck, nullptr, 0);
    }
    else if (op == "accepted")
    {
        // A host client of a guest server: the peer opens
        fromPeer(kSyn, nullptr, 0);
        fromMe(kSyn | kAck, nullptr, 0);
        fromPeer(kAck, nullptr, 0);
    }
    else if (op == "send" || op == "data")
    {
        for (size_t at = 0; at < length; at += kMss)
        {
            const size_t n = std::min(kMss, length - at);
            if (op == "send")
                fromMe(kPsh | kAck, data + at, n);
            else
                fromPeer(kPsh | kAck, data + at, n);
        }
    }
    else if (op == "shutdown" || op == "close")
    {
        if (!c.finSent)
            fromMe(kFin | kAck, nullptr, 0);
        c.finSent = true;
        if (op == "close")
            _conns.erase(key);
    }
    else if (op == "peer-closed")
        fromPeer(kFin | kAck, nullptr, 0);
    else if (op == "reset" || op == "connect-failed")
        fromPeer(kRst | kAck, nullptr, 0);
    return out;
}
