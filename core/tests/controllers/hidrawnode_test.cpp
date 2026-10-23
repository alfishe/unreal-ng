/// @file hidrawnode_test.cpp
/// @brief The Linux hidraw side of the evdev backend as pure logic (hid/hidrawnode.h), on every OS: which devices want
/// their hidraw node, the HID device's uevent, the node of an evdev node found in a sysfs tree built under scratch/
/// (plain directories everywhere, the real symlink layout where the OS makes symlinks freely), the reasons and the
/// udev rule the dialog names, the endpoint merge of evdev and hidraw into one device (the Bluetooth Elite capture
/// through it), the notice through the service, and the shipped rules file.

#include <gtest/gtest.h>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "fake/fakebackend.h"
#include "hid/hiddescriptor.h"
#include "hid/hidpad.h"
#include "hid/hidrawnode.h"
#include "hostinputservice.h"

using namespace controllers;
namespace fs = std::filesystem;

namespace
{
/// A Bluetooth LE Elite 2 through BlueZ's uhid, as /sys/bus/hid/devices/0005:045E:0B22.0004/uevent reads
constexpr const char* kEliteUevent = "DRIVER=microsoft\n"
                                     "HID_ID=0005:0000045E:00000B22\n"
                                     "HID_NAME=Xbox Wireless Controller\n"
                                     "HID_PHYS=a0:b1:c2:d3:e4:f5\n"
                                     "HID_UNIQ=11:22:33:44:55:66\n"
                                     "MODALIAS=hid:b0005g0001v0000045Ep00000B22\n";

/// A sysfs tree under @p root with one HID device and its evdev node event7, the way the kernel lays it out:
/// devices/.../<hid>/{uevent, hidraw/hidraw3, input/input12/event7}, class/input/event7 -> the event directory,
/// eventN/device -> inputM, inputM/device -> the HID device. @p symlinks false: plain directories at the link paths
void MakeTree(const fs::path& root, const std::string& uevent, bool hidraw, bool symlinks)
{
    fs::remove_all(root);
    if (!symlinks)
    {
        // The plain tree has no directory named like the HID device: ':' is not allowed in a Windows path
        const fs::path input = root / "class" / "input" / "event7" / "device";
        fs::create_directories(input / "device");
        std::ofstream(input / "device" / "uevent") << uevent;
        if (hidraw)
            fs::create_directories(input / "device" / "hidraw" / "hidraw3");
        return;
    }
    const fs::path hid = root / "devices" / "virtual" / "misc" / "uhid" / "0005:045E:0B22.0004";
    fs::create_directories(hid);
    std::ofstream(hid / "uevent") << uevent;
    if (hidraw)
        fs::create_directories(hid / "hidraw" / "hidraw3");
    const fs::path input = hid / "input" / "input12";
    fs::create_directories(input / "event7");
    fs::create_directory_symlink("../..", input / "device");    // input12/device -> the HID device
    fs::create_directory_symlink("..", input / "event7" / "device");  // event7/device -> input12
    fs::create_directories(root / "class" / "input");
    fs::create_directory_symlink(fs::relative(input / "event7", root / "class" / "input"),
                                 root / "class" / "input" / "event7");
}

std::vector<uint8_t> Bytes(const std::string& hex)
{
    std::vector<uint8_t> out;
    std::istringstream in(hex);
    std::string byte;
    while (in >> byte)
        out.push_back(static_cast<uint8_t>(std::stoul(byte, nullptr, 16)));
    return out;
}
}  // namespace

TEST(HidrawNode_Test, UseFor_EliteOverBluetoothReadsTheDialWrites)
{
    EXPECT_EQ(HidrawUseFor(true, 0x045E, 0x0B22, "xbox-elite-2"), HidrawUse::Input);
    EXPECT_EQ(HidrawUseFor(true, 0x045E, 0x0B05, "xbox-elite-2"), HidrawUse::Input);
    EXPECT_EQ(HidrawUseFor(false, 0x045E, 0x0B00, "xbox-elite-2"), HidrawUse::None) << "USB: xpad, no hidraw node";
    EXPECT_EQ(HidrawUseFor(true, 0x045E, 0x091B, "surface-dial"), HidrawUse::Output);
    EXPECT_EQ(HidrawUseFor(true, 0x045E, 0x0B13, "xbox-series"), HidrawUse::None) << "evdev gives the whole pad";
}

