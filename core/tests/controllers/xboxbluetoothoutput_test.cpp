/// @file xboxbluetoothoutput_test.cpp
/// @brief The Xbox One / Series Bluetooth rumble (hid/drivers/xboxbluetoothoutput.h), on every OS: which pads take
/// output report 3, its bytes for given levels (the builder the macOS IOHID and the Linux hidraw paths share), and the
/// RumbleWriter that keeps SetRumble from blocking: the latest levels win, equal levels are not written again, the end
/// stops the motors. The interval between writes needs the real clock: functional/controllers/xboxbluetoothoutput_functional_test.cpp.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include "_helpers/testwaithelper.h"
#include "hid/drivers/xboxbluetoothoutput.h"

using namespace controllers;

TEST(XboxBluetoothOutput_Test, Handles_XboxOneAndSeriesPadsOverBluetooth)
{
    EXPECT_TRUE(XboxBluetoothOutput::Handles(true, 0x045E, "xbox", "xbox-elite-2"));
    EXPECT_TRUE(XboxBluetoothOutput::Handles(true, 0x045E, "xbox", "xbox-series"));
    EXPECT_TRUE(XboxBluetoothOutput::Handles(true, 0x045E, "xbox", "xbox-one-s"));
    EXPECT_TRUE(XboxBluetoothOutput::Handles(true, 0x045E, "xbox", "xbox")) << "an unlisted Microsoft pad";
    EXPECT_FALSE(XboxBluetoothOutput::Handles(false, 0x045E, "xbox", "xbox-series")) << "USB: GIP, not report 3";
    EXPECT_FALSE(XboxBluetoothOutput::Handles(true, 0x045E, "xbox", "xbox-360"));
    EXPECT_FALSE(XboxBluetoothOutput::Handles(true, 0x2DC8, "xbox", "8bitdo")) << "another vendor's protocol";
    EXPECT_FALSE(XboxBluetoothOutput::Handles(true, 0x045E, "", "surface-dial"));
}

/// Report 3 as SDL's HIDAPI Xbox One driver and hid-microsoft's xb1s_ff_report lay it out
TEST(XboxBluetoothOutput_Test, RumbleReport_BytesForLevels)
{
    RumbleLevels levels;
    levels.low = 1.0f;
    levels.high = 0.5f;
    levels.leftTrigger = 0.25f;
    levels.rightTrigger = 0.004f;
    EXPECT_EQ(XboxBluetoothOutput::RumbleReport(levels),
              (std::vector<uint8_t>{0x03, 0x0F, 25, 0, 100, 50, 0xFF, 0x00, 0xEB}))
        << "id, enable all four, left trigger, right trigger, low, high (0..100), duration, delay, repeat";

    levels = {};
    levels.low = 2.0f;   // clamped
    levels.high = -1.0f;
    levels.rightTrigger = 0.996f;
    EXPECT_EQ(XboxBluetoothOutput::RumbleReport(levels),
              (std::vector<uint8_t>{0x03, 0x0F, 0, 100, 100, 0, 0xFF, 0x00, 0xEB}));

    EXPECT_EQ(XboxBluetoothOutput::RumbleReport({}),
              (std::vector<uint8_t>{0x03, 0x0F, 0, 0, 0, 0, 0xFF, 0x00, 0xEB}))
        << "zero stops the motors";
}

namespace
{
RumbleLevels Low(float level)
{
    RumbleLevels levels;
    levels.low = level;
    return levels;
}

/// A device whose writes can be held, as a Bluetooth round trip would
struct FakeRumbleDevice
{
    std::mutex mutex;
    std::condition_variable gate;
    bool open = true;
    std::vector<RumbleLevels> written;
    std::atomic<int> started{0};
    bool refuse = false;

    RumbleWriter::Write Writer()
    {
        return [this](const RumbleLevels& levels) {
            std::unique_lock<std::mutex> lock(mutex);
            ++started;
            gate.wait(lock, [this] { return open; });
            written.push_back(levels);
            return !refuse;
        };
    }
    void Open()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            open = true;
        }
        gate.notify_all();
    }
    std::vector<RumbleLevels> Written()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return written;
    }
};
}  // namespace

TEST(XboxBluetoothOutput_Test, Writer_LatestWinsWhileAWriteIsInFlight)
{
    FakeRumbleDevice device;
    device.open = false;
    {
        RumbleWriter writer(device.Writer(), std::chrono::milliseconds(0));
        writer.Set(Low(0.2f));
        ASSERT_TRUE(TestWait::For([&] { return device.started.load() == 1; }));
        writer.Set(Low(0.4f));
        writer.Set(Low(0.6f));  // replaces 0.4, not written yet
        device.Open();
        ASSERT_TRUE(TestWait::For([&] { return writer.Written() == 2; }));
        const std::vector<RumbleLevels> written = device.Written();
        ASSERT_EQ(written.size(), 2u);
        EXPECT_EQ(written[0], Low(0.2f));
        EXPECT_EQ(written[1], Low(0.6f)) << "the latest levels";
        EXPECT_EQ(writer.Failed(), 0u);
    }
    const std::vector<RumbleLevels> written = device.Written();
    ASSERT_EQ(written.size(), 3u);
    EXPECT_EQ(written[2], RumbleLevels{}) << "the end stops the motors";
}

TEST(XboxBluetoothOutput_Test, Writer_EqualLevelsAreNotWrittenAgainAndAStoppedPadIsLeftAlone)
{
    FakeRumbleDevice device;
    {
        RumbleWriter writer(device.Writer(), std::chrono::milliseconds(0));
        writer.Set({});  // the motors are off at the start: nothing to write
        writer.Set(Low(0.5f));
        ASSERT_TRUE(TestWait::For([&] { return writer.Written() == 1; }));
        writer.Set(Low(0.5f));
        writer.Set({});
        ASSERT_TRUE(TestWait::For([&] { return writer.Written() == 2; }));
    }
    const std::vector<RumbleLevels> written = device.Written();
    ASSERT_EQ(written.size(), 2u) << "no repeat of 0.5, no zero at the end after a zero";
    EXPECT_EQ(written[0], Low(0.5f));
    EXPECT_EQ(written[1], RumbleLevels{});
}

TEST(XboxBluetoothOutput_Test, Writer_ARefusedWriteIsRetriedAndStoppedAtTheEnd)
{
    FakeRumbleDevice device;
    device.refuse = true;
    {
        RumbleWriter writer(device.Writer(), std::chrono::milliseconds(0));
        writer.Set(Low(0.5f));
        ASSERT_TRUE(TestWait::For([&] { return writer.Failed() == 1; }));
        writer.Set(Low(0.5f));  // the refused write may not have landed: written again
        ASSERT_TRUE(TestWait::For([&] { return writer.Failed() == 2; }));
    }
    const std::vector<RumbleLevels> written = device.Written();
    ASSERT_EQ(written.size(), 3u);
    EXPECT_EQ(written[2], RumbleLevels{}) << "the motors may run: zero at the end";
}
