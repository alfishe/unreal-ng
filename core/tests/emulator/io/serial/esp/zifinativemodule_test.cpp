// The "new ZiFi" native firmware (zifinativemodule.h): the frame layer, the commands per group, the two real
// firmwares' differences (S3: the network on its own core, redirects, Content-Length, the proxy; ESP01S: one
// loop) and the TTD state. Sources: ZiFi-ESP32-S3-Zero 2e5ba83 and ZiFi-ESP-01S-Native-C-Project 90834e4
// (docs/PROTOCOL.md, src/main.cpp, src/net_client.cpp). Virtual network with a FakeHostNet, a hand clock.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fakehostnet.h"
#include "common/network/dnsmessage.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/serial/esp/zifinativemodule.h"

class ZiFiNativeModule_Test : public ::testing::Test
{
protected:
    using Variant = ZiFiNativeModule::Variant;
    using Bytes = std::vector<uint8_t>;

    struct Reply
    {
        uint8_t cmd = 0;
        Bytes data;
        std::string Text() const { return std::string(data.begin(), data.end()); }
    };

    void SetUp() override { Make(Variant::S3); }

    void Make(Variant variant)
    {
        _esp.reset();
        _net.reset();
        auto host = std::make_unique<FakeHostNet>();
        _host = host.get();
        _net = std::make_unique<VirtualNetwork>(nullptr, std::move(host), VirtualNetworkConfig());
        _esp = std::make_unique<ZiFiNativeModule>(_net.get(), variant);
        _esp->SetClock([this]() { return _now; }, 3500000);
        _esp->OnLineSettings(SerialLine());
        _pending.clear();
    }

    void Raw(const Bytes& bytes)
    {
        for (uint8_t b : bytes)
            _esp->Transmit(b);
    }
    void Send(uint8_t cmd, const Bytes& data = {}) { Raw(ZiFiNativeModule::Frame(cmd, data)); }
    void Send(uint8_t cmd, const std::string& text) { Send(cmd, Bytes(text.begin(), text.end())); }

    /// Let `us` pass in frames; the frames the module sent by then
    std::vector<Reply> Read(uint64_t us = 5000)
    {
        const uint64_t step = 20000;
        for (uint64_t t = 0; t <= us; t += step)
        {
            _now += std::min(step, us - std::min(us, t)) * 35 / 10 + 1;
            _esp->OnFrame();
            _net->Pump();
            while (_esp->HasByte())
                _pending.push_back(_esp->TakeByte());
        }
        std::vector<Reply> out;
        while (_pending.size() >= 5)
        {
            EXPECT_EQ(_pending[0], ZiFiNativeModule::kSync);
            const size_t length = static_cast<size_t>(_pending[2] | (_pending[3] << 8));
            if (_pending.size() < 5 + length)
                break;
            uint8_t sum = 0;
            for (size_t i = 1; i < 4 + length; ++i)
                sum ^= _pending[i];
            EXPECT_EQ(sum, _pending[4 + length]) << "the module's checksum";
            out.push_back({_pending[1], Bytes(_pending.begin() + 4, _pending.begin() + 4 + static_cast<std::ptrdiff_t>(length))});
            _pending.erase(_pending.begin(), _pending.begin() + 5 + static_cast<std::ptrdiff_t>(length));
        }
        return out;
    }

    /// The commands of the frames, e.g. "FE EE 90"
    static std::string Cmds(const std::vector<Reply>& r)
    {
        std::string s;
        for (const Reply& x : r)
        {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02X", x.cmd);
            s += (s.empty() ? "" : " ") + std::string(buf);
        }
        return s;
    }

    void AnswerDns(uint32_t addr)
    {
        const FakeHostNet::Command q = *_host->Last("dns");
        dns::Question question;
        ASSERT_TRUE(dns::ParseQuery(q.data.data(), q.data.size(), question));
        _host->Push(NetEventType::Datagram, q.socket, NetEventStatus::Ok, q.endpoint,
                    dns::BuildAnswer(q.data.data(), q.data.size(), question,
                                     addr ? std::vector<uint32_t>{addr} : std::vector<uint32_t>{},
                                     addr ? dns::kRcodeNoError : dns::kRcodeNxDomain));
        _net->Pump();
    }

