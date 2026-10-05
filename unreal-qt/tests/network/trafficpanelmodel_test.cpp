// The Network traffic window's Qt-free model (network #91 T4): the traffic report a real tap writes into rows, the
// filter, the decode of each protocol the window names, and the hex dump

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "emulator/io/network/traffic/networktraffictap.h"
#include "network/core/trafficpanelmodel.h"

namespace
{
const uint8_t kGuestMac[6] = {0x00, 0x20, 0xAF, 0x11, 0x22, 0x33};
const uint8_t kGatewayMac[6] = {0x52, 0x55, 0x0A, 0x00, 0x02, 0x02};

void Put16(std::vector<uint8_t>& f, size_t at, uint16_t v)
{
    f[at] = static_cast<uint8_t>(v >> 8);
    f[at + 1] = static_cast<uint8_t>(v);
}
void Put32(std::vector<uint8_t>& f, size_t at, uint32_t v)
{
    Put16(f, at, static_cast<uint16_t>(v >> 16));
    Put16(f, at + 2, static_cast<uint16_t>(v));
}

std::vector<uint8_t> Ethernet(uint16_t type, size_t payload)
{
    std::vector<uint8_t> f(14 + payload, 0);
    std::copy(kGatewayMac, kGatewayMac + 6, f.begin());
    std::copy(kGuestMac, kGuestMac + 6, f.begin() + 6);
    Put16(f, 12, type);
    return f;
}

/// An IPv4 packet from 10.0.2.15 to `dst` carrying `l4` bytes of protocol `proto`
std::vector<uint8_t> Ipv4(uint8_t proto, uint32_t dst, size_t l4)
{
    std::vector<uint8_t> f = Ethernet(0x0800, 20 + l4);
    f[14] = 0x45;
    Put16(f, 16, static_cast<uint16_t>(20 + l4));
    f[22] = 64;
    f[23] = proto;
    Put32(f, 26, NetIp(10, 0, 2, 15));
    Put32(f, 30, dst);
    return f;
}

std::vector<uint8_t> DnsQuery()
{
    const uint8_t question[] = {3, 'w', 'w', 'w', 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'o', 'r', 'g', 0, 0, 1, 0, 1};
    const size_t dns = 12 + sizeof(question);
    std::vector<uint8_t> f = Ipv4(17, NetIp(10, 0, 2, 3), 8 + dns);
    Put16(f, 34, 1025);
    Put16(f, 36, 53);
    Put16(f, 38, static_cast<uint16_t>(8 + dns));
    Put16(f, 42, 0x1234);
    Put16(f, 46, 1);
    std::copy(question, question + sizeof(question), f.begin() + 54);
    return f;
}

std::vector<uint8_t> DhcpDiscover()
{
    std::vector<uint8_t> f = Ipv4(17, 0xFFFFFFFFu, 8 + 244);
    Put16(f, 34, 68);
    Put16(f, 36, 67);
    Put16(f, 38, 8 + 244);
    f[42] = 1;                                                 // op: request
    std::copy(kGuestMac, kGuestMac + 6, f.begin() + 42 + 28);  // client MAC
    Put32(f, 42 + 236, 0x63825363);                            // magic cookie
    f[42 + 240] = 53;                                          // message type ...
    f[42 + 241] = 1;
    f[42 + 242] = 1;                                           // ... DISCOVER
    f[42 + 243] = 255;
    return f;
}

std::vector<uint8_t> TcpSyn()
{
    std::vector<uint8_t> f = Ipv4(6, NetIp(93, 184, 216, 34), 20);
    Put16(f, 34, 49152);
    Put16(f, 36, 80);
    Put32(f, 38, 1000);
    f[46] = 0x50;
    f[47] = 0x02;  // SYN
    Put16(f, 48, 8192);
    return f;
}

std::vector<uint8_t> ArpRequest()
{
    std::vector<uint8_t> f = Ethernet(0x0806, 28);
    Put16(f, 14, 1);
    Put16(f, 16, 0x0800);
    f[18] = 6;
    f[19] = 4;
    Put16(f, 20, 1);
    std::copy(kGuestMac, kGuestMac + 6, f.begin() + 22);
    Put32(f, 28, NetIp(10, 0, 2, 15));
    Put32(f, 38, NetIp(10, 0, 2, 2));
    return f;
}

/// The report every automation interface reads, written by a real tap
StateNode Report(NetworkTrafficTap& tap)
{
    StateNode report = StateNode::Object();
    StateNode& records = report["records"];
    records = StateNode::Array();
    for (const TrafficRecord& r : tap.Records({}))
        records.push(NetworkTrafficTap::RecordNode(r));
    return report;
}

bool Has(const std::vector<TrafficDecodeNode>& nodes, const std::string& text)
{
    for (const TrafficDecodeNode& n : nodes)
    {
        if (n.text.find(text) != std::string::npos || Has(n.children, text))
            return true;
    }
    return false;
}
}  // namespace

