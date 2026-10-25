/// @file windowsdevices_test.cpp
/// @brief The decisions of the Windows backends (platform/windowsdevices.h), on every OS: what a HID interface path
/// says, which reader serves a collection (the XInput-class endpoints left to GameInput / XInput: one device once), the
/// device ids, a field located from the bits HidP_Set* wrote and decoded by HidPad from a report with Windows' leading
/// report id 0, the HidD_Set* buffer, GameInput's gamepad state as canonical controls. On Windows also the native
/// backends as the factory starts them.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <map>

#include "hid/hidpad.h"
#include "hostcontrols.h"
#include "platform/windowsdevices.h"

#if defined(_WIN32)
#include <chrono>
#include <iostream>

#include "hostinputservice.h"
#endif

using namespace controllers;

namespace
{
using Events = std::vector<std::pair<std::string, float>>;

ControlSink Collect(Events& events)
{
    return [&events](const std::string& control, float value) { events.emplace_back(control, value); };
}

float Find(const Events& events, const std::string& control, float missing = -99.0f)
{
    for (const auto& [name, value] : events)
        if (name == control)
            return value;
    return missing;
}
}  // namespace

TEST(WindowsDevices_Test, HidPathsSayBusIdsAndXInputClass)
{
    // A wired Xbox pad's HID endpoint: XInput-class (IG_), the fake endpoint SDL ignores too
    WinHidPath xbox = ParseWinHidPath(
        R"(\\?\HID#VID_045E&PID_0B12&IG_00#8&2d6d0e9c&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030})");
    EXPECT_EQ(xbox.bus, "usb");
    EXPECT_TRUE(xbox.haveIds);
    EXPECT_EQ(xbox.vendor, 0x045E);
    EXPECT_EQ(xbox.product, 0x0B12);
    EXPECT_TRUE(xbox.xinput);

    WinHidPath dualSense = ParseWinHidPath(
        R"(\\?\hid#vid_054c&pid_0ce6&mi_03#8&1a2b3c4d&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030})");
    EXPECT_EQ(dualSense.bus, "usb");
    EXPECT_EQ(dualSense.vendor, 0x054C);
    EXPECT_EQ(dualSense.product, 0x0CE6);
    EXPECT_FALSE(dualSense.xinput);

    // Bluetooth classic: the vendor id carries its source (0002 = USB-IF) in front
    WinHidPath ds4 = ParseWinHidPath(R"(\\?\HID#{00001124-0000-1000-8000-00805f9b34fb}_VID&0002054c_PID&09cc#9&2f1e&0&0000)"
                                     R"(#{4d1e55b2-f16f-11cf-88cb-001111000030})");
    EXPECT_EQ(ds4.bus, "bt");
    EXPECT_EQ(ds4.vendor, 0x054C);
    EXPECT_EQ(ds4.product, 0x09CC);

    // HID over GATT (the Surface Dial): two digits of source
    WinHidPath dial = ParseWinHidPath(
        R"(\\?\HID#{00001812-0000-1000-8000-00805f9b34fb}_Dev_VID&02045e_PID&091b_REV&0001_c0a1b2c3d4e5&Col01#a&1&0000)"
        R"(#{4d1e55b2-f16f-11cf-88cb-001111000030})");
    EXPECT_EQ(dial.bus, "bt");
    EXPECT_EQ(dial.vendor, 0x045E);
    EXPECT_EQ(dial.product, 0x091B);
    EXPECT_FALSE(dial.xinput);

    WinHidPath i2c = ParseWinHidPath(R"(\\?\HID#ACPI0C50&Col01#4&1d2c&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030})");
    EXPECT_FALSE(i2c.haveIds);
    EXPECT_EQ(i2c.bus, "");
}

