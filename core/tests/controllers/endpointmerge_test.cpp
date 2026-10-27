/// @file endpointmerge_test.cpp
/// @brief One logical device per physical pad (architecture.md §5, D18) with fake endpoints: two raw HID interfaces
/// of one pad merge and only the one with the most controls gives input; a generic device never merges; a
/// GameController companion is matched by its vendor name and arrival, confirmed only by coincident presses, picks the
/// right pad of two identical ones; a companion no raw device claims becomes standalone (a `gc:` device) and is
/// promoted when its raw device appears and the presses confirm it.

#include <gtest/gtest.h>

#include "endpointmerge.h"

using namespace controllers;

namespace
{
HidEndpointInfo Hid(const std::string& key, const std::string& id, const std::string& physical, const std::string& name,
                    size_t controls, uint64_t arrivalMs = 1000)
{
    return {key, id, physical, name, controls, arrivalMs};
}
}  // namespace

/// region <Raw HID endpoints>

TEST(EndpointMerge_Test, Hid_TwoInterfacesOfOnePadAreOneDeviceWithOneInput)
{
    EndpointMerge merge;
    const auto first = merge.AddHid(Hid("a", "usb:054c:0ce6:0100@1", "usb:054c:0ce6:@1", "DualSense", 10));
    EXPECT_TRUE(first.newDevice);
    // The second interface has more controls: it takes the input, the device keeps the first id
    const auto second = merge.AddHid(Hid("b", "usb:054c:0ce6:0100@1#2", "usb:054c:0ce6:@1", "DualSense", 18));
    EXPECT_FALSE(second.newDevice);
    EXPECT_EQ(second.device, "usb:054c:0ce6:0100@1");
    EXPECT_FALSE(merge.IsInput("a"));
    EXPECT_TRUE(merge.IsInput("b"));
    EXPECT_EQ(merge.EndpointsOf(first.device), (std::vector<std::string>{"b", "a"}));

    // The input endpoint goes: the other gives the input, the device stays
    const auto gone = merge.RemoveHid("b");
    EXPECT_FALSE(gone.deviceGone);
    EXPECT_TRUE(merge.IsInput("a"));
    const auto last = merge.RemoveHid("a");
    EXPECT_TRUE(last.deviceGone);
    EXPECT_EQ(last.device, "usb:054c:0ce6:0100@1");
    EXPECT_TRUE(merge.EndpointsOf(last.device).empty());
}

TEST(EndpointMerge_Test, Hid_TieKeepsTheFirstAndEmptyPhysicalNeverMerges)
{
    EndpointMerge merge;
    merge.AddHid(Hid("a", "id-a", "bt:045e:0b22:x", "Xbox Wireless Controller", 20));
    merge.AddHid(Hid("b", "id-b", "bt:045e:0b22:x", "Xbox Wireless Controller", 20));
    EXPECT_TRUE(merge.IsInput("a"));
    EXPECT_FALSE(merge.IsInput("b"));

    // A two-player encoder: two pads on one port, the backend gives no physical key
    const auto one = merge.AddHid(Hid("p1", "usb:0079:0006:0107@9", "", "Generic USB Joystick", 12));
    const auto two = merge.AddHid(Hid("p2", "usb:0079:0006:0107@9#2", "", "Generic USB Joystick", 12));
    EXPECT_TRUE(one.newDevice);
    EXPECT_TRUE(two.newDevice);
    EXPECT_TRUE(merge.IsInput("p1"));
    EXPECT_TRUE(merge.IsInput("p2"));
    EXPECT_FALSE(merge.IsInput("unknown"));
    EXPECT_FALSE(merge.RemoveHid("unknown").deviceGone);
}

/// endregion </Raw HID endpoints>

/// region <The GameController companion>

