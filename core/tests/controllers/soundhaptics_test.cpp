/// @file soundhaptics_test.cpp
/// @brief H2 (haptics.md §4): the generic sound filters on frame summaries; the core collects the AY and beeper
/// counters a frame at a time

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "haptics/hapticmixer.h"
#include "haptics/soundhaptics.h"

using namespace controllers;

namespace
{
HapticFrame Frame()
{
    HapticFrame frame;
    frame.ayCount = 1;
    frame.frameTStates = 70000;  // 20 ms at 3.5 MHz
    frame.cpuHz = 3500000;
    return frame;
}
}  // namespace

TEST(SoundHaptics_Test, NoiseEnvelopeAndBeeper)
{
    HapticMixer mixer([](const HostDeviceId&, const RumbleLevels&) { return true; }, false);
    SoundHaptics haptics(mixer, [] { return std::vector<HostDeviceId>{"pad"}; });

    HapticFrame frame = Frame();
    haptics.OnHapticFrame(frame);
    EXPECT_EQ(mixer.Written("pad"), RumbleLevels{}) << "silence plays nothing";

    frame.ay[0].noiseVolume = {15, 0, 0};
    haptics.OnHapticFrame(frame);
    EXPECT_FLOAT_EQ(haptics.Last().low, 0.6f) << "ay-noise: a full-volume noise channel";
    EXPECT_GT(mixer.Written("pad").low, 0.0f);

    frame.ay[0].noiseVolume = {0, 0, 0};
    haptics.OnHapticFrame(frame);
    EXPECT_GT(haptics.Last().low, 0.0f) << "the noise level falls away, not at once";
    EXPECT_LT(haptics.Last().low, 0.6f);
    for (int i = 0; i < 20; i++)
        haptics.OnHapticFrame(frame);
    EXPECT_EQ(haptics.Last().low, 0.0f);

    frame.ay[0].envelopeRestarts = 1;
    frame.ay[0].envelopeShape = 0x09;
    frame.ay[0].envelopeUsed = true;
    haptics.OnHapticFrame(frame);
    EXPECT_EQ(haptics.Last().low, 1.0f) << "ay-envelope: a decaying restart is a thump";
    frame.ay[0].envelopeShape = 0x0E;
    mixer.StopAll();
    haptics.OnHapticFrame(frame);
    EXPECT_EQ(mixer.Written("pad"), RumbleLevels{}) << "a repeating triangle is music, not a boom";
    frame.ay[0] = {};

    frame.beeperEdges = 20;  // 1 edge per ms: a tune
    haptics.OnHapticFrame(frame);
    EXPECT_EQ(haptics.Last().high, 0.0f);
    frame.beeperEdges = 100;  // 5 per ms: a noise burst
    haptics.OnHapticFrame(frame);
    EXPECT_FLOAT_EQ(haptics.Last().high, 0.5f) << "beeper-density";
}
