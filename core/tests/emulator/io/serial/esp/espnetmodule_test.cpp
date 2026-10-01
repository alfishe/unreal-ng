// ESP module with NedoOS's ESPNET firmware 1.27 (network TDD step N3). Frames
// as the NedoOS kernel / SDK sends them (the byte examples of the protocol
// notes), the module on a virtual network with a FakeHostNet, a hand clock.

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "_helpers/fakehostnet.h"
#include "common/network/dnsmessage.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/serial/esp/espnetmodule.h"

namespace
{
struct Reply
{
    uint8_t cmd = 0, sock = 0, status = 0, seq = 0;
    uint16_t result = 0;
    std::vector<uint8_t> payload;
};
}  // namespace

class EspnetModule_Test : public ::testing::Test
{
protected:
    void SetUp() override { Make(EspModule::Chip::Esp32, {}); }

    void Make(EspModule::Chip chip, const VirtualNetworkConfig& config)
    {
        _esp.reset();
        _net.reset();
        auto host = std::make_unique<FakeHostNet>();
        _host = host.get();
        _net = std::make_unique<VirtualNetwork>(nullptr, std::move(host), config);
        _esp = std::make_unique<EspnetModule>(_net.get(), chip);
        _esp->SetClock([this]() { return _now; }, 3500000);
        SerialLine line;   // 115200 8N1: the module's line
        _esp->OnLineSettings(line);
    }

    /// The host's request: SOF, cmd, sock, arg, seq, len16 LE, payload
    void Request(uint8_t cmd, uint8_t sock, uint8_t arg, const std::vector<uint8_t>& payload = {})
    {
        std::vector<uint8_t> f = {0xA5, cmd, sock, arg, ++_seq, static_cast<uint8_t>(payload.size()),
                                  static_cast<uint8_t>(payload.size() >> 8)};
        f.insert(f.end(), payload.begin(), payload.end());
        for (uint8_t b : f)
            _esp->Transmit(b);
    }

    /// The next reply, if the module has one by `afterUs` from now
    bool Next(Reply& r, uint64_t afterUs = 10000)
    {
        _now += afterUs * 35 / 10;
        std::vector<uint8_t> bytes;
        while (_esp->HasByte() && bytes.size() < 9)
            bytes.push_back(_esp->TakeByte());
        if (bytes.size() < 9 || bytes[0] != 0xA5)
            return false;
        r.cmd = bytes[1];
        r.sock = bytes[2];
        r.status = bytes[3];
        r.seq = bytes[4];
        r.result = static_cast<uint16_t>(bytes[5] | (bytes[6] << 8));
        const uint16_t len = static_cast<uint16_t>(bytes[7] | (bytes[8] << 8));
        r.payload.clear();
        while (r.payload.size() < len && _esp->HasByte())
            r.payload.push_back(_esp->TakeByte());
        return r.payload.size() == len;
    }

    Reply Expect(uint8_t cmd)
    {
        Reply r;
        EXPECT_TRUE(Next(r)) << "no reply to cmd " << int(cmd);
        EXPECT_EQ(r.cmd, cmd);
        EXPECT_EQ(r.seq, _seq) << "the reply carries the request's seq";
        return r;
    }

    static std::vector<uint8_t> Sockaddr(uint32_t ip, uint16_t port)
    {
        std::vector<uint8_t> s = {2, static_cast<uint8_t>(port >> 8), static_cast<uint8_t>(port),
                                  static_cast<uint8_t>(ip >> 24), static_cast<uint8_t>(ip >> 16),
                                  static_cast<uint8_t>(ip >> 8), static_cast<uint8_t>(ip)};
        s.insert(s.end(), 8, 0);
        return s;
    }

    uint8_t OpenTcp()
    {
        Request(EspnetModule::kSocket, 0xFF, 1, {2});
        const Reply r = Expect(EspnetModule::kSocket);
        EXPECT_EQ(r.status, 0);
        return r.sock;
    }

    uint64_t _now = 1000;
    uint8_t _seq = 0;
    FakeHostNet* _host = nullptr;
    std::unique_ptr<VirtualNetwork> _net;
    std::unique_ptr<EspnetModule> _esp;
};

