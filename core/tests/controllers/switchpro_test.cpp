/// @file switchpro_test.cpp
/// @brief H4 (haptics.md §9): the Switch Pro controller's HD rumble encoding (dekuNukem's rumble_data_table.md), its
/// input report 0x30 and calibration (SDL_hidapi_switch.c), and SwitchProtocol's setup and rumble timing.

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "hid/drivers/switchpro.h"

using namespace controllers;

namespace
{
using Events = std::map<std::string, float>;

ControlSink Collect(Events& events)
{
    return [&events](const std::string& control, float value) { events[control] = value; };
}

/// Two 12-bit values packed as the pad sends a stick (and as the calibration packs them)
void PutStick(uint8_t* at, int x, int y)
{
    at[0] = static_cast<uint8_t>(x & 0xFF);
    at[1] = static_cast<uint8_t>(((x >> 8) & 0x0F) | ((y & 0x0F) << 4));
    at[2] = static_cast<uint8_t>(y >> 4);
}

/// A subcommand reply 0x21 acknowledging @p id, with @p data from byte 15
std::vector<uint8_t> Reply(uint8_t id, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> reply(64, 0);
    reply[0] = 0x21;
    reply[13] = 0x80 | (data.empty() ? 0 : 0x10);
    reply[14] = id;
    std::copy(data.begin(), data.end(), reply.begin() + 15);
    return reply;
}
}  // namespace

TEST(SwitchPro_Test, FrequencyCodesMatchTheTable)
{
    // rumble_data_table.md: LF byte = code - 0x40, HF = (code - 0x60) * 4
    EXPECT_EQ(SwitchRumble::FrequencyCode(160), 0x80) << "LF 40, the low band's resonance";
    EXPECT_EQ(SwitchRumble::FrequencyCode(320), 0xA0) << "HF 00 01, the high band's";
    EXPECT_EQ(SwitchRumble::FrequencyCode(41), 0x41) << "LF 01: 40.875885 Hz";
    EXPECT_EQ(SwitchRumble::FrequencyCode(626), 0xBF) << "LF 7F: 626.286 Hz";
    EXPECT_EQ(SwitchRumble::FrequencyCode(82), 0x61) << "HF 04 00: 81.75 Hz";
    EXPECT_EQ(SwitchRumble::FrequencyCode(795), 0xCA) << "HF A8 01: 794.789 Hz";
    EXPECT_EQ(SwitchRumble::FrequencyCode(1253), 0xDF) << "HF FC 01: 1252.57 Hz";
    EXPECT_EQ(SwitchRumble::FrequencyCode(263), 0x97) << "LF 57: 263.32 Hz";
    EXPECT_EQ(SwitchRumble::FrequencyCode(5), 0x40) << "clamped low";
    EXPECT_EQ(SwitchRumble::FrequencyCode(5000), 0xE0) << "clamped high";
}

TEST(SwitchPro_Test, AmplitudeStepsMatchTheTable)
{
    EXPECT_EQ(SwitchRumble::AmplitudeStep(0), 0);
    EXPECT_EQ(SwitchRumble::AmplitudeStep(-1), 0);
    EXPECT_EQ(SwitchRumble::AmplitudeStep(1.0f), 100);
    EXPECT_EQ(SwitchRumble::AmplitudeStep(2.0f), 100);
    // HA 88 / LA 00 62: 0.501433; HA 86 / LA 80 61: 0.490689
    EXPECT_EQ(SwitchRumble::AmplitudeStep(0.501433f), 68);
    EXPECT_EQ(SwitchRumble::AmplitudeStep(0.490689f), 67);
    EXPECT_EQ(SwitchRumble::AmplitudeStep(0.495f), 68) << "between two steps: the louder one";
}

