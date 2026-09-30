// W5300 chip model over the virtual network (network adapters TDD §4.2,
// reference-w5300-model.md). The host is a FakeHostNet: commands are
// recorded, host answers injected, no sockets, no threads.

#include <gtest/gtest.h>

#include <cstring>
#include <memory>

#include "_helpers/fakehostnet.h"
#include "common/network/dnsmessage.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/network/w5300.h"

namespace
{
constexpr uint16_t kSocketBase = 0x200;
constexpr uint16_t kMr = 0x01, kCr = 0x03, kIr = 0x07, kSsr = 0x09, kPortr = 0x0A, kDportr = 0x12, kDipr = 0x14;
constexpr uint16_t kProtor = 0x1B, kWrsr = 0x22, kFsr = 0x26, kRsr = 0x2A, kTx0 = 0x2E, kTx1 = 0x2F, kRx0 = 0x30,
                   kRx1 = 0x31;
}  // namespace

class W5300_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        auto host = std::make_unique<FakeHostNet>();
        _host = host.get();
        VirtualNetworkConfig config;
        config.hosts["next.zxart.ee"] = NetIp(10, 0, 2, 50);
        _net = std::make_unique<VirtualNetwork>(nullptr, std::move(host), config);
        _chip = std::make_unique<W5300>(_net.get());
    }

    void TearDown() override
    {
        _chip.reset();
        _net.reset();
    }

    uint16_t Reg(int n, uint16_t offset) const { return static_cast<uint16_t>(kSocketBase + 0x40 * n + offset); }
    uint8_t R(int n, uint16_t offset) { return _chip->Read(Reg(n, offset)); }
    void W(int n, uint16_t offset, uint8_t v) { _chip->Write(Reg(n, offset), v); }
    uint16_t R16(int n, uint16_t offset) { return static_cast<uint16_t>((R(n, offset) << 8) | R(n, offset + 1)); }

    void Cmd(int n, uint8_t cmd)
    {
        W(n, kCr, cmd);
        ASSERT_EQ(R(n, kCr), 0) << "Sn_CR must read 0 once the command was taken";
    }

    void SetDest(int n, uint32_t ip, uint16_t port)
    {
        W(n, kDipr, static_cast<uint8_t>(ip >> 24));
        W(n, kDipr + 1, static_cast<uint8_t>(ip >> 16));
        W(n, kDipr + 2, static_cast<uint8_t>(ip >> 8));
        W(n, kDipr + 3, static_cast<uint8_t>(ip));
        W(n, kDportr, static_cast<uint8_t>(port >> 8));
        W(n, kDportr + 1, static_cast<uint8_t>(port));
    }

    void OpenSocket(int n, uint8_t mode, uint16_t port = 0xC001)
    {
        W(n, kMr, mode);
        W(n, kPortr, static_cast<uint8_t>(port >> 8));
        W(n, kPortr + 1, static_cast<uint8_t>(port));
        Cmd(n, W5300::kCmdOpen);
    }

    // The NedoOS way: pairs through FIFOR0 / FIFOR1, then WRSR, then SEND
    void SendBytes(int n, const std::vector<uint8_t>& data)
    {
        for (size_t i = 0; i < data.size(); i += 2)
        {
            W(n, kTx0, data[i]);
            W(n, kTx1, i + 1 < data.size() ? data[i + 1] : 0);
        }
        W(n, kWrsr, static_cast<uint8_t>(data.size() >> 8));
        W(n, kWrsr + 1, static_cast<uint8_t>(data.size()));
        Cmd(n, W5300::kCmdSend);
    }

    std::vector<uint8_t> ReadWords(int n, size_t bytes)
    {
        std::vector<uint8_t> out;
        for (size_t i = 0; i < bytes; i += 2)
        {
            out.push_back(R(n, kRx0));
            out.push_back(R(n, kRx1));
        }
        return out;
    }

    FakeHostNet* _host = nullptr;
    std::unique_ptr<VirtualNetwork> _net;
    std::unique_ptr<W5300> _chip;
};

