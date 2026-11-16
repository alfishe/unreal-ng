/// @file drivehaptics_test.cpp
/// @brief haptics.md §4.4: the floppy drive under the hand - every step of the head a short dry knock (a lone one once, a
/// seek's series at the step rate), short and quiet; the turning disk plays nothing

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>

#include "haptics/drivehaptics.h"
#include "haptics/hapticmixer.h"

using namespace controllers;

namespace
{
using Clock = std::chrono::steady_clock;

struct Rig
{
    Clock::time_point now = Clock::time_point{} + std::chrono::hours(1);
    std::vector<std::pair<HostDeviceId, HapticClicks>> clicks;
    uint8_t actuators = kRumbleLow | kRumbleHigh | kRumbleHdLeft | kRumbleHdRight;  // a Steam Deck
    HapticMixer mixer{[](const HostDeviceId&, const RumbleLevels&) { return true; }, false, [this] { return now; }};
    DriveHaptics drive{nullptr,
                       mixer,
                       [] { return std::vector<HostDeviceId>{"deck"}; },
                       [this](const HostDeviceId&) { return actuators; },
                       [this](const HostDeviceId& device, const HapticClicks& c) { clicks.emplace_back(device, c); }};
    uint64_t frameNumber = 100;

    void Frame(uint16_t steps, uint8_t stepMs)
    {
        HapticFrame frame;
        frame.frame = frameNumber++;
        frame.drive.present = true;
        frame.drive.steps = steps;
        frame.drive.stepMs = stepMs;
        drive.OnHapticFrame(frame);
        now += std::chrono::milliseconds(20);
        mixer.Tick(now);
    }
};
}  // namespace

TEST(DriveHaptics_Test, ALoneStepIsOneShortKnock)
{
    Rig rig;
    rig.Frame(1, 6);
    ASSERT_EQ(rig.clicks.size(), 1u);
    const HapticClicks& knock = rig.clicks[0].second;
    EXPECT_EQ(knock.wave, HapticClicks::Wave::Click);
    EXPECT_EQ(knock.count, 1u);
    EXPECT_EQ(knock.pulseUs, DriveHaptics::kPulseUs) << "a raw pulse under a millisecond, not the firmware's click";
    EXPECT_LE(knock.pulseUs, 500u) << "short: under the 600 us that felt too long";
    EXPECT_FLOAT_EQ(knock.level, DriveHaptics::kTickLevel);
    EXPECT_LT(knock.level, 1.0f) << "below full strength, which felt too strong";
    for (int i = 0; i < 10; i++)  // 200 ms reading the track
        rig.Frame(0, 6);
    EXPECT_EQ(rig.clicks.size(), 1u) << "no steps, nothing";
    rig.Frame(1, 6);
    ASSERT_EQ(rig.clicks.size(), 2u);
    EXPECT_EQ(rig.drive.Last().step, DriveHaptics::Step::Tick) << "the next track: another lone knock";
}

TEST(DriveHaptics_Test, ASeekKnocksAtTheStepRate)
{
    Rig rig;
    rig.Frame(3, 6);
    ASSERT_EQ(rig.clicks.size(), 1u);
    const HapticClicks& train = rig.clicks[0].second;
    EXPECT_EQ(train.count, 3u) << "a knock per step";
    EXPECT_EQ(train.periodMs, 6u) << "at the step rate the controller was told";
    EXPECT_EQ(train.pulseUs, DriveHaptics::kPulseUs);
    EXPECT_FLOAT_EQ(train.level, DriveHaptics::kSeekLevel);
    EXPECT_EQ(rig.drive.Last().step, DriveHaptics::Step::Seek);

    // The seek goes on in the next frames: each frame's train at once, nothing queued
    rig.Frame(3, 6);
    ASSERT_EQ(rig.clicks.size(), 2u);
    EXPECT_EQ(rig.drive.Last().step, DriveHaptics::Step::Seek);
    rig.Frame(1, 6);
    EXPECT_EQ(rig.drive.Last().step, DriveHaptics::Step::Seek) << "a lone step inside a seek continues it";

    // A seek that started with a single step in its frame: a lone knock, then the train of the rest
    Rig late;
    late.Frame(1, 3);
    late.Frame(6, 3);
    ASSERT_EQ(late.clicks.size(), 2u);
    EXPECT_EQ(late.clicks[0].second.count, 1u);
    EXPECT_EQ(late.clicks[1].second.count, 6u);
    EXPECT_EQ(late.clicks[1].second.periodMs, 3u);
}

