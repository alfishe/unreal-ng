// The network traffic tap (network #91, networktraffictap.h): the ring (order, filters, the byte budget), the pcapng
// it writes (blocks parsed back here), the unbounded file recording, and the one tap point - the virtual network - fed
// by a frame card on its wire and by a socket adapter talking to a scripted host (no real network)

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "_helpers/scriptedhostnet.h"
#include "_helpers/testpathhelper.h"
#include "emulator/io/network/traffic/networktraffictap.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/network/vnet/ethernetgateway.h"

namespace
{
uint32_t Le32(const std::vector<uint8_t>& v, size_t at)
{
    return v[at] | (v[at + 1] << 8) | (v[at + 2] << 16) | (static_cast<uint32_t>(v[at + 3]) << 24);
}

/// The pcapng's blocks: their types, and the packet bytes of each EPB, read back as Wireshark would
struct Pcapng
{
    std::vector<uint32_t> types;
    std::vector<std::vector<uint8_t>> packets;
    std::vector<uint32_t> packetInterfaces;
};
Pcapng Parse(const std::vector<uint8_t>& file)
{
    Pcapng p;
    size_t at = 0;
    while (at + 12 <= file.size())
    {
        const uint32_t type = Le32(file, at), length = Le32(file, at + 4);
        EXPECT_EQ(length % 4, 0u);
        EXPECT_EQ(Le32(file, at + length - 4), length) << "trailing length";
        p.types.push_back(type);
        if (type == 6)
        {
            const uint32_t captured = Le32(file, at + 20);
            p.packetInterfaces.push_back(Le32(file, at + 8));
            p.packets.emplace_back(file.begin() + static_cast<std::ptrdiff_t>(at + 28),
                                   file.begin() + static_cast<std::ptrdiff_t>(at + 28 + captured));
        }
        at += length;
    }
    EXPECT_EQ(at, file.size());
    return p;
}

/// A socket adapter: keeps what the virtual network delivers
class Guest : public INetGuest
{
public:
    void OnNetEvent(uint32_t, NetEventType type, NetEventStatus, const NetEndpoint&, const uint8_t*, uint32_t, uint32_t) override
    {
        events.push_back(type);
    }
    std::vector<NetEventType> events;
};

class Station : public IEthernetPort
{
public:
    const std::string& PortKey() const override { return _key; }
    void StationMac(uint8_t out[6]) const override { std::memcpy(out, mac, 6); }
    bool Offer(const uint8_t*, size_t) override { return true; }
    uint8_t mac[6] = {0x02, 0x53, 0x50, 0x00, 0x00, 0x02};

private:
    std::string _key = "isa2.eth";
};
}  // namespace

TEST(NetworkTrafficTap_Test, RingKeepsOrderFiltersAndItsBudget)
{
    uint64_t frame = 100;
    NetworkTrafficTap tap([&frame]() { return TrafficTime{frame, 7, frame * 20000}; });
    const std::vector<uint8_t> eth(60, 0xAB);
    tap.Frame("isa2.eth", true, eth.data(), eth.size());
    ++frame;
    tap.Socket("zxnetusb", true, "send", 3, NetProto::Tcp, NetEndpoint{NetIp(93, 184, 216, 34), 80}, 0,
               reinterpret_cast<const uint8_t*>("GET / HTTP/1.0\r\n\r\n"), 18);

    std::vector<TrafficRecord> all = tap.Records({});
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].index, 1u);
    EXPECT_EQ(all[0].time.frame, 100u);
    EXPECT_EQ(all[0].time.tInFrame, 7u);
    EXPECT_EQ(all[1].op, "send");
    EXPECT_EQ(NetworkTrafficTap::Summary(all[1]), "TCP send to 93.184.216.34:80, 18 bytes: GET / HTTP/1.0");

    NetworkTrafficTap::Filter f;
    f.adapter = "zxnetusb";
    EXPECT_EQ(tap.Records(f).size(), 1u);
    f = {};
    f.kind = 0;
    EXPECT_EQ(tap.Records(f).size(), 1u) << "frames only";
    f = {};
    f.since = 2;
    EXPECT_EQ(tap.Records(f).size(), 1u) << "polling from the previous next index";

    // The budget: the oldest go, the indexes stay
    tap.SetRingBytes(64 * 1024);
    const std::vector<uint8_t> big(1000, 1);
    for (int i = 0; i < 200; ++i)
        tap.Frame("isa2.eth", false, big.data(), big.size());
    all = tap.Records({});
    EXPECT_LT(all.size(), 202u);
    EXPECT_EQ(all.back().index, 202u);
    EXPECT_GT(tap.Describe().find("trimmed")->i, 0);
    tap.Clear();
    EXPECT_TRUE(tap.Records({}).empty());
    EXPECT_EQ(tap.NextIndex(), 203u);
}

