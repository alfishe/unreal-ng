/// @file deviceimport_test.cpp
/// @brief Other projects' mappings as user device entries (device-database.md §3, FR-7): a gamecontrollerdb line and a
/// RetroArch autoconfig become an entry matched by their VID:PID with the button indices named, a name-based GUID is
/// refused, a family's `buttons` rename the HID pad's buttons, and the database-import verb merges the entry over a
/// user photo of the same device and saves it.

#include <gtest/gtest.h>

#include <filesystem>

#include "_helpers/testpathhelper.h"
#include "devicedatabase.h"
#include "deviceimport.h"
#include "hid/hidpad.h"
#include "hostinputcontrol.h"

using namespace controllers;

namespace
{
std::vector<std::string> Names(const JsonValue* list)
{
    std::vector<std::string> names;
    for (const JsonValue& item : list->Items())
        names.push_back(item.AsString());
    return names;
}

class DirOwner : public IHostInputOwner
{
public:
    explicit DirOwner(std::string dir) : _dir(std::move(dir))
    {
    }
    ProfileStore& Profiles() override
    {
        return _store;
    }
    ProfileContext Context() const override
    {
        return {};
    }
    const ResolvedProfiles& Resolved() const override
    {
        return _resolved;
    }
    std::string UserProfileDir() const override
    {
        return _dir + "/profiles";
    }
    std::string ManualProfile() const override
    {
        return {};
    }
    void SetManualProfile(const std::string&) override
    {
    }
    void Reapply() override
    {
    }
    std::string UserControllersDir() const override
    {
        return _dir;
    }

private:
    std::string _dir;
    ProfileStore _store;
    ResolvedProfiles _resolved;
};

class DeviceImport_Test : public ::testing::Test
{
protected:
    void TearDown() override
    {
        std::string error;
        DeviceDatabase::SetUser("", error);
    }
};

// 8BitDo SN30 Pro over USB as SDL writes it: bus 0003, vendor 2dc8, product 6101
const char* kSn30 =
    "03000000c82d00000161000000010000,8BitDo SN30 Pro,a:b1,b:b0,back:b10,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,"
    "dpup:h0.1,leftshoulder:b6,leftstick:b13,lefttrigger:b8,leftx:a0,lefty:a1,rightshoulder:b7,rightstick:b14,"
    "righttrigger:b9,rightx:a2,righty:a3,start:b11,x:b4,y:b3,misc2:b15,platform:Mac OS X,";
}  // namespace

TEST_F(DeviceImport_Test, GameControllerDbLine_ButtonsByIndexMatchedByItsIds)
{
    DeviceImport imported;
    std::string error;
    ASSERT_TRUE(ImportGameControllerDb(kSn30, imported, error)) << error;
    EXPECT_EQ(imported.source, "gamecontrollerdb");
    const JsonValue& entry = imported.entry;
    EXPECT_EQ(entry.Find("id")->AsString(), "user-2dc86101");
    EXPECT_EQ(entry.Find("name")->AsString(), "8BitDo SN30 Pro");
    const JsonValue& match = entry.Find("match")->Items().front();
    EXPECT_EQ(match.Find("vid")->AsString(), "2dc8");
    EXPECT_EQ(Names(match.Find("pid")), std::vector<std::string>{"6101"});
    EXPECT_EQ(match.Find("bus")->AsString(), "usb");
    const std::vector<std::string> buttons = Names(entry.Find("buttons"));
    ASSERT_EQ(buttons.size(), 15u) << "up to the highest index used, b14";
    EXPECT_EQ(buttons[0], "east") << "SDL's b is the right face button";
    EXPECT_EQ(buttons[1], "south");
    EXPECT_EQ(buttons[2], "") << "an index the line does not use keeps the layout's name";
    EXPECT_EQ(buttons[8], "lt") << "a digital trigger";
    EXPECT_EQ(buttons[11], "menu");
    const std::vector<std::string> controls = Names(entry.Find("controls"));
    for (const char* control : {"dpad.up", "south", "lt", "lstick", "rstick", "view", "menu"})
        EXPECT_NE(std::find(controls.begin(), controls.end(), control), controls.end()) << control;
    EXPECT_EQ(imported.skipped, std::vector<std::string>{"misc2:b15"}) << "a name the vocabulary has no control for";

    EXPECT_FALSE(ImportGameControllerDb("05000000426c6f75657472756c650000,Name GUID,a:b0,", imported, error))
        << "a name-based GUID has no USB ids";
    EXPECT_NE(error.find("no USB ids"), std::string::npos) << error;
    EXPECT_FALSE(ImportGameControllerDb("xyz,Pad,a:b0", imported, error));
}