    /// host\0 + port LE16 (+ path\0 for HTTP GET)
    static Bytes HostPort(const std::string& host, uint16_t port, const std::string& path = "", bool withPath = false)
    {
        Bytes b(host.begin(), host.end());
        b.push_back(0);
        b.push_back(static_cast<uint8_t>(port));
        b.push_back(static_cast<uint8_t>(port >> 8));
        if (withPath)
        {
            b.insert(b.end(), path.begin(), path.end());
            b.push_back(0);
        }
        return b;
    }

    /// NET_OPEN to an address, connected; returns the host socket
    uint16_t Open()
    {
        Send(ZiFiNativeModule::kNetOpen, HostPort("93.184.216.34", 23));
        Read();
        const uint16_t socket = _host->Last("connect")->socket;
        _host->Push(NetEventType::Connected, socket);
        _net->Pump();
        const std::vector<Reply> r = Read();
        EXPECT_EQ(Cmds(r), "90");
        return socket;
    }

    std::string LastSent() const
    {
        const FakeHostNet::Command* c = _host->Last("send");
        return c ? std::string(c->data.begin(), c->data.end()) : std::string();
    }

    uint64_t _now = 1000;
    FakeHostNet* _host = nullptr;
    std::unique_ptr<VirtualNetwork> _net;
    std::unique_ptr<ZiFiNativeModule> _esp;
    Bytes _pending;
};

// --- Frame layer -------------------------------------------------------------------------------------------------

TEST_F(ZiFiNativeModule_Test, Frames_EchoPingAndASilentPowerUp)
{
    EXPECT_TRUE(Read(500000).empty()) << "no boot text on the protocol line";
    Send(ZiFiNativeModule::kEcho, std::string("hi\0\r\n", 5));
    std::vector<Reply> r = Read();
    ASSERT_EQ(Cmds(r), "00");
    EXPECT_EQ(r[0].data, Bytes({'h', 'i', 0, '\r', '\n'})) << "payload as it was, NUL and CR LF included";
    Send(ZiFiNativeModule::kPing);
    EXPECT_EQ(Cmds(Read()), "F0");
    EXPECT_EQ(ZiFiNativeModule::Frame(0x04, {}), Bytes({0x5A, 0x04, 0x00, 0x00, 0x04})) << "the sync is not summed";
}

TEST_F(ZiFiNativeModule_Test, Frames_TheParserDropsWhatIsBroken)
{
    Raw({0x00, 0xFF, 0x13});                          // noise before the sync
    Bytes bad = ZiFiNativeModule::Frame(ZiFiNativeModule::kEcho, {'x'});
    bad.back() ^= 0x01;
    Raw(bad);                                         // a wrong checksum: dropped silently
    Raw({0x5A, 0x00, 0x01, 0x08});                    // length #801 > 1024: resync after LEN_H
    EXPECT_TRUE(Read().empty());
    EXPECT_EQ(_esp->BadChecksums(), 1u);
    EXPECT_EQ(_esp->Resyncs(), 1u);
    Raw({0x5A, 0x00, 0x02});                          // a frame left half-sent for 500 ms is dropped
    Read(600000);
    EXPECT_EQ(_esp->Resyncs(), 2u);
    Send(ZiFiNativeModule::kPing);
    EXPECT_EQ(Cmds(Read()), "F0") << "the line works again";
}

TEST_F(ZiFiNativeModule_Test, Frames_GetStepAndUnknownCommands)
{
    Send(0x30);
    std::vector<Reply> r = Read();
    ASSERT_EQ(Cmds(r), "EE");
    EXPECT_EQ(r[0].Text(), "unsupported:30");
    Send(ZiFiNativeModule::kGetStep);
    r = Read();
    ASSERT_EQ(Cmds(r), "85");
    EXPECT_EQ(r[0].data[0], 0x30) << "the last command";
    EXPECT_EQ(r[0].Text().substr(1), "unsupported:30");

    Make(Variant::Esp01s);
    Send(0x24);   // WEATHER_GET: the S3 firmware's only
    r = Read();
    ASSERT_EQ(Cmds(r), "EE");
    EXPECT_EQ(r[0].Text(), "unknown cmd 24");
}

// --- System ------------------------------------------------------------------------------------------------------

