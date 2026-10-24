/// @file devicemapping_test.cpp
/// @brief Unknown devices (device-database.md §3-§4, ui-ux.md §3.5): the SDL GUID both ways, the VID / PID of a host
/// device id, a gamecontrollerdb.txt line and a RetroArch autoconfig file imported, the line written back, a control
/// map translating raw controls, the learn wizard's edges against the baseline, devices.user.json merged and loaded.

#include <gtest/gtest.h>

#include "devicedatabase.h"
#include "devicemapping.h"

using namespace controllers;

TEST(DeviceMapping_Test, SdlGuidAndDeviceIds)
{
    DeviceIdentity identity;
    identity.bus = "usb";
    identity.vendor = 0x045e;
    identity.product = 0x0b00;
    identity.version = 0x0517;
    const std::string guid = SdlGuid(identity);
    EXPECT_EQ(guid, "030000005e040000000b000017050000") << "bus, VID, PID, version little-endian";
    DeviceIdentity back;
    ASSERT_TRUE(IdentityFromSdlGuid(guid, back));
    EXPECT_EQ(back.bus, "usb");
    EXPECT_EQ(back.vendor, 0x045e);
    EXPECT_EQ(back.product, 0x0b00);
    EXPECT_EQ(back.version, 0x0517);
    EXPECT_FALSE(IdentityFromSdlGuid("05000000504c415953544154494f4e00", back)) << "a name-hash GUID has no VID";
    EXPECT_FALSE(IdentityFromSdlGuid("xyz", back));

    ASSERT_TRUE(IdentityFromDeviceId("usb:045e:0b00:0517:ABC123", back));
    EXPECT_EQ(back.vendor, 0x045e);
    EXPECT_EQ(back.version, 0x0517);
    ASSERT_TRUE(IdentityFromDeviceId("usb:2dc8:6001:0100@1a2b", back));
    EXPECT_EQ(back.version, 0x0100);
    ASSERT_TRUE(IdentityFromDeviceId("bt:045e:0b22:9f00aa", back));
    EXPECT_EQ(back.bus, "bt");
    EXPECT_EQ(back.version, -1) << "a Bluetooth id carries no version";
    EXPECT_FALSE(IdentityFromDeviceId("fake:1", back));
    EXPECT_FALSE(IdentityFromDeviceId("kbd:system", back));

    uint16_t vendor = 0;
    uint16_t product = 0;
    EXPECT_TRUE(ParseVidPid("045e:0B00", vendor, product));
    EXPECT_EQ(product, 0x0b00);
    EXPECT_FALSE(ParseVidPid("045e", vendor, product));
    EXPECT_FALSE(ParseVidPid("045e:zz", vendor, product));
}

