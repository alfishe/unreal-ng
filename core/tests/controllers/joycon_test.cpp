/// @file joycon_test.cpp
/// @brief The Joy-Cons (haptics.md §9): their 0x30 report alone (held sideways) and as the halves of one pad, after
/// SDL_hidapi_switch.c (HandleMiniControllerStateL / R, HandleCombinedControllerStateL / R, libsdl-org/SDL
/// 3cffe56317b9), the pair's rumble split and battery, and JoyConPairing.

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "hid/drivers/switchpro.h"
#include "joyconpairing.h"

using namespace controllers;

namespace
{
using Events = std::map<std::string, float>;

ControlSink Collect(Events& events)
{
    return [&events](const std::string& control, float value) { events[control] = value; };
}

void PutStick(uint8_t* at, int x, int y)
{
    at[0] = static_cast<uint8_t>(x & 0xFF);
    at[1] = static_cast<uint8_t>(((x >> 8) & 0x0F) | ((y & 0x0F) << 4));
    at[2] = static_cast<uint8_t>(y >> 4);
}

/// A 0x30 report with both sticks at rest
std::vector<uint8_t> Report()
{
    std::vector<uint8_t> report(49, 0);
    report[0] = 0x30;
    report[2] = 0x80;  // battery 8 of 8
    PutStick(report.data() + 6, 2048, 2048);
    PutStick(report.data() + 9, 2048, 2048);
    return report;
}

/// The default calibration's reach (SwitchProPad: 0.7 of SDL's 0xFFF / 2 default range)
constexpr int kReach = 1433 * 7 / 10;
}  // namespace

TEST(JoyCon_Test, LayoutsByProductAndControls)
{
    EXPECT_EQ(SwitchProPad::LayoutFor(0x057E, 0x2009), SwitchLayout::Pro);
    EXPECT_EQ(SwitchProPad::LayoutFor(0x057E, 0x2006), SwitchLayout::JoyConLeft);
    EXPECT_EQ(SwitchProPad::LayoutFor(0x057E, 0x2007), SwitchLayout::JoyConRight);
    EXPECT_FALSE(SwitchProPad::LayoutFor(0x057E, 0x200E).has_value()) << "the charging grip stays generic";
    EXPECT_FALSE(SwitchProPad::LayoutFor(0x054C, 0x2006).has_value());

    // The halves of a pair give the Pro layout between them, with SL / SR as the four paddles, nothing twice
    std::vector<std::string> left = SwitchProPad::Controls(SwitchLayout::PairLeft);
    std::vector<std::string> right = SwitchProPad::Controls(SwitchLayout::PairRight);
    std::vector<std::string> both = left;
    both.insert(both.end(), right.begin(), right.end());
    std::sort(both.begin(), both.end());
    EXPECT_EQ(std::adjacent_find(both.begin(), both.end()), both.end()) << "no control on both halves";
    std::vector<std::string> expected = SwitchProPad::Controls();
    for (const char* paddle : {"paddle.p1", "paddle.p2", "paddle.p3", "paddle.p4"})
        expected.emplace_back(paddle);
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(both, expected);
    EXPECT_EQ(SwitchProPad::Controls().size(), 22u) << "the Pro Controller has no paddles";
    EXPECT_EQ(std::count(left.begin(), left.end(), "lstick.x"), 1);
    EXPECT_EQ(std::count(right.begin(), right.end(), "rstick.x"), 1);
}

