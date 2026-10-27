// COM port peers over the virtual network (network adapters TDD §7.2): a host
// TCP endpoint (by address or by name) and a host serial device, with a
// FakeHostNet (no sockets)

#include <gtest/gtest.h>

#include <deque>
#include <memory>
#include <vector>

#include "_helpers/fakehostnet.h"
#include "common/network/dnsmessage.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/serial/comport.h"
#include "emulator/io/serial/serialpeer.h"

namespace
{
ComPortSpec Spec(const std::string& text)
{
    ComPortSpec spec;
    std::string error;
    EXPECT_TRUE(ComPortSpec::Parse(text, spec, error)) << error;
    return spec;
}
}  // namespace

class SerialPeer_Test : public ::testing::Test
{
protected:
    void SetUp() override { Make({}); }

    void Make(const VirtualNetworkConfig& config)
    {
        _net.reset();
        auto host = std::make_unique<FakeHostNet>();
        _host = host.get();
        _net = std::make_unique<VirtualNetwork>(nullptr, std::move(host), config);
    }

    /// The host resolver answers the pending query (as the bridge would)
    void AnswerDns(const std::vector<uint32_t>& addresses, uint8_t rcode = dns::kRcodeNoError)
    {
        const FakeHostNet::Command query = *_host->Last("dns");
        dns::Question q;
        ASSERT_TRUE(dns::ParseQuery(query.data.data(), query.data.size(), q));
        _host->Push(NetEventType::Datagram, query.socket, NetEventStatus::Ok, query.endpoint,
                    dns::BuildAnswer(query.data.data(), query.data.size(), q, addresses, rcode));
        _net->Pump();
    }

    FakeHostNet* _host = nullptr;
    std::unique_ptr<VirtualNetwork> _net;
};

TEST(LoopbackPeer_Test, EchoesWhatTheZxSends)
{
    LoopbackPeer p;
    EXPECT_FALSE(p.HasByte());
    p.Transmit('a');
    p.Transmit('b');
    ASSERT_TRUE(p.HasByte());
    EXPECT_EQ(p.TakeByte(), 'a');
    EXPECT_EQ(p.TakeByte(), 'b');
    EXPECT_FALSE(p.HasByte());
}

TEST_F(SerialPeer_Test, TcpPeerConnectsReceivesAndSendsAtTheFrameBoundary)
{
    StreamPeer p(_net.get(), Spec("TCP:127.0.0.1:2323"));
    const FakeHostNet::Command connect = *_host->Last("connect");
    EXPECT_EQ(connect.endpoint.addr, NetIp(127, 0, 0, 1));
    EXPECT_EQ(connect.endpoint.port, 2323);
    EXPECT_EQ(p.GetPhase(), StreamPeer::Phase::Connecting);
    EXPECT_FALSE(p.Dcd());

    _host->Push(NetEventType::Connected, connect.socket);
    _net->Pump();
    EXPECT_TRUE(p.Connected());
    EXPECT_TRUE(p.Dcd());

    int notified = 0;
    p.onReceive = [&] { ++notified; };
    _host->Push(NetEventType::Data, connect.socket, NetEventStatus::Ok, {}, {'h', 'i'});
    _net->Pump();
    ASSERT_EQ(p.Pending(), 2u);
    EXPECT_EQ(notified, 1) << "the UART is told when the bytes arrive";
    EXPECT_EQ(p.TakeByte(), 'h');
    EXPECT_EQ(p.TakeByte(), 'i');

    p.Transmit('x');
    p.Transmit('y');
    EXPECT_EQ(_host->Last("send"), nullptr) << "held until the frame boundary";
    p.OnFrame();
    ASSERT_NE(_host->Last("send"), nullptr);
    EXPECT_EQ(_host->Last("send")->data, (std::vector<uint8_t>{'x', 'y'}));
}

TEST_F(SerialPeer_Test, AHostNameIsResolvedByTheHostResolverThenConnected)
{
    StreamPeer p(_net.get(), Spec("TCP:bbs.example.org:23"));
    EXPECT_EQ(p.GetPhase(), StreamPeer::Phase::Resolving);
    const FakeHostNet::Command* query = _host->Last("dns");
    ASSERT_NE(query, nullptr) << "the name goes to the virtual network's DNS, and on to the host resolver";
    dns::Question q;
    ASSERT_TRUE(dns::ParseQuery(query->data.data(), query->data.size(), q));
    EXPECT_EQ(q.name, "bbs.example.org");
    EXPECT_EQ(q.qtype, dns::kTypeA);
    EXPECT_EQ(_host->Last("connect"), nullptr);

    AnswerDns({NetIp(93, 184, 216, 34)});
    EXPECT_EQ(p.ResolvedAddress(), NetIp(93, 184, 216, 34));
    p.OnFrame();   // sockets change at the frame boundary
    ASSERT_NE(_host->Last("connect"), nullptr);
    EXPECT_EQ(_host->Last("connect")->endpoint.addr, NetIp(93, 184, 216, 34));
    EXPECT_EQ(_host->Last("connect")->endpoint.port, 23);
    EXPECT_EQ(p.Target(), "bbs.example.org:23 (93.184.216.34)");
}

