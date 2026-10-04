// The virtual network's host listeners (guest servers, Forward= rules) and the address they bind:
// [NETWORK] RemoteAccess, PLAN #92 phase N0 (docs/inprogress/2026-10-02-tsconf-zifi/tdd-smb-online-update.md §6
// item 1). On (default): 0.0.0.0, every host interface; off: 127.0.0.1, this computer only. Hermetic: the host is
// the scripted fake, every command it gets is recorded.

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "_helpers/scriptedhostnet.h"
#include "emulator/io/network/virtualnetwork.h"

namespace
{
constexpr uint32_t kAny = 0;
constexpr uint32_t kLoopback = NetIp(127, 0, 0, 1);

class Guest : public INetGuest
{
public:
    void OnNetEvent(uint32_t, NetEventType type, NetEventStatus, const NetEndpoint&, const uint8_t*, uint32_t,
                    uint32_t) override
    {
        events.push_back(type);
    }
    std::vector<NetEventType> events;
};
}  // namespace

class VirtualNetwork_Test : public ::testing::Test
{
protected:
    void Make(const VirtualNetworkConfig& config)
    {
        auto host = std::make_unique<ScriptedHostNet>();
        _host = host.get();
        _net = std::make_unique<VirtualNetwork>(nullptr, std::move(host), config);
    }

    /// A guest TCP socket listening on `guestPort`; returns the host listener's id
    uint16_t GuestServer(uint16_t guestPort)
    {
        const uint16_t s = _net->Open(NetProto::Tcp, &_guest, guestPort);
        _net->Listen(s, guestPort);
        const FakeHostNet::Command* listen = _host->Last("listen");
        return listen ? listen->socket : 0;
    }

    std::vector<FakeHostNet::Command> Listens() const
    {
        std::vector<FakeHostNet::Command> out;
        for (const FakeHostNet::Command& c : _host->commands)
        {
            if (c.op == "listen")
                out.push_back(c);
        }
        return out;
    }

    ScriptedHostNet* _host = nullptr;
    std::unique_ptr<VirtualNetwork> _net;
    Guest _guest;
};

TEST_F(VirtualNetwork_Test, RemoteAccessIsOnByDefault)
{
    VirtualNetworkConfig config;
    EXPECT_TRUE(config.remoteAccess);
    EXPECT_EQ(config.ListenAddress(), kAny);
    config.remoteAccess = false;
    EXPECT_EQ(config.ListenAddress(), kLoopback);
}

TEST_F(VirtualNetwork_Test, GuestServerListensOnAllInterfacesWithRemoteAccess)
{
    Make(VirtualNetworkConfig());
    ASSERT_NE(GuestServer(4444), 0);
    const FakeHostNet::Command* listen = _host->Last("listen");
    EXPECT_EQ(listen->endpoint.addr, kAny) << "0.0.0.0: other computers on the LAN can connect";
    EXPECT_EQ(listen->endpoint.port, 4444);
}

TEST_F(VirtualNetwork_Test, ForwardRuleListensOnLoopbackWithoutRemoteAccess)
{
    VirtualNetworkConfig config;
    config.remoteAccess = false;
    config.forwards[21] = 2121;
    Make(config);
    ASSERT_NE(GuestServer(21), 0);
    const FakeHostNet::Command* listen = _host->Last("listen");
    EXPECT_EQ(listen->endpoint.addr, kLoopback) << "127.0.0.1: this computer only";
    EXPECT_EQ(listen->endpoint.port, 2121) << "the Forward= rule's host port";
}

/// A runtime change moves every host listener to the new address under the same id (the bridge replaces it); the
/// guest's listening sockets stay and take the next client
TEST_F(VirtualNetwork_Test, ChangingRemoteAccessListensAgain)
{
    VirtualNetworkConfig config;
    config.forwards[21] = 2121;
    Make(config);
    const uint16_t ftp = GuestServer(21);
    const uint16_t web = GuestServer(8080);
    ASSERT_NE(ftp, 0);
    ASSERT_NE(web, 0);
    ASSERT_EQ(Listens().size(), 2u);

    _net->SetRemoteAccess(false);
    std::vector<FakeHostNet::Command> listens = Listens();
    ASSERT_EQ(listens.size(), 4u) << "both listeners again";
    EXPECT_EQ(listens[2].endpoint.addr, kLoopback);
    EXPECT_EQ(listens[3].endpoint.addr, kLoopback);
    EXPECT_TRUE((listens[2].socket == ftp && listens[3].socket == web) || (listens[2].socket == web && listens[3].socket == ftp))
        << "the same host listener ids";
    EXPECT_FALSE(_net->Config().remoteAccess);

    _net->SetRemoteAccess(false);
    EXPECT_EQ(Listens().size(), 4u) << "no change, no command";

    // The guest server still answers: a host client of the moved listener reaches it
    _host->ConnectClient(ftp, 0x8000, {NetIp(192, 168, 1, 20), 50000});
    _net->Pump();
    ASSERT_FALSE(_guest.events.empty());
    EXPECT_EQ(_guest.events.back(), NetEventType::Accepted);

    _net->SetRemoteAccess(true);
    listens = Listens();
    ASSERT_EQ(listens.size(), 6u);
    EXPECT_EQ(listens[4].endpoint.addr, kAny);
    EXPECT_EQ(listens[5].endpoint.addr, kAny);
}

/// Back from a TTD replay (a link reset) the listeners come back on the address in force
TEST_F(VirtualNetwork_Test, LinkResetListensOnTheCurrentAddress)
{
    Make(VirtualNetworkConfig());
    ASSERT_NE(GuestServer(4444), 0);
    _net->SetRemoteAccess(false);
    _net->ApplyLinkReset();
    const FakeHostNet::Command* listen = _host->Last("listen");
    EXPECT_EQ(listen->endpoint.addr, kLoopback);
    EXPECT_EQ(listen->endpoint.port, 4444);
}

/// Without host access nothing listens on the host; the setting still records the choice
TEST_F(VirtualNetwork_Test, NoHostNoListener)
{
    _net = std::make_unique<VirtualNetwork>(nullptr, nullptr, VirtualNetworkConfig());
    const uint16_t s = _net->Open(NetProto::Tcp, &_guest, 1);
    _net->Listen(s, 4444);
    _net->SetRemoteAccess(false);
    EXPECT_FALSE(_net->Config().remoteAccess);
}
