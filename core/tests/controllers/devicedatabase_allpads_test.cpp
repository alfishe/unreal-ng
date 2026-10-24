/// @file devicedatabase_allpads_test.cpp
/// @brief The device database over the pads one can connect today (POC 033 inventory): lookups by USB and Bluetooth
/// VID:PID, version-split entries, the compatible families from SDL's controller list, and the data-driven `hid` maps
/// (button order, the cross on X / Y, a stick reported as a hat, consumer usages, raw vendor bits) on synthetic
/// descriptors.

#include <gtest/gtest.h>

#include <algorithm>
#include <map>

#include "devicedatabase.h"
#include "hid/hiddescriptor.h"
#include "hid/hidpad.h"

using namespace controllers;

namespace
{
using Events = std::map<std::string, float>;

HidDescriptor Parse(const std::vector<uint8_t>& bytes)
{
    HidDescriptor descriptor;
    std::string error;
    EXPECT_TRUE(ParseHidDescriptor(bytes.data(), bytes.size(), descriptor, error)) << error;
    return descriptor;
}

Events Decode(HidPad& pad, const std::vector<uint8_t>& report)
{
    Events events;
    pad.Decode(report.data(), report.size(), [&events](const std::string& control, float value) {
        events[control] = value;
    });
    return events;
}

bool Has(const std::vector<std::string>& controls, const std::string& control)
{
    return std::find(controls.begin(), controls.end(), control) != controls.end();
}

// Gamepad: X, Y (8 bits, 0..255), ten buttons, six bits of padding; no report ids
const std::vector<uint8_t> kSnesStyle = {0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00,
                                         0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02, 0x05, 0x09, 0x19,
                                         0x01, 0x29, 0x0A, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0A, 0x81,
                                         0x02, 0x75, 0x06, 0x95, 0x01, 0x81, 0x03, 0xC0};
}  // namespace

TEST(DeviceDatabaseAllPads_Test, UsbAndBluetoothIdsFindTheirFamily)
{
    const DeviceDatabase& db = DeviceDatabase::Builtin();
    const struct
    {
        const char* bus;
        uint16_t vid;
        uint16_t pid;
        const char* family;
        const char* images;
    } cases[] = {
        {"usb", 0x045E, 0x02E3, "xbox-elite-1", "xbox-elite-2"},
        {"usb", 0x045E, 0x0719, "xbox-360-wireless", "xbox-360"},
        {"bt", 0x045E, 0x0B21, "xbox-adaptive", "xbox-adaptive"},
        {"usb", 0x054C, 0x0268, "dualshock-3", "dualshock-3"},
        {"bt", 0x054C, 0x0DF2, "dualsense-edge", "dualsense-edge"},
        {"bt", 0x057E, 0x2006, "joy-con-l", "joy-con-l"},
        {"bt", 0x057E, 0x2007, "joy-con-r", "joy-con-r"},
        {"usb", 0x057E, 0x200E, "joy-con-grip", "joy-con-grip"},
        {"bt", 0x057E, 0x2017, "nso-snes", "nso-snes"},
        {"bt", 0x057E, 0x2019, "nso-n64", "nso-n64"},
        {"usb", 0x057E, 0x0337, "gamecube-adapter", "gamecube"},
        {"usb", 0x28DE, 0x1142, "steam-controller", "steam-controller"},
        {"bt", 0x18D1, 0x9400, "stadia", "stadia"},
        {"bt", 0x0171, 0x0419, "luna", "luna"},
        {"usb", 0x046D, 0xC21D, "logitech-f310", "logitech-f310"},
        {"usb", 0x0F0D, 0x00C1, "switch-compatible", "generic-nintendo-like"},
        {"usb", 0x0F0D, 0x0066, "playstation-compatible", "generic-playstation-like"},
        {"usb", 0x0F0D, 0x00AA, "arcade-stick-switch", "arcade-stick"},
        {"usb", 0x0E6F, 0x02A0, "xbox-one-compatible", "generic-xbox-like"},
    };
    for (const auto& c : cases)
    {
        const DeviceMatch match = db.Lookup({c.bus, c.vid, c.pid, -1, "Some Pad", ""});
        EXPECT_EQ(match.family, c.family) << std::hex << c.vid << ":" << c.pid;
        EXPECT_EQ(match.images, c.images) << c.family;
    }
    // A compatible family has no name of its own: the device keeps the one its OS reports
    EXPECT_EQ(db.Lookup({"usb", 0x0F0D, 0x00C1, -1, "HORIPAD for Nintendo Switch", ""}).name,
              "HORIPAD for Nintendo Switch");
    EXPECT_EQ(db.Lookup({"usb", 0x046D, 0xC216, -1, "Logitech Dual Action", ""}).mode, "d-input");
    // An unknown stick by its name
    EXPECT_EQ(db.Lookup({"usb", 0x1234, 0x5678, -1, "Super Fight Stick", ""}).images, "arcade-stick");
}

