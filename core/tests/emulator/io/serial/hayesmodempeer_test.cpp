// The Hayes modem peer (network tdd §10, T-NET-6): AT commands, dialing a host:port / name / phone book number
// through the virtual network, CONNECT / NO CARRIER / BUSY, the +++ escape with its guard times, ATO / ATH, the
// remote hanging up, an inbound call (RING, RI, ATA), DTR, and the TTD state round trip. No UART: the test is the
// UART (Transmit = a character the ZX sent, TakeByte = one it receives) and owns the clock.

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <memory>
#include <string>

#include "_helpers/fakehostnet.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/serial/comport.h"
#include "emulator/io/serial/hayesmodempeer.h"

namespace
{
ComPortSpec Spec(const std::string& text)
{
    ComPortSpec spec;
    std::string error;
    EXPECT_TRUE(ComPortSpec::Parse(text, spec, error)) << error;
    return spec;
}

constexpr uint64_t kHz = 3500000;
constexpr uint64_t kSecond = kHz;
}  // namespace

class HayesModemPeer_Test : public ::testing::Test
{
protected:
    void SetUp() override { Make({}, "MODEM", "5551234=192.0.2.10:2323,5550000=bbs.test"); }

    void Make(const VirtualNetworkConfig& config, const std::string& spec, const std::string& phonebook)
    {
        _modem.reset();
        _net.reset();
        auto host = std::make_unique<FakeHostNet>();
        _host = host.get();
        _net = std::make_unique<VirtualNetwork>(nullptr, std::move(host), config);
        _modem = std::make_unique<HayesModemPeer>(_net.get(), Spec(spec), phonebook);
        _modem->SetClock([this]() { return _now; }, kHz);
        _modem->onReceive = [this]() { ++_notified; };
        SerialLine line;
        line.baud = 57600;
        _modem->OnLineSettings(line);
        _now = 10 * kSecond;
    }

    /// Characters the ZX sends, one character time (about 0.2 ms) apart
    void Send(const std::string& text)
    {
        for (char c : text)
        {
            _modem->Transmit(static_cast<uint8_t>(c));
            _now += 600;
        }
    }

    /// Everything the modem has for the ZX now
    std::string Receive()
    {
        std::string out;
        while (_modem->HasByte())
            out.push_back(static_cast<char>(_modem->TakeByte()));
        return out;
    }

    /// A frame boundary: the network delivers, the modem's timers run, its link flushes
    void Frame(uint64_t t = kHz / 50)
    {
        _now += t;
        _net->Pump();
        _modem->OnFrame();
    }

    /// Dial 192.0.2.10:2323 and let the host accept it; returns the host-side socket
    uint16_t Connect()
    {
        Send("ATDT 192.0.2.10:2323\r");
        EXPECT_EQ(Receive(), "ATDT 192.0.2.10:2323\r");
        const FakeHostNet::Command* connect = _host->Last("connect");
        EXPECT_NE(connect, nullptr);
        if (!connect)
            return 0;
        EXPECT_EQ(connect->endpoint.addr, NetIp(192, 0, 2, 10));
        EXPECT_EQ(connect->endpoint.port, 2323);
        _host->Push(NetEventType::Connected, connect->socket);
        Frame();
        EXPECT_EQ(Receive(), "\r\nCONNECT 57600\r\n");
        return connect->socket;
    }

    FakeHostNet* _host = nullptr;
    std::unique_ptr<VirtualNetwork> _net;
    std::unique_ptr<HayesModemPeer> _modem;
    uint64_t _now = 0;
    int _notified = 0;
};

