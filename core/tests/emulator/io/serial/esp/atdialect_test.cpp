// The ESP8266 ESP-AT 2.2.x dialect (atdialect.h) as AtModule speaks it, per command group: the build's command
// set (no _CUR / _DEF on ESP-AT), the reply forms, the error codes behind AT+SYSLOG=1 and the timing a client
// waits on. Primary source: Espressif's ESP-AT 2.2 (ESP8266) command reference, release notes and esp_at_core.h
// (docs/inprogress/2026-10-02-tsconf-zifi/tdd.md §7.1). Virtual network with a FakeHostNet, a hand clock.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "_helpers/fakehostnet.h"
#include "common/network/dnsmessage.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/serial/esp/atdialect.h"
#include "emulator/io/serial/esp/atmodule.h"

class AtDialect_Test : public ::testing::Test
{
protected:
    using Firmware = EspModule::Firmware;

    void SetUp() override { Make(Firmware::Esp8266At222); }

    void Make(Firmware firmware, atdialect::Flash flash = atdialect::Flash::TwoMbOrMore)
    {
        _esp.reset();
        _net.reset();
        auto host = std::make_unique<FakeHostNet>();
        _host = host.get();
        _net = std::make_unique<VirtualNetwork>(nullptr, std::move(host), VirtualNetworkConfig());
        _esp = std::make_unique<AtModule>(_net.get(), firmware);
        _esp->SetFlash(flash);
        _esp->SetClock([this]() { return _now; }, 3500000);
        _esp->OnLineSettings(SerialLine());
        Read(300000);   // the power-up banner
        Cmd("ATE0");
        Read();
    }

    void Cmd(const std::string& line)
    {
        for (char c : line + "\r\n")
            _esp->Transmit(static_cast<uint8_t>(c));
    }

