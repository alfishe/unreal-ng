// ESP module with Espressif's AT firmware (network TDD step N3): the dialect
// ZX software uses (NedoOS esp-com.c, Moon Rabbit, Karabas net-tools, the
// ZiFi client) and the order it relies on. Virtual network with a
// FakeHostNet, a hand clock.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "_helpers/fakehostnet.h"
#include "common/network/dnsmessage.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/serial/esp/atmodule.h"

class AtModule_Test : public ::testing::Test
{
protected:
    void SetUp() override { Make(EspModule::Chip::Esp8266, {}); }

    void Make(EspModule::Chip chip, const VirtualNetworkConfig& config)
    {
        _esp.reset();
        _net.reset();
        auto host = std::make_unique<FakeHostNet>();
        _host = host.get();
        _net = std::make_unique<VirtualNetwork>(nullptr, std::move(host), config);
        _esp = std::make_unique<AtModule>(_net.get(), chip);
        _esp->SetClock([this]() { return _now; }, 3500000);
        _esp->OnLineSettings(SerialLine());
        Read(300000);   // the power-up banner
    }

    void Type(const std::string& text)
    {
        for (char c : text)
            _esp->Transmit(static_cast<uint8_t>(c));
    }

    void Cmd(const std::string& line) { Type(line + "\r\n"); }

    /// Let `us` pass (the module's timers run at frame boundaries) and take
    /// everything it has sent by then
    std::string Read(uint64_t us = 5000)
    {
        std::string out;
        const uint64_t step = 20000;   // a frame
        for (uint64_t t = 0; t <= us; t += step)
        {
            _now += std::min(step, us - std::min(us, t)) * 35 / 10 + 1;
            _esp->OnFrame();
            _net->Pump();
            while (_esp->HasByte())
                out.push_back(static_cast<char>(_esp->TakeByte()));
        }
        return out;
    }

    /// Echo off, single link: what every client does first
    void Init()
    {
        Cmd("ATE0");
        EXPECT_NE(Read().find("OK"), std::string::npos);
    }

    /// The host resolver answers the pending lookup
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

    /// CIPSTART to a name, connected: returns the host socket id
    uint16_t Connect(const std::string& link = "")
    {
        Cmd("AT+CIPSTART=" + link + "\"TCP\",\"bbs.example.org\",23");
        Read();
        AnswerDns(NetIp(93, 184, 216, 34));
        Read();
        const uint16_t socket = _host->Last("connect")->socket;
        _host->Push(NetEventType::Connected, socket);
        _net->Pump();
        return socket;
    }

    uint64_t _now = 1000;
    FakeHostNet* _host = nullptr;
    std::unique_ptr<VirtualNetwork> _net;
    std::unique_ptr<AtModule> _esp;
};

TEST_F(AtModule_Test, PowerUpBannerThenTheSavedAccessPoint)
{
    Make(EspModule::Chip::Esp8266, {});
    auto fresh = std::make_unique<AtModule>(_net.get(), EspModule::Chip::Esp8266);
    fresh->SetClock([this]() { return _now; }, 3500000);
    fresh->OnLineSettings(SerialLine());
    _now += 400000 * 35 / 10;
    std::string out;
    while (fresh->HasByte())
        out.push_back(static_cast<char>(fresh->TakeByte()));
    EXPECT_EQ(out, "\r\nready\r\nWIFI CONNECTED\r\nWIFI GOT IP\r\n");
}

TEST_F(AtModule_Test, EchoIsOnUntilAte0)
{
    Cmd("AT");
    EXPECT_EQ(Read(), "AT\r\n\r\nOK\r\n") << "the command echoed, then the answer";
    Cmd("ATE0");
    EXPECT_EQ(Read(), "ATE0\r\n\r\nOK\r\n");
    Cmd("AT");
    EXPECT_EQ(Read(), "\r\nOK\r\n");
    Cmd("AT+NOSUCH");
    EXPECT_EQ(Read(), "\r\nERROR\r\n");
}

TEST_F(AtModule_Test, NedoOsRebootSequence)
{
    // esp-com.c espReBoot: AT+RST, wait for WIFI GOT IP, ATE0, CIPCLOSE,
    // CIPDINFO=0, CIPMUX=0, CIPSERVER=0, CIPRECVMODE=0
    Cmd("AT+RST");
    const std::string boot = Read(3000000);
    EXPECT_EQ(boot.rfind("AT+RST\r\n\r\nOK\r\n", 0), 0u) << boot;
    EXPECT_NE(boot.find("\r\nready\r\n"), std::string::npos);
    EXPECT_NE(boot.find("WIFI GOT IP\r\n"), std::string::npos);
    EXPECT_LT(boot.find("ready"), boot.find("WIFI GOT IP"));
    Cmd("ATE0");
    EXPECT_NE(Read().find("OK"), std::string::npos);
    Cmd("AT+CIPCLOSE");
    EXPECT_EQ(Read(), "\r\nERROR\r\n") << "nothing open";
    for (const char* c : {"AT+CIPDINFO=0", "AT+CIPMUX=0", "AT+CIPSERVER=0", "AT+CIPRECVMODE=0"})
    {
        Cmd(c);
        EXPECT_EQ(Read(), "\r\nOK\r\n") << c;
    }
}