TEST_F(W5300_Test, ResetValuesMatchTheDatasheet)
{
    EXPECT_EQ(_chip->Read(0x000), 0x38);  // MR = #3800
    EXPECT_EQ(_chip->Read(0x001), 0x00);
    EXPECT_EQ(_chip->Read(0x0FE), 0x53);  // IDR = #5300
    EXPECT_EQ(_chip->Read(0x0FF), 0x00);
    EXPECT_EQ(_chip->Read(0x01C), 0x07);  // RTR = #07D0
    EXPECT_EQ(_chip->Read(0x01D), 0xD0);
    EXPECT_EQ(_chip->Read(0x01F), 0x08);  // RCR
    EXPECT_EQ(_chip->Read(0x020), 8);     // TMSR0 = 8 KB
    for (int n = 0; n < W5300::kSockets; ++n)
    {
        EXPECT_EQ(R(n, kSsr), W5300::kSockClosed);
        EXPECT_EQ(R(n, 0x05), 0xFF);       // Sn_IMR
        EXPECT_EQ(R16(n, kFsr), 0x2000);   // 8 KB free
        EXPECT_EQ(R(n, 0x1F), 0x80);       // Sn_TTLR
    }
}

TEST_F(W5300_Test, CommonRegistersStoreWhatWizcfgWrites)
{
    // wizcfg writes MAC, then probes SUBR0 with #AA: a real chip keeps it
    const uint8_t mac[6] = {0x02, 0x02, 0x6A, 0x6A, 0x3B, 0x3B};
    for (int i = 0; i < 6; ++i)
        _chip->Write(static_cast<uint16_t>(0x008 + i), mac[i]);
    _chip->Write(0x014, 0xAA);
    EXPECT_EQ(_chip->Read(0x014), 0xAA);
    for (int i = 0; i < 6; ++i)
        EXPECT_EQ(_chip->Read(static_cast<uint16_t>(0x008 + i)), mac[i]);
    _chip->Write(0x0FE, 0x00);
    EXPECT_EQ(_chip->Read(0x0FE), 0x53) << "IDR is read-only";
}

TEST_F(W5300_Test, SoftwareResetRestoresEverything)
{
    _chip->Write(0x014, 0xAA);
    OpenSocket(0, W5300::kModeUdp);
    ASSERT_EQ(R(0, kSsr), W5300::kSockUdp);
    _chip->Write(0x001, 0x80);  // MR.RST
    EXPECT_EQ(_chip->Read(0x001) & 0x80, 0) << "RST clears itself";
    EXPECT_EQ(_chip->Read(0x014), 0x00);
    EXPECT_EQ(R(0, kSsr), W5300::kSockClosed);
}

TEST_F(W5300_Test, CommandRegisterClearsEvenForInvalidCommands)
{
    // A driver spins on Sn_CR: it must clear whatever was written, in any state
    for (uint8_t cmd : {W5300::kCmdConnect, W5300::kCmdSend, W5300::kCmdRecv, uint8_t{0x77}, W5300::kCmdDiscon})
    {
        W(3, kCr, cmd);
        EXPECT_EQ(R(3, kCr), 0) << "command #" << std::hex << int(cmd);
    }
}

TEST_F(W5300_Test, OpenWithModeZeroStaysClosed)
{
    W(1, kMr, W5300::kModeClosed);
    Cmd(1, W5300::kCmdOpen);
    EXPECT_EQ(R(1, kSsr), W5300::kSockClosed);
}