    /// Let `us` pass in frames and take what the module sent
    std::string Read(uint64_t us = 5000)
    {
        std::string out;
        const uint64_t step = 20000;
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

    /// CIPSTART to an address: returns the host socket
    uint16_t Connect(const std::string& link = "")
    {
        Cmd("AT+CIPSTART=" + link + "\"TCP\",\"93.184.216.34\",80");
        Read();
        const uint16_t socket = _host->Last("connect")->socket;
        _host->Push(NetEventType::Connected, socket);
        _net->Pump();
        Read();
        return socket;
    }

    uint64_t _now = 1000;
    FakeHostNet* _host = nullptr;
    std::unique_ptr<VirtualNetwork> _net;
    std::unique_ptr<AtModule> _esp;
};

// --- The table ---------------------------------------------------------------------------------------------------

TEST_F(AtDialect_Test, TheBuildsDifferWhereEspressifSaysTheyDo)
{
    const atdialect::Traits& nonOs = atdialect::TraitsOf(Firmware::Esp8266NonOs174);
    const atdialect::Traits& at221 = atdialect::TraitsOf(Firmware::Esp8266At221);
    const atdialect::Traits& at222 = atdialect::TraitsOf(Firmware::Esp8266At222);
    EXPECT_TRUE(nonOs.suffixForms);
    EXPECT_FALSE(at222.suffixForms) << "ESP-AT has no _CUR / _DEF (AT Command Set Comparison)";
    EXPECT_TRUE(at221.suffixForms) << "the kit's 2.2.1 binary has the _CUR tokens";
    EXPECT_FALSE(nonOs.sysStore);
    EXPECT_TRUE(at222.sysStore);
    EXPECT_FALSE(at221.sysStore);
    EXPECT_TRUE(at222.cipState) << "2.2.2.0 added AT+CIPSTATE";
    EXPECT_FALSE(at221.cipState);
    EXPECT_TRUE(at221.quotedDomain && at222.quotedDomain) << "2.2.0.0 quotes +CIPDOMAIN";
    EXPECT_EQ(atdialect::ErrorCode(atdialect::kSubUnsupported), 0x01090000u);
    EXPECT_EQ(atdialect::ErrCodeLine(0x0107000A), "ERR CODE:0x0107000a\r\n");
}

// --- Basic: identity, the flash build, error codes ---------------------------------------------------------------

TEST_F(AtDialect_Test, Basic_IdentityFollowsTheFlash)
{
    Cmd("AT+GMR");
    std::string out = Read();
    EXPECT_NE(out.find("AT version:2.2.2.0("), std::string::npos) << out;
    EXPECT_NE(out.find("Bin version:2.2.2(ESP8266_2MB)"), std::string::npos) << out;
    Cmd("AT+CIUPDATE");
    EXPECT_EQ(Read(), "+CIPUPDATE:1\r\n\r\nERROR\r\n") << "the 2 MB build has OTA (no server here)";

    Make(Firmware::Esp8266At222, atdialect::Flash::OneMb);   // an ESP-01 / ESP-01S
    Cmd("AT+GMR");
    out = Read();
    EXPECT_NE(out.find("Bin version:2.2.2(ESP8266_1MB)"), std::string::npos) << out;
    Cmd("AT+SYSLOG=1");
    Read();
    Cmd("AT+CIUPDATE");
    EXPECT_EQ(Read(), "ERR CODE:0x01090000\r\n\r\nERROR\r\n") << "the 1 MB build has no OTA command";
}

TEST_F(AtDialect_Test, Basic_SyslogPutsTheErrorCodeBeforeError)
{
    Cmd("AT+FAKE");
    EXPECT_EQ(Read(), "\r\nERROR\r\n") << "AT+SYSLOG=0: no code";
    Cmd("AT+SYSLOG?");
    EXPECT_EQ(Read(), "+SYSLOG:0\r\n\r\nOK\r\n");
    Cmd("AT+SYSLOG=1");
    EXPECT_EQ(Read(), "\r\nOK\r\n");
    Cmd("AT+FAKE");
    EXPECT_EQ(Read(), "ERR CODE:0x01090000\r\n\r\nERROR\r\n") << "the command is not supported";
    Cmd("XY");
    EXPECT_EQ(Read(), "ERR CODE:0x01030000\r\n\r\nERROR\r\n") << "no AT at the start";
    Cmd("AT+CWMODE=9");
    EXPECT_EQ(Read(), "ERR CODE:0x01070000\r\n\r\nERROR\r\n") << "a parameter is invalid";
    Cmd("AT+CIPSEND=4");
    EXPECT_EQ(Read(), "link is not valid\r\nERR CODE:0x010a0000\r\n\r\nERROR\r\n") << "the command failed";

    Make(Firmware::Esp8266NonOs174);
    Cmd("AT+SYSLOG=1");
    EXPECT_EQ(Read(), "\r\nERROR\r\n") << "NonOS AT has no SYSLOG";
}

TEST_F(AtDialect_Test, Basic_NoCurDefFormsOnEspAt)
{
    Cmd("AT+CWMODE_CUR=1");
    EXPECT_EQ(Read(), "\r\nERROR\r\n") << "ESP-AT 2.2.2: no _CUR form";
    Cmd("AT+CWJAP_DEF=\"UnrealNG\",\"x\"");
    EXPECT_EQ(Read(), "\r\nERROR\r\n");
    Cmd("AT+CIPDNS_CUR?");
    EXPECT_EQ(Read(), "\r\nERROR\r\n");
    Cmd("AT+UART_CUR?");
    EXPECT_EQ(Read(), "+UART_CUR:115200,8,1,0,3\r\n\r\nOK\r\n") << "UART_CUR / UART_DEF stay";
    Cmd("AT+CWMODE=1");
    EXPECT_EQ(Read(), "\r\nOK\r\n");

    Make(Firmware::Esp8266At221);   // the kit's 2.2.1 profile sends the _CUR forms
    Cmd("AT+CWMODE_CUR=1");
    EXPECT_EQ(Read(), "\r\nOK\r\n");
    Make(Firmware::Esp8266NonOs174);   // HackerVBI zifi.spg: AT+CWMODE_DEF=1
    Cmd("AT+CWMODE_DEF=1");
    EXPECT_EQ(Read(), "\r\nOK\r\n");
}

// --- Wi-Fi -------------------------------------------------------------------------------------------------------

TEST_F(AtDialect_Test, WiFi_AFailedJoinEndsWithItsCodeAndError)
{
    Cmd("AT+CWJAP");   // the execute form: the last access point again
    EXPECT_EQ(Read(2000000), "WIFI DISCONNECT\r\nWIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK\r\n");

    // <jap_timeout> 3 s: the answer comes then, not after the default 15 s
    Cmd("AT+CWJAP=\"Elsewhere\",\"secret\",,,,,,3");
    EXPECT_EQ(Read(2900000), "WIFI DISCONNECT\r\n");
    EXPECT_EQ(Read(200000), "+CWJAP:3\r\n\r\nERROR\r\n") << "3: cannot find the target AP; ERROR, not FAIL";
    Cmd("AT+CWJAP=\"UnrealNG\",\"x\",,,,,,2");
    EXPECT_EQ(Read(), "\r\nERROR\r\n") << "jap_timeout is 3..600";

    Make(Firmware::Esp8266NonOs174);
    Cmd("AT+CWJAP_CUR=\"Elsewhere\",\"secret\"");
    EXPECT_EQ(Read(16000000), "WIFI DISCONNECT\r\n+CWJAP:3\r\n\r\nFAIL\r\n") << "NonOS: FAIL after 15 s";
}

TEST_F(AtDialect_Test, WiFi_QueriesInTheTwoXForm)
{
    Cmd("AT+CWJAP?");
    EXPECT_EQ(Read(), "+CWJAP:\"UnrealNG\",\"52:54:00:12:35:02\",6,-48,0,1,3,0,0\r\n\r\nOK\r\n")
        << "ssid, bssid, channel, rssi, pci_en, reconn_interval, listen_interval, scan_mode, pmf";
    Cmd("AT+CWSTATE?");
    EXPECT_EQ(Read(), "+CWSTATE:2,\"UnrealNG\"\r\n\r\nOK\r\n") << "2: connected, IPv4 address obtained";
    Cmd("AT+CWQAP");
    Read();
    Cmd("AT+CWSTATE?");
    EXPECT_EQ(Read(), "+CWSTATE:4,\"UnrealNG\"\r\n\r\nOK\r\n") << "4: disconnected";
    Cmd("AT+CWLAP");
    EXPECT_EQ(Read(2000000), "+CWLAP:(3,\"UnrealNG\",-48,\"52:54:00:12:35:02\",6,-11,0,4,4,7,0)\r\n\r\nOK\r\n")
        << "the 2.x fields: pairwise / group cipher, bgn, wps";

    Make(Firmware::Esp8266NonOs174);
    Cmd("AT+CWSTATE?");
    EXPECT_EQ(Read(), "\r\nERROR\r\n") << "2.2.0.0 added it";
}

// --- TCP/IP ------------------------------------------------------------------------------------------------------

TEST_F(AtDialect_Test, TcpIp_DomainIsQuoted)
{
    Cmd("AT+CIPDOMAIN=\"example.test\"");
    Read();
    AnswerDns(NetIp(93, 184, 216, 34));
    EXPECT_EQ(Read(), "+CIPDOMAIN:\"93.184.216.34\"\r\n\r\nOK\r\n");
    Make(Firmware::Esp8266NonOs174);
    Cmd("AT+CIPDOMAIN=\"10.0.2.2\"");
    EXPECT_EQ(Read(), "+CIPDOMAIN:10.0.2.2\r\n\r\nOK\r\n") << "NonOS: no quotes";
}

TEST_F(AtDialect_Test, TcpIp_CipStateOn222Only)
{
    Cmd("AT+CIPMUX=1");
    Read();
    Cmd("AT+CIPSTATE?");
    EXPECT_EQ(Read(), "\r\nOK\r\n") << "no connection: OK alone";
    Connect("1,");
    Cmd("AT+CIPSTATE?");
    EXPECT_EQ(Read(), "+CIPSTATE:1,\"TCP\",\"93.184.216.34\",80,49409,0\r\n\r\nOK\r\n");
    Make(Firmware::Esp8266At221);
    Cmd("AT+CIPSTATE?");
    EXPECT_EQ(Read(), "\r\nERROR\r\n");
}

TEST_F(AtDialect_Test, TcpIp_PassiveReceiveAnnouncesOnceUntilRead)
{
    Cmd("AT+CIPRECVMODE=1");
    Read();
    const uint16_t socket = Connect();
    _host->Push(NetEventType::Data, socket, NetEventStatus::Ok, {}, {'a', 'b', 'c'});
    _net->Pump();
    EXPECT_EQ(Read(), "+IPD,3\r\n");
    _host->Push(NetEventType::Data, socket, NetEventStatus::Ok, {}, {'d', 'e'});
    _net->Pump();
    EXPECT_EQ(Read(), "") << "the next +IPD waits until the last one was read";
    Cmd("AT+CIPRECVLEN?");
    EXPECT_EQ(Read(), "+CIPRECVLEN:5,,,,\r\n\r\nOK\r\n") << "links that are not open are empty";
    Cmd("AT+CIPRECVDATA=4");
    EXPECT_EQ(Read(), "+CIPRECVDATA:4,abcd\r\n\r\nOK\r\n");
    _host->Push(NetEventType::Data, socket, NetEventStatus::Ok, {}, {'f'});
    _net->Pump();
    EXPECT_EQ(Read(), "+IPD,2\r\n") << "after the read: the total again";

    auto state = std::make_unique<netstate::EspModuleState>();
    netstate::Tail tail;
    _esp->SaveState(*state, tail);
    auto copy = std::make_unique<AtModule>(_net.get(), Firmware::Esp8266At222);
    copy->SetClock([this]() { return _now; }, 3500000);
    copy->LoadState(*state, tail, nullptr);
    auto again = std::make_unique<netstate::EspModuleState>();
    netstate::Tail againTail;
    copy->SaveState(*again, againTail);
    EXPECT_EQ(state->firmware[224], again->firmware[224]) << "the owed +IPD survives a checkpoint";
    EXPECT_EQ(state->firmware[224], 1) << "link 0 owes a read";
}
