/// @file sonypad_test.cpp
/// @brief H4 (haptics.md §9): the full input reports of the DualSense and the DualShock 4 over USB and Bluetooth, the
/// PlayStation CRC-32, the DualShock 4 output report and SonyProtocol's order of reports. The fixtures are built by
/// the offsets of Linux's hid-playstation.c (struct dualsense_input_report, struct dualshock4_input_report_*).

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "hid/drivers/dualsenseoutput.h"
#include "hid/drivers/sonypad.h"

using namespace controllers;

namespace
{
using Events = std::map<std::string, float>;

ControlSink Collect(Events& events)
{
    return [&events](const std::string& control, float value) { events[control] = value; };
}

void PutCrc(std::vector<uint8_t>& report, uint8_t seed)
{
    const uint32_t crc = PlayStationCrc32(seed, report.data(), report.size() - 4);
    for (size_t index = 0; index < 4; index++)
        report[report.size() - 4 + index] = static_cast<uint8_t>(crc >> (8 * index));
}

/// A DualSense common part (63 bytes) at rest: sticks centred, hat released, no finger on the touchpad
void DualSenseRest(uint8_t* common)
{
    common[0] = common[1] = common[2] = common[3] = 0x80;
    common[7] = 0x08;          // hat: none
    common[32] = 0x80;         // touch point 1: no contact
    common[36] = 0x80;         // touch point 2
    common[52] = 0x08 | 0x00;  // 80-89 %, discharging
}
}  // namespace