TEST_F(EspnetModule_Test, InfoDescribesTheModuleOnTheVirtualAccessPoint)
{
    Request(EspnetModule::kInfo, 0xFF, 0);
    const Reply r = Expect(EspnetModule::kInfo);
    EXPECT_EQ(r.sock, 0xFF);
    EXPECT_EQ(r.status, 0);
    ASSERT_EQ(r.payload.size(), 53u);
    EXPECT_EQ(r.payload[0], 1);
    EXPECT_EQ(r.payload[1], 27) << "firmware 1.27";
    EXPECT_EQ(r.payload[2], 32) << "ESP32";
    EXPECT_EQ(r.payload[3], 8) << "8 sockets";
    EXPECT_EQ(r.payload[4], 2) << "GOT_IP";
    EXPECT_EQ(r.payload[7], 0x01) << "caps: CRC";
    EXPECT_EQ(r.payload[8], 10);
    EXPECT_EQ(r.payload[11], 15) << "the first lease of the virtual network";
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(&r.payload[18])), "UnrealNG");
}

TEST_F(EspnetModule_Test, Esp8266HasFourSockets)
{
    Make(EspModule::Chip::Esp8266, {});
    Request(EspnetModule::kInfo, 0xFF, 0);
    const Reply r = Expect(EspnetModule::kInfo);
    EXPECT_EQ(r.payload[2], 86);
    EXPECT_EQ(r.payload[3], 4);
    for (int i = 0; i < 4; ++i)
        OpenTcp();
    Request(EspnetModule::kSocket, 0xFF, 1, {2});
    EXPECT_EQ(Expect(EspnetModule::kSocket).status, EspnetModule::kNfile);
}

TEST_F(EspnetModule_Test, TheReplyComesAfterTheTurnaround)
{
    Request(EspnetModule::kEcho, 3, 0, {1, 2, 3});
    EXPECT_FALSE(_esp->HasByte()) << "nothing before the module had time to answer";
    const Reply r = Expect(EspnetModule::kEcho);
    EXPECT_EQ(r.sock, 3);
    EXPECT_EQ(r.result, 3);
    EXPECT_EQ(r.payload, (std::vector<uint8_t>{1, 2, 3}));
}

TEST_F(EspnetModule_Test, TcpClientLikeTheKernel)
{
    const uint8_t s = OpenTcp();
    EXPECT_EQ(s, 0);

    // CONNECT waits for the network
    Request(EspnetModule::kConnect, s, 0, Sockaddr(NetIp(93, 184, 216, 34), 80));
    Reply r;
    EXPECT_FALSE(Next(r)) << "no reply until the connection is up";
    const FakeHostNet::Command connect = *_host->Last("connect");
    EXPECT_EQ(connect.endpoint.addr, NetIp(93, 184, 216, 34));
    EXPECT_EQ(connect.endpoint.port, 80);
    _host->Push(NetEventType::Connected, connect.socket);
    _net->Pump();
    r = Expect(EspnetModule::kConnect);
    EXPECT_EQ(r.status, 0);

    const std::string get = "GET / HTTP/1.0\r\n\r\n";
    Request(EspnetModule::kWrite, s, 0, std::vector<uint8_t>(get.begin(), get.end()));
    r = Expect(EspnetModule::kWrite);
    EXPECT_EQ(r.status, 0);
    EXPECT_EQ(r.result, get.size());
    EXPECT_EQ(std::string(_host->Last("send")->data.begin(), _host->Last("send")->data.end()), get);

    Request(EspnetModule::kRead, s, 0, {0xC0, 0x00});
    EXPECT_EQ(Expect(EspnetModule::kRead).status, EspnetModule::kAgain) << "nothing yet";

    std::vector<uint8_t> page(300, 'x');
    _host->Push(NetEventType::Data, connect.socket, NetEventStatus::Ok, {}, page);
    _host->Push(NetEventType::PeerClosed, connect.socket);
    _net->Pump();
    Request(EspnetModule::kRead, s, 0, {0xC0, 0x00});   // 192
    r = Expect(EspnetModule::kRead);
    EXPECT_EQ(r.status, 0);
    EXPECT_EQ(r.result, 192);
    EXPECT_EQ(r.payload.size(), 192u);
    Request(EspnetModule::kRead, s, 0, {0xC0, 0x00});
    EXPECT_EQ(Expect(EspnetModule::kRead).result, 108) << "the bytes after FIN are still read";
    Request(EspnetModule::kRead, s, 0, {0xC0, 0x00});
    EXPECT_EQ(Expect(EspnetModule::kRead).status, EspnetModule::kNotConn) << "then NOTCONN";

    Request(EspnetModule::kShutdown, s, 0);
    EXPECT_EQ(Expect(EspnetModule::kShutdown).status, 0);
    Request(EspnetModule::kRead, s, 0, {0xC0, 0x00});
    EXPECT_EQ(Expect(EspnetModule::kRead).status, EspnetModule::kNotSock);
}