TEST_F(DeviceImport_Test, RetroArchAutoconfig_RetroPadNamesAndDecimalIds)
{
    const std::string cfg = "# a comment\n"
                            "input_driver = \"hid\"\n"
                            "input_device = \"Retro-bit Sega Genesis Pad\"\n"
                            "input_vendor_id = \"4152\"\n"
                            "input_product_id = \"2056\"\n"
                            "input_b_btn = \"1\"\n"
                            "input_a_btn = \"2\"\n"
                            "input_y_btn = \"0\"\n"
                            "input_x_btn = \"3\"\n"
                            "input_start_btn = \"9\"\n"
                            "input_up_btn = \"h0up\"\n"
                            "input_l_x_plus_axis = \"+0\"\n"
                            "input_turbo_btn = \"5\"\n";
    DeviceImport imported;
    std::string error;
    ASSERT_TRUE(ImportDevice(cfg, imported, error)) << error;
    EXPECT_EQ(imported.source, "retroarch");
    EXPECT_EQ(imported.entry.Find("id")->AsString(), "user-10380808") << "4152 = 0x1038, 2056 = 0x0808";
    EXPECT_EQ(imported.entry.Find("name")->AsString(), "Retro-bit Sega Genesis Pad");
    const std::vector<std::string> buttons = Names(imported.entry.Find("buttons"));
    ASSERT_EQ(buttons.size(), 10u);
    EXPECT_EQ(buttons[0], "west");
    EXPECT_EQ(buttons[1], "south") << "RetroPad B is the bottom face button";
    EXPECT_EQ(buttons[2], "east");
    EXPECT_EQ(buttons[3], "north");
    EXPECT_EQ(buttons[9], "menu");
    const std::vector<std::string> controls = Names(imported.entry.Find("controls"));
    EXPECT_NE(std::find(controls.begin(), controls.end(), "dpad.up"), controls.end()) << "a hat direction counts";
    EXPECT_NE(std::find(controls.begin(), controls.end(), "lstick"), controls.end());
    EXPECT_EQ(imported.skipped, std::vector<std::string>{"input_turbo_btn"});

    EXPECT_FALSE(ImportRetroArch("input_device = \"No ids\"\ninput_a_btn = \"1\"\n", imported, error));
    EXPECT_NE(error.find("input_vendor_id"), std::string::npos) << error;
    EXPECT_FALSE(ImportDevice("  \n# only a comment\n", imported, error));
}

TEST_F(DeviceImport_Test, FamilyButtonsRenameTheHidPadButtons)
{
    const HidLayout plain = HidLayoutFor("generic", {});
    EXPECT_TRUE(plain.buttons.empty());
    const HidLayout named = HidLayoutFor("generic", {"east", "", "south"});
    ASSERT_EQ(named.buttons.size(), 3u);
    EXPECT_EQ(named.buttons[0], "east");
    EXPECT_EQ(named.buttons[1], "") << "button.1 stays";
    const HidLayout over = HidLayoutFor("xbox", {"", "west"});
    EXPECT_EQ(over.buttons[0], HidLayoutFor("xbox").buttons[0]) << "the layout's name where the entry has none";
    EXPECT_EQ(over.buttons[1], "west");
}

TEST_F(DeviceImport_Test, ImportVerb_SavesMergedOverAUserPhotoAndTheLookupUsesIt)
{
    namespace fs = std::filesystem;
    const fs::path root = TestPathHelper::GetUniqueTestScratchPath("device-import");
    DirOwner owner(root.string());
    HostInputControl control(owner);

    HostReply reply = control.Execute({"database-import", {{"text", kSn30}, {"save", "false"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_FALSE(fs::exists(root / "devices.user.json")) << "save=false only answers the entry";
    EXPECT_EQ(reply.body.Find("entry")->Find("id")->AsString(), "user-2dc86101");

    // The hotspot editor saved a photo for the pad first: the import keeps its image set
    ASSERT_TRUE(control
                    .Execute({"database-save",
                              {{"text", R"({"id": "user-2dc86101", "name": "My SN30", "images": "user-2dc86101",
                                 "match": [{"vid": "2dc8", "pid": ["6101"]}]})"}}})
                    .Ok());
    reply = control.Execute({"database-import", {{"text", kSn30}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_NE(reply.message.find("imported user-2dc86101 from the gamecontrollerdb mapping"), std::string::npos)
        << reply.message;
    DeviceIdentity identity;
    identity.bus = "usb";
    identity.vendor = 0x2dc8;
    identity.product = 0x6101;
    const DeviceMatch match = DeviceDatabase::Active()->Lookup(identity);
    EXPECT_EQ(match.family, "user-2dc86101");
    EXPECT_EQ(match.images, "user-2dc86101") << "the photo stays";
    EXPECT_EQ(match.name, "8BitDo SN30 Pro") << "the import names what it maps";
    ASSERT_GE(match.buttons.size(), 2u);
    EXPECT_EQ(match.buttons[1], "south");
    EXPECT_EQ(control.Execute({"database-import", {{"text", "nonsense"}}}).error, "bad-request");
}