TEST(JoyCon_Test, LeftAloneIsASidewaysPad)
{
    SwitchProPad pad(SwitchLayout::JoyConLeft);
    Events events;
    std::vector<uint8_t> report = Report();
    report[5] = 0x08 | 0x20;  // the left arrow, SL
    report[4] = 0x20;         // Capture
    PutStick(report.data() + 6, 2048 + kReach, 2048);  // pushed to the stick's +x
    ASSERT_TRUE(pad.Decode(report.data(), report.size(), Collect(events)));
    EXPECT_FLOAT_EQ(events.at("south"), 1.0f) << "the left arrow is the bottom face held sideways";
    EXPECT_FLOAT_EQ(events.at("lb"), 1.0f) << "SL";
    EXPECT_FLOAT_EQ(events.at("guide"), 1.0f) << "Capture";
    EXPECT_FLOAT_EQ(events.at("lstick.y"), -1.0f) << "SDL: LEFTY = ~x";
    EXPECT_FLOAT_EQ(events.at("lstick.x"), 0.0f);
    EXPECT_EQ(events.count("rstick.x"), 0u);
    EXPECT_EQ(events.count("dpad.left"), 0u);

    // Joined to a right Joy-Con: the same press is the d-pad now, the whole state again
    events.clear();
    pad.SetLayout(SwitchLayout::PairLeft);
    ASSERT_TRUE(pad.Decode(report.data(), report.size(), Collect(events)));
    EXPECT_FLOAT_EQ(events.at("dpad.left"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("paddle.p3"), 1.0f) << "SL: SDL's LEFT_PADDLE1";
    EXPECT_FLOAT_EQ(events.at("share"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("lstick.x"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("lstick.y"), 0.0f);
    EXPECT_EQ(events.count("south"), 0u);
}

TEST(JoyCon_Test, RightHalfAndRightAlone)
{
    SwitchProPad half(SwitchLayout::PairRight);
    Events events;
    std::vector<uint8_t> report = Report();
    report[3] = 0x04 | 0x80 | 0x10;  // B, ZR, SR
    report[4] = 0x10;                // Home
    PutStick(report.data() + 9, 2048, 2048 + kReach);  // up
    ASSERT_TRUE(half.Decode(report.data(), report.size(), Collect(events)));
    EXPECT_FLOAT_EQ(events.at("south"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("rt"), 1.0f) << "ZR: a digital trigger";
    EXPECT_FLOAT_EQ(events.at("paddle.p1"), 1.0f) << "SR: SDL's RIGHT_PADDLE1";
    EXPECT_FLOAT_EQ(events.at("guide"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("rstick.y"), -1.0f) << "up is -1";
    EXPECT_EQ(events.count("lstick.x"), 0u) << "the left half gives that";

    SwitchProPad alone(SwitchLayout::JoyConRight);
    events.clear();
    report[3] = 0x08;  // A: the bottom face held sideways
    report[4] = 0x04;  // its stick's click
    ASSERT_TRUE(alone.Decode(report.data(), report.size(), Collect(events)));
    EXPECT_FLOAT_EQ(events.at("south"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("lstick.click"), 1.0f);
    EXPECT_FLOAT_EQ(events.at("lstick.x"), 1.0f) << "SDL: LEFTX = y";
    EXPECT_EQ(events.count("rstick.y"), 0u);
}

TEST(JoyCon_Test, PairRumbleAndBattery)
{
    RumbleLevels levels;
    levels.low = 0.25f;
    levels.high = 0.75f;
    levels.lowHz = 160;
    levels.highHz = 320;
    const RumbleLevels left = SwitchProPad::HalfRumble(levels, true);
    const RumbleLevels right = SwitchProPad::HalfRumble(levels, false);
    EXPECT_FLOAT_EQ(left.low, 0.25f);
    EXPECT_FLOAT_EQ(left.high, 0.0f);
    EXPECT_FLOAT_EQ(right.low, 0.0f);
    EXPECT_FLOAT_EQ(right.high, 0.75f);
    EXPECT_EQ(right.highHz, 320u) << "the frequencies go along";

    BatteryState full = SwitchProPad::BatteryOf(0x80);
    BatteryState low = SwitchProPad::BatteryOf(0x20 | 0x10);  // 2 of 8, charging
    const BatteryState pair = JoyConPairing::Battery(full, low);
    EXPECT_TRUE(pair.known);
    EXPECT_EQ(pair.level, 25);
    EXPECT_FALSE(pair.charging) << "only when both charge";
    EXPECT_EQ(JoyConPairing::Battery(BatteryState{}, low), low) << "one half's until the other reports";
}

TEST(JoyCon_Test, PairingTakesTheOldestOfEachSide)
{
    JoyConPairing pairing;
    EXPECT_FALSE(pairing.Add("L1", true).formed.has_value());
    const JoyConPairing::Change first = pairing.Add("R1", false);
    ASSERT_TRUE(first.formed.has_value());
    EXPECT_EQ(first.formed->id, "L1+R1");
    EXPECT_EQ(pairing.PairOf("R1")->id, "L1+R1");
    EXPECT_EQ(pairing.Find("L1+R1")->right, "R1");
    EXPECT_FALSE(pairing.Add("L2", true).formed.has_value()) << "no right one waits";
    EXPECT_FALSE(pairing.PairOf("L2").has_value());
    EXPECT_EQ(pairing.PairCount(), 1u);

    // R1 goes: L1 is alone; a new right Joy-Con joins the oldest left one
    JoyConPairing::Change change = pairing.Remove("R1");
    ASSERT_TRUE(change.broken.has_value());
    EXPECT_EQ(change.broken->id, "L1+R1");
    EXPECT_FALSE(change.formed.has_value());
    change = pairing.Add("R2", false);
    ASSERT_TRUE(change.formed.has_value());
    EXPECT_EQ(change.formed->id, "L1+R2");

    // L1 goes: R2 joins L2, which waited
    change = pairing.Remove("L1");
    ASSERT_TRUE(change.broken.has_value());
    EXPECT_EQ(change.broken->left, "L1");
    ASSERT_TRUE(change.formed.has_value());
    EXPECT_EQ(change.formed->id, "L2+R2");
    EXPECT_FALSE(pairing.Remove("L9").broken.has_value());
    EXPECT_FALSE(pairing.Add("L2", true).formed.has_value()) << "known already";
    EXPECT_EQ(pairing.PairCount(), 1u);
}

TEST(JoyCon_Test, ChargingGripAsksTheDeviceInfo)
{
    // hid-nintendo's joycon_read_info: subcommand 0x02, the type in the reply's data byte 2 (1 left, 2 right)
    EXPECT_TRUE(SwitchProPad::IsChargingGrip(0x057E, 0x200E));
    int type = -1;
    SwitchProtocol protocol(false, {}, 0, [&type](uint8_t value) { type = value; });
    uint64_t wake = 0;
    uint64_t now = 0;
    for (const uint8_t command : std::vector<uint8_t>{0x02, 0x03, 0x02})
    {
        std::optional<PadOutReport> next = protocol.Next(now, wake);
        ASSERT_TRUE(next.has_value());
        EXPECT_EQ(next->bytes[1], command);
        const uint8_t ack[] = {0x81, command};
        protocol.OnInput(ack, sizeof(ack), ++now);
    }
    std::optional<PadOutReport> next = protocol.Next(now, wake);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->bytes[1], 0x04) << "force USB, no reply";
    next = protocol.Next(now, wake);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->bytes[0], 0x01);
    EXPECT_EQ(next->bytes[10], 0x02) << "the device info subcommand after the handshake";
    std::vector<uint8_t> reply(64, 0);
    reply[0] = 0x21;
    reply[13] = 0x82;
    reply[14] = 0x02;
    reply[15] = 0x04;  // firmware 4.33
    reply[16] = 0x21;
    reply[17] = 0x02;  // a right Joy-Con
    protocol.OnInput(reply.data(), reply.size(), ++now);
    EXPECT_EQ(type, 2);
    next = protocol.Next(now, wake);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->bytes[10], 0x10) << "on to the calibration reads";

    // No answer: type 0 once the step gives up
    int silent = -1;
    SwitchProtocol quiet(true, {}, 0, [&silent](uint8_t value) { silent = value; });
    now = 0;
    while (silent < 0 && now < 2000)
    {
        quiet.Next(now, wake);
        now += SwitchProtocol::kReplyMs;
    }
    EXPECT_EQ(silent, 0);
}
