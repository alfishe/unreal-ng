// Virtual network DHCP server (network adapters TDD §5.2)

#include <gtest/gtest.h>

#include "common/network/nettypes.h"
#include "emulator/io/network/vnet/dhcpserver.h"

namespace
{
std::vector<uint8_t> Request(uint8_t type, uint8_t macLast, uint32_t requested = 0)
{
    std::vector<uint8_t> r(240, 0);
    r[0] = 1;
    r[1] = 1;
    r[2] = 6;
    r[4] = 0x12;
    r[5] = 0x34;
    r[28] = 0x02;
    r[33] = macLast;
    r[236] = 99;
    r[237] = 130;
    r[238] = 83;
    r[239] = 99;
    r.insert(r.end(), {0, 53, 1, type});   // a Pad on input is fine
    if (requested)
        r.insert(r.end(), {50, 4, uint8_t(requested >> 24), uint8_t(requested >> 16), uint8_t(requested >> 8),
                           uint8_t(requested)});
    r.push_back(255);
    return r;
}

uint8_t MessageType(const std::vector<uint8_t>& reply)
{
    for (size_t pos = 240; pos + 1 < reply.size() && reply[pos] != 255; pos += 2 + reply[pos + 1])
    {
        if (reply[pos] == 53)
            return reply[pos + 2];
    }
    return 0;
}
}  // namespace

class DhcpServer_Test : public ::testing::Test
{
protected:
    DhcpServer server{DhcpServer::Settings{NetIp(10, 0, 2, 2), NetIp(255, 255, 255, 0), NetIp(10, 0, 2, 3),
                                           NetIp(10, 0, 2, 15), 64, 86400}};
};

TEST_F(DhcpServer_Test, DiscoverGetsAnOfferAndRequestAnAck)
{
    std::vector<uint8_t> req = Request(1, 0x01);
    auto offer = server.Handle(req.data(), req.size());
    ASSERT_GE(offer.size(), 300u);
    EXPECT_EQ(offer[0], 2);
    EXPECT_EQ(MessageType(offer), 2);
    EXPECT_EQ(offer[19], 15);

    req = Request(3, 0x01, NetIp(10, 0, 2, 15));
    auto ack = server.Handle(req.data(), req.size());
    EXPECT_EQ(MessageType(ack), 5);
}

TEST_F(DhcpServer_Test, EachMacGetsItsOwnStableLease)
{
    auto a = Request(1, 0x01);
    auto b = Request(1, 0x02);
    EXPECT_EQ(server.Handle(a.data(), a.size())[19], 15);
    EXPECT_EQ(server.Handle(b.data(), b.size())[19], 16);
    EXPECT_EQ(server.Handle(a.data(), a.size())[19], 15);
}

TEST_F(DhcpServer_Test, RequestForAnotherAddressIsNaked)
{
    auto req = Request(3, 0x01, NetIp(10, 0, 2, 99));
    EXPECT_EQ(MessageType(server.Handle(req.data(), req.size())), 6);
}

TEST_F(DhcpServer_Test, RepliesHaveNoPadOptionsBeforeEnd)
{
    auto req = Request(1, 0x01);
    auto offer = server.Handle(req.data(), req.size());
    size_t pos = 240;
    while (offer[pos] != 255)
    {
        ASSERT_NE(offer[pos], 0);
        pos += 2 + offer[pos + 1];
        ASSERT_LT(pos, offer.size());
    }
}

TEST_F(DhcpServer_Test, GarbageIsIgnored)
{
    std::vector<uint8_t> junk(20, 0x55);
    EXPECT_TRUE(server.Handle(junk.data(), junk.size()).empty());
}