class TrafficPanelModel_Test : public ::testing::Test
{
protected:
    uint64_t _frame = 100;
    NetworkTrafficTap _tap{[this] { return TrafficTime{_frame, 7000, _frame * 20000}; }};
};

TEST_F(TrafficPanelModel_Test, RowsCarryTheTtdPositionAndTheBytes)
{
    const std::vector<uint8_t> arp = ArpRequest();
    _tap.Frame("isa2.eth", true, arp.data(), arp.size());
    _frame = 102;
    const uint8_t hello[] = {'h', 'i'};
    _tap.Socket("zxnetusb", true, "send", 3, NetProto::Tcp, NetEndpoint{NetIp(93, 184, 216, 34), 80}, 1025, hello, 2);

    const std::vector<TrafficRow> rows = TrafficRows(Report(_tap));
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].frame, 100u);
    EXPECT_EQ(rows[0].tInFrame, 7000u);
    EXPECT_TRUE(rows[0].frameKind);
    EXPECT_TRUE(rows[0].out);
    EXPECT_EQ(rows[0].adapter, "isa2.eth");
    EXPECT_EQ(rows[0].bytes, arp);
    EXPECT_FALSE(rows[1].frameKind);
    EXPECT_EQ(rows[1].op, "send");
    EXPECT_EQ(rows[1].socket, 3u);
    EXPECT_EQ(rows[1].bytes, std::vector<uint8_t>({'h', 'i'}));
    EXPECT_EQ(rows[1].index, rows[0].index + 1);

    EXPECT_EQ(TrafficTimeText(rows[0], 0), "f 100");
    EXPECT_EQ(TrafficTimeText(rows[1], rows[0].timeUs), "f 102  +40.00 ms");
}

TEST_F(TrafficPanelModel_Test, TheFilterNeedsEveryWordAndTheKind)
{
    const std::vector<uint8_t> arp = ArpRequest();
    _tap.Frame("isa2.eth", true, arp.data(), arp.size());
    _tap.Socket("zxnetusb", false, "data", 3, NetProto::Tcp, NetEndpoint{NetIp(93, 184, 216, 34), 80}, 1025, nullptr, 0);
    const std::vector<TrafficRow> rows = TrafficRows(Report(_tap));
    ASSERT_EQ(rows.size(), 2u);

    EXPECT_TRUE(TrafficRowMatches(rows[0], "", 0));
    EXPECT_TRUE(TrafficRowMatches(rows[0], "ISA2 out", 0));
    EXPECT_FALSE(TrafficRowMatches(rows[0], "isa2 in", 0));
    EXPECT_TRUE(TrafficRowMatches(rows[1], "zxnetusb data", 0));
    EXPECT_TRUE(TrafficRowMatches(rows[0], "", 1));
    EXPECT_FALSE(TrafficRowMatches(rows[0], "", 2));
    EXPECT_TRUE(TrafficRowMatches(rows[1], "", 2));
    EXPECT_FALSE(TrafficRowMatches(rows[1], "", 1));
}