TEST_F(HayesModemPeer_Test, CommandModeEchoesAndAnswersOk)
{
    Send("AT\r");
    EXPECT_EQ(Receive(), "AT\r\r\nOK\r\n");
    Send("at e0 v1\r");
    EXPECT_EQ(Receive(), "at e0 v1\r\r\nOK\r\n") << "the line is echoed before E0 takes effect";
    Send("ATV0\r");
    EXPECT_EQ(Receive(), "0\r") << "no echo, numeric result";
    Send("ATX9\r");
    EXPECT_EQ(Receive(), "4\r") << "ERROR";
    Send("ATV1Q1\r");
    EXPECT_EQ(Receive(), "") << "quiet";
    Send("ATQ0S7=10S7?\r");
    EXPECT_EQ(Receive(), "\r\n010\r\n\r\nOK\r\n");
    Send("A/");
    EXPECT_EQ(Receive(), "\r\n\r\n010\r\n\r\nOK\r\n") << "A/ repeats the last line at once";
    Send("ATI3\r");
    EXPECT_NE(Receive().find("UNREAL-NG HAYES MODEM"), std::string::npos);
    Send("hello\r");
    EXPECT_EQ(Receive(), "") << "a line without AT is ignored";
    Send("ATZ\r");
    EXPECT_EQ(Receive(), "\r\nOK\r\n");
    Send("AT\r");
    EXPECT_EQ(Receive(), "AT\r\r\nOK\r\n") << "ATZ restored the factory echo";
    EXPECT_EQ(_modem->GetMode(), HayesModemPeer::Mode::Command);
    EXPECT_FALSE(_modem->Dcd()) << "&C1: DCD follows the carrier";
    EXPECT_TRUE(_modem->Dsr());
    EXPECT_TRUE(_modem->Cts());
}

TEST_F(HayesModemPeer_Test, DialsAHostConnectsAndCarriesBytesBothWays)
{
    const uint16_t socket = Connect();
    EXPECT_EQ(_modem->GetMode(), HayesModemPeer::Mode::Online);
    EXPECT_TRUE(_modem->Dcd());
    EXPECT_TRUE(_modem->Carrier());

    Send("hi");
    Frame();
    ASSERT_NE(_host->Last("send"), nullptr);
    EXPECT_EQ(_host->Last("send")->data, (std::vector<uint8_t>{'h', 'i'}));

    const int before = _notified;
    _host->Push(NetEventType::Data, socket, NetEventStatus::Ok, {}, {'b', 'b', 's'});
    Frame();
    EXPECT_GT(_notified, before) << "the UART is told when call data arrives";
    EXPECT_EQ(Receive(), "bbs");
    EXPECT_EQ(_modem->Target(), "192.0.2.10:2323 (192.0.2.10:2323)");
}

TEST_F(HayesModemPeer_Test, EscapeNeedsTheGuardTimesThenAtoResumesAndAthHangsUp)
{
    const uint16_t socket = Connect();
    // Too soon after data: the pluses are data
    Send("x+++");
    Frame(kSecond + kSecond / 10);
    EXPECT_EQ(_modem->GetMode(), HayesModemPeer::Mode::Online);
    EXPECT_EQ(Receive(), "");

    // A second of silence, +++, a second of silence: command mode, the call held
    Frame(kSecond + kSecond / 10);
    Send("+++");
    Frame(kSecond / 2);
    EXPECT_EQ(_modem->GetMode(), HayesModemPeer::Mode::Online) << "the trailing guard time is not over yet";
    Frame(kSecond / 2 + kSecond / 10);
    EXPECT_EQ(_modem->GetMode(), HayesModemPeer::Mode::Escaped);
    EXPECT_EQ(Receive(), "\r\nOK\r\n");
    EXPECT_TRUE(_modem->Dcd()) << "the call is still up";

    // Call data waits while the modem takes commands
    _host->Push(NetEventType::Data, socket, NetEventStatus::Ok, {}, {'z'});
    Frame();
    Send("AT\r");
    EXPECT_EQ(Receive(), "AT\r\r\nOK\r\n");
    Send("ATO\r");
    EXPECT_EQ(Receive(), "ATO\r\r\nCONNECT 57600\r\nz");

    Frame(kSecond + kSecond / 10);
    Send("+++");
    Frame(kSecond + kSecond / 10);
    EXPECT_EQ(Receive(), "\r\nOK\r\n");
    Send("ATH\r");
    EXPECT_EQ(Receive(), "ATH\r\r\nOK\r\n");
    EXPECT_FALSE(_modem->Dcd());
    Frame();
    ASSERT_NE(_host->Last("close"), nullptr) << "the call's socket closes at the frame boundary";
    EXPECT_EQ(_host->Last("close")->socket, socket);
}

