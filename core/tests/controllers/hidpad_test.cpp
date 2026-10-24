/// @file hidpad_test.cpp
/// @brief The HID side of the macOS backend on recorded reports (testdata/controllers/hid-captures/xbox-elite2/):
/// the report descriptor parser, the Bluetooth Elite through HidPad (paddles on profile 0), the USB Elite through the
/// dext's GIP report; the press counts agree with SDL3's events of the same sessions (research-host-apis.md §2.3).

#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <sstream>

#include "_helpers/testpathhelper.h"
#include "devicedatabase.h"
#include "hid/drivers/xboxgip.h"
#include "hid/hiddescriptor.h"
#include "hid/hidpad.h"

using namespace controllers;

namespace
{
struct Capture
{
    std::vector<uint8_t> descriptor;
    std::vector<std::vector<uint8_t>> reports;
};

std::vector<uint8_t> Bytes(const std::string& hex)
{
    std::vector<uint8_t> out;
    std::istringstream in(hex);
    std::string byte;
    while (in >> byte)
        out.push_back(static_cast<uint8_t>(std::stoul(byte, nullptr, 16)));
    return out;
}

/// hiddump's log: `descriptor dev0 len=N : <bytes>`, `<ms> dev0 id=XX len=N : <bytes>`
Capture Load(const std::string& relative)
{
    Capture capture;
    std::ifstream in(TestPathHelper::GetTestDataPath(relative));
    std::string line;
    while (std::getline(in, line))
    {
        const size_t colon = line.find(" : ");
        if (colon == std::string::npos)
            continue;
        if (line.rfind("descriptor", 0) == 0)
            capture.descriptor = Bytes(line.substr(colon + 3));
        else if (line.find(" id=") != std::string::npos)
            capture.reports.push_back(Bytes(line.substr(colon + 3)));
    }
    return capture;
}

using Presses = std::map<std::string, int>;

ControlSink Counter(Presses& presses)
{
    return [&presses](const std::string& control, float value) {
        const bool axis = control.size() > 2 && (control.compare(control.size() - 2, 2, ".x") == 0 ||
                                                 control.compare(control.size() - 2, 2, ".y") == 0);
        if (value >= 1.0f && !axis && control != "lt" && control != "rt")
            ++presses[control];
    };
}
}  // namespace

TEST(HidPad_Test, DeviceDatabase_LookupStepsAndTheSyntheticPad)
{
    const DeviceDatabase& db = DeviceDatabase::Builtin();
    ASSERT_GT(db.FamilyCount(), 20u) << "data/controllers/devices.json is compiled in and parses";
    DeviceMatch match = db.Lookup({"bt", 0x045E, 0x0B22, 0x0517, "Xbox Wireless Controller", "iohid"});
    EXPECT_EQ(match.family, "xbox-elite-2");
    EXPECT_EQ(match.step, "exact") << "the entry names the bus";
    EXPECT_EQ(match.layout, "xbox");
    EXPECT_EQ(db.Glyph(match.glyphs, "south"), "A");
    EXPECT_TRUE(db.Lookup({"usb", 0x045E, 0x028E, 0, "GamePad-1", "iohid"}).ignore) << "Apple's synthetic pad";
    EXPECT_EQ(db.Lookup({"usb", 0x045E, 0x028E, 0x0114, "Controller", "iohid"}).family, "xbox-360");
    match = db.Lookup({"usb", 0x2DC8, 0x6001, -1, "SN30 Pro", ""});
    EXPECT_EQ(match.mode, "d-input");
    EXPECT_EQ(match.step, "vid-pid");
    match = db.Lookup({"usb", 0x1234, 0x5678, -1, "Generic DB9 Joystick", ""});
    EXPECT_EQ(match.step, "name");
    EXPECT_EQ(match.name, "Generic DB9 Joystick");
    EXPECT_EQ(db.Lookup({"usb", 0x1234, 0x5678, -1, "Thing", ""}).step, "unknown");
    EXPECT_EQ(db.ForFamily("dualsense").images, "dualsense");
    EXPECT_EQ(db.ForFamily("dualsense-edge").images, "dualsense-edge");
    EXPECT_EQ(db.ForFamily("dualshock-4").images, "dualshock-4");
    EXPECT_EQ(db.ForFamily("8bitdo-m30").images, "8bitdo-m30");
    EXPECT_EQ(db.Lookup({"usb", 0x2DC8, 0x6002, -1, "SN30 Pro+", ""}).family, "8bitdo-sn30-pro-plus");
    EXPECT_EQ(db.ForFamily("fake-pad", "nintendo").images, "generic-nintendo-like");
    EXPECT_EQ(db.ForFamily("fake-pad").images, "generic-unknown");

    DeviceDatabase user = db;
    std::string error;
    ASSERT_TRUE(user.Merge(R"({"schema": 1, "families": [{"id": "my-stick", "name": "Left stick",
        "match": [{"vid": "1234", "pid": ["5678"]}], "layout": "generic"}]})", error)) << error;
    EXPECT_EQ(user.Lookup({"usb", 0x1234, 0x5678, -1, "Thing", ""}).name, "Left stick");
    EXPECT_FALSE(user.Merge(R"({"schema": 1, "families": [{"id": "x", "match": [{"vid": "12345", "pid": []}]}]})",
                            error));
}