TEST_F(TrafficPanelModel_Test, TheDecodeNamesEachLayer)
{
    for (const std::vector<uint8_t>& f : {ArpRequest(), DhcpDiscover(), DnsQuery(), TcpSyn()})
        _tap.Frame("isa2.eth", true, f.data(), f.size());
    const std::vector<TrafficRow> rows = TrafficRows(Report(_tap));
    ASSERT_EQ(rows.size(), 4u);

    const auto arp = TrafficDecode(rows[0]);
    EXPECT_TRUE(Has(arp, "TTD position: frame 100, 7000 units in"));
    EXPECT_TRUE(Has(arp, "source 00:20:AF:11:22:33"));
    EXPECT_TRUE(Has(arp, "ARP request"));
    EXPECT_TRUE(Has(arp, "target 10.0.2.2"));

    const auto dhcp = TrafficDecode(rows[1]);
    EXPECT_TRUE(Has(dhcp, "IPv4 10.0.2.15 > 255.255.255.255"));
    EXPECT_TRUE(Has(dhcp, "UDP 68 > 67"));
    EXPECT_TRUE(Has(dhcp, "message DISCOVER"));
    EXPECT_TRUE(Has(dhcp, "client MAC 00:20:AF:11:22:33"));

    const auto dns = TrafficDecode(rows[2]);
    EXPECT_TRUE(Has(dns, "query, id 4660"));
    EXPECT_TRUE(Has(dns, "question www.example.org"));

    const auto tcp = TrafficDecode(rows[3]);
    EXPECT_TRUE(Has(tcp, "TCP 49152 > 80 [SYN]"));
    EXPECT_TRUE(Has(tcp, "seq 1000"));
}

TEST_F(TrafficPanelModel_Test, ASocketOperationDecodesItsFields)
{
    _tap.Socket("isa1.esp", true, "connect", 2, NetProto::Tcp, NetEndpoint{NetIp(10, 0, 2, 3), 23}, 0, nullptr, 0);
    const std::vector<TrafficRow> rows = TrafficRows(Report(_tap));
    ASSERT_EQ(rows.size(), 1u);
    const auto d = TrafficDecode(rows[0]);
    EXPECT_TRUE(Has(d, "connect"));
    EXPECT_TRUE(Has(d, "socket 2"));
    EXPECT_TRUE(Has(d, "peer 10.0.2.3"));
}

TEST_F(TrafficPanelModel_Test, ATruncatedFrameDecodesWhatItHas)
{
    std::vector<uint8_t> f = TcpSyn();
    f.resize(40);  // cut inside the TCP header
    _tap.Frame("isa2.eth", false, f.data(), f.size());
    const std::vector<TrafficRow> rows = TrafficRows(Report(_tap));
    ASSERT_EQ(rows.size(), 1u);
    const auto d = TrafficDecode(rows[0]);
    EXPECT_TRUE(Has(d, "IPv4"));
    EXPECT_FALSE(Has(d, "TCP"));
}

TEST(TrafficPanelModelHex_Test, TheDumpHasOffsetsBytesAndText)
{
    std::vector<uint8_t> b;
    for (int i = 0; i < 18; ++i)
        b.push_back(static_cast<uint8_t>('A' + i));
    b[1] = 0x00;
    const std::string dump = TrafficHexDump(b);
    // The text column lines up with the full line above (column 56)
    EXPECT_EQ(dump, "0000  41 00 43 44 45 46 47 48  49 4A 4B 4C 4D 4E 4F 50  A.CDEFGHIJKLMNOP\n"
                    "0010  51 52" + std::string(45, ' ') + "QR\n");
    EXPECT_EQ(TrafficHexDump({}), "");
}