TEST_F(AtModule_Test, VersionByChip)
{
    Init();
    Cmd("AT+GMR");
    EXPECT_NE(Read().find("AT version:1.7.4.0"), std::string::npos);
    Make(EspModule::Chip::Esp32, {});
    Init();
    Cmd("AT+GMR");
    EXPECT_NE(Read().find("AT version:2.2.0.0"), std::string::npos);
}

TEST_F(AtModule_Test, TcpSessionInTheOrderClientsNeed)
{
    Init();
    Cmd("AT+CIPSTART=\"TCP\",\"bbs.example.org\",23");
    EXPECT_EQ(Read(), "") << "nothing until the name resolved and the link is up";
    AnswerDns(NetIp(93, 184, 216, 34));
    Read();
    const FakeHostNet::Command connect = *_host->Last("connect");
    EXPECT_EQ(connect.endpoint.addr, NetIp(93, 184, 216, 34));
    EXPECT_EQ(connect.endpoint.port, 23);
    _host->Push(NetEventType::Connected, connect.socket);
    _net->Pump();
    EXPECT_EQ(Read(), "CONNECT\r\n\r\nOK\r\n") << "CONNECT before OK";

    // Moon Rabbit: five digits with leading zeros
    Cmd("AT+CIPSEND=00012");
    EXPECT_EQ(Read(), "\r\nOK\r\n> ") << "OK before the prompt";
    _host->Push(NetEventType::Data, connect.socket, NetEventStatus::Ok, {}, {'h', 'i'});
    _net->Pump();
    EXPECT_EQ(Read(), "") << "no +IPD inside the prompt";
    Type("GET / HTTP\r");
    EXPECT_EQ(Read(), "") << "11 of 12 bytes";
    Type("\n");
    EXPECT_EQ(Read(), "\r\nRecv 12 bytes\r\n\r\nSEND OK\r\n\r\n+IPD,2:hi") << "SEND OK first, then the data";
    EXPECT_EQ(std::string(_host->Last("send")->data.begin(), _host->Last("send")->data.end()), "GET / HTTP\r\n");

    _host->Push(NetEventType::Data, connect.socket, NetEventStatus::Ok, {}, {'b', 'y', 'e'});
    _host->Push(NetEventType::PeerClosed, connect.socket);
    _net->Pump();
    EXPECT_EQ(Read(), "\r\n+IPD,3:byeCLOSED\r\n") << "CLOSED after the last data";
    Cmd("AT+CIPCLOSE");
    EXPECT_EQ(Read(), "\r\nERROR\r\n") << "the link is gone";
}

TEST_F(AtModule_Test, LargeDataComesInSegments)
{
    Init();
    const uint16_t socket = Connect();
    Read();
    _host->Push(NetEventType::Data, socket, NetEventStatus::Ok, {}, std::vector<uint8_t>(3000, 'z'));
    _net->Pump();
    const std::string out = Read();
    EXPECT_NE(out.find("+IPD,1460:"), std::string::npos);
    EXPECT_NE(out.find("+IPD,80:"), std::string::npos);
}

TEST_F(AtModule_Test, FailuresAndBusy)
{
    Init();
    Cmd("AT+CIPSTART=\"TCP\",\"no.such.host\",80");
    Read();
    Cmd("AT");
    EXPECT_EQ(Read(), "busy p...\r\n") << "a command while the module works is dropped";
    AnswerDns(0);
    EXPECT_EQ(Read(), "DNS Fail\r\n\r\nERROR\r\n");

    Cmd("AT+CIPSTART=\"TCP\",\"93.184.216.34\",81");
    Read();
    _host->Push(NetEventType::ConnectFailed, _host->Last("connect")->socket, NetEventStatus::Refused);
    _net->Pump();
    EXPECT_EQ(Read(), "CLOSED\r\n\r\nERROR\r\n");

    Connect();
    Read();
    Cmd("AT+CIPSTART=\"TCP\",\"93.184.216.34\",80");
    EXPECT_EQ(Read(), "ALREADY CONNECTED\r\n\r\nERROR\r\n");
    Cmd("AT+CIPSEND=0");
    EXPECT_EQ(Read(), "\r\nERROR\r\n");
}