TEST_F(ZiFiNativeModule_Test, System_SysInfoNamesTheBuild)
{
    Send(ZiFiNativeModule::kSysInfo);
    std::vector<Reply> r = Read();
    ASSERT_EQ(Cmds(r), "FE 82");
    EXPECT_NE(r[1].Text().find("FW:s3-native-0.6.94"), std::string::npos) << r[1].Text();
    EXPECT_NE(r[1].Text().find("PROXY:OFF"), std::string::npos);
    EXPECT_EQ(_esp->LastStep(), 8) << "native plugins take step 8 as SYS_INFO received";

    Make(Variant::Esp01s);
    Send(ZiFiNativeModule::kSysInfo);
    r = Read();
    ASSERT_EQ(Cmds(r), "FE 82");
    EXPECT_NE(r[1].Text().find("Flash:1048576B"), std::string::npos) << r[1].Text();
    EXPECT_NE(r[1].Text().find("FW:native-0.2.2"), std::string::npos);
}

TEST_F(ZiFiNativeModule_Test, System_ResetRestartsAndRejoins)
{
    const uint16_t socket = Open();
    (void)socket;
    Send(ZiFiNativeModule::kSysReset);
    EXPECT_EQ(Cmds(Read()), "FE") << "the ACK leaves before the restart";
    EXPECT_FALSE(_esp->ClientOpen()) << "the links go";
    Send(ZiFiNativeModule::kPing);
    EXPECT_TRUE(Read(100000).empty()) << "restarting: the UART takes nothing";
    Read(400000);
    Send(ZiFiNativeModule::kPing);
    EXPECT_EQ(Cmds(Read()), "F0");
    Read(1600000);
    EXPECT_EQ(_esp->GetWifi(), EspModule::Wifi::GotIp) << "the saved zifi.ini joins again";
    Send(ZiFiNativeModule::kSysInfo);
    EXPECT_NE(Read()[1].Text().find("RST:3"), std::string::npos) << "ESP_RST_SW";
}

// --- Wi-Fi -------------------------------------------------------------------------------------------------------

TEST_F(ZiFiNativeModule_Test, WiFi_IniJoinsAndSetsTheZone)
{
    Send(ZiFiNativeModule::kWifiIni, std::string("; zifi.ini\r\nSSID: UnrealNG\r\npassword: \"secret\"\r\ntime: +3 ; MSK\r\n"));
    EXPECT_EQ(Cmds(Read()), "FE") << "a new password: the module rejoins";
    std::vector<Reply> r = Read(1600000);
    ASSERT_EQ(Cmds(r), "83");
    EXPECT_EQ(r[0].data, Bytes({1, 10, 0, 2, 15})) << "status 1, the DHCP address";
    EXPECT_EQ(_esp->TimeZone(), 3);
    Send(ZiFiNativeModule::kWifiConnect, Bytes({'U', 'n', 'r', 'e', 'a', 'l', 'N', 'G', 0, 's', 'e', 'c', 'r', 'e', 't'}));
    EXPECT_EQ(Cmds(Read()), "FE 81") << "the same network and password: no rejoin";

    Send(ZiFiNativeModule::kWifiIni, std::string("password: x\r\n"));
    r = Read();
    ASSERT_EQ(Cmds(r), "FE EE 83");
    EXPECT_EQ(r[1].Text(), "ini:no ssid");
    EXPECT_EQ(r[2].data, Bytes(5, 0));
}

TEST_F(ZiFiNativeModule_Test, WiFi_AnotherNetworkTimesOutAfterTenSeconds)
{
    Send(ZiFiNativeModule::kWifiConnect, Bytes({'H', 'o', 'm', 'e', 0, 'p', 'w'}));
    EXPECT_EQ(Cmds(Read(9900000)), "FE");
    std::vector<Reply> r = Read(200000);
    ASSERT_EQ(Cmds(r), "EE 81");
    EXPECT_EQ(r[0].Text(), "wifi timeout");
    Send(ZiFiNativeModule::kNetIpConfig);
    r = Read();
    ASSERT_EQ(Cmds(r), "FE A0");
    EXPECT_EQ(r[1].data, Bytes(16, 0)) << "no address without Wi-Fi";
}