TEST_F(EspnetModule_Test, AFailedConnectIsHostUnreachable)
{
    const uint8_t s = OpenTcp();
    Request(EspnetModule::kConnect, s, 0, Sockaddr(NetIp(93, 184, 216, 34), 81));
    _host->Push(NetEventType::ConnectFailed, _host->Last("connect")->socket, NetEventStatus::Refused);
    _net->Pump();
    EXPECT_EQ(Expect(EspnetModule::kConnect).status, EspnetModule::kHostUnreach);
}

TEST_F(EspnetModule_Test, ABusyModuleAnswersInOrder)
{
    const uint8_t s = OpenTcp();
    Request(EspnetModule::kConnect, s, 0, Sockaddr(NetIp(93, 184, 216, 34), 80));
    const uint8_t connectSeq = _seq;
    Request(EspnetModule::kInfo, 0xFF, 0);   // arrives while CONNECT waits
    Reply r;
    EXPECT_FALSE(Next(r));
    _host->Push(NetEventType::Connected, _host->Last("connect")->socket);
    _net->Pump();
    ASSERT_TRUE(Next(r));
    EXPECT_EQ(r.cmd, EspnetModule::kConnect);
    EXPECT_EQ(r.seq, connectSeq);
    ASSERT_TRUE(Next(r));
    EXPECT_EQ(r.cmd, EspnetModule::kInfo);
}

TEST_F(EspnetModule_Test, UdpDnsQueryLikeTheKernel)
{
    Request(EspnetModule::kSocket, 0xFF, 3, {2});
    const uint8_t s = Expect(EspnetModule::kSocket).sock;
    const std::vector<uint8_t> query = dns::BuildQuery(0x1234, "example.com");
    std::vector<uint8_t> payload = Sockaddr(NetIp(8, 8, 4, 4), 53);
    payload.insert(payload.end(), query.begin(), query.end());
    Request(EspnetModule::kWrite, s, 0, payload);
    Reply r = Expect(EspnetModule::kWrite);
    EXPECT_EQ(r.result, query.size());

    Request(EspnetModule::kRead, s, 0, {0x00, 0x02});
    EXPECT_EQ(Expect(EspnetModule::kRead).status, EspnetModule::kAgain);

    const FakeHostNet::Command dnsCmd = *_host->Last("dns");
    dns::Question q;
    ASSERT_TRUE(dns::ParseQuery(dnsCmd.data.data(), dnsCmd.data.size(), q));
    const std::vector<uint8_t> answer =
        dns::BuildAnswer(dnsCmd.data.data(), dnsCmd.data.size(), q, {NetIp(93, 184, 216, 34)}, dns::kRcodeNoError);
    _host->Push(NetEventType::Datagram, dnsCmd.socket, NetEventStatus::Ok, dnsCmd.endpoint, answer);
    _net->Pump();
    Request(EspnetModule::kRead, s, 0, {0x00, 0x02});
    r = Expect(EspnetModule::kRead);
    EXPECT_EQ(r.status, 0);
    EXPECT_EQ(r.result, answer.size());
    ASSERT_EQ(r.payload.size(), 15 + answer.size());
    EXPECT_EQ(r.payload[0], 2);
    EXPECT_EQ((r.payload[1] << 8) | r.payload[2], 53) << "the source port, big-endian";
    EXPECT_EQ(r.payload[3], 8);
}