TEST(DeviceMapping_Test, ImportGameControllerDbAndRetroArch)
{
    ImportedMapping imported;
    std::string error;
    ASSERT_TRUE(ImportMapping("03000000341200007856000000010000,USB Pad,a:b2,b:b1,x:b3,y:b0,back:b8,start:b9,"
                              "leftshoulder:b4,dpup:h0.1,dpleft:h0.8,leftx:a0,lefty:a1,righttrigger:+a5,"
                              "touchpad:b13,misc2:b14,platform:Mac OS X,",
                              imported, error))
        << error;
    EXPECT_EQ(imported.source, "gamecontrollerdb");
    EXPECT_EQ(imported.name, "USB Pad");
    EXPECT_EQ(imported.identity.vendor, 0x1234);
    EXPECT_EQ(imported.identity.product, 0x5678);
    EXPECT_EQ(imported.map.at("south"), "button.2");
    EXPECT_EQ(imported.map.at("north"), "button.0");
    EXPECT_EQ(imported.map.at("view"), "button.8");
    EXPECT_EQ(imported.map.at("dpad.up"), "dpad.up") << "hat 0 is our D-pad";
    EXPECT_EQ(imported.map.at("dpad.left"), "dpad.left");
    EXPECT_EQ(imported.map.at("lstick.y"), "lstick.y") << "a1: HidPad's Y";
    EXPECT_EQ(imported.map.at("rt"), "axis.5+");
    EXPECT_EQ(imported.skipped, (std::vector<std::string>{"misc2:b14"})) << "no canonical control for it here";

    // The line written back from the map: what SDL reads
    std::vector<std::string> skipped;
    const std::string line = GameControllerDbLine(imported.identity, "USB Pad", imported.map, &skipped);
    EXPECT_EQ(line.rfind("03000000341200007856000000010000,USB Pad,a:b2,b:b1,x:b3,y:b0,back:b8,start:b9,", 0), 0u)
        << line;
    EXPECT_NE(line.find("dpup:h0.1,dpleft:h0.8,leftx:a0,lefty:a1,"), std::string::npos) << line;
    EXPECT_NE(line.find("righttrigger:+a5,"), std::string::npos) << line;
    EXPECT_TRUE(skipped.empty());

    const std::string retroarch = "# 8BitDo in D-input\n"
                                  "input_driver = \"udev\"\n"
                                  "input_device = \"8BitDo Zero 2\"\n"
                                  "input_vendor_id = \"11720\"\n"
                                  "input_product_id = \"12816\"\n"
                                  "input_b_btn = \"1\"\n"
                                  "input_a_btn = \"0\"\n"
                                  "input_y_btn = \"4\"\n"
                                  "input_x_btn = \"3\"\n"
                                  "input_select_btn = \"10\"\n"
                                  "input_up_btn = \"h0up\"\n"
                                  "input_left_axis = \"-0\"\n"
                                  "input_l_x_plus_axis = \"+0\"\n"
                                  "input_menu_toggle_btn = \"nul\"\n"
                                  "input_turbo_btn = \"7\"\n";
    ASSERT_TRUE(ImportMapping(retroarch, imported, error)) << error;
    EXPECT_EQ(imported.source, "retroarch");
    EXPECT_EQ(imported.name, "8BitDo Zero 2");
    EXPECT_EQ(imported.identity.vendor, 11720) << "decimal in RetroArch's files";
    EXPECT_EQ(imported.identity.product, 12816);
    EXPECT_EQ(imported.map.at("south"), "button.1") << "RetroPad B is at the bottom";
    EXPECT_EQ(imported.map.at("east"), "button.0");
    EXPECT_EQ(imported.map.at("view"), "button.10");
    EXPECT_EQ(imported.map.at("dpad.up"), "dpad.up");
    EXPECT_EQ(imported.map.at("dpad.left"), "lstick.x-") << "a D-pad on an axis: that half";
    EXPECT_EQ(imported.map.at("lstick.x"), "lstick.x") << "a stick: the whole axis";
    EXPECT_EQ(imported.skipped, (std::vector<std::string>{"input_turbo_btn = 7"}));

    EXPECT_FALSE(ImportMapping("", imported, error));
    EXPECT_FALSE(ImportMapping("nonsense,Pad,a:b0", imported, error));
    EXPECT_NE(error.find("SDL GUID"), std::string::npos) << error;
    EXPECT_FALSE(ImportMapping("input_b_btn = \"1\"\n", imported, error));
    EXPECT_NE(error.find("input_vendor_id"), std::string::npos) << error;
}

TEST(DeviceMapping_Test, ControlMapTranslatesRawControls)
{
    std::vector<MappedControl> map;
    std::string error;
    ASSERT_TRUE(ParseControlMap({{"south", "button.2"}, {"dpad.left", "axis.0-"}, {"dpad.right", "axis.0+"},
                                 {"lstick.x", "axis.0"}},
                                map, error))
        << error;
    auto translate = [&map](const std::string& control, float value) {
        std::map<std::string, float> out;
        for (const ControlEvent& event : TranslateControl(map, {"usb:1", control, value, 0}))
            out[event.control] = event.value;
        return out;
    };
    EXPECT_EQ(translate("button.2", 1), (std::map<std::string, float>{{"south", 1}}));
    EXPECT_EQ(translate("axis.0", -0.75f),
              (std::map<std::string, float>{{"dpad.left", 0.75f}, {"dpad.right", 0}, {"lstick.x", -0.75f}}))
        << "each half 0..1, the whole axis as it is";
    EXPECT_TRUE(translate("button.7", 1).empty());

    EXPECT_FALSE(ParseControlMap({{"bogus", "button.1"}}, map, error));
    EXPECT_NE(error.find("not a canonical control"), std::string::npos) << error;
    EXPECT_FALSE(ParseControlMap({{"south", "button.x"}}, map, error));
    EXPECT_FALSE(ParseControlMap({{"south", "button.1+"}}, map, error)) << "a button has no halves";
}

