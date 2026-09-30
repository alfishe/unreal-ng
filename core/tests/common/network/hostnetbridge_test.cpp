// Host bridge on real sockets, loopback only (network adapters TDD §12: no
// Internet in core-tests). Each test runs a tiny server on 127.0.0.1.

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "_helpers/testwaithelper.h"
#include "common/network/dnsmessage.h"
#include "common/network/hostnetbridge.h"
#include "common/network/netsockets.h"

namespace
{
constexpr uint32_t kLoopback = NetIp(127, 0, 0, 1);

/// Waits for the next bridge event (the bridge thread produces it)
bool NextEvent(HostNetBridge& bridge, HostNetEvent& ev)
{
    return TestWait::For([&] { return bridge.PollEvent(ev); });
}

/// One-connection TCP echo server on an ephemeral loopback port
class EchoServer
{
public:
    EchoServer()
    {
        _listener = netsock::OpenTcp();
        netsock::Bind(_listener, kLoopback, 0, true);
        netsock::Listen(_listener, 1);
        port = netsock::LocalPort(_listener);
        _thread = std::thread([this] { Run(); });
    }
    ~EchoServer()
    {
        _stop = true;
        _thread.join();
        netsock::Close(_listener);
    }
    uint16_t port = 0;

private:
    void Run()
    {
        netsock::Handle conn = netsock::kInvalid;
        uint8_t buf[4096];
        while (!_stop)
        {
            std::vector<netsock::PollItem> items(1);
            items[0].handle = conn == netsock::kInvalid ? _listener : conn;
            if (netsock::Poll(items, 5) <= 0)
                continue;
            if (conn == netsock::kInvalid)
            {
                NetEndpoint peer;
                conn = netsock::Accept(_listener, peer);
                continue;
            }
            size_t n = 0;
            const netsock::Result r = netsock::Recv(conn, buf, sizeof(buf), n);
            if (r == netsock::Result::Ok && n)
            {
                size_t sent = 0;
                netsock::Send(conn, buf, n, sent);
            }
            else if (r == netsock::Result::Closed)
            {
                netsock::Close(conn);   // echo the close
                conn = netsock::kInvalid;
            }
        }
        netsock::Close(conn);
    }

    netsock::Handle _listener = netsock::kInvalid;
    std::atomic<bool> _stop{false};
    std::thread _thread;
};
}  // namespace

TEST(HostNetBridge_Test, TcpConnectSendReceiveClose)
{
    EchoServer server;
    HostNetBridge bridge;
    bridge.TcpConnect(1, {kLoopback, server.port});

    HostNetEvent ev;
    ASSERT_TRUE(NextEvent(bridge, ev));
    ASSERT_EQ(ev.type, NetEventType::Connected);
    EXPECT_EQ(ev.socket, 1);

    const std::vector<uint8_t> hello = {'h', 'e', 'l', 'l', 'o'};
    bridge.TcpSend(1, hello.data(), static_cast<uint32_t>(hello.size()));
    std::vector<uint8_t> echoed;
    while (echoed.size() < hello.size())
    {
        ASSERT_TRUE(NextEvent(bridge, ev));
        ASSERT_EQ(ev.type, NetEventType::Data);
        echoed.insert(echoed.end(), ev.data.begin(), ev.data.end());
    }
    EXPECT_EQ(echoed, hello);

    bridge.TcpShutdownWrite(1);
    ASSERT_TRUE(NextEvent(bridge, ev));
    EXPECT_EQ(ev.type, NetEventType::PeerClosed);
}

TEST(HostNetBridge_Test, RefusedConnect)
{
    // A port nobody listens on: bind one, close it, connect to it
    netsock::Handle h = netsock::OpenTcp();
    netsock::Bind(h, kLoopback, 0, false);
    const uint16_t port = netsock::LocalPort(h);
    netsock::Close(h);

    HostNetBridge bridge;
    bridge.TcpConnect(7, {kLoopback, port});
    HostNetEvent ev;
    ASSERT_TRUE(NextEvent(bridge, ev));
    EXPECT_EQ(ev.type, NetEventType::ConnectFailed);
    EXPECT_EQ(ev.socket, 7);
}