TEST(DeviceDatabaseAllPads_Test, HidMapsPerMatchAndVersion)
{
    const DeviceDatabase& db = DeviceDatabase::Builtin();
    // The Luna's USB and Bluetooth reports differ: each match carries its own map
    const DeviceMatch usb = db.Lookup({"usb", 0x1949, 0x0419, -1, "Amazon Luna Controller", ""});
    ASSERT_GE(usb.hid.buttons.size(), 10u);
    EXPECT_EQ(usb.hid.buttons[9], "misc.1");
    EXPECT_EQ(usb.hid.rxRyTriggers, 1);
    const DeviceMatch bt = db.Lookup({"bt", 0x0171, 0x0419, -1, "Amazon Luna Controller", ""});
    EXPECT_EQ(bt.hid.rxRyTriggers, -1);
    EXPECT_EQ(bt.hid.consumer.size(), 3u);
    // One VID:PID, two button orders told apart by the version
    const DeviceMatch buffalo = db.Lookup({"usb", 0x0583, 0x2060, 0x0000, "iBuffalo", ""});
    const DeviceMatch retro = db.Lookup({"usb", 0x0583, 0x2060, 0x0100, "Retro Controller", ""});
    ASSERT_FALSE(buffalo.hid.buttons.empty());
    ASSERT_FALSE(retro.hid.buttons.empty());
    EXPECT_EQ(buffalo.hid.buttons[0], "east");
    EXPECT_EQ(retro.hid.buttons[0], "south");
    EXPECT_TRUE(buffalo.hid.dpadAxes);
    EXPECT_EQ(buffalo.step, "exact");
    // The family map reaches the layout; the layout's own fields stay where the map is silent
    const HidLayout stadia = HidLayoutFor(db.ForFamily("stadia"));
    EXPECT_EQ(stadia.family, "xbox");
    ASSERT_GE(stadia.buttons.size(), 9u);
    EXPECT_EQ(stadia.buttons[8], "guide");
    EXPECT_FALSE(stadia.rxRyTriggers);
    // ZL / ZR of the Switch layout drive the triggers at full travel (the picture's `lt` / `rt` light)
    const HidLayout nintendo = HidLayoutFor("nintendo");
    EXPECT_EQ(nintendo.buttons[6], "lt");
    EXPECT_EQ(nintendo.buttons[7], "rt");
    const HidLayout joyCon = HidLayoutFor(db.ForFamily("joy-con-l"));
    EXPECT_EQ(joyCon.hat, "lstick");
    EXPECT_EQ(joyCon.buttons[8], "menu") << "a Joy-Con alone: minus is its menu key, as SwitchProPad names it";
}

TEST(DeviceDatabaseAllPads_Test, MalformedHidMapsAreRefused)
{
    DeviceDatabase db;
    std::string error;
    EXPECT_FALSE(db.Merge(R"({"schema": 1, "families": [{"id": "a", "match": [{"vid": "1234", "pid": ["0001"]}],
        "hid": {"bits": [{"report": 1, "byte": 2, "bit": 8, "control": "misc.1"}]}}]})", error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(db.Merge(R"({"schema": 1, "families": [{"id": "b", "match": [{"vid": "1234", "pid": ["0002"]}],
        "hid": {"consumer": {"xyz": "guide"}}}]})", error));
    EXPECT_FALSE(db.Merge(R"({"schema": 1, "families": [{"id": "c", "match": [{"vid": "1234", "pid": ["0003"]}],
        "hid": {"buttons": "south"}}]})", error));
    EXPECT_TRUE(db.Merge(R"({"schema": 1, "families": [{"id": "d", "match": [{"vid": "1234", "pid": ["0004"]}],
        "hid": {"buttons": ["south", "", "east"], "dpadAxes": true}}]})", error)) << error;
}

