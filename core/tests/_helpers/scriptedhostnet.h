#pragma once

/// @file scriptedhostnet.h
/// @brief A host network with scripted servers behind it, for the network adapter tests (network tdd §15, phase
/// SN0: "a scripted host server helper ... on FakeHostNet, reusable for every adapter"). No real sockets, no
/// threads: every answer is a HostNetEvent the virtual network drains at the next frame boundary (journaled like
/// a real host's), so a test runs the same way every time.
///
/// Servers (by endpoint; an address with no server refuses TCP, drops UDP, does not answer pings):
///   - HTTP/1.0: GET <path> -> 200 with Content-Length and the body, or 404; the server closes after the answer;
///   - TCP echo; Gopher (selector -> text, then close); a "banner" server that greets and then echoes (telnet);
///   - UDP echo; NTP (mode 4 answer with a fixed transmit time);
///   - DNS: the names it knows answer A records, others NXDOMAIN (the host resolver of a DnsMode=HOST network);
///   - ICMP echo for the addresses marked pingable.
/// Host clients for guest servers: ConnectClient() makes a listener accept one (the Forward= path).
///
/// Worked example: AddHttp({NetIp(192,0,2,10), 80}, {{"/f.bin", bytes}}); a guest connects to 192.0.2.10:80 and
/// sends "GET /f.bin HTTP/1.0\r\n\r\n": the next Pump delivers Connected, then Data (headers + bytes in 1460-byte
/// pieces), then PeerClosed.

#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "_helpers/fakehostnet.h"
#include "common/network/dnsmessage.h"

class ScriptedHostNet : public FakeHostNet
{
public:
    static constexpr size_t kSegment = 1460;

    // --- Servers -------------------------------------------------------------------

    void AddHttp(const NetEndpoint& at, std::map<std::string, std::vector<uint8_t>> files)
    {
        _tcp[Key(at)] = {Kind::Http, std::move(files), {}, {}};
    }
    void AddEcho(const NetEndpoint& at) { _tcp[Key(at)] = {Kind::Echo, {}, {}, {}}; }
    void AddGopher(const NetEndpoint& at, std::map<std::string, std::string> selectors)
    {
        _tcp[Key(at)] = {Kind::Gopher, {}, std::move(selectors), {}};
    }
    void AddBanner(const NetEndpoint& at, const std::string& banner) { _tcp[Key(at)] = {Kind::Banner, {}, {}, banner}; }
    void AddUdpEcho(const NetEndpoint& at) { _udp[Key(at)] = UdpKind::Echo; }
    void AddNtp(const NetEndpoint& at, uint32_t unixSeconds)
    {
        _udp[Key(at)] = UdpKind::Ntp;
        _ntpSeconds = unixSeconds;
    }
    void AddName(const std::string& name, uint32_t addr) { _names[name] = addr; }
    void SetPingable(uint32_t addr) { _pingable.insert(addr); }

    /// A host client connects to the guest server behind host listener `listenerId` (the id TcpListen got): the
    /// listener accepts it as connection `clientId` from `peer`; later SendFromClient / CloseClient act on it
    void ConnectClient(uint16_t listenerId, uint16_t clientId, const NetEndpoint& peer)
    {
        Push(NetEventType::Accepted, listenerId, NetEventStatus::Ok, peer,
             {static_cast<uint8_t>(clientId), static_cast<uint8_t>(clientId >> 8)});
    }
    void SendFromClient(uint16_t clientId, const std::string& text)
    {
        Push(NetEventType::Data, clientId, NetEventStatus::Ok, {}, std::vector<uint8_t>(text.begin(), text.end()));
    }
    void CloseClient(uint16_t clientId) { Push(NetEventType::PeerClosed, clientId); }

    /// Bytes a socket sent to a host server so far (what the server received)
    std::vector<uint8_t> Received(uint16_t socket) const
    {
        auto it = _conns.find(socket);
        return it == _conns.end() ? std::vector<uint8_t>() : it->second.in;
    }