TEST(EndpointMerge_Test, Names_EqualOrInsideIgnoringCase)
{
    EXPECT_TRUE(EndpointMerge::NamesMatch("Xbox Wireless Controller", "Xbox Wireless Controller"));
    EXPECT_TRUE(EndpointMerge::NamesMatch("Controller", "controller "));  // the USB dext's "Controller"
    EXPECT_TRUE(EndpointMerge::NamesMatch("DUALSHOCK 4 Wireless Controller", "Wireless Controller"));
    EXPECT_FALSE(EndpointMerge::NamesMatch("DualSense Wireless Controller", "Pro Controller"));
    EXPECT_FALSE(EndpointMerge::NamesMatch("", "Controller"));
}

TEST(EndpointMerge_Test, Companion_TentativeByArrivalConfirmedOnlyByPresses)
{
    EndpointMerge merge;
    const HostDeviceId pad = merge.AddHid(Hid("a", "bt:054c:0ce6:1", "bt:054c:0ce6:1", "DualSense Wireless Controller",
                                              18, 1000))
                                 .device;
    merge.AddCompanion({"gc#1", "DualSense Wireless Controller", 1200});
    EXPECT_EQ(merge.StateOf(pad), EndpointMerge::MatchState::Tentative);
    EXPECT_EQ(merge.CompanionOf(pad), "") << "no outputs from a tentative match";

    // The raw press first, the companion's 6 ms later; then the companion first, the raw one 4 ms later
    EXPECT_FALSE(merge.HidEdge(pad, 5000).has_value());
    EXPECT_FALSE(merge.CompanionEdge("gc#1", 5006).has_value());
    EXPECT_FALSE(merge.CompanionEdge("gc#1", 6000).has_value());
    const auto confirmed = merge.HidEdge(pad, 6004);
    ASSERT_TRUE(confirmed.has_value());
    EXPECT_EQ(confirmed->companion, "gc#1");
    EXPECT_EQ(confirmed->device, pad);
    EXPECT_EQ(merge.StateOf(pad), EndpointMerge::MatchState::Confirmed);
    EXPECT_EQ(merge.CompanionOf(pad), "gc#1");
    EXPECT_EQ(merge.DeviceOf("gc#1"), pad);
    // Once matched, presses cost nothing and change nothing
    EXPECT_FALSE(merge.HidEdge(pad, 7000).has_value());
    EXPECT_FALSE(merge.CompanionEdge("gc#1", 7000).has_value());

    // The companion goes: the device stays, without outputs
    merge.RemoveCompanion("gc#1");
    EXPECT_EQ(merge.CompanionOf(pad), "");
    EXPECT_EQ(merge.StateOf(pad), EndpointMerge::MatchState::None);
    EXPECT_EQ(merge.EndpointsOf(pad).size(), 1u) << "a companion is never an endpoint of its own";
}

TEST(EndpointMerge_Test, Companion_PressesOutsideTheWindowConfirmNothing)
{
    EndpointMerge merge;
    const HostDeviceId pad = merge.AddHid(Hid("a", "bt:1", "bt:1", "Xbox Wireless Controller", 20)).device;
    merge.AddCompanion({"gc#1", "Xbox Wireless Controller", 1000});
    for (uint64_t t = 10000; t < 20000; t += 1000)
    {
        merge.HidEdge(pad, t);
        EXPECT_FALSE(merge.CompanionEdge("gc#1", t + EndpointMerge::kEdgeWindowMs + 1).has_value());
    }
    EXPECT_EQ(merge.CompanionOf(pad), "");
    EXPECT_EQ(merge.StateOf(pad), EndpointMerge::MatchState::Tentative);
}

TEST(EndpointMerge_Test, Companion_TwoIdenticalPadsThePressesDecide)
{
    EndpointMerge merge;
    const HostDeviceId one = merge.AddHid(Hid("a", "bt:1", "bt:1", "Xbox Wireless Controller", 20, 1000)).device;
    const HostDeviceId two = merge.AddHid(Hid("b", "bt:2", "bt:2", "Xbox Wireless Controller", 20, 1010)).device;
    merge.AddCompanion({"gc#1", "Xbox Wireless Controller", 1100});
    merge.AddCompanion({"gc#2", "Xbox Wireless Controller", 1105});
    EXPECT_EQ(merge.StateOf(one), EndpointMerge::MatchState::Tentative);
    EXPECT_EQ(merge.StateOf(two), EndpointMerge::MatchState::Tentative);

    // gc#1 is pad two (the arrival order says nothing), gc#2 pad one; the other player presses at other times
    merge.HidEdge(one, 3000);
    merge.CompanionEdge("gc#2", 3005);
    merge.HidEdge(two, 3500);
    merge.CompanionEdge("gc#1", 3502);
    merge.HidEdge(two, 4000);
    const auto first = merge.CompanionEdge("gc#1", 4003);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->device, two);
    merge.HidEdge(one, 4500);
    const auto second = merge.CompanionEdge("gc#2", 4510);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->device, one);
    EXPECT_EQ(merge.CompanionOf(one), "gc#2");
    EXPECT_EQ(merge.CompanionOf(two), "gc#1");
}