TEST(HidrawNode_Test, Uevent_ParsesTheIdsAndRefusesANonHidDevice)
{
    HidUevent uevent;
    ASSERT_TRUE(ParseHidUevent(kEliteUevent, uevent));
    EXPECT_EQ(uevent.bus, 0x0005);
    EXPECT_EQ(uevent.vendor, 0x045E);
    EXPECT_EQ(uevent.product, 0x0B22);
    EXPECT_EQ(uevent.name, "Xbox Wireless Controller");
    EXPECT_EQ(uevent.phys, "a0:b1:c2:d3:e4:f5");
    EXPECT_EQ(uevent.uniq, "11:22:33:44:55:66");

    HidUevent dial;
    ASSERT_TRUE(ParseHidUevent("HID_ID=0005:0000045E:0000091B\r\nHID_NAME=Surface Dial System Multi Axis\r\n", dial));
    EXPECT_EQ(dial.product, 0x091B) << "CRLF lines";
    EXPECT_EQ(dial.name, "Surface Dial System Multi Axis");

    // xpad's parent is the USB interface: no HID_ID
    HidUevent none;
    EXPECT_FALSE(ParseHidUevent("DEVTYPE=usb_interface\nDRIVER=xpad\nINTERFACE=255/71/208\n", none));
    EXPECT_FALSE(ParseHidUevent("HID_ID=0005:0000045E\n", none)) << "two fields";
    EXPECT_FALSE(ParseHidUevent("HID_ID=0005:0000045E:0000ZZZZ\n", none)) << "not hex";
    EXPECT_FALSE(ParseHidUevent("HID_ID=0005:0001045E:00000B22\n", none)) << "a vendor above 16 bits";
}

TEST(HidrawNode_Test, Find_TheNodeOfAnEvdevNodesHidDevice)
{
    const fs::path root = TestPathHelper::GetUniqueTestScratchPath("hidrawnode-sys");
    for (const bool symlinks : {false, true})
    {
#ifdef _WIN32
        if (symlinks)
            continue;  // directory symlinks need a privilege on Windows; the plain tree runs the same lookup
#endif
        SCOPED_TRACE(symlinks ? "symlinks" : "plain directories");
        MakeTree(root, kEliteUevent, true, symlinks);
        HidrawNode found;
        ASSERT_TRUE(FindHidrawNode(root.string(), "/dev", "event7", 0x045E, 0x0B22, found));
        EXPECT_EQ(found.node, "/dev/hidraw3");
        EXPECT_EQ(found.uevent.product, 0x0B22);
        if (symlinks)
            EXPECT_EQ(fs::path(found.hidDevice).filename().string(), "0005:045E:0B22.0004");

        EXPECT_FALSE(FindHidrawNode(root.string(), "/dev", "event7", 0x045E, 0x091B, found)) << "another device";
        EXPECT_FALSE(FindHidrawNode(root.string(), "/dev", "event8", 0x045E, 0x0B22, found)) << "no such node";

        MakeTree(root, kEliteUevent, false, symlinks);
        EXPECT_FALSE(FindHidrawNode(root.string(), "/dev", "event7", 0x045E, 0x0B22, found)) << "no hidraw child";

        MakeTree(root, "DEVTYPE=usb_interface\nDRIVER=xpad\n", true, symlinks);
        EXPECT_FALSE(FindHidrawNode(root.string(), "/dev", "event7", 0x045E, 0x0B22, found)) << "not a HID device";
    }
    fs::remove_all(root);
}

