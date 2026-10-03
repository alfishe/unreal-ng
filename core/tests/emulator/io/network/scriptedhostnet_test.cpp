// The scripted host server helper (core/tests/_helpers/scriptedhostnet.h; network tdd §15 phase SN0) driven through
// the virtual network, as every adapter test uses it: HTTP, DNS, ping, NTP, echo, refused connects, host clients

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "_helpers/scriptedhostnet.h"
#include "common/network/dnsmessage.h"
#include "emulator/io/network/virtualnetwork.h"

namespace
{
struct Event
{
    NetEventType type;
    NetEventStatus status;
    NetEndpoint peer;
    std::vector<uint8_t> data;
};

class Guest : public INetGuest
{
public:
    void OnNetEvent(uint32_t, NetEventType type, NetEventStatus status, const NetEndpoint& peer, const uint8_t* data,
                    uint32_t length, uint32_t) override
    {
        events.push_back({type, status, peer, std::vector<uint8_t>(data, data + length)});
    }
    std::string Text() const
    {
        std::string out;
        for (const Event& e : events)
        {
            if (e.type == NetEventType::Data)
                out.append(e.data.begin(), e.data.end());
        }
        return out;
    }
    std::vector<Event> events;
};

constexpr uint32_t kServer = NetIp(192, 0, 2, 10);
}  // namespace

class ScriptedHostNet_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        auto host = std::make_unique<ScriptedHostNet>();
        _host = host.get();
        _net = std::make_unique<VirtualNetwork>(nullptr, std::move(host), VirtualNetworkConfig());
    }
    ScriptedHostNet* _host = nullptr;
    std::unique_ptr<VirtualNetwork> _net;
    Guest _guest;
};

TEST_F(ScriptedHostNet_Test, HttpGetAnswersTheFileAndCloses)
{
    std::vector<uint8_t> body(4000);
    for (size_t i = 0; i < body.size(); ++i)
        body[i] = static_cast<uint8_t>(i * 7);
    _host->AddHttp({kServer, 80}, {{"/f.bin", body}});
    const uint16_t s = _net->Open(NetProto::Tcp, &_guest, 1);
    _net->Connect(s, {kServer, 80});
    _net->Pump();
    ASSERT_EQ(_guest.events.size(), 1u);
    EXPECT_EQ(_guest.events[0].type, NetEventType::Connected);
    const std::string get = "GET /f.bin HTTP/1.0\r\nHost: x\r\n\r\n";
    _net->Send(s, reinterpret_cast<const uint8_t*>(get.data()), static_cast<uint32_t>(get.size()));
    _net->Pump();
    const std::string text = _guest.Text();
    const size_t head = text.find("\r\n\r\n");
    ASSERT_NE(head, std::string::npos) << text.substr(0, 200);
    EXPECT_EQ(text.compare(0, 15, "HTTP/1.0 200 OK"), 0);
    EXPECT_NE(text.find("Content-Length: 4000"), std::string::npos);
    EXPECT_EQ(std::vector<uint8_t>(text.begin() + static_cast<std::ptrdiff_t>(head + 4), text.end()), body);
    EXPECT_EQ(_guest.events.back().type, NetEventType::PeerClosed);
}

TEST_F(ScriptedHostNet_Test, RefusedWithoutAServer_404ForAMissingFile)
{
    _host->AddHttp({kServer, 80}, {});
    const uint16_t a = _net->Open(NetProto::Tcp, &_guest, 1);
    _net->Connect(a, {kServer, 81});
    _net->Pump();
    ASSERT_EQ(_guest.events.size(), 1u);
    EXPECT_EQ(_guest.events[0].type, NetEventType::ConnectFailed);
    EXPECT_EQ(_guest.events[0].status, NetEventStatus::Refused);

    Guest other;
    const uint16_t b = _net->Open(NetProto::Tcp, &other, 2);
    _net->Connect(b, {kServer, 80});
    _net->Pump();
    const std::string get = "GET /none HTTP/1.0\r\n\r\n";
    _net->Send(b, reinterpret_cast<const uint8_t*>(get.data()), static_cast<uint32_t>(get.size()));
    _net->Pump();
    EXPECT_EQ(other.Text().compare(0, 22, "HTTP/1.0 404 Not Found"), 0) << other.Text();
}