// --- TCP client --------------------------------------------------------------------------------------------------

TEST_F(ZiFiNativeModule_Test, Tcp_OpenSendReceiveClose)
{
    Send(ZiFiNativeModule::kNetOpen, HostPort("bbs.example.org", 23));
    EXPECT_EQ(Cmds(Read()), "FE") << "ACK at once, the result when connected";
    AnswerDns(NetIp(93, 184, 216, 34));
    Read();
    const FakeHostNet::Command connect = *_host->Last("connect");
    EXPECT_EQ(connect.endpoint.port, 23);
    _host->Push(NetEventType::Connected, connect.socket);
    _net->Pump();
    std::vector<Reply> r = Read();
    ASSERT_EQ(Cmds(r), "90");
    EXPECT_EQ(r[0].data, Bytes({1}));

    Send(ZiFiNativeModule::kNetSend, std::string("hello"));
    EXPECT_EQ(Cmds(Read()), "FE 91");
    EXPECT_EQ(LastSent(), "hello");

    Send(ZiFiNativeModule::kNetRecv, Bytes({0x00, 0x04}));
    r = Read();
    ASSERT_EQ(Cmds(r), "92") << "NET_RECV answers without an ACK";
    EXPECT_EQ(r[0].data, Bytes({0})) << "nothing yet on a live socket";
    _host->Push(NetEventType::Data, connect.socket, NetEventStatus::Ok, {}, {'a', 'b', 'c'});
    _host->Push(NetEventType::PeerClosed, connect.socket);
    _net->Pump();
    Send(ZiFiNativeModule::kNetRecv, Bytes({0x02, 0x00}));
    r = Read();
    EXPECT_EQ(r[0].data, Bytes({0, 'a', 'b'})) << "at most what was asked";
    Send(ZiFiNativeModule::kNetRecv, Bytes({0x00, 0x04}));
    EXPECT_EQ(Read()[0].data, Bytes({0, 'c'}));
    Send(ZiFiNativeModule::kNetRecv, Bytes({0x00, 0x04}));
    EXPECT_EQ(Read()[0].data, Bytes({1})) << "EOF only after the last byte";
    Send(ZiFiNativeModule::kNetSend, std::string("x"));
    r = Read();
    ASSERT_EQ(Cmds(r), "FE EE 91");
    EXPECT_EQ(r[1].Text(), "send:not open");
    Send(ZiFiNativeModule::kNetClose);
    EXPECT_EQ(Read()[1].data, Bytes({1}));
}

TEST_F(ZiFiNativeModule_Test, Tcp_OpenFailsWithItsReason)
{
    Send(ZiFiNativeModule::kNetOpen, Bytes({0}));
    std::vector<Reply> r = Read();
    ASSERT_EQ(Cmds(r), "FE EE 90");
    EXPECT_EQ(r[1].Text(), "open:no host");
    Send(ZiFiNativeModule::kNetOpen, HostPort("nowhere.test", 80));
    Read();
    AnswerDns(0);
    r = Read();
    ASSERT_EQ(Cmds(r), "EE 90");
    EXPECT_EQ(r[0].Text(), "open:connect failed");
    EXPECT_EQ(r[1].data, Bytes({0}));
}

TEST_F(ZiFiNativeModule_Test, Tcp_PingIsATcpProbeToPort80)
{
    Send(ZiFiNativeModule::kNetPing, std::string("93.184.216.34"));
    Read(10000);
    const FakeHostNet::Command connect = *_host->Last("connect");
    EXPECT_EQ(connect.endpoint.port, 80) << "not ICMP: a connect to port 80";
    Read(30000);
    _host->Push(NetEventType::Connected, connect.socket);
    _net->Pump();
    std::vector<Reply> r = Read();
    ASSERT_EQ(Cmds(r), "A1");
    ASSERT_EQ(r[0].data.size(), 3u);
    EXPECT_EQ(r[0].data[0], 1);
    const unsigned ms = r[0].data[1] | (r[0].data[2] << 8);
    EXPECT_GE(ms, 30u);
    EXPECT_LE(ms, 70u);

    Send(ZiFiNativeModule::kNetPing, std::string("93.184.216.35"));
    r = Read(3100000);
    ASSERT_EQ(Cmds(r), "FE A1") << "3 s without an answer: a negative probe, no error report";
    EXPECT_EQ(r[1].data, Bytes({0, 0, 0}));
}

