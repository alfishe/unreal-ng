/// @file steampad_test.cpp
/// @brief Valve's Steam Controllers (SteamPad, SteamProtocol) on byte fixtures after SDL_hidapi_steam.c and
/// SDL_hidapi_steam_triton.c (libsdl-org/SDL 3cffe56317b9): the 2015 pad's state report and lizard-mode set-up, the
/// dongle's wireless events, Triton's state report and its repeated lizard mode off.

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "hid/drivers/steampad.h"

using namespace controllers;

namespace
{
using Events = std::map<std::string, float>;

ControlSink Collect(Events& events)
{
    return [&events](const std::string& control, float value) { events[control] = value; };
}

void Put16(std::vector<uint8_t>& report, size_t at, int value)
{
    report[at] = static_cast<uint8_t>(value & 0xFF);
    report[at + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

/// A 2015 state report (ValveInReport_t, type 1) with @p buttons and the left pair at @p x, @p y
std::vector<uint8_t> ControllerState(uint64_t buttons, int x, int y, uint32_t packet = 1)
{
    std::vector<uint8_t> report(64, 0);
    report[0] = 0x01;  // version 0x0001
    report[2] = 1;     // ID_CONTROLLER_STATE
    report[3] = 60;
    report[4] = static_cast<uint8_t>(packet);
    for (int i = 0; i < 8; i++)
        report[8 + static_cast<size_t>(i)] = static_cast<uint8_t>(buttons >> (8 * i));
    Put16(report, 16, x);
    Put16(report, 18, y);
    return report;
}

/// A Triton state report 0x42 with @p buttons, the left trigger and the left stick
std::vector<uint8_t> TritonState(uint32_t buttons, int trigger, int x, int y)
{
    std::vector<uint8_t> report(64, 0);
    report[0] = 0x42;
    for (int i = 0; i < 4; i++)
        report[2 + static_cast<size_t>(i)] = static_cast<uint8_t>(buttons >> (8 * i));
    Put16(report, 6, trigger);
    Put16(report, 10, x);
    Put16(report, 12, y);
    return report;
}
}  // namespace

TEST(SteamPad_Test, ModelsByProduct)
{
    EXPECT_EQ(SteamPad::ModelOf(0x28DE, 0x1102), SteamModel::Controller);
    EXPECT_EQ(SteamPad::ModelOf(0x28DE, 0x1142), SteamModel::ControllerDongle);
    EXPECT_EQ(SteamPad::ModelOf(0x28DE, 0x1302), SteamModel::Triton);
    EXPECT_EQ(SteamPad::ModelOf(0x28DE, 0x1305), SteamModel::TritonDongle);
    EXPECT_FALSE(SteamPad::ModelOf(0x28DE, 0x1106).has_value()) << "the 2015 pad over Bluetooth LE: not read";
    EXPECT_FALSE(SteamPad::ModelOf(0x28DE, 0x1205).has_value()) << "the Steam Deck";
    EXPECT_FALSE(SteamPad::ModelOf(0x045E, 0x1102).has_value());

    // The 2015 picture's controls: the left pad's click misc.1, the grips paddle.p1 / p3
    const std::vector<std::string> controls = SteamPad::Controls(SteamModel::Controller);
    for (const char* name : {"misc.1", "paddle.p1", "paddle.p3", "rstick.click", "lt", "dpad.up"})
        EXPECT_NE(std::find(controls.begin(), controls.end(), name), controls.end()) << name;
    EXPECT_EQ(std::find(controls.begin(), controls.end(), "paddle.p2"), controls.end());
    EXPECT_EQ(SteamPad::Controls(SteamModel::Triton).size(), 28u);
}

TEST(SteamPad_Test, ControllerStateButtonsTriggersStick)
{
    SteamPad pad(SteamModel::Controller);
    Events events;
    // A, the left grip, the right pad's click; the left trigger byte 0xFF (byte 3 of the buttons)
    const uint64_t buttons = 0x80ULL | 0x8000ULL | 0x40000ULL | (0xFFULL << 24);
    std::vector<uint8_t> report = ControllerState(buttons, 32767, -32767);
    ASSERT_EQ(pad.Decode(report.data(), report.size(), Collect(events)), SteamPad::Event::State);
    EXPECT_EQ(events["south"], 1.0f);
    EXPECT_EQ(events["paddle.p3"], 1.0f);
    EXPECT_EQ(events["rstick.click"], 1.0f);
    EXPECT_EQ(events["lt"], 1.0f) << "0xFF widened to 32767 is past 26000: full";
    EXPECT_EQ(events["rt"], 0.0f);
    EXPECT_EQ(events["east"], 0.0f) << "the trigger byte is not a button";
    EXPECT_FLOAT_EQ(events["lstick.x"], 1.0f);
    EXPECT_FLOAT_EQ(events["lstick.y"], 1.0f) << "the pad's down is +1 here";

    // The left pad touched (not interleaving): the pair is the pad, the stick rests
    events.clear();
    report = ControllerState(0x80000ULL, 1000, 1000, 2);
    ASSERT_EQ(pad.Decode(report.data(), report.size(), Collect(events)), SteamPad::Event::State);
    EXPECT_EQ(events["lstick.x"], 0.0f);
    EXPECT_EQ(events["south"], 0.0f);

    // Old firmware: the pad's click with the pad unused is the stick's click
    events.clear();
    report = ControllerState(0x20000ULL, 0, 0, 3);
    pad.Decode(report.data(), report.size(), Collect(events));
    EXPECT_EQ(events["lstick.click"], 1.0f);
    EXPECT_EQ(events.count("misc.1"), 0u) << "misc.1 never pressed";
}

TEST(SteamPad_Test, DongleWirelessEvents)
{
    std::vector<uint8_t> report(64, 0);
    report[0] = 0x01;
    report[2] = 3;  // ID_CONTROLLER_WIRELESS
    report[3] = 1;
    report[4] = 2;  // established
    SteamPad pad(SteamModel::ControllerDongle);
    EXPECT_EQ(pad.Decode(report.data(), report.size(), {}), SteamPad::Event::Connected);
    report[4] = 1;
    EXPECT_EQ(pad.Decode(report.data(), report.size(), {}), SteamPad::Event::Disconnected);
    report[0] = 0x02;
    EXPECT_EQ(pad.Decode(report.data(), report.size(), {}), SteamPad::Event::None);

    // A pad that goes: what it held is released
    Events events;
    std::vector<uint8_t> state = ControllerState(0x80ULL, 0, 0);
    pad.Decode(state.data(), state.size(), Collect(events));
    ASSERT_EQ(events["south"], 1.0f);
    pad.Release(Collect(events));
    EXPECT_EQ(events["south"], 0.0f);
}

TEST(SteamPad_Test, TritonState)
{
    SteamPad pad(SteamModel::Triton);
    Events events;
    // B, R4 (paddle.p1), L5 (paddle.p4), QAM, the left touchpad's click
    const uint32_t buttons = 0x02 | 0x80 | 0x40000 | 0x10 | 0x04000000;
    std::vector<uint8_t> report = TritonState(buttons, 16384, -32767, 32767);
    ASSERT_EQ(pad.Decode(report.data(), report.size(), Collect(events)), SteamPad::Event::State);
    EXPECT_EQ(events["east"], 1.0f);
    EXPECT_EQ(events["paddle.p1"], 1.0f);
    EXPECT_EQ(events["paddle.p4"], 1.0f);
    EXPECT_EQ(events["misc.2"], 1.0f);
    EXPECT_EQ(events["misc.1"], 1.0f);
    EXPECT_NEAR(events["lt"], 0.5f, 0.001f);
    EXPECT_FLOAT_EQ(events["lstick.x"], -1.0f);
    EXPECT_FLOAT_EQ(events["lstick.y"], -1.0f) << "the stick's up is -1 here";

    report.resize(20);
    EXPECT_EQ(pad.Decode(report.data(), report.size(), {}), SteamPad::Event::None) << "too short";
    const uint8_t gone[] = {0x79, 1};
    EXPECT_EQ(pad.Decode(gone, sizeof(gone), {}), SteamPad::Event::Disconnected);
}

TEST(SteamProtocol_Test, ControllerLeavesLizardMode)
{
    SteamProtocol protocol(SteamModel::Controller);
    uint64_t wake = 0;
    std::vector<std::vector<uint8_t>> sent;
    while (auto next = protocol.Next(0, wake))
    {
        EXPECT_EQ(next->kind, PadOutReport::Kind::SetFeature);
        sent.push_back(next->bytes);
    }
    ASSERT_EQ(sent.size(), 3u);
    for (const auto& report : sent)
    {
        EXPECT_EQ(report.size(), 65u);
        EXPECT_EQ(report[0], 0) << "report id 0";
    }
    EXPECT_EQ(sent[0][1], 0x81) << "CLEAR_DIGITAL_MAPPINGS";
    EXPECT_EQ(sent[1][1], 0x8E) << "LOAD_DEFAULT_SETTINGS";
    EXPECT_EQ(sent[2][1], 0x87) << "SET_SETTINGS_VALUES";
    EXPECT_EQ(sent[2][2], 12) << "four settings of three bytes";
    // The trackpads to TRACKPAD_NONE (7): left mode 7, right mode 8
    EXPECT_EQ(sent[2][6], 7);
    EXPECT_EQ(sent[2][7], 7);
    EXPECT_EQ(sent[2][9], 8);
    EXPECT_EQ(sent[2][10], 7);
    EXPECT_EQ(wake, 0u) << "nothing more until a pad comes";
}

TEST(SteamProtocol_Test, DongleSetsUpAPadThatComes)
{
    SteamProtocol protocol(SteamModel::ControllerDongle);
    uint64_t wake = 0;
    auto first = protocol.Next(0, wake);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->bytes[1], 0xB4) << "DONGLE_GET_WIRELESS_STATE first";
    int count = 0;
    while (protocol.Next(0, wake))
        count++;
    EXPECT_EQ(count, 3);
    std::vector<uint8_t> connect(64, 0);
    connect[0] = 0x01;
    connect[2] = 3;
    connect[3] = 1;
    connect[4] = 2;
    protocol.OnInput(connect.data(), connect.size(), 10);
    count = 0;
    while (protocol.Next(10, wake))
        count++;
    EXPECT_EQ(count, 3) << "the set-up again for the pad that came";
}

TEST(SteamProtocol_Test, TritonRepeatsLizardOff)
{
    SteamProtocol protocol(SteamModel::Triton);
    uint64_t wake = 0;
    auto first = protocol.Next(100, wake);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->bytes, SteamProtocol::TritonLizardOff());
    EXPECT_EQ(first->bytes.size(), 64u);
    EXPECT_EQ(first->bytes[0], 1) << "report id 1";
    EXPECT_EQ(first->bytes[3], 9) << "SETTING_LIZARD_MODE";
    EXPECT_FALSE(protocol.Next(200, wake).has_value());
    EXPECT_EQ(wake, 100 + SteamProtocol::kTritonRepeatMs);
    EXPECT_TRUE(protocol.Next(100 + SteamProtocol::kTritonRepeatMs, wake).has_value()) << "before the watchdog";

    // A dongle's slot: nothing until a pad is there
    SteamProtocol dongle(SteamModel::TritonDongle);
    EXPECT_FALSE(dongle.Next(0, wake).has_value());
    const uint8_t there[] = {0x79, 2};
    dongle.OnInput(there, sizeof(there), 5);
    EXPECT_TRUE(dongle.Next(5, wake).has_value());
}