TEST_F(AtModule_Test, MultipleLinksAndAServer)
{
    Init();
    Cmd("AT+CIPMUX=1");
    EXPECT_EQ(Read(), "\r\nOK\r\n");
    Cmd("AT+CIPSERVER=1,8080");
    EXPECT_EQ(Read(), "\r\nOK\r\n");
    const FakeHostNet::Command* listen = _host->Last("listen");
    ASSERT_NE(listen, nullptr);
    EXPECT_EQ(listen->endpoint.port, 8080);
    _host->Push(NetEventType::Accepted, listen->socket, NetEventStatus::Ok, {NetIp(127, 0, 0, 1), 50000}, {0x00, 0x80});
    _host->Push(NetEventType::Data, 0x8000, NetEventStatus::Ok, {}, {'G', 'E', 'T'});
    _net->Pump();
    EXPECT_EQ(Read(), "0,CONNECT\r\n\r\n+IPD,0,3:GET");

    Cmd("AT+CIPSEND=0,2");
    EXPECT_EQ(Read(), "\r\nOK\r\n> ");
    Type("ok");
    EXPECT_EQ(Read(), "\r\nRecv 2 bytes\r\n\r\nSEND OK\r\n");
    Cmd("AT+CIPCLOSE=0");
    EXPECT_EQ(Read(), "0,CLOSED\r\n\r\nOK\r\n");

    Cmd("AT+CIPMUX=0");
    EXPECT_EQ(Read(), "link is builded\r\n\r\nERROR\r\n") << "not while the server runs";
}

TEST_F(AtModule_Test, PassiveReceiveMode)
{
    Init();
    Cmd("AT+CIPRECVMODE=1");
    Read();
    const uint16_t socket = Connect();
    Read();
    _host->Push(NetEventType::Data, socket, NetEventStatus::Ok, {}, {'a', 'b', 'c', 'd'});
    _net->Pump();
    EXPECT_EQ(Read(), "+IPD,4\r\n") << "only the length";
    Cmd("AT+CIPRECVDATA=3");
    EXPECT_EQ(Read(), "+CIPRECVDATA,3:abc\r\nOK\r\n");
    Cmd("AT+CIPRECVLEN?");
    EXPECT_EQ(Read(), "+CIPRECVLEN:1,0,0,0,0\r\n\r\nOK\r\n");
}

TEST_F(AtModule_Test, RemoteInfoInIpd)
{
    Init();
    Cmd("AT+CIPDINFO=1");
    Read();
    const uint16_t socket = Connect();
    Read();
    _host->Push(NetEventType::Data, socket, NetEventStatus::Ok, {}, {'x'});
    _net->Pump();
    EXPECT_EQ(Read(), "\r\n+IPD,1,93.184.216.34,23:x");
}

TEST_F(AtModule_Test, TransparentModeAndThePlusEscape)
{
    Init();
    Cmd("AT+CIPMODE=1");
    Read();
    const uint16_t socket = Connect();
    Read();
    Cmd("AT+CIPSEND");
    EXPECT_EQ(Read(), "\r\nOK\r\n\r\n>");
    Type("hello");
    Read(50000);   // a 20 ms pause ends the packet
    ASSERT_NE(_host->Last("send"), nullptr);
    EXPECT_EQ(std::string(_host->Last("send")->data.begin(), _host->Last("send")->data.end()), "hello");
    _host->Push(NetEventType::Data, socket, NetEventStatus::Ok, {}, {'r', 'a', 'w'});
    _net->Pump();
    EXPECT_EQ(Read(), "raw") << "no +IPD in transparent mode";
    Read(1100000);
    Type("+++");
    Read(1100000);
    Cmd("AT");
    EXPECT_EQ(Read(), "\r\nOK\r\n") << "back in command mode";
}