TEST_F(EspnetModule_Test, DnsResolveOnTheModule)
{
    VirtualNetworkConfig config;
    config.hosts["next.zxart.ee"] = NetIp(217, 146, 69, 13);
    Make(EspModule::Chip::Esp32, config);
    const std::string name = "next.zxart.ee";
    Request(EspnetModule::kDnsResolve, 0xFF, 0, std::vector<uint8_t>(name.begin(), name.end()));
    _net->Pump();   // the hosts table answers through the virtual network
    Reply r = Expect(EspnetModule::kDnsResolve);
    EXPECT_EQ(r.status, 0);
    EXPECT_EQ(r.payload, (std::vector<uint8_t>{217, 146, 69, 13}));

    const std::string numeric = "10.0.2.2";
    Request(EspnetModule::kDnsResolve, 0xFF, 0, std::vector<uint8_t>(numeric.begin(), numeric.end()));
    EXPECT_EQ(Expect(EspnetModule::kDnsResolve).payload, (std::vector<uint8_t>{10, 0, 2, 2}));

    const std::string unknown = "no-such-host.example";
    Request(EspnetModule::kDnsResolve, 0xFF, 0, std::vector<uint8_t>(unknown.begin(), unknown.end()));
    const FakeHostNet::Command dnsCmd = *_host->Last("dns");
    dns::Question q;
    ASSERT_TRUE(dns::ParseQuery(dnsCmd.data.data(), dnsCmd.data.size(), q));
    _host->Push(NetEventType::Datagram, dnsCmd.socket, NetEventStatus::Ok, dnsCmd.endpoint,
                dns::BuildAnswer(dnsCmd.data.data(), dnsCmd.data.size(), q, {}, dns::kRcodeNxDomain));
    _net->Pump();
    EXPECT_EQ(Expect(EspnetModule::kDnsResolve).status, EspnetModule::kHostUnreach);

    Request(EspnetModule::kGetDns, 0xFF, 0);
    EXPECT_EQ(Expect(EspnetModule::kGetDns).payload, (std::vector<uint8_t>{10, 0, 2, 3}));
}

TEST_F(EspnetModule_Test, ServerAcceptsQueuedClientsLike3ws)
{
    const uint8_t l = OpenTcp();
    Request(EspnetModule::kListen, l, 0);
    EXPECT_EQ(Expect(EspnetModule::kListen).status, EspnetModule::kNotSock) << "LISTEN needs a bound port";
    Request(EspnetModule::kBind, l, 0, Sockaddr(0, 4444));
    EXPECT_EQ(Expect(EspnetModule::kBind).status, 0);
    Request(EspnetModule::kListen, l, 0);
    EXPECT_EQ(Expect(EspnetModule::kListen).status, 0);
    const FakeHostNet::Command* listen = _host->Last("listen");
    ASSERT_NE(listen, nullptr);
    EXPECT_EQ(listen->endpoint.port, 4444);

    Request(EspnetModule::kAccept, l, 0);
    EXPECT_EQ(Expect(EspnetModule::kAccept).status, EspnetModule::kAgain) << "no client yet";

    _host->Push(NetEventType::Accepted, listen->socket, NetEventStatus::Ok, {NetIp(127, 0, 0, 1), 50000}, {0x00, 0x80});
    _host->Push(NetEventType::Data, 0x8000, NetEventStatus::Ok, {}, {'G', 'E', 'T'});
    _net->Pump();
    _esp->OnFrame();   // the server is armed again for the next client

    Request(EspnetModule::kAccept, l, 0);
    Reply r = Expect(EspnetModule::kAccept);
    EXPECT_EQ(r.status, 0);
    EXPECT_EQ(r.sock, 1) << "the client in a new slot; the server keeps listening";
    Request(EspnetModule::kRead, r.sock, 0, {0x00, 0x08});
    const Reply data = Expect(EspnetModule::kRead);
    EXPECT_EQ(data.payload, (std::vector<uint8_t>{'G', 'E', 'T'})) << "what the client sent before ACCEPT";
}