    // --- IHostNet ------------------------------------------------------------------

    void TcpConnect(uint16_t socket, const NetEndpoint& to) override
    {
        FakeHostNet::TcpConnect(socket, to);
        auto it = _tcp.find(Key(to));
        if (it == _tcp.end())
        {
            Push(NetEventType::ConnectFailed, socket, NetEventStatus::Refused, to);
            return;
        }
        _conns[socket] = Conn{it->second.kind, to, {}, false};
        Push(NetEventType::Connected, socket, NetEventStatus::Ok, to);
        if (it->second.kind == Kind::Banner)
            Answer(socket, std::vector<uint8_t>(it->second.banner.begin(), it->second.banner.end()));
    }

    void TcpSend(uint16_t socket, const uint8_t* data, uint32_t length) override
    {
        FakeHostNet::TcpSend(socket, data, length);
        auto c = _conns.find(socket);
        if (c == _conns.end() || c->second.done)
            return;
        Conn& conn = c->second;
        conn.in.insert(conn.in.end(), data, data + length);
        const Server& server = _tcp[Key(conn.at)];
        switch (conn.kind)
        {
            case Kind::Echo:
            case Kind::Banner:
                Answer(socket, std::vector<uint8_t>(data, data + length));
                break;
            case Kind::Http:
            {
                const std::string text(conn.in.begin(), conn.in.end());
                const size_t end = text.find("\r\n\r\n");
                if (end == std::string::npos)
                    break;
                conn.done = true;
                std::string path;
                if (text.compare(0, 4, "GET ") == 0)
                    path = text.substr(4, text.find(' ', 4) - 4);
                auto file = server.files.find(path);
                std::string head;
                std::vector<uint8_t> body;
                if (file == server.files.end())
                {
                    head = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                }
                else
                {
                    body = file->second;
                    head = "HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
                           std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
                }
                std::vector<uint8_t> reply(head.begin(), head.end());
                reply.insert(reply.end(), body.begin(), body.end());
                Answer(socket, reply);
                Push(NetEventType::PeerClosed, socket, NetEventStatus::Ok, conn.at);
                break;
            }
            case Kind::Gopher:
            {
                const std::string text(conn.in.begin(), conn.in.end());
                const size_t end = text.find("\r\n");
                if (end == std::string::npos)
                    break;
                conn.done = true;
                auto sel = server.selectors.find(text.substr(0, end));
                const std::string page = sel == server.selectors.end() ? "3Not found\t\terror.host\t1\r\n.\r\n" : sel->second;
                Answer(socket, std::vector<uint8_t>(page.begin(), page.end()));
                Push(NetEventType::PeerClosed, socket, NetEventStatus::Ok, conn.at);
                break;
            }
        }
    }

    void TcpShutdownWrite(uint16_t socket) override
    {
        FakeHostNet::TcpShutdownWrite(socket);
        auto c = _conns.find(socket);
        if (c != _conns.end() && (c->second.kind == Kind::Echo || c->second.kind == Kind::Banner) && !c->second.done)
        {
            // An echo server closes its side once the client is done
            c->second.done = true;
            Push(NetEventType::PeerClosed, socket, NetEventStatus::Ok, c->second.at);
        }
    }

    void UdpSend(uint16_t socket, const NetEndpoint& to, const uint8_t* data, uint32_t length) override
    {
        FakeHostNet::UdpSend(socket, to, data, length);
        auto it = _udp.find(Key(to));
        if (it == _udp.end())
            return;
        if (it->second == UdpKind::Echo)
        {
            Push(NetEventType::Datagram, socket, NetEventStatus::Ok, to, std::vector<uint8_t>(data, data + length));
            return;
        }
        if (length < 48)
            return;
        // NTP: version of the request, mode 4 (server), stratum 1, the request's transmit time as originate, the
        // fixed time as receive and transmit time (seconds since 1900)
        std::vector<uint8_t> reply(48, 0);
        reply[0] = static_cast<uint8_t>((data[0] & 0x38) | 0x04);
        reply[1] = 1;
        std::memcpy(&reply[24], data + 40, 8);
        const uint32_t ntp = _ntpSeconds + 2208988800u;
        for (int field : {32, 40})
        {
            reply[field] = static_cast<uint8_t>(ntp >> 24);
            reply[field + 1] = static_cast<uint8_t>(ntp >> 16);
            reply[field + 2] = static_cast<uint8_t>(ntp >> 8);
            reply[field + 3] = static_cast<uint8_t>(ntp);
        }
        Push(NetEventType::Datagram, socket, NetEventStatus::Ok, to, std::move(reply));
    }