TEST_F(HayesModemPeer_Test, TheRemoteHangingUpGivesItsLastBytesThenNoCarrier)
{
    const uint16_t socket = Connect();
    _host->Push(NetEventType::Data, socket, NetEventStatus::Ok, {}, {'b', 'y', 'e'});
    _host->Push(NetEventType::PeerClosed, socket);
    Frame();
    EXPECT_EQ(Receive(), "bye\r\nNO CARRIER\r\n");
    EXPECT_EQ(_modem->GetMode(), HayesModemPeer::Mode::Command);
    EXPECT_FALSE(_modem->Dcd());
}

TEST_F(HayesModemPeer_Test, RefusedIsBusyUnknownNumbersFindNobodyAndPhoneBookNumbersDial)
{
    Send("ATDT 192.0.2.99:23\r");
    Receive();
    const FakeHostNet::Command* connect = _host->Last("connect");
    ASSERT_NE(connect, nullptr);
    _host->Push(NetEventType::ConnectFailed, connect->socket, NetEventStatus::Refused);
    Frame();
    EXPECT_EQ(Receive(), "\r\nBUSY\r\n");

    Send("ATDT 555-9999\r");
    EXPECT_EQ(Receive(), "ATDT 555-9999\r\r\nNO CARRIER\r\n") << "not in the phone book";

    const size_t connects = _host->commands.size();
    Send("ATDT555-1234\r");
    Receive();
    connect = _host->Last("connect");
    ASSERT_NE(connect, nullptr);
    EXPECT_GT(_host->commands.size(), connects);
    EXPECT_EQ(connect->endpoint.addr, NetIp(192, 0, 2, 10)) << "5551234=192.0.2.10:2323";
    EXPECT_EQ(connect->endpoint.port, 2323);
    EXPECT_EQ(_modem->GetMode(), HayesModemPeer::Mode::Dialing);

    // A key while dialing aborts
    Send("x");
    EXPECT_EQ(Receive(), "\r\nNO CARRIER\r\n");
    EXPECT_EQ(_modem->GetMode(), HayesModemPeer::Mode::Command);
}

TEST_F(HayesModemPeer_Test, ANameResolvesThroughTheVirtualNetworkAndTheDialTimesOut)
{
    VirtualNetworkConfig config;
    config.hosts["bbs.test"] = NetIp(192, 0, 2, 20);
    Make(config, "MODEM", "5550000=bbs.test");
    Send("ATS7=2DT5550000\r");
    EXPECT_EQ(Receive(), "ATS7=2DT5550000\r");
    Frame();   // the hosts table answers
    Frame();   // the connect starts at the frame boundary
    const FakeHostNet::Command* connect = _host->Last("connect");
    ASSERT_NE(connect, nullptr);
    EXPECT_EQ(connect->endpoint.addr, NetIp(192, 0, 2, 20));
    EXPECT_EQ(connect->endpoint.port, 23) << "a phone book entry without a port: telnet";
    Frame(2 * kSecond);
    EXPECT_EQ(Receive(), "\r\nNO ANSWER\r\n") << "S7 = 2 s without a carrier";
}

TEST_F(HayesModemPeer_Test, AnInboundCallRingsAndAtaAnswersIt)
{
    Make({}, "MODEM,2323", "");
    const FakeHostNet::Command* listen = _host->Last("listen");
    ASSERT_NE(listen, nullptr) << "MODEM,2323 listens on guest port 2323";
    EXPECT_EQ(listen->endpoint.port, 2323);
    constexpr uint16_t kClient = 0x8001;
    _host->Push(NetEventType::Accepted, listen->socket, NetEventStatus::Ok, NetEndpoint{NetIp(127, 0, 0, 1), 50000},
                {static_cast<uint8_t>(kClient & 0xFF), static_cast<uint8_t>(kClient >> 8)});
    _host->Push(NetEventType::Data, kClient, NetEventStatus::Ok, {}, {'h', 'e', 'y'});
    Frame();
    EXPECT_TRUE(_modem->Ringing());
    Frame();
    EXPECT_EQ(Receive(), "\r\nRING\r\n");
    EXPECT_TRUE(_modem->Ri());
    Frame(2 * kSecond);
    EXPECT_FALSE(_modem->Ri()) << "RI: 2 s on, 4 s off";
    EXPECT_EQ(_modem->SRegister(1), 1);

    Send("ATA\r");
    EXPECT_EQ(Receive(), "ATA\r\r\nCONNECT 57600\r\nhey") << "what the caller sent before the answer follows CONNECT";
    EXPECT_TRUE(_modem->Dcd());
    Send("ok");
    Frame();
    ASSERT_NE(_host->Last("send"), nullptr);
    EXPECT_EQ(_host->Last("send")->socket, kClient);
    EXPECT_EQ(_host->Last("send")->data, (std::vector<uint8_t>{'o', 'k'}));
    const std::vector<VirtualNetwork::ListenerInfo> listeners = _net->Listeners();
    ASSERT_EQ(listeners.size(), 1u);
    EXPECT_EQ(listeners[0].waitingSockets, 1u) << "a new listening socket for the next caller";
}

