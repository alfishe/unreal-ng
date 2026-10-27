/// @file devicestatus_test.cpp
/// @brief The battery and light of host devices as pure logic (devicestatus.h, hostbackend.h), on every OS:
/// GameController's and XInput's battery values, the Linux power supply files as hid-playstation, hid-nintendo and
/// hid-input write them, the text every surface shows, the colour a light verb takes, the power supply and the LED
/// class of a HID device found in a sysfs tree built under scratch/ and the writes that set a colour there, the
/// DualSense light bar report, and the battery / light through the service.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "_helpers/testpathhelper.h"
#include "devicestatus.h"
#include "fake/fakebackend.h"
#include "hid/drivers/dualsenseoutput.h"
#include "hostinputservice.h"

using namespace controllers;
namespace fs = std::filesystem;

namespace
{
void Write(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

BatteryState Battery(bool known, int level, bool coarse, bool charging, bool wired)
{
    BatteryState battery;
    battery.known = known;
    battery.level = level;
    battery.coarse = coarse;
    battery.charging = charging;
    battery.wired = wired;
    return battery;
}

/// A HID device's directory as event7 reaches it (plain directories at the link paths: ':' is not allowed in a
/// Windows path, so the real names stay out)
/// An LED's directory name: the kernel's, with '_' for ':' on Windows, where a path takes no ':'
std::string Led(std::string name)
{
#ifdef _WIN32
    for (char& c : name)
        if (c == ':')
            c = '_';
#endif
    return name;
}

fs::path MakeHid(const fs::path& root)
{
    fs::remove_all(root);
    const fs::path hid = root / "class" / "input" / "event7" / "device" / "device";
    Write(hid / "uevent", "DRIVER=playstation\nHID_ID=0003:0000054C:00000CE6\nHID_NAME=Sony DualSense\n");
    return hid;
}
}  // namespace

TEST(DeviceStatus_Test, GameController_UnknownWithoutALevelFullIsWired)
{
    EXPECT_EQ(BatteryFromGameController(0.0f, -1), BatteryState{}) << "an Elite 2: state unknown, level 0";
    EXPECT_EQ(BatteryFromGameController(0.55f, -1), Battery(true, 55, false, false, false));
    EXPECT_EQ(BatteryFromGameController(0.804f, 0), Battery(true, 80, false, false, false)) << "discharging";
    EXPECT_EQ(BatteryFromGameController(0.4f, 1), Battery(true, 40, false, true, true)) << "charging";
    EXPECT_EQ(BatteryFromGameController(0.0f, 2), Battery(true, 100, false, false, true)) << "full, no level";
    EXPECT_EQ(BatteryFromGameController(1.5f, 0).level, 100) << "clamped";
}

TEST(DeviceStatus_Test, XInput_WiredHasNoLevelBatteriesAreCoarse)
{
    EXPECT_EQ(BatteryFromXInput(0, 0), BatteryState{}) << "disconnected";
    EXPECT_EQ(BatteryFromXInput(0xFF, 3), BatteryState{}) << "unknown";
    EXPECT_EQ(BatteryFromXInput(1, 0), Battery(true, -1, false, false, true)) << "a wired pad";
    EXPECT_EQ(BatteryFromXInput(2, 0), Battery(true, 0, true, false, false));
    EXPECT_EQ(BatteryFromXInput(3, 1).level, 33);
    EXPECT_EQ(BatteryFromXInput(2, 2).level, 67);
    EXPECT_EQ(BatteryFromXInput(3, 3), Battery(true, 100, true, false, false));
}

TEST(DeviceStatus_Test, PowerSupply_StatusCapacityAndCapacityLevel)
{
    // hid-playstation: status and capacity
    EXPECT_EQ(BatteryFromPowerSupply("Discharging\n", "70\n", ""), Battery(true, 70, false, false, false));
    EXPECT_EQ(BatteryFromPowerSupply("Charging\n", "45\n", ""), Battery(true, 45, false, true, true));
    EXPECT_EQ(BatteryFromPowerSupply("Full\n", "100\n", ""), Battery(true, 100, false, false, true));
    EXPECT_EQ(BatteryFromPowerSupply("Full\n", "", ""), Battery(true, 100, false, false, true));
    EXPECT_EQ(BatteryFromPowerSupply("Not charging\n", "", ""), Battery(true, -1, false, false, true));
    // hid-nintendo: capacity_level only
    EXPECT_EQ(BatteryFromPowerSupply("Discharging\n", "", "High\n"), Battery(true, 80, true, false, false));
    EXPECT_EQ(BatteryFromPowerSupply("Charging\n", "", "Critical\n"), Battery(true, 5, true, true, true));
    // hid-input's generic battery before the device answered
    EXPECT_EQ(BatteryFromPowerSupply("Unknown\n", "", "Unknown\n"), BatteryState{});
    EXPECT_EQ(BatteryFromPowerSupply("", "", ""), BatteryState{}) << "no files";
    EXPECT_EQ(BatteryFromPowerSupply("Discharging", "", ""), Battery(true, -1, false, false, false));
    EXPECT_EQ(BatteryFromPowerSupply("Unknown", "250", "").level, -1) << "not a percentage";
    EXPECT_EQ(BatteryFromPowerSupply("Unknown", "x1", "").known, false);
}

TEST(DeviceStatus_Test, BatteryText_IsHonest)
{
    EXPECT_EQ(BatteryText({}), "unknown");
    EXPECT_EQ(BatteryText(Battery(true, 80, false, false, false)), "80 %");
    EXPECT_EQ(BatteryText(Battery(true, 67, true, false, false)), "about 67 %");
    EXPECT_EQ(BatteryText(Battery(true, 40, false, true, true)), "charging, 40 %");
    EXPECT_EQ(BatteryText(Battery(true, -1, false, true, true)), "charging");
    EXPECT_EQ(BatteryText(Battery(true, -1, false, false, true)), "wired");
    EXPECT_EQ(BatteryText(Battery(true, 100, false, false, true)), "wired, 100 %");
    EXPECT_EQ(BatteryText(Battery(true, -1, false, false, false)), "unknown level");
}

TEST(DeviceStatus_Test, LightColor_HexTripleOrOff)
{
    LightColor color;
    ASSERT_TRUE(ParseLightColor("#FF8000", color));
    EXPECT_EQ(color, (LightColor{255, 128, 0}));
    ASSERT_TRUE(ParseLightColor("00ff7f", color));
    EXPECT_EQ(color, (LightColor{0, 255, 127}));
    ASSERT_TRUE(ParseLightColor("12,0,255", color));
    EXPECT_EQ(color, (LightColor{12, 0, 255}));
    ASSERT_TRUE(ParseLightColor("off", color));
    EXPECT_EQ(color, LightColor{});
    for (const char* bad : {"", "#", "#ff80", "#ff800g", "red", "1,2", "1,2,3,4", "256,0,0", "1,,3", "-1,0,0", "1,2,3 ",
                            "1234,0,0"})
        EXPECT_FALSE(ParseLightColor(bad, color)) << "'" << bad << "'";
    EXPECT_EQ(LightColorText({255, 128, 0}), "#ff8000");
    EXPECT_EQ(LightColorText({}), "#000000");
}

TEST(DeviceStatus_Test, Sysfs_PowerSupplyOfTheHidDevice)
{
    const fs::path root = TestPathHelper::GetUniqueTestScratchPath("devicestatus-power");
    const fs::path hid = MakeHid(root);
    EXPECT_EQ(fs::path(HidDeviceDir(root.string(), "event7")), fs::canonical(hid));
    EXPECT_EQ(HidDeviceDir(root.string(), "event9"), "") << "no such node";
    EXPECT_EQ(FindPowerSupply(hid.string()), "") << "no power supply registered";
    EXPECT_EQ(ReadPowerSupply(""), BatteryState{});

    // A mains supply the driver may list as well is not the battery; the device's scope wins
    Write(hid / "power_supply" / "a-mains" / "type", "Mains\n");
    Write(hid / "power_supply" / "b-system" / "type", "Battery\n");
    Write(hid / "power_supply" / "b-system" / "scope", "System\n");
    const fs::path battery = hid / "power_supply" / "ps-controller-battery-a0b1c2d3e4f5";
    Write(battery / "type", "Battery\n");
    Write(battery / "scope", "Device\n");
    Write(battery / "status", "Discharging\n");
    Write(battery / "capacity", "70\n");
    Write(battery / "present", "1\n");
    const std::string found = FindPowerSupply(hid.string());
    EXPECT_EQ(fs::path(found), battery);
    EXPECT_EQ(ReadPowerSupply(found), Battery(true, 70, false, false, false));
    Write(battery / "present", "0\n");
    EXPECT_EQ(ReadPowerSupply(found), BatteryState{}) << "not present";
    fs::remove_all(root);
}

TEST(DeviceStatus_Test, Sysfs_MulticolorLightOfADualSense)
{
    const fs::path root = TestPathHelper::GetUniqueTestScratchPath("devicestatus-multicolor");
    const fs::path hid = MakeHid(root);
    EXPECT_EQ(FindLight(hid.string()).kind, SysfsLight::Kind::None);
    EXPECT_NE(SysfsLightUnavailable(FindLight(hid.string()), {}).find("no colour light"), std::string::npos);

    // hid-playstation: player LEDs of one colour, then the light bar
    Write(hid / "leds" / Led("input12:white:player-1") / "max_brightness", "1\n");
    const fs::path bar = hid / "leds" / Led("input12:rgb:indicator");
    Write(bar / "multi_index", "red green blue\n");
    Write(bar / "max_brightness", "255\n");
    Write(bar / "multi_intensity", "0 0 255\n");
    Write(bar / "brightness", "255\n");
    const SysfsLight light = FindLight(hid.string());
    ASSERT_EQ(light.kind, SysfsLight::Kind::Multicolor);
    EXPECT_EQ(fs::path(light.multicolor), bar);
    EXPECT_EQ(light.Files().size(), 2u);

    const auto writes = LightWrites(light, {255, 128, 0});
    ASSERT_EQ(writes.size(), 2u);
    EXPECT_EQ(fs::path(writes[0].first), bar / "multi_intensity");
    EXPECT_EQ(writes[0].second, "255 128 0");
    EXPECT_EQ(fs::path(writes[1].first), bar / "brightness");
    EXPECT_EQ(writes[1].second, "255");
    EXPECT_EQ(LightWrites(light, {})[1].second, "0") << "off: brightness 0";

    // Another order and range: each channel by its name, scaled to max_brightness
    Write(bar / "multi_index", "blue green red white\n");
    Write(bar / "max_brightness", "100\n");
    const auto scaled = LightWrites(FindLight(hid.string()), {255, 128, 0});
    EXPECT_EQ(scaled[0].second, "0 50 100 0");
    EXPECT_EQ(scaled[1].second, "100");

    const std::string why = SysfsLightUnavailable(light, writes[0].first);
    EXPECT_NE(why.find("multi_intensity"), std::string::npos) << why;
    EXPECT_NE(why.find("70-unreal-ng.rules"), std::string::npos) << "names the udev rule: " << why;
    fs::remove_all(root);
}

TEST(DeviceStatus_Test, Sysfs_ChannelLightsOfADualShock4)
{
#ifdef _WIN32
    GTEST_SKIP() << "the channels are found by their ':red' / ':green' / ':blue' names, which a Windows path cannot hold";
#else
    const fs::path root = TestPathHelper::GetUniqueTestScratchPath("devicestatus-channels");
    const fs::path hid = MakeHid(root);
    // hid-sony: <hid>:red, :green, :blue and :global; a lone red indicator (sorted first) is not a light
    Write(hid / "leds" / "0000-lone:red" / "max_brightness", "255\n");
    for (const char* name : {"0003:054C:09CC.0001:red", "0003:054C:09CC.0001:green", "0003:054C:09CC.0001:blue",
                             "0003:054C:09CC.0001:global"})
        Write(hid / "leds" / name / "max_brightness", "255\n");
    const SysfsLight light = FindLight(hid.string());
    ASSERT_EQ(light.kind, SysfsLight::Kind::Channels);
    EXPECT_NE(light.channels[0].find("09CC.0001:red"), std::string::npos) << "the complete triple";
    EXPECT_NE(light.channels[2].find(":blue"), std::string::npos);
    const auto writes = LightWrites(light, {255, 128, 0});
    ASSERT_EQ(writes.size(), 3u);
    EXPECT_EQ(writes[0].second, "255");
    EXPECT_EQ(writes[1].second, "128");
    EXPECT_EQ(writes[2].second, "0");
    EXPECT_EQ(fs::path(writes[1].first).filename(), "brightness");
    for (const auto& [file, text] : writes)
        std::ofstream(file) << text;
    std::ifstream green(fs::path(light.channels[1]) / "brightness");
    std::string written;
    std::getline(green, written);
    EXPECT_EQ(written, "128");
    fs::remove_all(root);
#endif
}

TEST(DeviceStatus_Test, DualSense_LightReportSetsOnlyTheLightBar)
{
    const std::vector<uint8_t> first = DualSenseOutput::UsbLightReport({255, 128, 1}, true);
    ASSERT_EQ(first.size(), 48u);
    EXPECT_EQ(first[0], 0x02);
    EXPECT_EQ(first[1], 0x00) << "valid flag 0: motors and triggers untouched";
    EXPECT_EQ(first[2], 0x04) << "valid flag 1: light bar control";
    EXPECT_EQ(first[39], 0x02) << "valid flag 2: light bar setup";
    EXPECT_EQ(first[42], 0x02) << "light out: the firmware's light ends";
    EXPECT_EQ(first[45], 255);
    EXPECT_EQ(first[46], 128);
    EXPECT_EQ(first[47], 1);
    const std::vector<uint8_t> next = DualSenseOutput::UsbLightReport({0, 0, 255}, false);
    EXPECT_EQ(next[39], 0x00);
    EXPECT_EQ(next[42], 0x00);
    EXPECT_EQ(next[47], 255);
    // The rumble report leaves the light alone
    EXPECT_EQ(DualSenseOutput::UsbReport(RumbleLevels{})[2], 0x00);
}

TEST(DeviceStatus_Test, Service_PassesBatteryAndLightThrough)
{
    HostInputService service;
    auto owned = std::make_unique<FakeBackend>();
    FakeBackend* fake = owned.get();
    std::string reason;
    ASSERT_TRUE(service.AddBackend(std::move(owned), reason)) << reason;
    fake->SetBattery("fake:1", Battery(true, 30, false, false, false));
    fake->SetHasLight("fake:1", true);
    ASSERT_TRUE(fake->Connect({"fake:1", "Pad", "dualsense", "playstation", "fake"}));
    EXPECT_EQ(service.Battery("fake:1"), Battery(true, 30, false, false, false));
    EXPECT_EQ(service.Battery("fake:9"), BatteryState{}) << "not connected: unknown";
    EXPECT_TRUE(service.HasLight("fake:1"));
    EXPECT_EQ(service.LightUnavailable("fake:1"), "");
    EXPECT_EQ(service.LightUnavailable("fake:9"), "The device is not connected");
    EXPECT_TRUE(service.SetLight("fake:1", {1, 2, 3}));
    EXPECT_FALSE(service.SetLight("fake:9", {1, 2, 3}));
    ASSERT_EQ(fake->Lights("fake:1").size(), 1u);
    EXPECT_EQ(fake->Lights("fake:1")[0], (LightColor{1, 2, 3}));
    fake->SetHasLight("fake:1", false);
    EXPECT_FALSE(service.SetLight("fake:1", {1, 2, 3})) << "the service asks HasLight first";
    EXPECT_EQ(fake->Lights("fake:1").size(), 1u);
    service.Stop();
}