// --- HTTP --------------------------------------------------------------------------------------------------------

TEST_F(ZiFiNativeModule_Test, Http_GetParsesTheHeaderAndEndsTheBodyAtContentLength)
{
    Send(ZiFiNativeModule::kNetHttpGet, HostPort("93.184.216.34", 8080, "/files/a.txt", true));
    Read();
    const FakeHostNet::Command connect = *_host->Last("connect");
    _host->Push(NetEventType::Connected, connect.socket);
    _net->Pump();
    Read();
    EXPECT_EQ(LastSent(), "GET /files/a.txt HTTP/1.0\r\nHost: 93.184.216.34:8080\r\nUser-Agent: ZiFi (ZX Evo)\r\n"
                          "Accept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n");
    const std::string response = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nhello";
    _host->Push(NetEventType::Data, connect.socket, NetEventStatus::Ok, {}, Bytes(response.begin(), response.end()));
    _net->Pump();
    std::vector<Reply> r = Read();
    ASSERT_EQ(Cmds(r), "94");
    EXPECT_EQ(r[0].data, Bytes({1, 200, 0, 5, 0, 0, 0})) << "status, code LE16, length LE32";
    Send(ZiFiNativeModule::kNetRecv, Bytes({0xFF, 0x03}));
    EXPECT_EQ(Read()[0].data, Bytes({0, 'h', 'e', 'l', 'l', 'o'}));
    Send(ZiFiNativeModule::kNetRecv, Bytes({0xFF, 0x03}));
    EXPECT_EQ(Read()[0].data, Bytes({1})) << "S3: EOF at Content-Length, no FIN needed";
}

TEST_F(ZiFiNativeModule_Test, Http_RedirectsChunkedAndHttpsOnS3)
{
    Send(ZiFiNativeModule::kNetHttpGet, HostPort("93.184.216.34", 80, "/old", true));
    Read();
    FakeHostNet::Command connect = *_host->Last("connect");
    _host->Push(NetEventType::Connected, connect.socket);
    _net->Pump();
    Read();
    const std::string moved = "HTTP/1.1 302 Found\r\nLocation: /new?x=1\r\nContent-Length: 0\r\n\r\n";
    _host->Push(NetEventType::Data, connect.socket, NetEventStatus::Ok, {}, Bytes(moved.begin(), moved.end()));
    _net->Pump();
    EXPECT_EQ(Cmds(Read()), "") << "the redirect is followed, no answer yet";
    connect = *_host->Last("connect");
    _host->Push(NetEventType::Connected, connect.socket);
    _net->Pump();
    Read();
    EXPECT_EQ(LastSent().rfind("GET /new?x=1 HTTP/1.0\r\nHost: 93.184.216.34\r\n", 0), 0u) << LastSent();
    const std::string chunked = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    _host->Push(NetEventType::Data, connect.socket, NetEventStatus::Ok, {}, Bytes(chunked.begin(), chunked.end()));
    _net->Pump();
    std::vector<Reply> r = Read();
    ASSERT_EQ(Cmds(r), "EE 94");
    EXPECT_EQ(r[0].Text(), "get:chunked unsupported");

    Send(ZiFiNativeModule::kNetHttpGet, HostPort("93.184.216.34", 443, "/", true));
    r = Read();
    ASSERT_EQ(Cmds(r), "FE EE 94");
    EXPECT_EQ(r[1].Text(), "get:tls connect failed") << "HTTPS: no TLS in the virtual network";
}