TEST(EndpointMerge_Test, Companion_SimultaneousPressesOnBothPadsAreAmbiguous)
{
    EndpointMerge merge;
    const HostDeviceId one = merge.AddHid(Hid("a", "bt:1", "bt:1", "Xbox Wireless Controller", 20)).device;
    const HostDeviceId two = merge.AddHid(Hid("b", "bt:2", "bt:2", "Xbox Wireless Controller", 20)).device;
    merge.AddCompanion({"gc#1", "Xbox Wireless Controller", 1000});
    // Both players press together each time: the companion's press fits both, nothing is confirmed
    for (uint64_t t = 2000; t < 2600; t += 100)
    {
        merge.HidEdge(one, t);
        merge.HidEdge(two, t + 2);
        EXPECT_FALSE(merge.CompanionEdge("gc#1", t + 5).has_value());
    }
    // Then pad two alone
    merge.HidEdge(two, 5000);
    const auto confirmed = merge.CompanionEdge("gc#1", 5004);
    ASSERT_TRUE(confirmed.has_value());
    EXPECT_EQ(confirmed->device, two);
}

TEST(EndpointMerge_Test, Companion_MostPressesMustCoincide)
{
    EndpointMerge merge;
    const HostDeviceId pad = merge.AddHid(Hid("a", "bt:1", "bt:1", "Pro Controller", 16)).device;
    merge.AddCompanion({"gc#1", "Pro Controller", 1000});
    // Four companion presses the raw device never had (another pad of the companion's that IOHID does not see), then
    // two coincident ones: two of six is not enough
    for (uint64_t t = 2000; t < 2400; t += 100)
        merge.CompanionEdge("gc#1", t);
    merge.HidEdge(pad, 3000);
    EXPECT_FALSE(merge.CompanionEdge("gc#1", 3001).has_value());
    merge.HidEdge(pad, 3500);
    EXPECT_FALSE(merge.CompanionEdge("gc#1", 3501).has_value());
    // More coincidences than misses: confirmed
    std::optional<EndpointMerge::Confirmed> confirmed;
    for (uint64_t t = 4000; t < 7000 && !confirmed; t += 500)
    {
        merge.HidEdge(pad, t);
        confirmed = merge.CompanionEdge("gc#1", t + 1);
    }
    ASSERT_TRUE(confirmed.has_value());
    EXPECT_EQ(confirmed->device, pad);
}

TEST(EndpointMerge_Test, Companion_ANameNoDeviceHasFallsBackToThePresses)
{
    EndpointMerge merge;
    const HostDeviceId pad = merge.AddHid(Hid("a", "usb:1", "usb:1", "Wireless Gamepad", 14)).device;
    merge.AddCompanion({"gc#1", "Some Other Name", 1000});
    merge.HidEdge(pad, 2000);
    merge.CompanionEdge("gc#1", 2001);
    merge.HidEdge(pad, 2500);
    const auto confirmed = merge.CompanionEdge("gc#1", 2502);
    ASSERT_TRUE(confirmed.has_value());
    EXPECT_EQ(confirmed->device, pad);
}