TEST(HidPad_Test, DataMap_CrossOnTheAxesAndButtonOrder)
{
    HidLayout layout = HidLayoutFor("generic");
    layout.dpadAxes = true;
    layout.buttons = {"north", "east", "south", "west", "lb", "rb", "", "", "view", "menu"};
    HidPad pad(Parse(kSnesStyle), layout);
    const std::vector<std::string> controls = pad.Controls();
    EXPECT_TRUE(Has(controls, "dpad.left"));
    EXPECT_TRUE(Has(controls, "dpad.down"));
    EXPECT_FALSE(Has(controls, "lstick.x")) << "the axes are the cross, not a stick";
    EXPECT_TRUE(Has(controls, "button.6")) << "an empty name keeps the raw button";

    Decode(pad, {0x80, 0x80, 0x00, 0x00});
    Events events = Decode(pad, {0x00, 0xFF, 0x04, 0x00});
    EXPECT_EQ(events["dpad.left"], 1.0f);
    EXPECT_EQ(events["dpad.down"], 1.0f);
    EXPECT_EQ(events["south"], 1.0f) << "usage 3 is the bottom key on this pad";
    events = Decode(pad, {0xFF, 0x80, 0x00, 0x02});
    EXPECT_EQ(events.count("dpad.left"), 1u);
    EXPECT_EQ(events["dpad.left"], 0.0f);
    EXPECT_EQ(events["dpad.right"], 1.0f);
    EXPECT_EQ(events["dpad.down"], 0.0f);
    EXPECT_EQ(events["south"], 0.0f);
    EXPECT_EQ(events["menu"], 1.0f);
}

TEST(HidPad_Test, DataMap_StickHatConsumerAndVendorBits)
{
    // Report 1: a hat (4 bits, 0..7, null state), four bits of padding, eight buttons, AC Home (consumer), seven
    // bits of padding, then nine vendor bytes; the map reads bits 4 and 7 of payload byte 3
    const std::vector<uint8_t> bytes = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x75, 0x04,
        0x95, 0x01, 0x81, 0x42, 0x75, 0x04, 0x95, 0x01, 0x81, 0x03, 0x05, 0x09, 0x19, 0x01, 0x29, 0x08,
        0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x05, 0x0C, 0x0A, 0x23, 0x02, 0x75,
        0x01, 0x95, 0x01, 0x81, 0x02, 0x75, 0x07, 0x95, 0x01, 0x81, 0x03, 0x06, 0x00, 0xFF, 0x09, 0x01,
        0x75, 0x08, 0x95, 0x09, 0x81, 0x02, 0xC0};
    HidLayout layout = HidLayoutFor("nintendo");
    layout.hat = "lstick";
    layout.consumer = {{0x0223, "guide"}};
    layout.bits = {{1, 3, 4, "paddle.p3"}, {1, 3, 7, "paddle.p1"}};
    HidPad pad(Parse(bytes), layout);
    const std::vector<std::string> controls = pad.Controls();
    EXPECT_TRUE(Has(controls, "lstick.x"));
    EXPECT_TRUE(Has(controls, "guide"));
    EXPECT_TRUE(Has(controls, "paddle.p1"));
    EXPECT_FALSE(Has(controls, "dpad.up"));

    std::vector<uint8_t> report(13, 0x00);
    report[0] = 0x01;
    report[1] = 0x08;  // the hat at rest (null state)
    Decode(pad, report);
    report[1] = 0x03;  // down-right
    report[3] = 0x01;  // AC Home
    report[4] = 0x90;  // payload byte 3: bits 4 and 7
    Events events = Decode(pad, report);
    EXPECT_EQ(events["lstick.x"], 1.0f);
    EXPECT_EQ(events["lstick.y"], 1.0f);
    EXPECT_EQ(events["guide"], 1.0f);
    EXPECT_EQ(events["paddle.p1"], 1.0f);
    EXPECT_EQ(events["paddle.p3"], 1.0f);
    report[1] = 0x06;  // left
    report[4] = 0x10;
    events = Decode(pad, report);
    EXPECT_EQ(events["lstick.x"], -1.0f);
    EXPECT_EQ(events["lstick.y"], 0.0f);
    EXPECT_EQ(events["paddle.p1"], 0.0f);
    EXPECT_EQ(events.count("paddle.p3"), 0u) << "unchanged bits send nothing";
}