TEST_F(ZiFiNativeModule_Test, Http_Esp01sFollowsNothingAndSendsItsOwnRequest)
{
    Make(Variant::Esp01s);
    Send(ZiFiNativeModule::kNetHttpGet, HostPort("93.184.216.34", 8080, "", true));
    Read();
    const FakeHostNet::Command connect = *_host->Last("connect");
    _host->Push(NetEventType::Connected, connect.socket);
    _net->Pump();
    Read();
    EXPECT_EQ(LastSent(), "GET / HTTP/1.0\r\nHost: 93.184.216.34\r\nUser-Agent: ZiFi (ZX Evo)\r\nAccept: */*\r\n"
                          "Connection: close\r\n\r\n") << "no port in Host, no Accept-Encoding";
    const std::string moved = "HTTP/1.1 301 Moved\r\nLocation: /x\r\n\r\nbody";
    _host->Push(NetEventType::Data, connect.socket, NetEventStatus::Ok, {}, Bytes(moved.begin(), moved.end()));
    _net->Pump();
    std::vector<Reply> r = Read();
    ASSERT_EQ(Cmds(r), "94");
    EXPECT_EQ(r[0].data, Bytes({1, 0x2D, 0x01, 0, 0, 0, 0})) << "301 handed to the Z80";
    Send(ZiFiNativeModule::kNetRecv, Bytes({0x10, 0x00}));
    EXPECT_EQ(Read()[0].data, Bytes({0, 'b', 'o', 'd', 'y'}));
}

// --- The two cores against one loop -------------------------------------------------------------------------------

TEST_F(ZiFiNativeModule_Test, Concurrency_S3AnswersWhileTheNetworkWorks)
{
    Send(ZiFiNativeModule::kNetOpen, HostPort("bbs.example.org", 23));   // waits for DNS
    EXPECT_EQ(Cmds(Read()), "FE");
    EXPECT_TRUE(_esp->Busy());
    Send(ZiFiNativeModule::kPing);
    EXPECT_EQ(Cmds(Read()), "F0") << "core 1 still answers";
    Send(ZiFiNativeModule::kNetPing, std::string("10.0.2.2"));
    std::vector<Reply> r = Read();
    ASSERT_EQ(Cmds(r), "FE EE A1");
    EXPECT_EQ(r[1].Text(), "network busy");
    EXPECT_EQ(r[2].data, Bytes({0, 0, 0}));
    Send(ZiFiNativeModule::kNetRecv);
    r = Read();
    ASSERT_EQ(Cmds(r), "EE 92");
    EXPECT_EQ(r[1].data, Bytes({1}));
}

TEST_F(ZiFiNativeModule_Test, Concurrency_Esp01sHoldsTheNextCommand)
{
    Make(Variant::Esp01s);
    Send(ZiFiNativeModule::kNetOpen, HostPort("bbs.example.org", 23));
    EXPECT_EQ(Cmds(Read()), "FE");
    Send(ZiFiNativeModule::kPing);
    EXPECT_TRUE(Read(600000).empty()) << "the loop is inside NET_OPEN; the PING waits in the UART";
    AnswerDns(0);
    std::vector<Reply> r = Read();
    EXPECT_EQ(Cmds(r), "EE 90 F0") << "then the PING, not dropped as a stale frame";
}

// --- NTP, IP, proxy -----------------------------------------------------------------------------------------------

TEST_F(ZiFiNativeModule_Test, Ntp_FourteenDigitsInTheIniZone)
{
    Send(ZiFiNativeModule::kWifiIni, std::string("ssid: UnrealNG\r\ntime: -5\r\n"));
    Read(1600000);
    Send(ZiFiNativeModule::kNetNtp);
    Read();
    AnswerDns(NetIp(203, 0, 113, 5));
    Read();
    const FakeHostNet::Command* ntp = _host->Last("udp");
    ASSERT_NE(ntp, nullptr);
    EXPECT_EQ(ntp->endpoint.port, 123);
    Bytes reply(48, 0);
    reply[0] = 0x1C;   // LI 0, version 3, server
    reply[1] = 2;      // stratum
    const uint32_t t = 1470321485u + 2208988800u;   // 2016-08-04 14:38:05 UTC
    reply[40] = static_cast<uint8_t>(t >> 24);
    reply[41] = static_cast<uint8_t>(t >> 16);
    reply[42] = static_cast<uint8_t>(t >> 8);
    reply[43] = static_cast<uint8_t>(t);
    _host->Push(NetEventType::Datagram, ntp->socket, NetEventStatus::Ok, {NetIp(203, 0, 113, 5), 123}, reply);
    _net->Pump();
    const std::vector<Reply> r = Read();
    ASSERT_EQ(Cmds(r), "A2");
    EXPECT_EQ(r[0].Text(), "20160804093805") << "UTC-5";
}