TEST(HostNetBridge_Test, UdpDatagramRoundTrip)
{
    netsock::Handle server = netsock::OpenUdp();
    ASSERT_EQ(netsock::Bind(server, kLoopback, 0, false), netsock::Result::Ok);
    const uint16_t port = netsock::LocalPort(server);

    HostNetBridge bridge;
    const std::vector<uint8_t> ping = {1, 2, 3};
    bridge.UdpSend(3, {kLoopback, port}, ping.data(), static_cast<uint32_t>(ping.size()));

    uint8_t buf[64];
    size_t n = 0;
    NetEndpoint from;
    ASSERT_TRUE(TestWait::For([&] { return netsock::RecvFrom(server, buf, sizeof(buf), n, from) == netsock::Result::Ok; }));
    ASSERT_EQ(n, 3u);
    netsock::SendTo(server, from, buf, n);

    HostNetEvent ev;
    ASSERT_TRUE(NextEvent(bridge, ev));
    EXPECT_EQ(ev.type, NetEventType::Datagram);
    EXPECT_EQ(ev.socket, 3);
    EXPECT_EQ(ev.peer.port, port);
    EXPECT_EQ(ev.data, ping);
    netsock::Close(server);
}

TEST(HostNetBridge_Test, ListenAcceptsALoopbackClient)
{
    HostNetBridge bridge;
    bridge.TcpListen(9, 0);   // any free port
    ASSERT_TRUE(TestWait::For([&] { return bridge.ListenerHostPort(9) != 0; }));

    netsock::Handle client = netsock::OpenTcp();
    netsock::Connect(client, {kLoopback, bridge.ListenerHostPort(9)});

    HostNetEvent ev;
    ASSERT_TRUE(NextEvent(bridge, ev));
    ASSERT_EQ(ev.type, NetEventType::Accepted);
    EXPECT_EQ(ev.socket, 9);
    ASSERT_EQ(ev.data.size(), 2u);
    const uint16_t id = static_cast<uint16_t>(ev.data[0] | (ev.data[1] << 8));
    EXPECT_GE(id, IHostNet::kFirstAcceptedId);
    netsock::Close(client);
}

TEST(HostNetBridge_Test, DnsQueryIsAnsweredFromTheResolver)
{
    HostNetBridge bridge;
    bridge.SetResolver([](const std::string& name, std::vector<uint32_t>& out) {
        if (name != "next.zxart.ee")
            return false;
        out = {NetIp(94, 130, 1, 2)};
        return true;
    });
    const std::vector<uint8_t> query = {0x11, 0x22, 0x01, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0, 4, 'n', 'e', 'x', 't',
                                        5, 'z', 'x', 'a', 'r', 't', 2, 'e', 'e', 0, 0, 1, 0, 1};
    const NetEndpoint server{NetIp(8, 8, 4, 4), 53};
    bridge.DnsQuery(5, server, query.data(), static_cast<uint32_t>(query.size()));

    HostNetEvent ev;
    ASSERT_TRUE(NextEvent(bridge, ev));
    EXPECT_EQ(ev.type, NetEventType::Datagram);
    EXPECT_EQ(ev.peer, server);
    ASSERT_EQ(ev.data.size(), query.size() + 16);
    EXPECT_EQ(ev.data[ev.data.size() - 4], 94);
}

TEST(HostNetBridge_Test, PingOfTheLoopbackThroughTheHost)
{
    const std::vector<uint8_t> echo = {8, 0, 0, 0, 0x12, 0x34, 0, 1, 'p', 'i', 'n', 'g'};
    std::vector<uint8_t> reply;
    if (!netsock::Ping({kLoopback, 0}, echo.data(), echo.size(), 1000, reply))
        GTEST_SKIP() << "this host does not allow unprivileged ICMP (Linux: net.ipv4.ping_group_range)";
    ASSERT_EQ(reply.size(), echo.size());
    EXPECT_EQ(reply[0], 0) << "echo reply";
    EXPECT_EQ(reply[4], 0x12) << "the request's identifier, whatever the host used on the wire";
    EXPECT_EQ(reply[5], 0x34);
    EXPECT_EQ(reply[7], 1);
    EXPECT_EQ(reply[8], 'p');

    HostNetBridge bridge;
    bridge.IcmpEcho(4, {kLoopback, 0}, echo.data(), static_cast<uint32_t>(echo.size()));
    HostNetEvent ev;
    ASSERT_TRUE(NextEvent(bridge, ev));
    EXPECT_EQ(ev.type, NetEventType::EchoReply);
    EXPECT_EQ(ev.socket, 4);
    EXPECT_EQ(ev.data, reply);
}