TEST(SwitchPro_Test, EncodesDekuNukemsExample)
{
    // rumble_data_table.md: hf 0x01A8, ha 0x88, lf 0x63, la 0x804D -> A8 89 E3 4D. ha 0x88 is step 68, la 0x804D the
    // odd step 27 (0x40 + 27 / 2 = 0x4D), lf 0x63 code 0xA3 (343.6 Hz), hf 0x01A8 code 0xCA (794.8 Hz)
    const std::array<uint8_t, 4> bytes = SwitchRumble::Encode(12398 / 65535.0f, 344, 0.501433f, 795);
    EXPECT_EQ(bytes, (std::array<uint8_t, 4>{0xA8, 0x89, 0xE3, 0x4D}));
    EXPECT_EQ(SwitchRumble::Encode(0, 0, 0, 0), (std::array<uint8_t, 4>{0x00, 0x01, 0x40, 0x40})) << "neutral";
    // The defaults: 160 Hz low, 320 Hz high; full on both: ha C8, la 0x72
    EXPECT_EQ(SwitchRumble::Encode(1.0f, 0, 1.0f, 0), (std::array<uint8_t, 4>{0x00, 0xC9, 0x40, 0x72}));

    RumbleLevels levels{1.0f, 0, 0, 0};
    levels.lowHz = 80;
    const std::array<uint8_t, 8> pair = SwitchRumble::Pair(levels);
    EXPECT_EQ(pair[2], 0x20) << "80 Hz: code 0x60, LF 0x20";
    EXPECT_EQ(pair[3], 0x72);
    EXPECT_EQ(pair[1], 0x01) << "no high band";
    EXPECT_EQ(pair[4], pair[0]) << "the right actuator plays the same";
}