TEST(NetworkTrafficTap_Test, PcapngHasOneInterfacePerAdapterAndTheFrames)
{
    NetworkTrafficTap tap([]() { return TrafficTime{5, 0, 123456}; });
    std::vector<uint8_t> a(60, 0x11), b(64, 0x22);
    tap.Frame("isa2.eth", true, a.data(), a.size());
    tap.Frame("lan", false, b.data(), b.size());
    tap.Frame("isa2.eth", false, a.data(), a.size());
    const Pcapng p = Parse(tap.Pcapng({}));
    ASSERT_GE(p.types.size(), 6u);
    EXPECT_EQ(p.types[0], 0x0A0D0D0Au) << "section header";
    EXPECT_EQ(std::count(p.types.begin(), p.types.end(), 1u), 2) << "an interface per adapter";
    ASSERT_EQ(p.packets.size(), 3u);
    EXPECT_EQ(p.packets[1], b);
    EXPECT_EQ(p.packetInterfaces, (std::vector<uint32_t>{0, 1, 0}));
}

TEST(NetworkTrafficTap_Test, FileRecordsFromStartToStop)
{
    NetworkTrafficTap tap([]() { return TrafficTime{}; });
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("traffic.pcapng");
    std::vector<uint8_t> eth(60, 0x33);
    tap.Frame("isa2.eth", true, eth.data(), eth.size());   // before the start: not in the file
    std::string error;
    ASSERT_TRUE(tap.StartFile(path, error)) << error;
    EXPECT_TRUE(tap.FileOpen());
    tap.Frame("isa2.eth", true, eth.data(), eth.size());
    tap.Frame("isa2.eth", false, eth.data(), eth.size());
    tap.StopFile();
    tap.Frame("isa2.eth", false, eth.data(), eth.size());   // after the stop: not either
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(Parse(file).packets.size(), 2u);
    std::remove(path.c_str());
    EXPECT_FALSE(tap.StartFile("", error)) << "no path";
}

/// The one tap point: a frame card's frames on the wire and a socket adapter's operations, both recorded by the
/// virtual network, with the adapters' names
TEST(NetworkTrafficTap_Test, TheVirtualNetworkRecordsFramesAndSocketOperations)
{
    auto host = std::make_unique<ScriptedHostNet>();
    ScriptedHostNet* scripted = host.get();
    const NetEndpoint server{NetIp(192, 0, 2, 10), 80};
    scripted->AddHttp(server, {{"/", {'o', 'k'}}});
    VirtualNetwork net(nullptr, std::move(host), VirtualNetworkConfig());

    // A frame card on the wire: its broadcast is recorded as it leaves
    net.EnableFrames({});
    Station card;
    net.AttachStation(&card);
    std::vector<uint8_t> frame(60, 0);
    std::memset(frame.data(), 0xFF, 6);
    std::memcpy(frame.data() + 6, card.mac, 6);
    frame[12] = 0x88;
    frame[13] = 0xB5;
    net.Transmit(card, frame.data(), frame.size());

    // A socket adapter, named as NetworkManager names it
    Guest guest;
    net.NameGuest(&guest, "zxnetusb");
    const uint16_t s = net.Open(NetProto::Tcp, &guest, 1);
    net.Connect(s, server);
    net.Pump();
    const char* get = "GET / HTTP/1.0\r\n\r\n";
    net.Send(s, reinterpret_cast<const uint8_t*>(get), static_cast<uint32_t>(std::strlen(get)));
    net.Pump();
    net.Close(s);

    std::vector<std::string> seen;
    for (const TrafficRecord& r : net.Traffic().Records({}))
        seen.push_back(r.adapter + (r.out ? " > " : " < ") + (r.kind == TrafficRecord::Kind::Frame ? std::string("frame") : r.op));
    ASSERT_GE(seen.size(), 5u);
    EXPECT_EQ(seen[0], "isa2.eth > frame");
    EXPECT_EQ(seen[1], "zxnetusb > connect");
    EXPECT_NE(std::find(seen.begin(), seen.end(), "zxnetusb < connected"), seen.end());
    EXPECT_NE(std::find(seen.begin(), seen.end(), "zxnetusb > send"), seen.end());
    EXPECT_NE(std::find(seen.begin(), seen.end(), "zxnetusb < data"), seen.end()) << "the server's answer";
    EXPECT_EQ(seen.back(), "zxnetusb > close");
}

/// T2: the pcapng carries a socket conversation as packets too, on the adapter's own interface
TEST(NetworkTrafficTap_Test, PcapngCarriesSocketOperationsAsPackets)
{
    NetworkTrafficTap tap([]() { return TrafficTime{}; });
    const NetEndpoint peer{NetIp(93, 184, 216, 34), 80};
    tap.Socket("zxnetusb", true, "connect", 3, NetProto::Tcp, peer, 0, nullptr, 0);
    tap.Socket("zxnetusb", false, "connected", 3, NetProto::Tcp, peer, 0, nullptr, 0);
    const char* get = "GET / HTTP/1.0\r\n\r\n";
    tap.Socket("zxnetusb", true, "send", 3, NetProto::Tcp, peer, 0, reinterpret_cast<const uint8_t*>(get), std::strlen(get));
    tap.Socket("zxnetusb", true, "listen", 4, NetProto::Tcp, NetEndpoint{}, 8080, nullptr, 0);
    const Pcapng p = Parse(tap.Pcapng({}));
    EXPECT_EQ(std::count(p.types.begin(), p.types.end(), 1u), 1);
    ASSERT_EQ(p.packets.size(), 4u) << "SYN, SYN-ACK, ACK, the request; a listen gives none";
    EXPECT_EQ(p.packets[0][47], 0x02) << "SYN";
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(p.packets[3].data() + 54), std::strlen(get)), get);
}