TEST_F(HayesModemPeer_Test, DtrDroppingHangsUpUnderAmpD2)
{
    _modem->OnModemLines(true, true);
    Connect();
    _modem->OnModemLines(true, false);
    EXPECT_EQ(Receive(), "\r\nOK\r\n");
    EXPECT_EQ(_modem->GetMode(), HayesModemPeer::Mode::Command);
    EXPECT_FALSE(_modem->Carrier());
}

TEST_F(HayesModemPeer_Test, TheStateRoundTripsThroughTheComPortRecord)
{
    Connect();
    Send("ATS0=3");   // typed into the call: data, not a command (unsent until the frame boundary)
    netstate::Com saved{};
    ASSERT_TRUE(ComPort::SavePeer(_modem.get(), saved));
    EXPECT_EQ(saved.unsentLength, 6u);
    EXPECT_EQ(saved.peerKind, 6);
    EXPECT_EQ(saved.modem.mode, static_cast<uint8_t>(HayesModemPeer::Mode::Online));

    HayesModemPeer other(_net.get(), Spec("MODEM"), "");
    other.SetClock([this]() { return _now; }, kHz);
    ComPort::LoadPeer(&other, saved, nullptr);
    EXPECT_EQ(other.GetMode(), HayesModemPeer::Mode::Online);
    EXPECT_TRUE(other.Dcd());
    EXPECT_EQ(other.Dialed(), "192.0.2.10:2323");
    EXPECT_EQ(other.LastResult(), "CONNECT 57600");
    netstate::Com again{};
    ComPort::SavePeer(&other, again);
    EXPECT_EQ(std::memcmp(&saved.modem, &again.modem, sizeof(saved.modem)), 0) << "the modem's record is the same";
    EXPECT_EQ(std::memcmp(&saved.link, &again.link, sizeof(saved.link)), 0);
}

TEST(HayesModemPhonebook_Test, ParsesNumbersAndRefusesJunk)
{
    std::map<std::string, std::string> book;
    std::string error;
    ASSERT_TRUE(HayesModemPeer::ParsePhonebook(" 555-1234 = bbs.example.org:2323 , 7=10.0.2.2", book, error)) << error;
    ASSERT_EQ(book.size(), 2u);
    EXPECT_EQ(book["5551234"], "TCP:bbs.example.org:2323");
    EXPECT_EQ(book["7"], "TCP:10.0.2.2:23");
    EXPECT_TRUE(HayesModemPeer::ParsePhonebook("", book, error));
    EXPECT_TRUE(book.empty());
    EXPECT_FALSE(HayesModemPeer::ParsePhonebook("abc=host:23", book, error));
    EXPECT_FALSE(HayesModemPeer::ParsePhonebook("555=", book, error));
}

TEST(HayesModemSpec_Test, ModemWithAndWithoutAListenPort)
{
    ComPortSpec spec;
    std::string error;
    ASSERT_TRUE(ComPortSpec::Parse("modem", spec, error));
    EXPECT_EQ(spec.kind, ComPortSpec::Kind::Modem);
    EXPECT_EQ(spec.port, 0);
    EXPECT_EQ(spec.ToString(), "MODEM");
    ASSERT_TRUE(ComPortSpec::Parse("MODEM,2323", spec, error));
    EXPECT_EQ(spec.port, 2323);
    EXPECT_EQ(spec.ToString(), "MODEM,2323");
    EXPECT_FALSE(ComPortSpec::Parse("MODEM,0", spec, error));
    EXPECT_FALSE(ComPortSpec::Parse("MODEM,x", spec, error));
}
