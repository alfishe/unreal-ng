/// @file gamecubeadapter_test.cpp
/// @brief The GameCube controller adapter (GameCubeAdapter, GameCubeProtocol) on byte fixtures after
/// SDL_hidapi_gamecube.c (libsdl-org/SDL 3cffe56317b9): ports that come and go, buttons, the widening axis range.

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "hid/drivers/gamecubeadapter.h"

using namespace controllers;

namespace
{
using PortEvents = std::map<std::pair<int, std::string>, float>;

GameCubeAdapter::PortSink Collect(PortEvents& events)
{
    return [&events](int port, const std::string& control, float value) { events[{port, control}] = value; };
}

/// Report 0x21 with every port empty and its axes at rest
std::vector<uint8_t> Report()
{
    std::vector<uint8_t> report(37, 0);
    report[0] = 0x21;
    for (size_t port = 0; port < 4; port++)
        for (size_t axis = 3; axis < 9; axis++)
            report[1 + port * 9 + axis] = axis >= 7 ? 40 : 128;
    return report;
}
}  // namespace

TEST(GameCubeAdapter_Test, PortsComeAndGo)
{
    GameCubeAdapter adapter;
    PortEvents events;
    std::vector<GameCubeAdapter::PortChange> changes;
    std::vector<uint8_t> report = Report();
    ASSERT_TRUE(adapter.Decode(report.data(), report.size(), Collect(events), changes));
    EXPECT_TRUE(changes.empty());
    EXPECT_TRUE(events.empty()) << "no controller, no controls";

    report[1 + 2 * 9] = 0x14;  // port 3: a wired controller, the power cable in
    ASSERT_TRUE(adapter.Decode(report.data(), report.size(), Collect(events), changes));
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].port, 2);
    EXPECT_TRUE(changes[0].connected);
    EXPECT_TRUE(adapter.Connected(2));
    EXPECT_EQ(events.count({2, "south"}), 1u) << "its whole state once";
    EXPECT_EQ(events.count({0, "south"}), 0u);

    changes.clear();
    report[1 + 2 * 9] = 0x04;
    adapter.Decode(report.data(), report.size(), Collect(events), changes);
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_FALSE(changes[0].connected);
    EXPECT_FALSE(adapter.Connected(2));

    const uint8_t other[] = {0x22, 0};
    EXPECT_FALSE(adapter.Decode(other, sizeof(other), Collect(events), changes));
}

TEST(GameCubeAdapter_Test, ButtonsAndAxes)
{
    GameCubeAdapter adapter;
    PortEvents events;
    std::vector<GameCubeAdapter::PortChange> changes;
    std::vector<uint8_t> report = Report();
    uint8_t* port = report.data() + 1;
    port[0] = 0x20;         // a WaveBird
    port[1] = 0x01 | 0x80;  // A, up
    port[2] = 0x02;         // Z
    port[3] = 128 + 88;     // the stick right, at the edge of the starting range
    port[4] = 128 + 88;     // and up
    port[7] = 40 + 176;     // L all the way
    ASSERT_TRUE(adapter.Decode(report.data(), report.size(), Collect(events), changes));
    EXPECT_EQ((events[{0, "south"}]), 1.0f);
    EXPECT_EQ((events[{0, "dpad.up"}]), 1.0f);
    EXPECT_EQ((events[{0, "rb"}]), 1.0f) << "Z";
    EXPECT_EQ((events[{0, "west"}]), 0.0f);
    EXPECT_FLOAT_EQ((events[{0, "lstick.x"}]), 1.0f);
    EXPECT_FLOAT_EQ((events[{0, "lstick.y"}]), -1.0f) << "up is -1 here";
    EXPECT_FLOAT_EQ((events[{0, "lt"}]), 1.0f);
    EXPECT_FLOAT_EQ((events[{0, "rt"}]), 0.0f);
    EXPECT_FLOAT_EQ((events[{0, "rstick.x"}]), 0.0f) << "128 is the middle of 40..216";

    // A stick that reaches past the range widens it: the far edge is full again
    port[3] = 250;
    adapter.Decode(report.data(), report.size(), Collect(events), changes);
    EXPECT_FLOAT_EQ((events[{0, "lstick.x"}]), 1.0f);
    port[3] = 128 + 88;
    adapter.Decode(report.data(), report.size(), Collect(events), changes);
    EXPECT_LT((events[{0, "lstick.x"}]), 1.0f);
}

TEST(GameCubeAdapter_Test, ProtocolStartsTheAdapterOnce)
{
    GameCubeProtocol protocol;
    uint64_t wake = 0;
    auto start = protocol.Next(0, wake);
    ASSERT_TRUE(start.has_value());
    EXPECT_EQ(start->kind, PadOutReport::Kind::Output);
    EXPECT_EQ(start->bytes, std::vector<uint8_t>{0x13});
    protocol.OnWritten(false, 0);
    EXPECT_FALSE(protocol.Next(10, wake).has_value());
    EXPECT_EQ(wake, 1000u) << "a refused start again a second later";
    ASSERT_TRUE(protocol.Next(1000, wake).has_value());
    protocol.OnWritten(true, 1000);
    EXPECT_FALSE(protocol.Next(5000, wake).has_value());
    EXPECT_EQ(wake, 0u);
}