TEST(DeviceMapping_Test, LearnWalksThePromptsWithEdgesAgainstTheBaseline)
{
    LearnSession session;
    std::string error;
    ASSERT_TRUE(LearnSession::Make("dpad.up, dpad.down, south, east", session, error)) << error;
    EXPECT_FALSE(LearnSession::Make("dpad.up,warp", session, error));
    ASSERT_TRUE(LearnSession::Make("dpad.up,dpad.down,south,east", session, error)) << error;
    // A stick resting off centre, a button held when it starts
    session.Baseline({{"axis.1", 0.3f}, {"button.9", 1.0f}});
    EXPECT_EQ(session.Current(), "dpad.up");
    EXPECT_EQ(LearnSession::Prompt("dpad.up"), "Press Up");

    EXPECT_FALSE(session.Feed("axis.1", 0.6f)) << "0.3 from its rest: noise";
    EXPECT_FALSE(session.Feed("button.9", 1.0f)) << "held from the start: no edge";
    EXPECT_FALSE(session.Feed("mouse.dx", 5.0f)) << "a mouse delta answers nothing";
    EXPECT_TRUE(session.Feed("axis.1", -0.9f)) << "up: the axis moved more than 0.4 from its rest";
    EXPECT_EQ(session.Mapping().at("dpad.up"), "axis.1-");
    EXPECT_TRUE(session.WaitingForRelease());
    EXPECT_FALSE(session.Feed("button.3", 1.0f)) << "the last answer comes back first";
    EXPECT_FALSE(session.Feed("axis.1", 0.35f)) << "back at its rest";
    EXPECT_FALSE(session.WaitingForRelease());

    EXPECT_TRUE(session.Feed("axis.1", 1.0f));
    EXPECT_EQ(session.Mapping().at("dpad.down"), "axis.1+") << "the other half of the same axis";
    session.Feed("axis.1", 0.3f);
    EXPECT_TRUE(session.Feed("button.3", 1.0f));
    session.Feed("button.3", 0.0f);
    EXPECT_FALSE(session.Feed("button.3", 1.0f)) << "a raw control answers once";
    session.Feed("button.3", 0.0f);
    session.Back();
    EXPECT_EQ(session.Current(), "south") << "back asks again";
    EXPECT_EQ(session.Mapping().count("south"), 0u);
    EXPECT_TRUE(session.Feed("button.4", 1.0f));
    session.Feed("button.4", 0.0f);
    session.Skip();
    EXPECT_TRUE(session.Done());
    EXPECT_EQ(session.Mapping(), (std::map<std::string, std::string>{
                                     {"dpad.up", "axis.1-"}, {"dpad.down", "axis.1+"}, {"south", "button.4"}}));
}

TEST(DeviceMapping_Test, UserFamilyMergesAndLoads)
{
    DeviceIdentity identity;
    identity.vendor = 0x1234;
    identity.product = 0x5678;
    const JsonValue family = UserFamilyJson(identity, "USB Pad", {{"south", "button.2"}}, "learn");
    EXPECT_EQ(UserFamilyId(identity), "user-12345678");
    std::string text;
    std::string error;
    ASSERT_TRUE(DeviceDatabase::PutUserFamily("", family, text, error)) << error;
    ASSERT_TRUE(DeviceDatabase::PutUserFamily(text, UserFamilyJson(identity, "USB Pad", {{"east", "button.1"}}, "learn"),
                                              text, error))
        << error;
    DeviceDatabase database = DeviceDatabase::Builtin();
    ASSERT_TRUE(database.Merge(text, error)) << error;
    const DeviceMatch match = database.Lookup(identity);
    EXPECT_EQ(match.family, "user-12345678");
    EXPECT_EQ(match.name, "USB Pad");
    EXPECT_EQ(match.step, "vid-pid");
    EXPECT_EQ(match.map, (std::map<std::string, std::string>{{"east", "button.1"}})) << "the family was replaced";
    EXPECT_FALSE(DeviceDatabase::PutUserFamily("{\"schema\": 2}", family, text, error));
}
