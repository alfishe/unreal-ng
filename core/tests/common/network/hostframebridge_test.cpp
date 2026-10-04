// The host side of the network bridge (network SN6, hostframebridge.h): which frames from the host adapter reach the
// cards. The pure filter runs always; the test that loads the packet library and lists the host's adapters touches the
// host and is DISABLED (run it by hand with --gtest_also_run_disabled_tests)

#include <gtest/gtest.h>

#include <vector>

#include "common/network/hostframebridge.h"

namespace
{
std::vector<uint8_t> Frame(const IHostFrames::Mac& dst, const IHostFrames::Mac& src)
{
    std::vector<uint8_t> f(60, 0);
    std::copy(dst.begin(), dst.end(), f.begin());
    std::copy(src.begin(), src.end(), f.begin() + 6);
    f[12] = 0x08;
    return f;
}
}  // namespace

TEST(HostFrameBridge_Test, WantsFramesForTheCardsBroadcastsAndMulticastsOnly)
{
    const IHostFrames::Mac card{0x02, 0x53, 0x50, 0x00, 0x01, 0x02};
    const IHostFrames::Mac lan{0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    const IHostFrames::Mac other{0x00, 0x11, 0x22, 0x33, 0x44, 0x66};
    const IHostFrames::Mac broadcast{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const IHostFrames::Mac multicast{0x01, 0x00, 0x5E, 0x00, 0x00, 0xFB};
    const std::vector<IHostFrames::Mac> stations{card};

    auto wants = [&](const std::vector<uint8_t>& f) { return HostFrameBridge::WantsFrame(f.data(), f.size(), stations); };
    EXPECT_TRUE(wants(Frame(card, lan))) << "to the card";
    EXPECT_FALSE(wants(Frame(other, lan))) << "to another station of the LAN (promiscuous capture sees it)";
    EXPECT_TRUE(wants(Frame(broadcast, lan)));
    EXPECT_TRUE(wants(Frame(multicast, lan)));
    EXPECT_FALSE(wants(Frame(broadcast, card))) << "our own frame, echoed by the adapter";
    EXPECT_FALSE(wants(Frame(lan, card)));
    const std::vector<uint8_t> runt(10, 0xFF);
    EXPECT_FALSE(HostFrameBridge::WantsFrame(runt.data(), runt.size(), stations));
    EXPECT_FALSE(HostFrameBridge::WantsFrame(Frame(card, lan).data(), 60, {})) << "no cards: only group frames";
}

// Touches the host (libpcap / Npcap, the adapter list): disabled, run by hand
TEST(HostFrameBridge_Test, DISABLED_UnknownAdapterIsRefusedWithAReason)
{
    HostFrameBridge bridge;
    std::string error;
    EXPECT_FALSE(bridge.Open("no-such-adapter-unreal-ng", error));
    EXPECT_FALSE(error.empty()) << "the library is missing, the host refuses, or the adapter does not exist";
    EXPECT_FALSE(bridge.IsOpen());
    EXPECT_FALSE(bridge.Open("", error));
    EXPECT_NE(error.find("BridgeAdapter"), std::string::npos);

    // The list works or explains itself; it never throws
    std::string listError;
    const std::vector<HostAdapter> adapters = bridge.Adapters(listError);
    EXPECT_TRUE(!adapters.empty() || !listError.empty()) << "a host always has an adapter (loopback) unless the library is missing";
    for (const HostAdapter& a : adapters)
        EXPECT_FALSE(a.name.empty());
}