TEST_F(EspnetModule_Test, ErrorsAsTheFirmwareReportsThem)
{
    Request(0x33, 5, 0);
    Reply r = Expect(0x33);
    EXPECT_EQ(r.status, EspnetModule::kProtoType) << "unknown command";
    EXPECT_EQ(r.sock, 5);
    Request(EspnetModule::kSocket, 0xFF, 2, {2});
    EXPECT_EQ(Expect(EspnetModule::kSocket).status, EspnetModule::kProtoType) << "no ICMP";
    Request(EspnetModule::kSocket, 0xFF, 1, {10});
    EXPECT_EQ(Expect(EspnetModule::kSocket).status, EspnetModule::kAfNoSupport);
    Request(EspnetModule::kRead, 6, 0, {0x10, 0});
    EXPECT_EQ(Expect(EspnetModule::kRead).status, EspnetModule::kNotSock);
    const uint8_t s = OpenTcp();
    Request(EspnetModule::kWrite, s, 0, {'x'});
    EXPECT_EQ(Expect(EspnetModule::kWrite).status, EspnetModule::kNotConn);
    Request(EspnetModule::kConnect, s, 0, {1, 2, 3});
    EXPECT_EQ(Expect(EspnetModule::kConnect).status, EspnetModule::kIntr) << "a short sockaddr";
}

TEST_F(EspnetModule_Test, GarbageBeforeAFrameIsIgnoredAndCrcFramesAreChecked)
{
    for (uint8_t b : {0x00, 0xFF, 0x13})
        _esp->Transmit(b);
    Request(EspnetModule::kEcho, 1, 0, {9});
    EXPECT_EQ(Expect(EspnetModule::kEcho).payload, (std::vector<uint8_t>{9}));

    // CRC frame: cmd | #80, XOR of header and payload after it
    std::vector<uint8_t> f = {0xA5, static_cast<uint8_t>(EspnetModule::kEcho | 0x80), 1, 0, ++_seq, 1, 0, 0x42};
    uint8_t x = 0;
    for (size_t i = 1; i < f.size(); ++i)
        x = static_cast<uint8_t>(x ^ f[i]);
    f.push_back(x);
    for (uint8_t b : f)
        _esp->Transmit(b);
    Reply r;
    ASSERT_TRUE(Next(r));
    EXPECT_EQ(r.cmd, EspnetModule::kEcho | 0x80);
    EXPECT_EQ(r.payload, (std::vector<uint8_t>{0x42}));
    _esp->TakeByte();   // the reply's CRC byte

    f[4] = ++_seq;   // the same frame with the next seq and a wrong CRC: dropped, no reply
    uint8_t good = 0;
    for (size_t i = 1; i + 1 < f.size(); ++i)
        good = static_cast<uint8_t>(good ^ f[i]);
    f.back() = static_cast<uint8_t>(good ^ 0x5A);
    for (uint8_t b : f)
        _esp->Transmit(b);
    EXPECT_FALSE(Next(r));
}

TEST_F(EspnetModule_Test, WifiDisconnectStatusAndJoin)
{
    Request(EspnetModule::kWifiStatus, 0xFF, 0);
    Reply r = Expect(EspnetModule::kWifiStatus);
    ASSERT_EQ(r.payload.size(), 45u);
    EXPECT_EQ(r.payload[44], 0x03) << "CONNECTED | HASIP";

    Request(EspnetModule::kWifiDisc, 0xFF, 0);
    EXPECT_EQ(Expect(EspnetModule::kWifiDisc).status, 0);
    Request(EspnetModule::kWifiStatus, 0xFF, 0);
    EXPECT_EQ(Expect(EspnetModule::kWifiStatus).payload[44], 0x00);

    std::vector<uint8_t> join(98, 0);
    const std::string ssid = "UnrealNG";
    std::copy(ssid.begin(), ssid.end(), join.begin());
    Request(EspnetModule::kWifiConnect, 0xFF, 0, join);
    EXPECT_EQ(Expect(EspnetModule::kWifiConnect).status, 0) << "acknowledged at once";
    Request(EspnetModule::kInfo, 0xFF, 0);
    EXPECT_EQ(Expect(EspnetModule::kInfo).payload[4], 1) << "CONNECTING";
    _now += EspModule::kJoinUs * 35 / 10;
    Request(EspnetModule::kInfo, 0xFF, 0);
    EXPECT_EQ(Expect(EspnetModule::kInfo).payload[4], 2) << "GOT_IP after the join time";

    Request(EspnetModule::kWifiScan, 0xFF, 0);
    Reply scan;
    ASSERT_TRUE(Next(scan, 2000000));
    EXPECT_EQ(scan.result, 1);
    ASSERT_EQ(scan.payload.size(), 42u);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(scan.payload.data())), "UnrealNG");
}