TEST(EndpointMerge_Test, Companion_TheDeviceGoesAndComesBack)
{
    EndpointMerge merge;
    HostDeviceId pad = merge.AddHid(Hid("a", "bt:1", "bt:1", "Xbox Wireless Controller", 20)).device;
    merge.AddCompanion({"gc#1", "Xbox Wireless Controller", 1000});
    merge.HidEdge(pad, 2000);
    merge.CompanionEdge("gc#1", 2001);
    merge.HidEdge(pad, 2500);
    ASSERT_TRUE(merge.CompanionEdge("gc#1", 2501).has_value());

    EXPECT_TRUE(merge.RemoveHid("a").deviceGone);
    EXPECT_EQ(merge.DeviceOf("gc#1"), "");
    // The pad back as a new IOHID device with the same id: matched again by presses
    pad = merge.AddHid(Hid("a2", "bt:1", "bt:1", "Xbox Wireless Controller", 20, 9000)).device;
    EXPECT_EQ(merge.CompanionOf(pad), "");
    merge.HidEdge(pad, 10000);
    merge.CompanionEdge("gc#1", 10001);
    merge.HidEdge(pad, 10500);
    ASSERT_TRUE(merge.CompanionEdge("gc#1", 10501).has_value());
    EXPECT_EQ(merge.CompanionOf(pad), "gc#1");
}

/// endregion </The GameController companion>

/// region <Standalone companions (gc: devices)>

TEST(EndpointMerge_Test, Standalone_AfterTheWindowWithNoDeviceOfItsName)
{
    EndpointMerge merge;
    // A device of another name arrived with it: it does not hold the companion back
    merge.AddHid(Hid("dial", "bt:045e:091b:x", "", "Surface Dial System Multi Axis", 0, 1000));
    merge.AddCompanion({"gc#1", "Pro Controller", 1000});
    EXPECT_TRUE(merge.StandaloneDue(1000 + EndpointMerge::kArrivalWindowMs - 1).empty()) << "within the window";
    EXPECT_EQ(merge.StandaloneDue(1000 + EndpointMerge::kArrivalWindowMs), (std::vector<std::string>{"gc#1"}));
    EXPECT_TRUE(merge.IsStandalone("gc#1"));
    EXPECT_TRUE(merge.StandaloneDue(10000).empty()) << "reported once";

    // A device of its name holds the next companion back
    merge.AddHid(Hid("pad", "bt:054c:0ce6:y", "bt:054c:0ce6:y", "DualSense Wireless Controller", 30, 2000));
    merge.AddCompanion({"gc#2", "DualSense Wireless Controller", 2000});
    EXPECT_TRUE(merge.StandaloneDue(20000).empty());
    EXPECT_FALSE(merge.IsStandalone("gc#2"));
}

TEST(EndpointMerge_Test, Standalone_PromotedWhenItsDeviceAppearsAndPressesConfirm)
{
    EndpointMerge merge;
    merge.AddCompanion({"gc#1", "Wireless Gamepad", 1000});
    ASSERT_EQ(merge.StandaloneDue(5000).size(), 1u);
    // The pad's IOHID endpoint appears later under another name
    merge.AddHid(Hid("a", "bt:2dc8:6001:z", "bt:2dc8:6001:z", "8BitDo SN30 Pro", 20, 9000));
    EXPECT_FALSE(merge.CompanionEdge("gc#1", 10000).has_value());
    EXPECT_FALSE(merge.HidEdge("bt:2dc8:6001:z", 10005).has_value());
    EXPECT_FALSE(merge.CompanionEdge("gc#1", 11000).has_value());
    const auto confirmed = merge.HidEdge("bt:2dc8:6001:z", 11010);
    ASSERT_TRUE(confirmed.has_value());
    EXPECT_TRUE(confirmed->promoted);
    EXPECT_EQ(confirmed->device, "bt:2dc8:6001:z");
    EXPECT_FALSE(merge.IsStandalone("gc#1"));
    EXPECT_EQ(merge.CompanionOf("bt:2dc8:6001:z"), "gc#1");

    // The raw endpoint goes, the companion stays: standalone again a window later
    merge.RemoveHid("a", 20000);
    EXPECT_TRUE(merge.StandaloneDue(20000 + EndpointMerge::kArrivalWindowMs - 1).empty());
    EXPECT_EQ(merge.StandaloneDue(20000 + EndpointMerge::kArrivalWindowMs).size(), 1u);
}