TEST(HidrawNode_Test, Reasons_NameTheNodeAndTheUdevRule)
{
    const std::string denied = HidrawUnavailable(HidrawUse::Input, "/dev/hidraw3", EACCES, "Permission denied");
    EXPECT_NE(denied.find("paddles"), std::string::npos) << denied;
    EXPECT_NE(denied.find("read access to /dev/hidraw3"), std::string::npos) << denied;
    EXPECT_NE(denied.find("data/controllers/70-unreal-ng.rules"), std::string::npos) << denied;
    EXPECT_NE(denied.find("udevadm control --reload"), std::string::npos) << denied;
    EXPECT_NE(denied.find("profiles 1-3"), std::string::npos) << "what the user sees until then: " << denied;

    const std::string dial = HidrawUnavailable(HidrawUse::Output, "/dev/hidraw5", EPERM, "Operation not permitted");
    EXPECT_NE(dial.find("Surface Dial"), std::string::npos) << dial;
    EXPECT_NE(dial.find("write access to /dev/hidraw5"), std::string::npos) << dial;
    EXPECT_NE(dial.find("70-unreal-ng.rules"), std::string::npos) << dial;
    EXPECT_EQ(dial.find("paddles"), std::string::npos) << dial;

    const std::string missing = HidrawUnavailable(HidrawUse::Output, {}, 0, {});
    EXPECT_NE(missing.find("not found"), std::string::npos) << missing;
    EXPECT_EQ(missing.find("udevadm"), std::string::npos) << "no rule fixes a missing node: " << missing;

    const std::string other = HidrawUnavailable(HidrawUse::Input, "/dev/hidraw3", EBUSY, "Device or resource busy");
    EXPECT_NE(other.find("Device or resource busy"), std::string::npos) << other;
    EXPECT_EQ(other.find("udevadm"), std::string::npos) << other;

    const std::string usb = XpadPaddlesNotice();
    EXPECT_NE(usb.find("profile 0"), std::string::npos) << "D15: " << usb;
}

TEST(HidrawNode_Test, Merge_OneSourceAtATimeAndASwitchReleasesWhatWasHeld)
{
    std::vector<std::pair<std::string, float>> out;
    const ControlSink sink = [&out](const std::string& control, float value) { out.emplace_back(control, value); };
    HidrawSourceMerge merge;
    EXPECT_EQ(merge.Active(), HidrawSourceMerge::Source::Evdev);
    merge.Control(HidrawSourceMerge::Source::Evdev, "south", 1.0f, sink);
    merge.Control(HidrawSourceMerge::Source::Evdev, "lstick.x", -0.5f, sink);
    merge.Control(HidrawSourceMerge::Source::Evdev, "east", 1.0f, sink);
    merge.Control(HidrawSourceMerge::Source::Evdev, "east", 0.0f, sink);
    merge.Control(HidrawSourceMerge::Source::Hidraw, "south", 1.0f, sink);  // the same press from the other endpoint
    ASSERT_EQ(out.size(), 4u) << "hidraw is dropped while evdev is the source";

    out.clear();
    merge.Switch(HidrawSourceMerge::Source::Hidraw, sink);
    const std::vector<std::pair<std::string, float>> released = {{"lstick.x", 0.0f}, {"south", 0.0f}};
    EXPECT_EQ(out, released) << "what evdev still held is released; east was already up";

    out.clear();
    merge.Control(HidrawSourceMerge::Source::Evdev, "north", 1.0f, sink);
    merge.Control(HidrawSourceMerge::Source::Hidraw, "paddle.p1", 1.0f, sink);
    const std::vector<std::pair<std::string, float>> only = {{"paddle.p1", 1.0f}};
    EXPECT_EQ(out, only);

    out.clear();
    merge.Switch(HidrawSourceMerge::Source::Hidraw, sink);
    EXPECT_TRUE(out.empty()) << "a switch to the active source changes nothing";
    merge.Switch(HidrawSourceMerge::Source::Evdev, sink);  // the hidraw node went away
    const std::vector<std::pair<std::string, float>> back = {{"paddle.p1", 0.0f}};
    EXPECT_EQ(out, back);
}