TEST(DriveHaptics_Test, AFastSeekPlaysAtTheDrivesPaceAndIsBounded)
{
    Rig rig;
    rig.Frame(80, 6);  // fast disk: a whole seek in one frame
    ASSERT_EQ(rig.clicks.size(), 1u);
    EXPECT_EQ(rig.clicks[0].second.count, 80u) << "an 80-track seek, every step at the drive's pace";

    // Frames fed in a burst (the emulator runs ahead of the audio): each plays its own steps, none waits for another
    Rig burst;
    for (int i = 0; i < 3; i++)
    {
        HapticFrame frame;
        frame.frame = 500 + i;
        frame.drive.present = true;
        frame.drive.steps = 3;
        frame.drive.stepMs = 6;
        burst.drive.OnHapticFrame(frame);
    }
    ASSERT_EQ(burst.clicks.size(), 3u);
    for (const auto& [device, c] : burst.clicks)
        EXPECT_EQ(c.count, 3u) << "the frame's own steps, not a growing queue";

    Rig huge;
    huge.Frame(60000, 3);
    EXPECT_EQ(huge.clicks[0].second.count, DriveHaptics::kSeekMaxMs / 3) << "a runaway count is cut to kSeekMaxMs";
}

TEST(DriveHaptics_Test, AHoldStopsTheDriveAndPassesToTheInnerFeed)
{
    struct Inner : IHapticFeed
    {
        int frames = 0;
        bool held = false;
        void OnHapticFrame(const HapticFrame&) override { frames++; }
        void OnHapticHold(bool h) override { held = h; }
    };
    auto inner = std::make_shared<Inner>();
    Rig rig;
    DriveHaptics drive(inner, rig.mixer, [] { return std::vector<HostDeviceId>{"deck"}; },
                       [&rig](const HostDeviceId&) { return rig.actuators; },
                       [&rig](const HostDeviceId& d, const HapticClicks& c) { rig.clicks.emplace_back(d, c); });
    HapticFrame frame;
    frame.drive.present = true;
    frame.drive.steps = 40;
    frame.drive.stepMs = 6;
    drive.OnHapticFrame(frame);
    EXPECT_EQ(inner->frames, 1) << "the wrapped feed sees every frame";
    EXPECT_EQ(rig.mixer.Written("deck").low, 0.0f) << "the turning disk plays nothing";
    drive.OnHapticHold(true);
    EXPECT_TRUE(inner->held);
    const size_t sent = rig.clicks.size();
    drive.OnHapticFrame(frame);
    EXPECT_EQ(rig.clicks.size(), sent) << "held: the drive plays nothing";
    EXPECT_EQ(inner->frames, 2);
}

TEST(DriveHaptics_Test, APadWithoutCoilsPlaysItsHighMotor)
{
    Rig rig;
    rig.actuators = kRumbleLow | kRumbleHigh;
    rig.Frame(1, 6);
    EXPECT_TRUE(rig.clicks.empty());
    EXPECT_EQ(rig.mixer.Written("deck").high, 0.0f) << "a tick is a 15 ms pulse: over by the next frame";

    HapticFrame frame;
    frame.drive.present = true;
    frame.drive.steps = 10;
    frame.drive.stepMs = 6;
    rig.drive.OnHapticFrame(frame);
    rig.mixer.Tick(rig.now);
    EXPECT_FLOAT_EQ(rig.mixer.Written("deck").high, DriveHaptics::kStepMotor) << "a seek holds the high motor";
}