TEST(EndpointMerge_Test, Standalone_AMatchedCompanionIsNotPromoted)
{
    EndpointMerge merge;
    merge.AddHid(Hid("a", "usb:054c:0ce6:1", "usb:054c:0ce6:1", "DualSense Wireless Controller", 30));
    merge.AddCompanion({"gc#1", "DualSense Wireless Controller", 1000});
    merge.HidEdge("usb:054c:0ce6:1", 2000);
    merge.CompanionEdge("gc#1", 2003);
    merge.HidEdge("usb:054c:0ce6:1", 3000);
    const auto confirmed = merge.CompanionEdge("gc#1", 3004);
    ASSERT_TRUE(confirmed.has_value());
    EXPECT_FALSE(confirmed->promoted);
    EXPECT_TRUE(merge.StandaloneDue(100000).empty());
}

TEST(EndpointMerge_Test, Standalone_ADeviceOfItsNameHoldsItBackUntilConfirmed)
{
    // D18: until the match is confirmed the pad gives input from IOHID only
    EndpointMerge merge;
    merge.AddCompanion({"gc#1", "Pro Controller", 1000});
    ASSERT_EQ(merge.StandaloneDue(5000).size(), 1u);
    for (uint64_t t = 6000; t < 6010; ++t)
        merge.CompanionEdge("gc#1", t * 10);  // played as a gc: device for a while
    EXPECT_FALSE(merge.IsShadowed("gc#1"));

    merge.AddHid(Hid("a", "bt:057e:2009:1", "bt:057e:2009:1", "Pro Controller", 20, 90000));
    EXPECT_TRUE(merge.IsShadowed("gc#1"));
    // Its standalone presses do not count against the match: two coincident presses confirm
    EXPECT_FALSE(merge.HidEdge("bt:057e:2009:1", 100000).has_value());
    EXPECT_FALSE(merge.CompanionEdge("gc#1", 100004).has_value());
    EXPECT_FALSE(merge.HidEdge("bt:057e:2009:1", 101000).has_value());
    const auto confirmed = merge.CompanionEdge("gc#1", 101006);
    ASSERT_TRUE(confirmed.has_value());
    EXPECT_TRUE(confirmed->promoted);
    EXPECT_FALSE(merge.IsShadowed("gc#1"));
}

TEST(EndpointMerge_Test, Standalone_TheShadowEndsWithTheDeviceOrWithoutCoincidences)
{
    EndpointMerge merge;
    merge.AddCompanion({"gc#1", "Pro Controller", 1000});
    ASSERT_EQ(merge.StandaloneDue(5000).size(), 1u);
    merge.AddHid(Hid("other", "usb:4567:0001:x", "usb:4567:0001:x", "USB Gamepad", 20, 6000));
    EXPECT_FALSE(merge.IsShadowed("gc#1")) << "a device of another name does not hold it back";

    merge.AddHid(Hid("a", "bt:057e:2009:1", "bt:057e:2009:1", "Pro Controller", 20, 7000));
    EXPECT_TRUE(merge.IsShadowed("gc#1"));
    merge.RemoveHid("a", 8000);
    EXPECT_FALSE(merge.IsShadowed("gc#1")) << "the device went before a confirmation";

    // Another pad of the same name: the companion's presses never coincide on it
    merge.AddHid(Hid("b", "bt:057e:2009:2", "bt:057e:2009:2", "Pro Controller", 20, 9000));
    ASSERT_TRUE(merge.IsShadowed("gc#1"));
    for (int i = 0; i < EndpointMerge::kShadowPresses - 1; ++i)
        merge.CompanionEdge("gc#1", 10000 + static_cast<uint64_t>(i) * 500);
    EXPECT_TRUE(merge.IsShadowed("gc#1"));
    merge.CompanionEdge("gc#1", 20000);
    EXPECT_FALSE(merge.IsShadowed("gc#1"));
    EXPECT_TRUE(merge.IsStandalone("gc#1"));
}

/// endregion </Standalone companions>