TEST(HidrawNode_Test, Merge_EliteBluetoothCaptureThroughHidrawCountsEachPressOnce)
{
    // The macOS BLE capture is the report hidraw gives on Linux too: the id first, then the 19 bytes of report 1
    std::ifstream in(
        TestPathHelper::GetTestDataPath("controllers/hid-captures/xbox-elite2/macos-ble/session-2026-10-17-raw.txt"));
    std::vector<uint8_t> descriptorBytes;
    std::vector<std::vector<uint8_t>> reports;
    std::string line;
    while (std::getline(in, line))
    {
        const size_t colon = line.find(" : ");
        if (colon == std::string::npos)
            continue;
        if (line.rfind("descriptor", 0) == 0)
            descriptorBytes = Bytes(line.substr(colon + 3));
        else if (line.find(" id=") != std::string::npos)
            reports.push_back(Bytes(line.substr(colon + 3)));
    }
    HidDescriptor descriptor;
    std::string error;
    ASSERT_TRUE(ParseHidDescriptor(descriptorBytes.data(), descriptorBytes.size(), descriptor, error)) << error;
    HidPad pad(descriptor, HidLayoutFor("xbox"));

    std::map<std::string, int> presses;
    const ControlSink sink = [&presses](const std::string& control, float value) {
        const bool axis = control.size() > 2 && (control.compare(control.size() - 2, 2, ".x") == 0 ||
                                                 control.compare(control.size() - 2, 2, ".y") == 0);
        if (value >= 1.0f && !axis && control != "lt" && control != "rt")
            ++presses[control];
    };
    HidrawSourceMerge merge;
    merge.Switch(HidrawSourceMerge::Source::Hidraw, sink);
    for (const std::vector<uint8_t>& report : reports)
        pad.Decode(report.data(), report.size(), [&](const std::string& control, float value) {
            merge.Control(HidrawSourceMerge::Source::Hidraw, control, value, sink);
            // evdev reports the same buttons from its node at the same time: never twice
            merge.Control(HidrawSourceMerge::Source::Evdev, control, value, sink);
        });
    // The counts of HidPad_Test.EliteBluetooth_PressesMatchSdlOnProfileZero (SDL3's log of the session)
    const std::map<std::string, int> sdl = {{"south", 1},     {"east", 2},      {"west", 3},      {"north", 3},
                                            {"dpad.down", 2}, {"dpad.left", 1}, {"dpad.up", 1},   {"paddle.p1", 2},
                                            {"paddle.p3", 2}, {"paddle.p2", 7}, {"paddle.p4", 2}};
    EXPECT_EQ(presses, sdl);
}

TEST(HidrawNode_Test, Notice_ReachesTheServiceFromTheBackend)
{
    HostInputService service;
    auto owned = std::make_unique<FakeBackend>();
    FakeBackend* fake = owned.get();
    std::string reason;
    ASSERT_TRUE(service.AddBackend(std::move(owned), reason)) << reason;
    ASSERT_TRUE(fake->Connect({"bt:045e:0b22:00000001", "Elite", "xbox-elite-2", "xbox", "fake"}));
    EXPECT_EQ(service.DeviceNotice("bt:045e:0b22:00000001"), "");
    fake->SetNotice("bt:045e:0b22:00000001", HidrawUnavailable(HidrawUse::Input, "/dev/hidraw3", EACCES, ""));
    EXPECT_NE(service.DeviceNotice("bt:045e:0b22:00000001").find("/dev/hidraw3"), std::string::npos);
    EXPECT_EQ(service.DeviceNotice("bt:045e:0b22:00000002"), "") << "an unknown device has none";
    service.Stop();
}

TEST(HidrawNode_Test, RulesFile_GivesTheEliteAndTheDialToTheSeatUser)
{
    std::ifstream in(TestPathHelper::FindProjectRoot() / "data" / "controllers" / "70-unreal-ng.rules");
    ASSERT_TRUE(in) << "data/controllers/70-unreal-ng.rules, named by kHidrawRulesHint";
    std::stringstream text;
    text << in.rdbuf();
    std::map<std::string, bool> hidraw = {{"045E:0B05", false}, {"045E:0B22", false}, {"045E:091B", false}};
    std::istringstream lines(text.str());
    std::string line;
    while (std::getline(lines, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        EXPECT_NE(line.find("TAG+=\"uaccess\""), std::string::npos) << line;
        for (auto& [id, seen] : hidraw)
            if (line.find("KERNEL==\"hidraw*\"") != std::string::npos && line.find("*" + id + "*") != std::string::npos)
                seen = true;
    }
    for (const auto& [id, seen] : hidraw)
        EXPECT_TRUE(seen) << id;
    EXPECT_NE(std::string(kHidrawRulesHint).find("70-unreal-ng.rules"), std::string::npos);
}