TEST_F(EspnetModule_Test, UartSetSwitchesTheModuleAfterTheReply)
{
    Request(EspnetModule::kUart, 0xFF, 0);
    Reply r = Expect(EspnetModule::kUart);
    ASSERT_EQ(r.payload.size(), 8u);
    EXPECT_EQ(r.payload[0] | (r.payload[1] << 8) | (r.payload[2] << 16), 115200);

    Request(EspnetModule::kUart, 0xFF, 1, {0x00, 0x96, 0x00, 0x00, 0, 0, 0, 0});   // 38400
    r = Expect(EspnetModule::kUart);
    EXPECT_EQ(r.status, 0);
    _now += 100000 * 35 / 10;
    Request(EspnetModule::kEcho, 0, 0, {1});
    EXPECT_FALSE(Next(r)) << "the module runs at 38400 now, the ZX still at 115200";
    EXPECT_TRUE(_esp->LineMismatch());
    SerialLine line;
    line.baud = 38400;
    _esp->OnLineSettings(line);
    Request(EspnetModule::kEcho, 0, 0, {1});
    EXPECT_EQ(Expect(EspnetModule::kEcho).payload, (std::vector<uint8_t>{1}));

    Request(EspnetModule::kUart, 0xFF, 1, {0x10, 0x27, 0, 0, 0, 0, 0, 0});   // 10000: not a rate it takes
    EXPECT_EQ(Expect(EspnetModule::kUart).status, EspnetModule::kMsgSize);
}

TEST_F(EspnetModule_Test, StateRoundTripContinuesTheSameWay)
{
    const uint8_t s = OpenTcp();
    Request(EspnetModule::kConnect, s, 0, Sockaddr(NetIp(93, 184, 216, 34), 80));
    const uint16_t vnet = _host->Last("connect")->socket;
    _host->Push(NetEventType::Connected, vnet);
    _host->Push(NetEventType::Data, vnet, NetEventStatus::Ok, {}, {'a', 'b', 'c'});
    _net->Pump();
    Expect(EspnetModule::kConnect);

    // Bytes the journal would hold: a TTD byte source over a copy
    const std::vector<uint8_t> journal = {'a', 'b', 'c'};
    auto state = std::make_unique<netstate::EspModuleState>();
    ASSERT_FALSE(_esp->SaveState(*state)) << "the bytes did not come from a journal: references are incomplete";

    // Restore into a second module and compare what READ gives
    auto copy = std::make_unique<EspnetModule>(_net.get(), EspModule::Chip::Esp32);
    copy->SetClock([this]() { return _now; }, 3500000);
    copy->OnLineSettings(SerialLine());
    netstate::EspModuleState& st = *state;
    st.stack.slots[s].rxRuns = 1;
    st.stack.slots[s].rx[0] = {1, 0, 3};
    ASSERT_TRUE(copy->LoadState(st, [&](uint32_t, uint32_t offset, uint32_t length, std::vector<uint8_t>& out) {
        out.assign(journal.begin() + offset, journal.begin() + offset + length);
        return true;
    }));
    EXPECT_EQ(copy->Stack().GetSlot(s).state, EspStack::State::Tcp);
    EXPECT_EQ(copy->Stack().GetSlot(s).rx.size(), 3u);
    EXPECT_EQ(copy->Ip(), _esp->Ip());
}