TEST_F(ScriptedHostNet_Test, DnsPingNtpAndEcho)
{
    _host->AddName("example.test", kServer);
    _host->SetPingable(kServer);
    _host->AddNtp({kServer, 123}, 1767268800);   // 2026-01-01 12:00:00 UTC
    _host->AddUdpEcho({kServer, 7});

    const uint16_t udp = _net->Open(NetProto::Udp, &_guest, 1);
    const std::vector<uint8_t> query = dns::BuildQuery(0x1234, "example.test");
    _net->SendTo(udp, 1024, {NetIp(10, 0, 2, 3), 53}, query.data(), static_cast<uint32_t>(query.size()));
    _net->Pump();
    ASSERT_EQ(_guest.events.size(), 1u);
    std::vector<uint32_t> addresses;
    uint8_t rcode = 0xFF;
    ASSERT_TRUE(dns::ParseAnswer(_guest.events[0].data.data(), _guest.events[0].data.size(), 0x1234, addresses, rcode));
    EXPECT_EQ(rcode, dns::kRcodeNoError);
    ASSERT_EQ(addresses.size(), 1u);
    EXPECT_EQ(addresses[0], kServer);

    std::vector<uint8_t> ntp(48, 0);
    ntp[0] = 0x1B;   // v3, client
    _net->SendTo(udp, 1024, {kServer, 123}, ntp.data(), 48);
    const std::vector<uint8_t> hello = {'h', 'i'};
    _net->SendTo(udp, 1024, {kServer, 7}, hello.data(), 2);
    _net->Pump();
    ASSERT_EQ(_guest.events.size(), 3u);
    EXPECT_EQ(_guest.events[1].data[0] & 7, 4) << "mode 4: server";
    const uint32_t seconds = (static_cast<uint32_t>(_guest.events[1].data[40]) << 24) | (_guest.events[1].data[41] << 16) |
                             (_guest.events[1].data[42] << 8) | _guest.events[1].data[43];
    EXPECT_EQ(seconds, 1767268800u + 2208988800u);
    EXPECT_EQ(_guest.events[2].data, hello);

    Guest pinger;
    const uint16_t icmp = _net->Open(NetProto::Icmp, &pinger, 3);
    const std::vector<uint8_t> echo = {8, 0, 0xF7, 0xFE, 0, 1, 0, 1};
    _net->SendTo(icmp, 0, {kServer, 0}, echo.data(), static_cast<uint32_t>(echo.size()));
    _net->SendTo(icmp, 0, {NetIp(192, 0, 2, 99), 0}, echo.data(), static_cast<uint32_t>(echo.size()));
    _net->Pump();
    ASSERT_EQ(pinger.events.size(), 1u) << "only the pingable address answers";
    EXPECT_EQ(pinger.events[0].type, NetEventType::EchoReply);
    EXPECT_EQ(pinger.events[0].data[0], 0);
}

TEST_F(ScriptedHostNet_Test, HostClientReachesAGuestServer)
{
    VirtualNetworkConfig config;
    config.forwards[80] = 8080;
    auto host = std::make_unique<ScriptedHostNet>();
    ScriptedHostNet* scripted = host.get();
    VirtualNetwork net(nullptr, std::move(host), config);
    const uint16_t s = net.Open(NetProto::Tcp, &_guest, 1);
    net.Listen(s, 80);
    const ScriptedHostNet::Command* listen = scripted->Last("listen");
    ASSERT_NE(listen, nullptr);
    EXPECT_EQ(listen->endpoint.port, 8080);
    scripted->ConnectClient(listen->socket, 0x8001, {NetIp(127, 0, 0, 1), 50000});
    scripted->SendFromClient(0x8001, "hello");
    net.Pump();
    ASSERT_GE(_guest.events.size(), 2u);
    EXPECT_EQ(_guest.events[0].type, NetEventType::Accepted);
    EXPECT_EQ(_guest.Text(), "hello");
}