TEST_F(SerialPeer_Test, TheHostsTableAnswersWithoutTheHost)
{
    VirtualNetworkConfig config;
    config.hosts["bbs.test"] = NetIp(192, 168, 1, 20);
    Make(config);
    StreamPeer p(_net.get(), Spec("TCP:BBS.test:2323"));
    _net->Pump();
    EXPECT_EQ(_host->Last("dns"), nullptr) << "Hosts= answers first";
    EXPECT_EQ(p.ResolvedAddress(), NetIp(192, 168, 1, 20));
    p.OnFrame();
    ASSERT_NE(_host->Last("connect"), nullptr);
    EXPECT_EQ(_host->Last("connect")->endpoint.addr, NetIp(192, 168, 1, 20));
}

TEST_F(SerialPeer_Test, AnUnknownNameFailsWithTheReasonAndIsRetried)
{
    StreamPeer p(_net.get(), Spec("TCP:no-such-host.example:23"));
    AnswerDns({}, dns::kRcodeNxDomain);
    EXPECT_EQ(p.GetPhase(), StreamPeer::Phase::Idle);
    EXPECT_NE(p.LastError().find("no such host"), std::string::npos) << p.LastError();
    const size_t before = _host->commands.size();
    for (uint32_t f = 0; f < StreamPeer::kRetryFrames; ++f)
        p.OnFrame();
    size_t queries = 0;
    for (size_t i = before; i < _host->commands.size(); ++i)
        queries += _host->commands[i].op == "dns" ? 1 : 0;
    EXPECT_EQ(queries, 1u) << "asked again after the retry time";
    EXPECT_EQ(p.GetPhase(), StreamPeer::Phase::Resolving);
}

TEST_F(SerialPeer_Test, ADnsQueryWithoutAnswerTimesOut)
{
    StreamPeer p(_net.get(), Spec("TCP:slow.example:23"));
    for (uint32_t f = 0; f < StreamPeer::kResolveFrames; ++f)
        p.OnFrame();
    EXPECT_EQ(p.GetPhase(), StreamPeer::Phase::Idle);
    EXPECT_NE(p.LastError().find("no DNS answer"), std::string::npos);
}

TEST_F(SerialPeer_Test, ALostConnectionIsRetried)
{
    StreamPeer p(_net.get(), Spec("TCP:127.0.0.1:2323"));
    const uint16_t first = _host->Last("connect")->socket;
    _host->Push(NetEventType::ConnectFailed, first, NetEventStatus::Refused);
    _net->Pump();
    EXPECT_FALSE(p.Connected());
    EXPECT_NE(p.LastError().find("refused"), std::string::npos);

    for (uint32_t f = 0; f + 1 < StreamPeer::kRetryFrames; ++f)
        p.OnFrame();
    EXPECT_EQ(_host->Last("connect")->socket, first);
    p.OnFrame();
    EXPECT_NE(_host->Last("connect")->socket, first) << "a new socket after the retry time";
}

TEST_F(SerialPeer_Test, SerialPeerOpensTheDeviceAndFollowsTheZxLineFormat)
{
    StreamPeer p(_net.get(), Spec("SERIAL:/dev/tty.usbserial-0001,57600"));
    const FakeHostNet::Command open = *_host->Last("serial");
    EXPECT_EQ(std::string(open.data.begin(), open.data.end()), "/dev/tty.usbserial-0001");
    EXPECT_EQ(open.endpoint.addr, 57600u);

    _host->Push(NetEventType::Connected, open.socket);
    _net->Pump();
    ASSERT_TRUE(p.Connected());
    ASSERT_NE(_host->Last("serial-line"), nullptr) << "the line format goes to the device on connect";
    EXPECT_EQ(_host->Last("serial-line")->endpoint.addr, 57600u);

    SerialLine line;
    line.baud = 9600;
    line.dataBits = 7;
    line.parity = 'E';
    line.stopBits = 2;
    p.OnLineSettings(line);
    const FakeHostNet::Command* set = _host->Last("serial-line");
    EXPECT_EQ(set->endpoint.addr, 9600u);
    EXPECT_EQ(set->endpoint.port, 702) << "7 data bits, 2 stop bits";
    EXPECT_EQ(set->data, (std::vector<uint8_t>{'E'}));
    EXPECT_EQ(p.Target(), "/dev/tty.usbserial-0001,9600");

    EXPECT_EQ(_host->Last("serial-lines"), nullptr) << "ComModemLines off: RTS / DTR stay as the OS set them";
    p.OnModemLines(true, true);
    EXPECT_EQ(_host->Last("serial-lines"), nullptr);
    EXPECT_TRUE(p.Cts()) << "no line watched: CTS reads asserted";
}