TEST_F(W5300_Test, TcpConnectGoesThroughSynSentToEstablished)
{
    OpenSocket(0, W5300::kModeTcp);
    EXPECT_EQ(R(0, kSsr), W5300::kSockInit);

    SetDest(0, NetIp(93, 184, 216, 34), 80);
    Cmd(0, W5300::kCmdConnect);
    EXPECT_EQ(R(0, kSsr), W5300::kSockSynSent) << "non-zero while the host connects";

    const FakeHostNet::Command* connect = _host->Last("connect");
    ASSERT_NE(connect, nullptr);
    EXPECT_EQ(connect->endpoint.addr, NetIp(93, 184, 216, 34));
    EXPECT_EQ(connect->endpoint.port, 80);

    _host->Push(NetEventType::Connected, connect->socket);
    _net->Pump();
    EXPECT_EQ(R(0, kSsr), W5300::kSockEstablished);
    EXPECT_EQ(R(0, kIr) & W5300::kIrCon, W5300::kIrCon);
}

TEST_F(W5300_Test, RefusedConnectClosesWithoutInterruptBit)
{
    OpenSocket(0, W5300::kModeTcp);
    SetDest(0, NetIp(1, 2, 3, 4), 23);
    Cmd(0, W5300::kCmdConnect);
    _host->Push(NetEventType::ConnectFailed, _host->Last("connect")->socket, NetEventStatus::Refused);
    _net->Pump();
    EXPECT_EQ(R(0, kSsr), W5300::kSockClosed);
    EXPECT_EQ(R(0, kIr) & W5300::kIrTimeout, 0);
}

TEST_F(W5300_Test, TimedOutConnectSetsTimeout)
{
    OpenSocket(0, W5300::kModeTcp);
    SetDest(0, NetIp(1, 2, 3, 4), 23);
    Cmd(0, W5300::kCmdConnect);
    _host->Push(NetEventType::ConnectFailed, _host->Last("connect")->socket, NetEventStatus::Timeout);
    _net->Pump();
    EXPECT_EQ(R(0, kSsr), W5300::kSockClosed);
    EXPECT_EQ(R(0, kIr) & W5300::kIrTimeout, W5300::kIrTimeout);
}

TEST_F(W5300_Test, ConnectToTheGatewayIsRefused)
{
    OpenSocket(0, W5300::kModeTcp);
    SetDest(0, NetIp(10, 0, 2, 2), 80);
    Cmd(0, W5300::kCmdConnect);
    EXPECT_EQ(_host->Last("connect"), nullptr) << "the virtual network answers for its own addresses";
    _net->Pump();
    EXPECT_EQ(R(0, kSsr), W5300::kSockClosed);
}

TEST_F(W5300_Test, TcpSendWritesPairsAndDropsThePadByte)
{
    OpenSocket(0, W5300::kModeTcp);
    SetDest(0, NetIp(1, 2, 3, 4), 80);
    Cmd(0, W5300::kCmdConnect);
    _host->Push(NetEventType::Connected, _host->Last("connect")->socket);
    _net->Pump();

    const std::vector<uint8_t> request = {'G', 'E', 'T', ' ', '/', '\r', '\n'};  // odd length
    SendBytes(0, request);
    const FakeHostNet::Command* send = _host->Last("send");
    ASSERT_NE(send, nullptr);
    EXPECT_EQ(send->data, request);
    EXPECT_EQ(R(0, kIr) & W5300::kIrSendOk, W5300::kIrSendOk);
    EXPECT_EQ(R16(0, kFsr), 0x2000) << "TX memory free again after SEND (NedoOS close checks #20xx)";

    // Back-to-back SEND without clearing SENDOK (NedoOS never clears it)
    SendBytes(0, {'x', 'y'});
    EXPECT_EQ(_host->Last("send")->data, (std::vector<uint8_t>{'x', 'y'}));
}

TEST_F(W5300_Test, FreeSpaceDropsPerWordWritten)
{
    OpenSocket(0, W5300::kModeTcp);
    W(0, kTx0, 1);
    W(0, kTx1, 2);
    W(0, kTx0, 3);
    W(0, kTx1, 4);
    EXPECT_EQ(R16(0, kFsr), 0x2000 - 4);
}