TEST(SonyPad_Test, Crc32IsZlibsWithTheSeedFirst)
{
    // The CRC-32 check value: "123456789" -> CBF43926, the seed being the first byte
    const uint8_t rest[] = {'2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(PlayStationCrc32('1', rest, sizeof(rest)), 0xCBF43926u);

    std::vector<uint8_t> report(78, 0x5A);
    report[0] = 0x31;
    PutCrc(report, SonyPad::kInputCrcSeed);
    EXPECT_TRUE(PlayStationCrcValid(SonyPad::kInputCrcSeed, report.data(), report.size()));
    EXPECT_FALSE(PlayStationCrcValid(SonyPad::kFeatureCrcSeed, report.data(), report.size())) << "another seed";
    report[10] ^= 1;
    EXPECT_FALSE(PlayStationCrcValid(SonyPad::kInputCrcSeed, report.data(), report.size()));
}

TEST(SonyPad_Test, ModelsByVidPid)
{
    EXPECT_EQ(SonyPad::ModelOf(0x054C, 0x0CE6), SonyModel::DualSense);
    EXPECT_EQ(SonyPad::ModelOf(0x054C, 0x0DF2), SonyModel::DualSenseEdge);
    EXPECT_EQ(SonyPad::ModelOf(0x054C, 0x05C4), SonyModel::DualShock4);
    EXPECT_EQ(SonyPad::ModelOf(0x054C, 0x09CC), SonyModel::DualShock4);
    EXPECT_EQ(SonyPad::ModelOf(0x054C, 0x0BA0), SonyModel::DualShock4);
    EXPECT_FALSE(SonyPad::ModelOf(0x054C, 0x0268).has_value()) << "a DualShock 3 is HidPad's";
    EXPECT_FALSE(SonyPad::ModelOf(0x057E, 0x0CE6).has_value());
}

TEST(SonyPad_Test, DualSenseUsbReport)
{
    SonyPad pad(SonyModel::DualSense);
    std::vector<uint8_t> report(64, 0);
    report[0] = 0x01;
    uint8_t* common = report.data() + 1;
    DualSenseRest(common);
    Events events;
    ASSERT_TRUE(pad.Decode(report.data(), report.size(), Collect(events)));
    // The first report: the axes' positions, no released button
    EXPECT_FLOAT_EQ(events.at("lstick.x"), 0.0f);
    EXPECT_FLOAT_EQ(events.at("lt"), 0.0f);
    EXPECT_FLOAT_EQ(events.at("touchpad.x"), 0.0f);
    EXPECT_EQ(events.count("south"), 0u);
    EXPECT_EQ(pad.Battery().level, 85);
    EXPECT_FALSE(pad.Battery().charging);

    events.clear();
    common[0] = 0x00;          // left stick full left
    common[1] = 0xFF;          // full down
    common[5] = 0xFF;          // R2 all the way
    common[7] = 0x20 | 0x02;   // Cross, hat right
    common[8] = 0x01 | 0x20;   // L1, Options
    common[9] = 0x01 | 0x04;   // PS, mute
    common[32] = 0x05;         // a finger (contact bit 7 clear)
    common[33] = 0x80;         // x = 0x780 = 1920 / 2 + 960 ... 1920
    common[34] = 0x07 | 0x00;  // x high nibble 7 -> 0x780 = 1920; y low nibble 0
    common[35] = 0x00;         // y = 0
    common[52] = 0x15;         // charging, 50-59 %
    ASSERT_TRUE(pad.Decode(report.data(), report.size(), Collect(events)));
    EXPECT_FLOAT_EQ(events.at("lstick.x"), -1.0f);
    EXPECT_FLOAT_EQ(events.at("lstick.y"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("rt"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("south"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("dpad.right"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("lb"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("menu"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("guide"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("misc.1"), 1.0f) << "the mute button";
    EXPECT_FLOAT_EQ(events.at("touchpad.x"), 1.0f) << "x 1920 clamps to the right edge";
    EXPECT_FLOAT_EQ(events.at("touchpad.y"), -1.0f) << "y 0, the top";
    EXPECT_EQ(events.count("dpad.up"), 0u) << "unchanged";
    EXPECT_TRUE(pad.Battery().charging);
    EXPECT_EQ(pad.Battery().level, 55);

    events.clear();
    common[32] = 0x85;  // the finger lifted: back to the centre
    ASSERT_TRUE(pad.Decode(report.data(), report.size(), Collect(events)));
    EXPECT_FLOAT_EQ(events.at("touchpad.x"), 0.0f);
    EXPECT_FLOAT_EQ(events.at("touchpad.y"), 0.0f);
    EXPECT_EQ(events.size(), 2u);
}

TEST(SonyPad_Test, DualSenseBluetoothReportNeedsItsCrc)
{
    SonyPad pad(SonyModel::DualSenseEdge);
    std::vector<uint8_t> report(78, 0);
    report[0] = 0x31;
    report[1] = 0x10;  // the tag / sequence byte
    uint8_t* common = report.data() + 2;
    DualSenseRest(common);
    common[9] = 0x40 | 0x10;  // the left paddle, Fn 1
    PutCrc(report, SonyPad::kInputCrcSeed);
    Events events;
    ASSERT_TRUE(pad.Decode(report.data(), report.size(), Collect(events)));
    EXPECT_TRUE(pad.Extended());
    EXPECT_FLOAT_EQ(events.at("paddle.p1"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("misc.2"), 1.0f);
    EXPECT_EQ(events.count("paddle.p2"), 0u);

    events.clear();
    report[3] = 0x00;  // a change, the CRC not updated
    EXPECT_FALSE(pad.Decode(report.data(), report.size(), Collect(events)));
    EXPECT_TRUE(events.empty());
    EXPECT_EQ(pad.CrcFailures(), 1u);

    // The short report 0x01 the pad starts with over Bluetooth stays HidPad's
    std::vector<uint8_t> simple(10, 0);
    simple[0] = 0x01;
    EXPECT_FALSE(pad.Decode(simple.data(), simple.size(), Collect(events)));
    EXPECT_EQ(SonyPad::Controls(SonyModel::DualSense).size() + 4, SonyPad::Controls(SonyModel::DualSenseEdge).size());
}

TEST(SonyPad_Test, DualShock4UsbAndBluetooth)
{
    SonyPad usbPad(SonyModel::DualShock4);
    std::vector<uint8_t> usb(64, 0);
    usb[0] = 0x01;
    uint8_t* common = usb.data() + 1;
    common[0] = common[1] = common[2] = common[3] = 0x80;
    common[4] = 0x08 | 0x80;  // hat none, Triangle
    common[5] = 0x02 | 0x10;  // R1, Share
    common[6] = 0x02;         // touchpad click
    common[7] = 0x80;         // L2 half
    common[29] = 0x10 | 0x0B;  // on the cable, full
    common[32] = 1;            // one touch report
    common[33] = 0x00;         // its time stamp
    common[34] = 0x01;         // a finger
    common[35] = 0x00;         // x = 0
    common[36] = 0xD0;         // y low nibble 0xD
    common[37] = 0x1D;         // y = 0x1DD = 477: past the centre 471
    Events events;
    ASSERT_TRUE(usbPad.Decode(usb.data(), usb.size(), Collect(events)));
    EXPECT_FLOAT_EQ(events.at("north"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("rb"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("view"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("touchpad.click"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("lt"), 128 / 255.0f);
    EXPECT_FLOAT_EQ(events.at("touchpad.x"), -1.0f);
    EXPECT_FLOAT_EQ(events.at("touchpad.y"), 6 / 470.0f);
    EXPECT_EQ(events.count("misc.1"), 0u) << "no mute button";
    EXPECT_TRUE(usbPad.Battery().wired);
    EXPECT_FALSE(usbPad.Battery().charging);
    EXPECT_EQ(usbPad.Battery().level, 100);

    SonyPad btPad(SonyModel::DualShock4);
    std::vector<uint8_t> bt(78, 0);
    bt[0] = 0x11;
    bt[1] = 0xC0;
    std::memcpy(bt.data() + 3, common, 32);
    bt[3 + 29] = 0x03;  // on battery, 30-39 %
    PutCrc(bt, SonyPad::kInputCrcSeed);
    events.clear();
    ASSERT_TRUE(btPad.Decode(bt.data(), bt.size(), Collect(events)));
    EXPECT_FLOAT_EQ(events.at("north"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("touchpad.x"), 0.0f) << "no touch reports: the centre";
    EXPECT_EQ(btPad.Battery().level, 35);
    EXPECT_FALSE(btPad.Battery().wired);
}

TEST(SonyPad_Test, BatteryStates)
{
    EXPECT_EQ(SonyPad::DualSenseBattery(0x09).level, 95);
    EXPECT_EQ(SonyPad::DualSenseBattery(0x0A).level, 100) << "capped";
    EXPECT_EQ(SonyPad::DualSenseBattery(0x20).level, 100) << "full";
    EXPECT_TRUE(SonyPad::DualSenseBattery(0x20).wired);
    EXPECT_FALSE(SonyPad::DualSenseBattery(0xF0).known) << "a charging error";
    EXPECT_TRUE(SonyPad::DualShock4Battery(0x15).charging);
    EXPECT_EQ(SonyPad::DualShock4Battery(0x15).level, 55);
    EXPECT_FALSE(SonyPad::DualShock4Battery(0x1E).known);
}

TEST(SonyPad_Test, DualShock4OutputReports)
{
    const RumbleLevels levels{1.0f, 0.5f, 0, 0};
    const LightColor light{0x10, 0x20, 0x30};
    const std::vector<uint8_t> usb = DualShock4Output::UsbReport(levels, light);
    ASSERT_EQ(usb.size(), 32u);
    EXPECT_EQ(usb[0], 0x05);
    EXPECT_EQ(usb[1], 0x03) << "motor and light valid";
    EXPECT_EQ(usb[4], 128) << "the right (light) motor: the high level";
    EXPECT_EQ(usb[5], 255) << "the left (heavy) motor: the low level";
    EXPECT_EQ(usb[6], 0x10);
    EXPECT_EQ(usb[8], 0x30);

    const std::vector<uint8_t> bt = DualShock4Output::BluetoothReport(levels, light);
    ASSERT_EQ(bt.size(), 78u);
    EXPECT_EQ(bt[0], 0x11);
    EXPECT_EQ(bt[1], 0xC0) << "HID data + CRC";
    EXPECT_EQ(bt[3], 0x03);
    EXPECT_EQ(bt[6], 128);
    EXPECT_EQ(bt[7], 255);
    EXPECT_EQ(bt[8], 0x10);
    EXPECT_TRUE(PlayStationCrcValid(0xA2, bt.data(), bt.size()));
}

TEST(SonyPad_Test, ProtocolReadsTheFeatureFirstOverBluetooth)
{
    SonyProtocol protocol(SonyModel::DualSense, true);
    protocol.SetRumble({0.5f, 0, 0, 0});
    uint64_t wake = 0;
    std::optional<PadOutReport> next = protocol.Next(0, wake);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->kind, PadOutReport::Kind::GetFeature);
    EXPECT_EQ(next->featureId, 0x05);
    EXPECT_EQ(next->featureSize, 41u);
    // A failed read: once more 100 ms later
    protocol.OnFeature(0x05, {}, 0);
    EXPECT_FALSE(protocol.Next(10, wake).has_value());
    EXPECT_EQ(wake, SonyProtocol::kFeatureRetryMs);
    next = protocol.Next(100, wake);
    ASSERT_TRUE(next.has_value());
    std::vector<uint8_t> feature(41, 0);
    feature[0] = 0x05;
    PutCrc(feature, SonyPad::kFeatureCrcSeed);
    protocol.OnFeature(0x05, feature, 101);
    EXPECT_TRUE(protocol.Ready());

    next = protocol.Next(101, wake);
    ASSERT_TRUE(next.has_value());
    ASSERT_EQ(next->bytes.size(), 78u);
    EXPECT_EQ(next->bytes[0], 0x31);
    EXPECT_EQ(next->bytes[3 + 3], 128) << "the left motor";
    EXPECT_TRUE(PlayStationCrcValid(0xA2, next->bytes.data(), next->bytes.size()));
    protocol.OnWritten(true, 101);
    EXPECT_FALSE(protocol.Next(102, wake).has_value()) << "nothing changed";

    // The player LEDs and a colour together, after the interval
    protocol.SetPlayer(2);
    protocol.SetLight({1, 2, 3});
    EXPECT_FALSE(protocol.Next(104, wake).has_value());
    EXPECT_EQ(wake, 101 + SonyProtocol::kIntervalMs);
    next = protocol.Next(109, wake);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->bytes[1] >> 4, 1) << "the sequence counts on";
    EXPECT_EQ(next->bytes[3 + 43], DualSenseOutput::PlayerLeds(2));
    EXPECT_EQ(next->bytes[3 + 44], 1);
    protocol.OnWritten(true, 109);

    const std::optional<PadOutReport> last = protocol.Final();
    ASSERT_TRUE(last.has_value()) << "the motor runs";
    EXPECT_EQ(last->bytes[3 + 3], 0);
}

TEST(SonyPad_Test, ProtocolDualShock4UsbKeepsMotorsAndLightTogether)
{
    SonyProtocol protocol(SonyModel::DualShock4, false);
    protocol.SetPlayer(2);
    uint64_t wake = 0;
    std::optional<PadOutReport> next = protocol.Next(0, wake);
    ASSERT_TRUE(next.has_value()) << "USB: no feature read";
    EXPECT_EQ(next->bytes[0], 0x05);
    EXPECT_EQ(next->bytes[6], 0x40) << "player 2: red";
    protocol.OnWritten(true, 0);
    protocol.SetRumble({0, 1.0f, 0, 0});
    next = protocol.Next(20, wake);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->bytes[4], 255);
    EXPECT_EQ(next->bytes[6], 0x40) << "the light stays";
}

TEST(SonyPad_Test, ProtocolDualShock4BluetoothReadsFeature02Then05)
{
    // SDL_hidapi_ps4.c HIDAPI_DriverPS4_LoadCalibrationData: 0x02 switches a Bluetooth pad to the 0x11 report, then
    // 0x05 (41 bytes, CRC seed 0xA3) is the Bluetooth calibration hid-playstation reads
    SonyProtocol protocol(SonyModel::DualShock4, true);
    uint64_t wake = 0;
    std::optional<PadOutReport> next = protocol.Next(0, wake);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->kind, PadOutReport::Kind::GetFeature);
    EXPECT_EQ(next->featureId, 0x02);
    EXPECT_EQ(next->featureSize, 37u);

    std::vector<uint8_t> shortRead(20, 0);
    shortRead[0] = 0x02;
    protocol.OnFeature(0x02, shortRead, 0);
    EXPECT_FALSE(protocol.Next(10, wake).has_value()) << "a short read waits for the retry";
    next = protocol.Next(100, wake);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->featureId, 0x02);

    std::vector<uint8_t> usbCalibration(37, 0);
    usbCalibration[0] = 0x02;
    protocol.OnFeature(0x02, usbCalibration, 100);
    EXPECT_FALSE(protocol.Ready());
    next = protocol.Next(100, wake);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->featureId, 0x05);
    EXPECT_EQ(next->featureSize, 41u);

    std::vector<uint8_t> btCalibration(41, 0);
    btCalibration[0] = 0x05;
    PutCrc(btCalibration, SonyPad::kFeatureCrcSeed);
    protocol.OnFeature(0x05, btCalibration, 101);
    EXPECT_TRUE(protocol.Ready());
    protocol.SetPlayer(1);
    next = protocol.Next(101, wake);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->kind, PadOutReport::Kind::Output);
    EXPECT_EQ(next->bytes[0], 0x11) << "the light (player colour) goes out in the Bluetooth report";
}