TEST_F(SerialPeer_Test, ModemLinesGoBothWaysWhenEnabled)
{
    StreamPeer p(_net.get(), Spec("SERIAL:COM3"), true);
    const uint16_t socket = _host->Last("serial")->socket;
    _host->Push(NetEventType::Connected, socket);
    _net->Pump();
    ASSERT_NE(_host->Last("serial-lines"), nullptr) << "the ZX's lines are applied on connect";
    EXPECT_FALSE(p.Cts()) << "until the device reports its lines";

    p.OnModemLines(true, false);
    EXPECT_EQ(_host->Last("serial-lines")->endpoint.addr, 2u) << "RTS on, DTR off";

    _host->Push(NetEventType::ModemLines, socket, NetEventStatus::Ok, {}, {0x10 | 0x80});
    _net->Pump();
    EXPECT_TRUE(p.Cts());
    EXPECT_TRUE(p.Dcd());
    EXPECT_FALSE(p.Dsr());
    EXPECT_FALSE(p.Ri());
}

/// A stream's TTD state past the blob's arrays: more received runs than netstate::kMaxComRuns (every byte its own
/// record, every other one received before the recording, so not in the journal) and more unsent bytes than
/// netstate::kMaxComBytes. All of it comes back: references through the journal, the rest from the tail
TEST_F(SerialPeer_Test, AStreamStateBeyondTheFixedArraysRestoresWhole)
{
    StreamPeer p(_net.get(), Spec("TCP:127.0.0.1:2323"));
    auto journalByte = [](uint32_t source, uint32_t offset) { return static_cast<uint8_t>(source * 31 + offset); };
    std::deque<StreamPeer::RxByte> rx;
    for (uint32_t i = 0; i < 3u * netstate::kMaxComRuns; ++i)
    {
        const uint32_t source = (i % 2) ? i + 1 : 0;   // odd: journal record i + 1; even: not journaled
        rx.push_back({source ? journalByte(source, 5) : static_cast<uint8_t>(i ^ 0x5A), source, source ? 5u : 0u});
    }
    p.SetReceived(rx);
    std::vector<uint8_t> unsent(netstate::kMaxComBytes + 1000);
    for (size_t i = 0; i < unsent.size(); ++i)
        unsent[i] = static_cast<uint8_t>(i * 13);
    p.SetUnsent(unsent.data(), unsent.size());

    auto saved = std::make_unique<netstate::Com>();
    netstate::Tail tail;
    ComPort::SavePeer(&p, *saved, tail);
    EXPECT_FALSE(tail.Empty());

    StreamPeer other(_net.get(), Spec("TCP:127.0.0.1:2323"));
    const ComPort::ByteSource journal = [&](uint32_t source, uint32_t offset, uint32_t length, std::vector<uint8_t>& out) {
        out.clear();
        for (uint32_t i = 0; i < length; ++i)
            out.push_back(journalByte(source, offset + i));
        return true;
    };
    EXPECT_TRUE(ComPort::LoadPeer(&other, *saved, tail, journal));
    const std::deque<StreamPeer::RxByte>& back = other.Received();
    ASSERT_EQ(back.size(), rx.size());
    for (size_t i = 0; i < rx.size(); ++i)
    {
        EXPECT_EQ(back[i].value, rx[i].value) << "byte " << i;
        EXPECT_EQ(back[i].source, rx[i].source) << "byte " << i;
        EXPECT_EQ(back[i].offset, rx[i].offset) << "byte " << i;
    }
    EXPECT_TRUE(other.Unsent() == unsent);
}

TEST(SerialPeerNoHost_Test, WithoutHostAccessTheLinkNeverComesUp)
{
    VirtualNetwork net(nullptr, nullptr, VirtualNetworkConfig{});
    StreamPeer p(&net, Spec("TCP:127.0.0.1:23"));
    net.Pump();
    EXPECT_FALSE(p.Connected());
    EXPECT_EQ(p.RetryFrames(), StreamPeer::kRetryFrames);
}