TEST_F(W5300_Test, TcpDataArrivesAsPacketsWithSizeHeader)
{
    OpenSocket(0, W5300::kModeTcp);
    SetDest(0, NetIp(1, 2, 3, 4), 80);
    Cmd(0, W5300::kCmdConnect);
    const uint16_t id = _host->Last("connect")->socket;
    _host->Push(NetEventType::Connected, id);
    _host->Push(NetEventType::Data, id, NetEventStatus::Ok, {}, {'H', 'T', 'T', 'P', '/'});  // 5 bytes: odd
    _net->Pump();

    EXPECT_EQ(R(0, kIr) & W5300::kIrRecv, W5300::kIrRecv);
    EXPECT_EQ(R16(0, kRsr), 2 + 5 + 1) << "size header + data + pad";
    std::vector<uint8_t> packet = ReadWords(0, 8);
    EXPECT_EQ(packet, (std::vector<uint8_t>{0x00, 0x05, 'H', 'T', 'T', 'P', '/', 0x00}));
    EXPECT_EQ(R16(0, kRsr), 0);
    Cmd(0, W5300::kCmdRecv);
}

TEST_F(W5300_Test, LargeTcpDataIsSplitAtTheMssAndWaitsForRoom)
{
    OpenSocket(0, W5300::kModeTcp);
    SetDest(0, NetIp(1, 2, 3, 4), 80);
    Cmd(0, W5300::kCmdConnect);
    const uint16_t id = _host->Last("connect")->socket;
    _host->Push(NetEventType::Connected, id);
    _host->Push(NetEventType::Data, id, NetEventStatus::Ok, {}, std::vector<uint8_t>(20000, 0x5A));
    _net->Pump();

    // 8 KB of receive memory: five full 1460-byte packets fit (5 * 1462 = 7310), the rest waits
    const uint32_t rsr = R16(0, kRsr);
    EXPECT_LE(rsr, 8192u);
    EXPECT_EQ(R(0, kRx0), 0x05);  // first packet: 1460 = #05B4
    EXPECT_EQ(R(0, kRx1), 0xB4);
    EXPECT_GT(_chip->GetSocket(0).tcpBacklog, 0u);

    // Read everything, RECV after each packet: all 20000 bytes come through
    size_t total = 0;
    size_t size = 1460;
    for (;;)
    {
        ReadWords(0, size + (size & 1));
        total += size;
        Cmd(0, W5300::kCmdRecv);
        if (R16(0, kRsr) == 0)
            break;
        size = static_cast<size_t>((R(0, kRx0) << 8) | R(0, kRx1));
    }
    EXPECT_EQ(total, 20000u);
    EXPECT_EQ(_chip->GetSocket(0).tcpBacklog, 0u);
}

TEST_F(W5300_Test, FifoPairSurvivesOtherRegisterAccessesInBetween)
{
    // NedoOS reads FIFOR0, returns to the program, touches Sn_MR and the card,
    // then reads FIFOR1 on the next kernel call
    OpenSocket(0, W5300::kModeTcp);
    SetDest(0, NetIp(1, 2, 3, 4), 80);
    Cmd(0, W5300::kCmdConnect);
    const uint16_t id = _host->Last("connect")->socket;
    _host->Push(NetEventType::Connected, id);
    _host->Push(NetEventType::Data, id, NetEventStatus::Ok, {}, {'a', 'b', 'c', 'd'});
    _net->Pump();

    ReadWords(0, 2);                     // size header
    EXPECT_EQ(R(0, kRx0), 'a');
    EXPECT_EQ(R(0, kMr), W5300::kModeTcp);
    EXPECT_EQ(R(0, kSsr), W5300::kSockEstablished);
    EXPECT_EQ(R(0, kRx1), 'b');
    EXPECT_EQ(R(0, kRx0), 'c');
    EXPECT_EQ(R(0, kRx1), 'd');
}