TEST(HidPad_Test, Descriptor_FieldsOfTheEliteBluetoothReport)
{
    const Capture capture = Load("controllers/hid-captures/xbox-elite2/macos-ble/session-2026-10-17-raw.txt");
    ASSERT_EQ(capture.descriptor.size(), 464u);
    HidDescriptor descriptor;
    std::string error;
    ASSERT_TRUE(ParseHidDescriptor(capture.descriptor.data(), capture.descriptor.size(), descriptor, error)) << error;
    EXPECT_TRUE(descriptor.hasReportIds);

    std::map<std::pair<uint16_t, uint16_t>, HidField> byUsage;
    for (const HidField& field : descriptor.fields)
        if (field.reportId == 1)
            byUsage[{field.usagePage, field.usage}] = field;
    const auto At = [&byUsage](uint16_t page, uint16_t usage) { return byUsage[{page, usage}]; };
    // research README: [1..4] X Y, [5..8] Z Rz, [9..10] Brake, [11..12] Accelerator, [13] hat, [14..15] buttons,
    // [17] profile (Consumer 0x85), [19] paddles (Consumer 0x81); offsets are bits after the report id
    EXPECT_EQ(At(0x01, 0x30).bitOffset, 0u);
    EXPECT_EQ(At(0x01, 0x30).logicalMax, 65535);
    EXPECT_EQ(At(0x01, 0x35).bitOffset, 48u);
    EXPECT_EQ(At(0x02, 0xC5).bitOffset, 64u);
    EXPECT_EQ(At(0x02, 0xC5).bitSize, 10u);
    EXPECT_EQ(At(0x02, 0xC4).bitOffset, 80u);
    EXPECT_EQ(At(0x01, 0x39).bitOffset, 96u);
    EXPECT_TRUE(At(0x01, 0x39).nullState);
    EXPECT_EQ(At(0x09, 0x01).bitOffset, 104u);
    EXPECT_EQ(At(0x0C, 0x85).bitOffset, 128u);
    EXPECT_EQ(At(0x0C, 0x81).bitOffset, 144u);
    EXPECT_EQ(At(0x0C, 0x81).bitSize, 4u);
}

TEST(HidPad_Test, EliteBluetooth_PressesMatchSdlOnProfileZero)
{
    const Capture capture = Load("controllers/hid-captures/xbox-elite2/macos-ble/session-2026-10-17-raw.txt");
    HidDescriptor descriptor;
    std::string error;
    ASSERT_TRUE(ParseHidDescriptor(capture.descriptor.data(), capture.descriptor.size(), descriptor, error)) << error;
    HidPad pad(descriptor, HidLayoutFor("xbox"));
    ASSERT_FALSE(pad.Empty());

    Presses presses;
    float lastY = 0;
    for (const std::vector<uint8_t>& report : capture.reports)
        pad.Decode(report.data(), report.size(), [&](const std::string& control, float value) {
            Counter(presses)(control, value);
            if (control == "lstick.y")
                lastY = value;
        });
    EXPECT_EQ(pad.Profile(), 0);
    // sdl3-hidapi log of the session; SDL's paddle1..4 are P1, P3, P2, P4
    const Presses sdl = {{"south", 1},     {"east", 2},      {"west", 3},      {"north", 3},     {"dpad.down", 2},
                         {"dpad.left", 1}, {"dpad.up", 1},   {"paddle.p1", 2}, {"paddle.p3", 2}, {"paddle.p2", 7},
                         {"paddle.p4", 2}};
    EXPECT_EQ(presses, sdl);
    EXPECT_LT(std::abs(lastY), 0.1f) << "the stick rests near the centre";
}

TEST(HidPad_Test, EliteBluetooth_ProfileMirrorIsUndone)
{
    const Capture capture = Load("controllers/hid-captures/xbox-elite2/macos-ble/session-2026-10-17-profiles-raw.txt");
    HidDescriptor descriptor;
    std::string error;
    ASSERT_TRUE(ParseHidDescriptor(capture.descriptor.data(), capture.descriptor.size(), descriptor, error)) << error;
    HidPad pad(descriptor, HidLayoutFor("xbox"));
    Presses presses;
    for (const std::vector<uint8_t>& report : capture.reports)
        pad.Decode(report.data(), report.size(), Counter(presses));
    // The owner's sequence: P1-P4 on each of the profiles 0-3, then P4 twice on profile 0 (README, the raw log)
    for (const char* paddle : {"paddle.p1", "paddle.p2", "paddle.p3"})
        EXPECT_EQ(presses[paddle], 4) << paddle;
    EXPECT_EQ(presses["paddle.p4"], 6);
    for (const char* face : {"south", "east", "west", "north"})
        EXPECT_EQ(presses[face], 0) << face << ": the mirrored press belongs to the paddle";
}

TEST(HidPad_Test, EliteUsbGip_PressesMatchSdl)
{
    const Capture capture = Load("controllers/hid-captures/xbox-elite2/macos-usb-dext/session-2026-10-17-raw.txt");
    ASSERT_GT(capture.reports.size(), 1000u);
    ASSERT_TRUE(XboxGipPad::Handles(0x045E, 0x0B00));
    XboxGipPad pad;
    Presses presses;
    for (const std::vector<uint8_t>& report : capture.reports)
        pad.Decode(report.data(), report.size(), Counter(presses));
    const Presses sdl = {{"south", 8},        {"east", 9},     {"view", 1},          {"dpad.down", 3},
                         {"dpad.left", 4},    {"dpad.right", 2}, {"dpad.up", 4},     {"lb", 1},
                         {"lstick.click", 3}, {"rb", 1},       {"rstick.click", 1},  {"menu", 1},
                         {"west", 10},        {"north", 10}};
    EXPECT_EQ(presses, sdl);
}