TEST(SwitchPro_Test, InputReportAndCalibration)
{
    SwitchProPad pad;
    std::vector<uint8_t> report(49, 0);
    report[0] = 0x30;
    report[2] = 0x80 | 0x10;  // level 8 of 8, charging (full: not charging any more)
    PutStick(&report[6], 2048, 2048);
    PutStick(&report[9], 2048, 2048);
    Events events;
    ASSERT_TRUE(pad.Decode(report.data(), report.size(), Collect(events)));
    EXPECT_FLOAT_EQ(events.at("lstick.x"), 0.0f);
    EXPECT_FLOAT_EQ(events.at("lt"), 0.0f) << "ZL is a trigger: its first value is reported";
    EXPECT_EQ(events.count("south"), 0u);
    EXPECT_EQ(pad.Battery().level, 100);
    EXPECT_FALSE(pad.Battery().charging);
    EXPECT_TRUE(pad.Battery().wired);

    events.clear();
    report[2] = 0x40;          // level 4: half, on battery
    report[3] = 0x04 | 0x80;   // B, ZR
    report[4] = 0x10 | 0x02;   // Home, +
    report[5] = 0x02 | 0x40;   // up, L
    // SDL's defaults: centre 2048, range 1433, the extent 0.7 of it = 1003
    PutStick(&report[6], 2048 + 1003, 2048 + 1003);
    ASSERT_TRUE(pad.Decode(report.data(), report.size(), Collect(events)));
    EXPECT_FLOAT_EQ(events.at("south"), 1.0f) << "B is south";
    EXPECT_FLOAT_EQ(events.at("rt"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("guide"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("menu"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("dpad.up"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("lb"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("lstick.x"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("lstick.y"), -1.0f) << "up is larger on the pad, -1 here";
    EXPECT_EQ(pad.Battery().level, 50);
    EXPECT_FALSE(pad.Battery().wired);

    // A factory calibration: the left stick centred at 2000, 1000 above and below on x
    uint8_t left[9];
    uint8_t right[9];
    PutStick(&left[0], 1000, 1000);  // above
    PutStick(&left[3], 2000, 2000);  // centre
    PutStick(&left[6], 1000, 0xFFF); // below; y unset
    PutStick(&right[0], 0xFFF, 0xFFF);
    PutStick(&right[3], 0xFFF, 0xFFF);
    PutStick(&right[6], 0xFFF, 0xFFF);
    const SwitchStickCalibration calibration = SwitchStickCalibration::Parse(left, right);
    EXPECT_EQ(calibration.centre[0][0], 2000);
    EXPECT_EQ(calibration.below[0][1], 1400) << "unset: 0.7 of the centre";
    EXPECT_EQ(calibration.centre[1][1], 2048);
    pad.SetCalibration(calibration);
    events.clear();
    PutStick(&report[6], 2000 - 350, 2000);  // half the extent (700) below
    ASSERT_TRUE(pad.Decode(report.data(), report.size(), Collect(events)));
    EXPECT_FLOAT_EQ(events.at("lstick.x"), -0.5f);
    EXPECT_FLOAT_EQ(events.at("lstick.y"), 0.0f);
    events.clear();
    PutStick(&report[6], 2000 - 1400, 2000);  // past the extent: it grows
    ASSERT_TRUE(pad.Decode(report.data(), report.size(), Collect(events)));
    EXPECT_FLOAT_EQ(events.at("lstick.x"), -1.0f);
    events.clear();
    PutStick(&report[6], 2000 - 700, 2000);
    ASSERT_TRUE(pad.Decode(report.data(), report.size(), Collect(events)));
    EXPECT_FLOAT_EQ(events.at("lstick.x"), -0.5f) << "the grown extent stays";

    std::vector<uint8_t> simple(12, 0);
    simple[0] = 0x3F;
    EXPECT_FALSE(pad.Decode(simple.data(), simple.size(), Collect(events))) << "0x3F stays HidPad's";
}

TEST(SwitchPro_Test, BluetoothSetupWaitsForEachReply)
{
    std::optional<SwitchStickCalibration> calibrated;
    SwitchProtocol protocol(true, [&](const SwitchStickCalibration& c) { calibrated = c; });
    protocol.SetPlayer(3);
    uint64_t wake = 0;
    EXPECT_FALSE(protocol.Next(0, wake).has_value()) << "macOS's own handshake first";
    EXPECT_EQ(wake, SwitchProtocol::kStartDelayMs);

    // The user calibration: SPI read 0x8010, 22 bytes
    std::optional<PadOutReport> next = protocol.Next(250, wake);
    ASSERT_TRUE(next.has_value());
    ASSERT_EQ(next->bytes.size(), 49u);
    EXPECT_EQ(next->bytes[0], 0x01);
    EXPECT_EQ(next->bytes[1], 0) << "the packet number";
    EXPECT_EQ(next->bytes[2], 0x00);
    EXPECT_EQ(next->bytes[3], 0x01) << "the neutral rumble rides along";
    EXPECT_EQ(next->bytes[10], 0x10);
    EXPECT_EQ((std::vector<uint8_t>(next->bytes.begin() + 11, next->bytes.begin() + 16)),
              (std::vector<uint8_t>{0x10, 0x80, 0x00, 0x00, 22}));
    EXPECT_FALSE(protocol.Next(260, wake).has_value()) << "waiting for the reply";
    EXPECT_EQ(wake, 250 + SwitchProtocol::kReplyMs);
    // No reply: the same read again, the packet number on
    next = protocol.Next(350, wake);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->bytes[1], 1);
    EXPECT_EQ(next->bytes[10], 0x10);
    // A reply of another subcommand does not count
    std::vector<uint8_t> other = Reply(0x48, {});
    protocol.OnInput(other.data(), other.size(), 355);
    EXPECT_FALSE(protocol.Next(356, wake).has_value());
    // The user calibration: the left stick's magic only
    std::vector<uint8_t> user = {0x10, 0x80, 0x00, 0x00, 22, 0xB2, 0xA1};
    user.resize(5 + 22, 0);
    PutStick(&user[5 + 2 + 3], 1900, 1950);  // the left centre
    PutStick(&user[5 + 2 + 0], 900, 900);
    PutStick(&user[5 + 2 + 6], 900, 900);
    std::vector<uint8_t> reply = Reply(0x10, user);
    protocol.OnInput(reply.data(), reply.size(), 360);

    // The factory calibration: 0x603D, 18 bytes
    next = protocol.Next(360, wake);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ((std::vector<uint8_t>(next->bytes.begin() + 10, next->bytes.begin() + 16)),
              (std::vector<uint8_t>{0x10, 0x3D, 0x60, 0x00, 0x00, 18}));
    std::vector<uint8_t> factory = {0x3D, 0x60, 0x00, 0x00, 18};
    factory.resize(5 + 18, 0);
    PutStick(&factory[5 + 3], 2100, 2100);      // the left centre: the user's wins
    PutStick(&factory[5 + 9 + 0], 1990, 2010);  // the right centre
    PutStick(&factory[5 + 9 + 3], 1000, 1000);
    PutStick(&factory[5 + 9 + 6], 1000, 1000);
    reply = Reply(0x10, factory);
    protocol.OnInput(reply.data(), reply.size(), 370);
    ASSERT_TRUE(calibrated.has_value());
    EXPECT_EQ(calibrated->centre[0][0], 1900);
    EXPECT_EQ(calibrated->centre[0][1], 1950);
    EXPECT_EQ(calibrated->centre[1][0], 1990);
    EXPECT_EQ(calibrated->centre[1][1], 2010);

    next = protocol.Next(370, wake);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->bytes[10], 0x48) << "vibration on";
    EXPECT_EQ(next->bytes[11], 0x01);
    reply = Reply(0x48, {});
    protocol.OnInput(reply.data(), reply.size(), 375);
    next = protocol.Next(375, wake);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->bytes[10], 0x03) << "the full report";
    EXPECT_EQ(next->bytes[11], 0x30);
    EXPECT_FALSE(protocol.Ready());
    reply = Reply(0x03, {});
    protocol.OnInput(reply.data(), reply.size(), 380);
    EXPECT_TRUE(protocol.Ready());

    next = protocol.Next(380, wake);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->bytes[10], 0x30) << "the player lights";
    EXPECT_EQ(next->bytes[11], 0x07) << "player 3";
    EXPECT_FALSE(protocol.Next(381, wake).has_value());
    EXPECT_EQ(wake, 0u) << "nothing to do until told";
    EXPECT_FALSE(protocol.Final().has_value()) << "the motors never ran";
}

TEST(SwitchPro_Test, UsbSetupStartsWithTheProprietaryCommands)
{
    SwitchProtocol protocol(false, {}, 0);
    uint64_t wake = 0;
    std::optional<PadOutReport> next = protocol.Next(0, wake);
    ASSERT_TRUE(next.has_value());
    ASSERT_EQ(next->bytes.size(), 64u);
    EXPECT_EQ(next->bytes[0], 0x80);
    EXPECT_EQ(next->bytes[1], 0x02) << "the handshake";
    const uint8_t ack[] = {0x81, 0x02};
    protocol.OnInput(ack, sizeof(ack), 1);
    next = protocol.Next(1, wake);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->bytes[1], 0x03) << "high speed";
    // No answers at all: every step gives up after its attempts and the setup ends
    uint64_t now = 1;
    int writes = 2;  // the handshake and the first high speed
    while (!protocol.Ready() && writes < 100)
    {
        now += SwitchProtocol::kReplyMs;
        if (protocol.Next(now, wake).has_value())
            ++writes;
    }
    EXPECT_TRUE(protocol.Ready());
    // 80 03, 80 02 five times each; 80 04 once (no ack); the two reads, 0x48, 0x03 five times; 80 04 once
    EXPECT_EQ(writes, 1 + 5 + 5 + 1 + 5 * 4 + 1);
}

TEST(SwitchPro_Test, RumbleIsPacedAndRefreshed)
{
    SwitchProtocol protocol(true, {}, 0);
    uint64_t now = 0;
    uint64_t wake = 0;
    while (!protocol.Ready())
    {
        now += SwitchProtocol::kReplyMs;
        protocol.Next(now, wake);
    }
    EXPECT_FALSE(protocol.Next(now, wake).has_value()) << "nothing plays: no neutral report";

    protocol.SetRumble({1.0f, 0, 0, 0});
    std::optional<PadOutReport> next = protocol.Next(now, wake);
    ASSERT_TRUE(next.has_value());
    ASSERT_EQ(next->bytes.size(), 49u);
    EXPECT_EQ(next->bytes[0], 0x10) << "the rumble-only report";
    EXPECT_EQ(next->bytes[4], 0x40);
    EXPECT_EQ(next->bytes[5], 0x72);
    // A change 10 ms later waits for the 30 ms interval
    protocol.SetRumble({0.5f, 0, 0, 0});
    EXPECT_FALSE(protocol.Next(now + 10, wake).has_value());
    EXPECT_EQ(wake, now + SwitchProtocol::kRumbleIntervalMs);
    next = protocol.Next(now + 30, wake);
    ASSERT_TRUE(next.has_value());
    // Unchanged: written again 50 ms later to keep the motors running
    EXPECT_FALSE(protocol.Next(now + 40, wake).has_value());
    EXPECT_EQ(wake, now + 30 + SwitchProtocol::kRumbleRefreshMs);
    next = protocol.Next(now + 80, wake);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->bytes[0], 0x10);

    protocol.SetRumble({});
    next = protocol.Next(now + 110, wake);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->bytes[2], 0x00);
    EXPECT_EQ(next->bytes[3], 0x01);
    EXPECT_EQ(next->bytes[4], 0x40);
    EXPECT_EQ(next->bytes[5], 0x40) << "neutral";
    EXPECT_FALSE(protocol.Next(now + 200, wake).has_value()) << "no refresh at rest";
    EXPECT_EQ(wake, 0u);
    EXPECT_FALSE(protocol.Final().has_value());
}