TEST_F(W5300_Test, PeerCloseKeepsBufferedDataReadable)
{
    OpenSocket(0, W5300::kModeTcp);
    SetDest(0, NetIp(1, 2, 3, 4), 80);
    Cmd(0, W5300::kCmdConnect);
    const uint16_t id = _host->Last("connect")->socket;
    _host->Push(NetEventType::Connected, id);
    _host->Push(NetEventType::Data, id, NetEventStatus::Ok, {}, {'o', 'k'});
    _host->Push(NetEventType::PeerClosed, id);
    _net->Pump();
    EXPECT_EQ(R(0, kSsr), W5300::kSockCloseWait);
    EXPECT_EQ(R(0, kIr) & W5300::kIrDiscon, W5300::kIrDiscon);
    EXPECT_EQ(R16(0, kRsr), 4);

    Cmd(0, W5300::kCmdDiscon);
    EXPECT_EQ(R(0, kSsr), W5300::kSockClosed);
    EXPECT_NE(_host->Last("shutdown"), nullptr);
}

TEST_F(W5300_Test, UdpDnsQueryIsAnsweredFromTheHostsTableWithPacketInfo)
{
    OpenSocket(1, W5300::kModeUdp, 0xC002);
    SetDest(1, NetIp(8, 8, 4, 4), 53);  // the NedoOS kernel's DNS
    // DNS query: id #1122, RD, A next.zxart.ee
    std::vector<uint8_t> query = {0x11, 0x22, 0x01, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
    for (const char* label : {"next", "zxart", "ee"})
    {
        query.push_back(static_cast<uint8_t>(strlen(label)));
        query.insert(query.end(), label, label + strlen(label));
    }
    query.insert(query.end(), {0, 0, 1, 0, 1});
    ASSERT_EQ(query.size(), 31u) << "the zxdb query of the hang report";
    SendBytes(1, query);
    EXPECT_EQ(_host->Last("dns"), nullptr) << "pinned name: no host lookup";
    _net->Pump();

    // PACKET-INFO: sender 8.8.4.4:53, size
    const uint16_t rsr = R16(1, kRsr);
    ASSERT_GT(rsr, 8);
    std::vector<uint8_t> header = ReadWords(1, 8);
    EXPECT_EQ(header[0], 8);
    EXPECT_EQ(header[1], 8);
    EXPECT_EQ(header[2], 4);
    EXPECT_EQ(header[3], 4);
    EXPECT_EQ((header[4] << 8) | header[5], 53);
    const size_t size = static_cast<size_t>((header[6] << 8) | header[7]);
    std::vector<uint8_t> answer = ReadWords(1, size);
    answer.resize(size);
    EXPECT_EQ(answer[0], 0x11);
    EXPECT_EQ(answer[1], 0x22);
    EXPECT_EQ(answer[7], 1) << "one answer";
    EXPECT_EQ(answer[size - 4], 10);
    EXPECT_EQ(answer[size - 1], 50) << "10.0.2.50 from the hosts table";
    EXPECT_EQ(R16(1, kRsr), 0);
}

TEST_F(W5300_Test, UdpDnsQueryGoesToTheHostResolverAndTheAnswerComesBack)
{
    OpenSocket(1, W5300::kModeUdp);
    SetDest(1, NetIp(8, 8, 4, 4), 53);
    std::vector<uint8_t> query = {0xAB, 0xCD, 0x01, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0, 3, 'f', 'o', 'o', 0, 0, 1, 0, 1};
    SendBytes(1, query);
    const FakeHostNet::Command* dnsCmd = _host->Last("dns");
    ASSERT_NE(dnsCmd, nullptr);
    EXPECT_EQ(dnsCmd->data, query);
    _host->Push(NetEventType::Datagram, dnsCmd->socket, NetEventStatus::Ok, dnsCmd->endpoint, {0xAB, 0xCD, 0x81, 0x80, 0x00});
    _net->Pump();
    EXPECT_EQ(R16(1, kRsr), 8 + 5 + 1);
}

TEST_F(W5300_Test, DhcpExchangeLikeWizcfg)
{
    OpenSocket(2, W5300::kModeUdp, 68);
    SetDest(2, 0xFFFFFFFF, 67);
    std::vector<uint8_t> discover(240 + 4, 0);
    discover[0] = 1;
    discover[1] = 1;
    discover[2] = 6;
    discover[4] = 0xAC;  // xid
    discover[5] = 0xBA;
    discover[6] = 0xE7;
    discover[7] = 0x51;
    const uint8_t mac[6] = {0x02, 0x02, 0x6A, 0x6A, 0x3B, 0x3B};
    std::copy(mac, mac + 6, discover.begin() + 28);
    discover[236] = 99;
    discover[237] = 130;
    discover[238] = 83;
    discover[239] = 99;
    discover[240] = 53;
    discover[241] = 1;
    discover[242] = 1;  // DISCOVER
    discover[243] = 255;
    SendBytes(2, discover);
    EXPECT_EQ(_host->Last("udp"), nullptr) << "DHCP never leaves the virtual network";
    _net->Pump();

    std::vector<uint8_t> header = ReadWords(2, 8);
    EXPECT_EQ(header[0], 10);
    EXPECT_EQ(header[3], 2) << "from the gateway 10.0.2.2";
    EXPECT_EQ((header[4] << 8) | header[5], 67);
    const size_t size = static_cast<size_t>((header[6] << 8) | header[7]);
    std::vector<uint8_t> offer = ReadWords(2, size);
    offer.resize(size);
    EXPECT_EQ(offer[0], 2);
    EXPECT_EQ(offer[4], 0xAC) << "xid echoed";
    EXPECT_EQ(offer[16], 10);
    EXPECT_EQ(offer[19], 15) << "yiaddr 10.0.2.15";
    // Options: code + length only, no Pad before End (wizcfg's parser)
    size_t pos = 240;
    bool sawEnd = false;
    while (pos < offer.size())
    {
        const uint8_t code = offer[pos];
        if (code == 255)
        {
            sawEnd = true;
            break;
        }
        ASSERT_NE(code, 0) << "Pad option at " << pos;
        pos += 2 + offer[pos + 1];
    }
    EXPECT_TRUE(sawEnd);
}

TEST_F(W5300_Test, NedoOsUdpUnderReadLeavesTheFifoMisaligned)
{
    // nedoos-bugs.md B-1: buffer 64 (even), datagram 101 (odd, longer). The
    // kernel reads 32 words, skips floor(37 / 2) = 18 words, RECVs: one word
    // (the last data byte and the pad) stays. A faithful FIFO keeps it.
    OpenSocket(1, W5300::kModeUdp);
    SetDest(1, NetIp(1, 2, 3, 4), 7);
    SendBytes(1, {0});
    const uint16_t id = _host->Last("udp")->socket;
    _host->Push(NetEventType::Datagram, id, NetEventStatus::Ok, {NetIp(1, 2, 3, 4), 7}, std::vector<uint8_t>(101, 0x42));
    _net->Pump();
    ASSERT_EQ(R16(1, kRsr), 8 + 101 + 1);

    ReadWords(1, 8);           // header
    ReadWords(1, 64);          // 32 words to the user
    for (int i = 0; i < 18; ++i)
    {
        R(1, kRx0);            // the kernel's skip loop
        R(1, kRx1);
    }
    Cmd(1, W5300::kCmdRecv);
    EXPECT_EQ(R16(1, kRsr), 2) << "the word the kernel did not skip";
}

TEST_F(W5300_Test, PingOfTheGatewayGetsAnEchoReply)
{
    W(0, kProtor, 1);
    OpenSocket(0, W5300::kModeIpRaw);
    EXPECT_EQ(R(0, kSsr), W5300::kSockIpRaw);
    SetDest(0, NetIp(10, 0, 2, 2), 0);
    // echo request: type 8, code 0, checksum, id 1, seq 1, data "hi"
    std::vector<uint8_t> echo = {8, 0, 0, 0, 0, 1, 0, 1, 'h', 'i'};
    SendBytes(0, echo);
    _net->Pump();
    ASSERT_EQ(R16(0, kRsr), 6 + 10);
    std::vector<uint8_t> header = ReadWords(0, 6);
    EXPECT_EQ(header[3], 2);
    std::vector<uint8_t> reply = ReadWords(0, 10);
    EXPECT_EQ(reply[0], 0) << "echo reply";
    EXPECT_EQ(reply[8], 'h');
}

TEST_F(W5300_Test, ListenAcceptsAHostClient)
{
    OpenSocket(4, W5300::kModeTcp, 4444);
    Cmd(4, W5300::kCmdListen);
    EXPECT_EQ(R(4, kSsr), W5300::kSockListen);
    const FakeHostNet::Command* listen = _host->Last("listen");
    ASSERT_NE(listen, nullptr);
    EXPECT_EQ(listen->endpoint.port, 4444);

    _host->Push(NetEventType::Accepted, listen->socket, NetEventStatus::Ok, {NetIp(127, 0, 0, 1), 50000},
                {0x00, 0x80});   // client id #8000
    _net->Pump();
    EXPECT_EQ(R(4, kSsr), W5300::kSockEstablished);
    EXPECT_EQ(R(4, kDipr), 127);

    _host->Push(NetEventType::Data, 0x8000, NetEventStatus::Ok, {}, {'G', 'E'});
    _net->Pump();
    EXPECT_EQ(R16(4, kRsr), 4);
}

TEST_F(W5300_Test, CloseReleasesTheNetworkSocket)
{
    OpenSocket(0, W5300::kModeTcp);
    const uint16_t net = _chip->GetSocket(0).networkSocket;
    ASSERT_NE(net, 0);
    Cmd(0, W5300::kCmdClose);
    EXPECT_EQ(R(0, kSsr), W5300::kSockClosed);
    EXPECT_EQ(_chip->GetSocket(0).networkSocket, 0);
    EXPECT_TRUE(_net->Sockets().empty());
}

TEST_F(W5300_Test, LinkResetClosesConnections)
{
    OpenSocket(0, W5300::kModeTcp);
    SetDest(0, NetIp(1, 2, 3, 4), 80);
    Cmd(0, W5300::kCmdConnect);
    _host->Push(NetEventType::Connected, _host->Last("connect")->socket);
    _net->Pump();
    ASSERT_EQ(R(0, kSsr), W5300::kSockEstablished);
    _net->ApplyLinkReset();
    EXPECT_EQ(R(0, kSsr), W5300::kSockClosed);
    EXPECT_EQ(R(0, kIr) & W5300::kIrTimeout, W5300::kIrTimeout);
}

TEST_F(W5300_Test, PingOfAnExternalAddressGoesToTheHost)
{
    W(0, kProtor, 1);
    OpenSocket(0, W5300::kModeIpRaw);
    SetDest(0, NetIp(1, 1, 1, 1), 0);
    const std::vector<uint8_t> echo = {8, 0, 0, 0, 0, 7, 0, 3, 'z', 'x'};
    SendBytes(0, echo);
    const FakeHostNet::Command* ping = _host->Last("ping");
    ASSERT_NE(ping, nullptr);
    EXPECT_EQ(ping->endpoint.addr, NetIp(1, 1, 1, 1));
    EXPECT_EQ(ping->data, echo);

    std::vector<uint8_t> reply = echo;
    reply[0] = 0;
    _host->Push(NetEventType::EchoReply, ping->socket, NetEventStatus::Ok, {NetIp(1, 1, 1, 1), 0}, reply);
    _net->Pump();
    ASSERT_EQ(R16(0, kRsr), 6 + 10);
    std::vector<uint8_t> header = ReadWords(0, 6);
    EXPECT_EQ(header[0], 1);
    EXPECT_EQ(ReadWords(0, 10), reply);
}