TEST_F(ZiFiNativeModule_Test, Ip_ConfigAndProxyStatus)
{
    Send(ZiFiNativeModule::kNetIpConfig);
    std::vector<Reply> r = Read();
    ASSERT_EQ(Cmds(r), "FE A0");
    EXPECT_EQ(r[1].data, Bytes({10, 0, 2, 15, 255, 255, 255, 0, 10, 0, 2, 2, 10, 0, 2, 3}));
    Send(ZiFiNativeModule::kNetProxyStatus);
    EXPECT_EQ(Read()[1].data, Bytes({0})) << "no proxy in zifi.ini";

    // A proxy in zifi.ini is probed when the Wi-Fi comes up; then GET goes through it
    Send(ZiFiNativeModule::kWifiIni, std::string("ssid: UnrealNG\r\nproxy_ip: 203.0.113.7\r\nproxy_port: 3128\r\n"));
    Read(1600000);
    const FakeHostNet::Command probe = *_host->Last("connect");
    EXPECT_EQ(probe.endpoint.port, 3128);
    _host->Push(NetEventType::Connected, probe.socket);
    _net->Pump();
    EXPECT_EQ(Cmds(Read()), "83");
    Send(ZiFiNativeModule::kNetProxyStatus);
    r = Read();
    EXPECT_EQ(r[1].Text(), std::string("\x01") + "203.0.113.7:3128");
    Send(ZiFiNativeModule::kNetHttpGet, HostPort("example.test", 80, "/p", true));
    Read();
    const FakeHostNet::Command connect = *_host->Last("connect");
    EXPECT_EQ(connect.endpoint.port, 3128) << "through the proxy";
    _host->Push(NetEventType::Connected, connect.socket);
    _net->Pump();
    Read();
    EXPECT_EQ(LastSent().rfind("GET http://example.test/p HTTP/1.0\r\nHost: example.test\r\nProxy-Authorization: Basic eng6eng=\r\n", 0),
              0u)
        << LastSent();
}

// --- What the emulation leaves out --------------------------------------------------------------------------------

TEST_F(ZiFiNativeModule_Test, Services_AnswerAsWhenTheyCannotStart)
{
    Send(ZiFiNativeModule::kFtpStart);
    std::vector<Reply> r = Read();
    ASSERT_EQ(Cmds(r), "FE EE 86");
    EXPECT_EQ(r[1].Text(), "ftp:not emulated");
    EXPECT_EQ(r[2].data, Bytes(3, 0));
    Send(ZiFiNativeModule::kFtpStop);
    EXPECT_EQ(Read()[1].data, Bytes({1}));
    Send(ZiFiNativeModule::kWeatherGet);
    r = Read();
    ASSERT_EQ(Cmds(r), "FE EE A4");
    EXPECT_EQ(r[2].data, Bytes({0, 1})) << "status 0, record version 1";
}

// --- TTD ----------------------------------------------------------------------------------------------------------

TEST_F(ZiFiNativeModule_Test, Ttd_ACommandWaitingForTheNetworkSurvivesACheckpoint)
{
    Send(ZiFiNativeModule::kWifiIni, std::string("ssid: UnrealNG\r\ntime: 2\r\n"));
    Read(1600000);
    Send(ZiFiNativeModule::kNetOpen, HostPort("bbs.example.org", 23));
    EXPECT_EQ(Cmds(Read()), "FE");
    auto state = std::make_unique<netstate::EspModuleState>();
    _esp->SaveState(*state);

    auto copy = std::make_unique<ZiFiNativeModule>(_net.get(), Variant::S3);
    copy->SetClock([this]() { return _now; }, 3500000);
    copy->LoadState(*state, nullptr);
    EXPECT_TRUE(copy->Busy());
    EXPECT_EQ(copy->TimeZone(), 2);
    EXPECT_EQ(copy->LastStep(), ZiFiNativeModule::kNetOpen);
    auto again = std::make_unique<netstate::EspModuleState>();
    copy->SaveState(*again);
    EXPECT_EQ(std::memcmp(state->firmware, again->firmware, sizeof(state->firmware)), 0) << "the firmware part round-trips";
    EXPECT_EQ(state->rxLength, again->rxLength) << "the held request stays in the receive buffer";
    EXPECT_GT(state->rxLength, 0u);
}