TEST_F(AtModule_Test, SntpThroughTheVirtualNetwork)
{
    Init();
    Cmd("AT+CIPSNTPTIME?");
    EXPECT_NE(Read().find("+CIPSNTPTIME:Thu Jan 01 00:00:00 1970"), std::string::npos) << "1970 until synced";
    Cmd("AT+CIPSNTPCFG=1,3,\"203.0.113.5\"");
    EXPECT_EQ(Read(), "\r\nOK\r\n");
    const FakeHostNet::Command* ntp = _host->Last("udp");
    ASSERT_NE(ntp, nullptr);
    EXPECT_EQ(ntp->endpoint.port, 123);
    ASSERT_EQ(ntp->data.size(), 48u);
    EXPECT_EQ(ntp->data[0], 0x1B);
    std::vector<uint8_t> reply(48, 0);
    reply[0] = 0x1C;
    const uint32_t ntpTime = 1470321485u + 2208988800u;   // Thu Aug 04 2016 14:38:05 UTC
    reply[40] = static_cast<uint8_t>(ntpTime >> 24);
    reply[41] = static_cast<uint8_t>(ntpTime >> 16);
    reply[42] = static_cast<uint8_t>(ntpTime >> 8);
    reply[43] = static_cast<uint8_t>(ntpTime);
    _host->Push(NetEventType::Datagram, ntp->socket, NetEventStatus::Ok, {NetIp(203, 0, 113, 5), 123}, reply);
    _net->Pump();
    Read();
    Cmd("AT+CIPSNTPTIME?");
    EXPECT_EQ(Read(), "+CIPSNTPTIME:Thu Aug 04 17:38:05 2016\r\n\r\nOK\r\n") << "UTC+3";
}

TEST_F(AtModule_Test, UartCurChangesTheLineAndFlowControl)
{
    Init();
    EXPECT_TRUE(_esp->HonorsRts());
    Cmd("AT+UART_CUR=38400,8,1,0,0");
    EXPECT_EQ(Read(), "\r\nOK\r\n") << "the answer at the old rate";
    Read();   // then the module switches
    EXPECT_FALSE(_esp->HonorsRts()) << "flow control off: the module sends regardless of RTS";
    EXPECT_TRUE(_esp->LineMismatch()) << "the ZX still runs at 115200";
    SerialLine line;
    line.baud = 38400;
    _esp->OnLineSettings(line);
    Cmd("AT");
    EXPECT_EQ(Read(), "\r\nOK\r\n");
}

TEST_F(AtModule_Test, JoiningTheVirtualAccessPointOrAnother)
{
    Init();
    Cmd("AT+CWJAP_CUR=\"Elsewhere\",\"secret\"");
    EXPECT_EQ(Read(16000000), "WIFI DISCONNECT\r\n+CWJAP:3\r\n\r\nFAIL\r\n");
    Cmd("AT+CWJAP=\"UnrealNG\",\"\"");
    EXPECT_EQ(Read(2000000), "WIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK\r\n");
    Cmd("AT+CWJAP?");
    EXPECT_NE(Read().find("+CWJAP:\"UnrealNG\""), std::string::npos);
    Cmd("AT+CWLAP");
    EXPECT_NE(Read(2000000).find("+CWLAP:(3,\"UnrealNG\""), std::string::npos);
}

TEST_F(AtModule_Test, AddressesAndStatus)
{
    Init();
    Cmd("AT+CIFSR");
    const std::string ifs = Read();
    EXPECT_NE(ifs.find("+CIFSR:STAIP,\"10.0.2.15\""), std::string::npos) << ifs;
    EXPECT_NE(ifs.find("+CIFSR:STAMAC,\"5c:cf:7f:5a:00:01\""), std::string::npos);
    Cmd("AT+CIPSTATUS");
    EXPECT_EQ(Read(), "STATUS:2\r\n\r\nOK\r\n");
    Connect();
    Read();
    Cmd("AT+CIPSTATUS");
    EXPECT_NE(Read().find("STATUS:3\r\n+CIPSTATUS:0,\"TCP\",\"93.184.216.34\",23,"), std::string::npos);
}

TEST_F(AtModule_Test, PingThroughTheVirtualNetwork)
{
    Init();
    Cmd("AT+PING=\"10.0.2.2\"");
    const std::string out = Read(100000);
    EXPECT_EQ(out.rfind("+", 0), 0u) << out;
    EXPECT_NE(out.find("\r\n\r\nOK\r\n"), std::string::npos) << "the gateway answers";
}

TEST_F(AtModule_Test, StateRoundTrip)
{
    Init();
    Cmd("AT+CIPMUX=1");
    Cmd("AT+CIPDINFO=1");
    Read();
    auto state = std::make_unique<netstate::EspModuleState>();
    _esp->SaveState(*state);
    auto copy = std::make_unique<AtModule>(_net.get(), EspModule::Chip::Esp8266);
    copy->SetClock([this]() { return _now; }, 3500000);
    copy->OnLineSettings(SerialLine());
    copy->LoadState(*state, nullptr);
    for (char c : std::string("AT+CIPMUX?\r\n"))
        copy->Transmit(static_cast<uint8_t>(c));
    _now += 100000;
    std::string out;
    while (copy->HasByte())
        out.push_back(static_cast<char>(copy->TakeByte()));
    EXPECT_EQ(out, "+CIPMUX:1\r\n\r\nOK\r\n") << "echo off and mux on came back";
}
