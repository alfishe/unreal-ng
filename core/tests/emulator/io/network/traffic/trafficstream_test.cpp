// The traffic tap as a live pcapng stream (network #91 T3, trafficstream.h) over loopback only: a reader connecting
// gets the pcapng header and the ring as it is, then each new packet; a second reader gets the same; the stream
// stops cleanly. No external network (loopback, a free port)

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "_helpers/testwaithelper.h"
#include "common/network/netsockets.h"
#include "emulator/io/network/traffic/networktraffictap.h"
#include "emulator/io/network/traffic/trafficstream.h"

namespace
{
uint32_t Le32(const std::vector<uint8_t>& v, size_t at)
{
    return v[at] | (v[at + 1] << 8) | (v[at + 2] << 16) | (static_cast<uint32_t>(v[at + 3]) << 24);
}

/// Block types in the bytes received so far (whole blocks only)
std::vector<uint32_t> Blocks(const std::vector<uint8_t>& b)
{
    std::vector<uint32_t> types;
    size_t at = 0;
    while (at + 12 <= b.size() && at + Le32(b, at + 4) <= b.size())
    {
        types.push_back(Le32(b, at));
        at += Le32(b, at + 4);
    }
    return types;
}

class Reader
{
public:
    explicit Reader(uint16_t port)
    {
        _s = netsock::OpenTcp();
        netsock::Connect(_s, NetEndpoint{NetIp(127, 0, 0, 1), port});
    }
    ~Reader() { netsock::Close(_s); }
    /// Read what arrived until `packets` EPBs are in (or the wait runs out)
    size_t ReadUntil(size_t packets)
    {
        TestWait::For([&]() {
            uint8_t buf[4096];
            size_t got = 0;
            while (netsock::Recv(_s, buf, sizeof(buf), got) == netsock::Result::Ok && got)
                bytes.insert(bytes.end(), buf, buf + got);
            const std::vector<uint32_t> types = Blocks(bytes);
            return static_cast<size_t>(std::count(types.begin(), types.end(), 6u)) >= packets;
        });
        const std::vector<uint32_t> types = Blocks(bytes);
        return static_cast<size_t>(std::count(types.begin(), types.end(), 6u));
    }
    std::vector<uint8_t> bytes;

private:
    netsock::Handle _s = netsock::kInvalid;
};
}  // namespace

TEST(TrafficStream_Test, ReadersGetTheRingThenEveryNewPacket)
{
    ASSERT_TRUE(netsock::Startup());
    NetworkTrafficTap tap([]() { return TrafficTime{}; });
    const std::vector<uint8_t> eth(60, 0x42);
    tap.Frame("isa2.eth", true, eth.data(), eth.size());   // before anyone reads: comes with the ring

    TrafficStream stream(tap);
    std::string error;
    ASSERT_TRUE(stream.Start(NetIp(127, 0, 0, 1), 0, error)) << error;
    ASSERT_NE(stream.Port(), 0) << "a free port was picked";

    Reader first(stream.Port());
    EXPECT_EQ(first.ReadUntil(1), 1u) << "the ring";
    EXPECT_EQ(Blocks(first.bytes).front(), 0x0A0D0D0Au) << "a section header first";
    tap.Frame("isa2.eth", false, eth.data(), eth.size());
    tap.Frame("lan", false, eth.data(), eth.size());
    EXPECT_EQ(first.ReadUntil(3), 3u) << "the new packets, live";
    const std::vector<uint32_t> types = Blocks(first.bytes);
    EXPECT_EQ(std::count(types.begin(), types.end(), 1u), 2) << "an interface per adapter";

    Reader second(stream.Port());
    EXPECT_EQ(second.ReadUntil(3), 3u) << "a later reader gets the whole ring";
    EXPECT_TRUE(TestWait::For([&]() { return stream.Clients() == 2; }));

    stream.Stop();
    EXPECT_FALSE(stream.Running());
    EXPECT_EQ(stream.Port(), 0);
}