TEST(WindowsDevices_Test, RawInputRolesServeEachDeviceOnce)
{
    const std::string pad = R"(\\?\HID#VID_054C&PID_0CE6&MI_03#8&1&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030})";
    const std::string xbox = R"(\\?\HID#VID_045E&PID_0B12&IG_00#8&2&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030})";
    EXPECT_EQ(WinRawInputRole(0x01, 0x05, pad), WinHidRole::Pad);
    EXPECT_EQ(WinRawInputRole(0x01, 0x04, pad), WinHidRole::Pad) << "a joystick";
    EXPECT_EQ(WinRawInputRole(0x01, 0x08, pad), WinHidRole::Pad) << "a multi-axis controller";
    EXPECT_EQ(WinRawInputRole(0x01, 0x05, xbox), WinHidRole::Skip) << "GameInput or XInput serves it";
    EXPECT_EQ(WinRawInputRole(0x01, 0x0E, pad), WinHidRole::Dial) << "System Multi-Axis Controller";
    EXPECT_EQ(WinRawInputRole(0x01, 0x06, pad), WinHidRole::Skip) << "a keyboard";
    EXPECT_EQ(WinRawInputRole(0x0C, 0x01, pad), WinHidRole::Skip) << "consumer control";

    // GameInput takes the XInput-class families only; a HID pad stays with Raw Input and the shared drivers
    EXPECT_TRUE(GameInputServes(kGameInputFamilyXboxOne));
    EXPECT_TRUE(GameInputServes(kGameInputFamilyXbox360));
    EXPECT_FALSE(GameInputServes(3)) << "GameInputFamilyHid";
    EXPECT_FALSE(GameInputServes(-1)) << "virtual";
}

TEST(WindowsDevices_Test, DeviceIdsFollowTheIoHidForm)
{
    const std::string path = R"(\\?\HID#VID_054C&PID_0CE6&MI_03#8&1A2B&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030})";
    EXPECT_EQ(WinDeviceId("usb", 0x054C, 0x0CE6, 0x0100, "ABC123", path), "usb:054c:0ce6:0100:ABC123");
    const std::string noSerial = WinDeviceId("usb", 0x054C, 0x0CE6, 0x0100, "", path);
    EXPECT_EQ(noSerial.rfind("usb:054c:0ce6:0100@", 0), 0u) << noSerial;
    EXPECT_EQ(noSerial.size(), std::string("usb:054c:0ce6:0100@").size() + 8) << "a hash, not the path";
    std::string upper = path;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(toupper(c)); });
    EXPECT_EQ(WinDeviceId("usb", 0x054C, 0x0CE6, 0x0100, "", upper), noSerial) << "the path's case does not count";
    EXPECT_NE(WinDeviceId("usb", 0x054C, 0x0CE6, 0x0100, "", path + "x"), noSerial);

    const std::string bt = WinDeviceId("bt", 0x045E, 0x091B, 0x0001, "", path);
    EXPECT_EQ(bt.rfind("bt:045e:091b:", 0), 0u) << bt;
    EXPECT_EQ(bt.find('@'), std::string::npos);
    EXPECT_EQ(WinDeviceId("bt", 0x045E, 0x091B, 0x0001, "c0a1b2c3d4e5", path),
              WinDeviceId("bt", 0x045E, 0x091B, 0x0001, "c0a1b2c3d4e5", path + "other"))
        << "a Bluetooth id hashes the serial when there is one, not the path";
    EXPECT_EQ(WinDeviceId("", 0x1234, 0x5678, 0x0001, "S", path), "hid:1234:5678:0001:S");
}