    void DnsQuery(uint16_t socket, const NetEndpoint& server, const uint8_t* query, uint32_t length) override
    {
        FakeHostNet::DnsQuery(socket, server, query, length);
        dns::Question q;
        if (!dns::ParseQuery(query, length, q))
            return;
        auto it = _names.find(q.name);
        const std::vector<uint32_t> addresses = it == _names.end() ? std::vector<uint32_t>() : std::vector<uint32_t>{it->second};
        Push(NetEventType::Datagram, socket, NetEventStatus::Ok, server,
             dns::BuildAnswer(query, length, q, addresses, addresses.empty() ? dns::kRcodeNxDomain : dns::kRcodeNoError));
    }

    void IcmpEcho(uint16_t socket, const NetEndpoint& to, const uint8_t* data, uint32_t length) override
    {
        FakeHostNet::IcmpEcho(socket, to, data, length);
        if (!_pingable.count(to.addr) || length < 8)
            return;
        std::vector<uint8_t> reply(data, data + length);
        reply[0] = 0;   // echo reply
        reply[2] = reply[3] = 0;
        uint32_t sum = 0;
        for (size_t i = 0; i + 1 < reply.size(); i += 2)
            sum += static_cast<uint32_t>((reply[i] << 8) | reply[i + 1]);
        if (reply.size() & 1)
            sum += static_cast<uint32_t>(reply.back() << 8);
        while (sum >> 16)
            sum = (sum & 0xFFFF) + (sum >> 16);
        reply[2] = static_cast<uint8_t>(~sum >> 8);
        reply[3] = static_cast<uint8_t>(~sum);
        Push(NetEventType::EchoReply, socket, NetEventStatus::Ok, NetEndpoint{to.addr, 0}, std::move(reply));
    }

    void Close(uint16_t socket) override
    {
        FakeHostNet::Close(socket);
        _conns.erase(socket);
    }

private:
    enum class Kind : uint8_t { Http, Echo, Gopher, Banner };
    enum class UdpKind : uint8_t { Echo, Ntp };
    struct Server
    {
        Kind kind = Kind::Echo;
        std::map<std::string, std::vector<uint8_t>> files;
        std::map<std::string, std::string> selectors;
        std::string banner;
    };
    struct Conn
    {
        Kind kind = Kind::Echo;
        NetEndpoint at;
        std::vector<uint8_t> in;
        bool done = false;
    };

    static uint64_t Key(const NetEndpoint& e) { return (static_cast<uint64_t>(e.addr) << 16) | e.port; }

    void Answer(uint16_t socket, const std::vector<uint8_t>& bytes)
    {
        for (size_t at = 0; at < bytes.size(); at += kSegment)
        {
            const size_t n = bytes.size() - at < kSegment ? bytes.size() - at : kSegment;
            Push(NetEventType::Data, socket, NetEventStatus::Ok, {},
                 std::vector<uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                                      bytes.begin() + static_cast<std::ptrdiff_t>(at + n)));
        }
    }

    std::map<uint64_t, Server> _tcp;
    std::map<uint64_t, UdpKind> _udp;
    std::map<std::string, uint32_t> _names;
    std::set<uint32_t> _pingable;
    std::map<uint16_t, Conn> _conns;
    uint32_t _ntpSeconds = 0;
};