TEST(WindowsDevices_Test, FieldsLocatedFromTheBitsHidPWrote)
{
    // Byte 0 is the report id; a 4-bit field at bits 12..15 of the payload
    uint8_t cleared[5] = {0x03, 0x00, 0x00, 0x00, 0x00};
    uint8_t set[5] = {0x03, 0x00, 0xF0, 0x00, 0x00};
    uint32_t offset = 0;
    uint8_t size = 0;
    ASSERT_TRUE(LocateHidBits(cleared, set, sizeof(cleared), offset, size));
    EXPECT_EQ(offset, 12u);
    EXPECT_EQ(size, 4u);

    // Across bytes, with neighbours already set in both (a null hat beside it)
    uint8_t neighboursCleared[4] = {0x00, 0x0F, 0x00, 0x80};
    uint8_t neighboursSet[4] = {0x00, 0xFF, 0x7F, 0x80};
    ASSERT_TRUE(LocateHidBits(neighboursCleared, neighboursSet, sizeof(neighboursSet), offset, size));
    EXPECT_EQ(offset, 4u);
    EXPECT_EQ(size, 11u);

    uint8_t one[3] = {0x00, 0x00, 0x00};
    uint8_t oneSet[3] = {0x00, 0x00, 0x02};
    ASSERT_TRUE(LocateHidBits(one, oneSet, sizeof(one), offset, size));
    EXPECT_EQ(offset, 9u);
    EXPECT_EQ(size, 1u);

    uint8_t gap[3] = {0x00, 0x05, 0x00};
    EXPECT_FALSE(LocateHidBits(one, gap, sizeof(one), offset, size)) << "not one field";
    EXPECT_FALSE(LocateHidBits(one, one, sizeof(one), offset, size)) << "nothing written";
    uint8_t idOnly[3] = {0x07, 0x00, 0x00};
    EXPECT_FALSE(LocateHidBits(one, idOnly, sizeof(one), offset, size)) << "the report id is not a field";
    uint8_t wideCleared[6] = {};
    uint8_t wideSet[6] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};
    EXPECT_FALSE(LocateHidBits(wideCleared, wideSet, sizeof(wideSet), offset, size)) << "33 bits";

    EXPECT_EQ(WinLogicalMax(0, -1, 8), 255) << "0..255 in one byte, sign-extended by Windows";
    EXPECT_EQ(WinLogicalMax(0, -1, 16), 65535);
    EXPECT_EQ(WinLogicalMax(-127, 127, 8), 127);
    EXPECT_EQ(WinLogicalMax(0, 1023, 10), 1023);
    EXPECT_EQ(WinLogicalMax(0, -1, 32), -1) << "a 32-bit range stays as it is";

    const std::vector<uint8_t> report = {0x01, 0x03, 0x03, 0x00, 0x00};
    const std::vector<uint8_t> padded = PadHidReport(report, 8);
    ASSERT_EQ(padded.size(), 8u);
    EXPECT_TRUE(std::equal(report.begin(), report.end(), padded.begin()));
    EXPECT_EQ(padded[7], 0x00);
    EXPECT_EQ(PadHidReport(report, 2).size(), report.size()) << "never shorter";
}

TEST(WindowsDevices_Test, HidPadDecodesWindowsReportsWithReportIdZero)
{
    // What the Raw Input backend builds for a pad without report ids: the reports start with byte 0
    HidDescriptor descriptor;
    descriptor.hasReportIds = true;
    auto field = [](uint32_t offset, uint8_t size, uint16_t page, uint16_t usage, int32_t min, int32_t max) {
        HidField out;
        out.reportId = 0;
        out.bitOffset = offset;
        out.bitSize = size;
        out.usagePage = page;
        out.usage = usage;
        out.logicalMin = min;
        out.logicalMax = WinLogicalMax(min, max, size);
        out.application = 0x05;
        return out;
    };
    descriptor.fields.push_back(field(0, 8, 0x01, 0x30, 0, -1));  // X 0..255 as Windows reports it
    descriptor.fields.push_back(field(8, 8, 0x01, 0x31, 0, -1));
    descriptor.fields.push_back(field(16, 1, 0x09, 1, 0, 1));
    descriptor.fields.push_back(field(17, 1, 0x09, 2, 0, 1));
    HidField hat = field(20, 4, 0x01, 0x39, 0, 7);
    hat.nullState = true;
    descriptor.fields.push_back(hat);
    HidPad pad(descriptor, HidLayoutFor("generic"));
    ASSERT_FALSE(pad.Empty());

    Events events;
    const uint8_t rest[] = {0x00, 0x80, 0x80, 0x80, 0x00};  // centred, nothing held, hat null (8)
    pad.Decode(rest, sizeof(rest), Collect(events));
    EXPECT_NEAR(Find(events, "lstick.x"), 0.0f, 0.01f);
    EXPECT_EQ(Find(events, "button.0"), -99.0f) << "a released button is not reported first";

    events.clear();
    const uint8_t moved[] = {0x00, 0xFF, 0x00, 0x21, 0x00};  // right, up, button 1, hat 2 (right)
    pad.Decode(moved, sizeof(moved), Collect(events));
    EXPECT_NEAR(Find(events, "lstick.x"), 1.0f, 0.01f) << "the range is 0..255, not 0..-1";
    EXPECT_NEAR(Find(events, "lstick.y"), -1.0f, 0.01f);
    EXPECT_EQ(Find(events, "button.0"), 1.0f);
    EXPECT_EQ(Find(events, "dpad.right"), 1.0f);

    events.clear();
    const uint8_t otherId[] = {0x02, 0x00, 0x00, 0x00, 0x00};
    pad.Decode(otherId, sizeof(otherId), Collect(events));
    EXPECT_TRUE(events.empty()) << "a report of another id is not this pad's";
}

TEST(WindowsDevices_Test, GameInputGamepadStateAsCanonicalControls)
{
    for (const std::string& control : GameInputPadControls())
        EXPECT_NE(std::find(KnownControls().begin(), KnownControls().end(), control), KnownControls().end())
            << control << " is a canonical control";

    GameInputPadState rest;
    GameInputPadState now;
    now.buttons = 0x00000004 | 0x00000040;  // A, d-pad up
    now.leftY = 1.0f;                       // up
    now.rightTrigger = 0.5f;
    Events events;
    DiffGameInputPad(rest, now, true, Collect(events));
    EXPECT_EQ(Find(events, "south"), 1.0f);
    EXPECT_EQ(Find(events, "dpad.up"), 1.0f);
    EXPECT_EQ(Find(events, "east"), -99.0f) << "a released button is not reported first";
    EXPECT_EQ(Find(events, "lstick.y"), -1.0f) << "GameInput's up is the vocabulary's minus";
    EXPECT_EQ(Find(events, "rt"), 0.5f);
    EXPECT_EQ(Find(events, "lstick.x"), 0.0f) << "every axis on the first reading";

    events.clear();
    DiffGameInputPad(now, now, false, Collect(events));
    EXPECT_TRUE(events.empty()) << "nothing changed";

    // The Elite paddles: right upper / lower P1 / P2, left upper / lower P3 / P4
    GameInputPadState paddles = now;
    paddles.buttons |= 0x10000000 | 0x08000000;  // PaddleRight1, PaddleLeft2
    events.clear();
    DiffGameInputPad(now, paddles, false, Collect(events));
    EXPECT_EQ(events.size(), 2u);
    EXPECT_EQ(Find(events, "paddle.p1"), 1.0f);
    EXPECT_EQ(Find(events, "paddle.p4"), 1.0f);

    events.clear();
    DiffGameInputPad(paddles, rest, false, Collect(events));
    EXPECT_EQ(Find(events, "south"), 0.0f);
    EXPECT_EQ(Find(events, "paddle.p1"), 0.0f);
    EXPECT_EQ(Find(events, "lstick.y"), 0.0f);
}

TEST(WindowsDevices_Test, GameInputSystemButtonsAsGuideAndShare)
{
    Events events;
    GameInputSystemButtonsDiff(0, 0x1, Collect(events));
    EXPECT_EQ(events.size(), 1u);
    EXPECT_EQ(Find(events, "guide"), 1.0f);
    events.clear();
    GameInputSystemButtonsDiff(0x1, 0x2, Collect(events));
    EXPECT_EQ(Find(events, "guide"), 0.0f);
    EXPECT_EQ(Find(events, "share"), 1.0f);
    events.clear();
    GameInputSystemButtonsDiff(0x2, 0x2 | 0x80, Collect(events));
    EXPECT_TRUE(events.empty()) << "an unknown bit is no control";
    for (const char* control : {"guide", "share"})
        EXPECT_NE(std::find(KnownControls().begin(), KnownControls().end(), control), KnownControls().end());
}

#if defined(_WIN32)
/// The factory's backends on this host: Raw Input always runs, GameInput or XInput serves the Xbox pads, never both.
/// Boot-free but bound by the backends' start (loading GameInput, the Raw Input window): no 50 ms promise
TEST(WindowsDevices_Test, NativeBackendsServeXboxPadsOnce)
{
    const auto started = std::chrono::steady_clock::now();
    HostInputService service;
    service.StartNative();
    std::map<std::string, BackendProbe> probes;
    for (const BackendProbe& probe : service.Probe())
        probes[probe.name] = probe;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    service.Stop();
    for (const auto& [name, probe] : probes)
        std::cout << "[ backend  ] " << name << (probe.running ? " running" : " off: " + probe.reason) << ", "
                  << probe.devices << " devices\n";
    std::cout << "[ backend  ] started in " << ms.count() << " ms\n";

    ASSERT_EQ(probes.size(), 3u);
    ASSERT_TRUE(probes.count("gameinput") && probes.count("rawinput") && probes.count("xinput"));
    EXPECT_TRUE(probes["rawinput"].running) << probes["rawinput"].reason;
    EXPECT_FALSE(probes["gameinput"].running && probes["xinput"].running) << "one device once";
    if (probes["gameinput"].running)
    {
        EXPECT_EQ(probes["xinput"].reason, "GameInput serves the Xbox pads");
    }
    else
        EXPECT_FALSE(probes["gameinput"].reason.empty());
}
#endif
