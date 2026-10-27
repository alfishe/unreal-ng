/// @file haptics_test.cpp
/// @brief H1 (haptics.md §3, §6.3, H-5): the effect mixer takes the loudest effect per channel, ends effects on time,
/// stops every motor when the gate closes; HostInputService writes the levels to the backend that owns the device.

#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "fake/fakebackend.h"
#include "haptics/hapticmixer.h"
#include "_helpers/fixedmixerclock.h"
#include "hostinputservice.h"

using namespace controllers;
using namespace std::chrono_literals;

namespace
{
struct Recorder
{
    std::vector<std::pair<HostDeviceId, RumbleLevels>> writes;
    HapticMixer::Writer Writer()
    {
        return [this](const HostDeviceId& device, const RumbleLevels& levels) {
            writes.emplace_back(device, levels);
            return device != "no-motors";
        };
    }
};
}  // namespace

TEST(Haptics_Test, Mixer_LoudestPerChannelEndsOnTimeAndTheGate)
{
    Recorder recorder;
    HapticMixer mixer(recorder.Writer(), false, FixedMixerClock());
    const auto start = FixedMixerTime();
    mixer.Play("pad", {{0.5f, 0.0f, 0.0f, 0.0f}, 1000, false});
    mixer.Play("pad", {{0.2f, 0.8f, 0.0f, 0.0f}, 1000, false});
    EXPECT_EQ(mixer.Written("pad"), (RumbleLevels{0.5f, 0.8f, 0.0f, 0.0f})) << "the loudest per channel";

    mixer.Tick(start + 2s);
    EXPECT_EQ(mixer.Written("pad"), RumbleLevels{}) << "both effects over: the motors stop";
    const size_t writes = recorder.writes.size();
    mixer.Tick(start + 3s);
    EXPECT_EQ(recorder.writes.size(), writes) << "nothing changed, nothing written";

    mixer.SetStrength(0.5f);
    mixer.Play("pad", {{1.0f, 1.0f, 0.0f, 0.0f}, 60000, false});
    EXPECT_EQ(mixer.Written("pad"), (RumbleLevels{0.5f, 0.5f, 0.0f, 0.0f})) << "the master strength";
    mixer.SetGate(false);
    EXPECT_EQ(mixer.Written("pad"), RumbleLevels{}) << "H-5: a closed gate stops the motors at once";
    mixer.Play("pad", {{1.0f, 1.0f, 0.0f, 0.0f}, 60000, false});
    EXPECT_EQ(mixer.Written("pad"), RumbleLevels{}) << "effects are dropped while the gate is closed";
    mixer.Play("pad", {{0.0f, 1.0f, 0.0f, 0.0f}, 60000, true});
    EXPECT_EQ(mixer.Written("pad").high, 0.5f) << "the debug feedback plays through a closed gate";
    mixer.StopAll();
    mixer.SetGate(true);

    mixer.Play("no-motors", {{1.0f, 0.0f, 0.0f, 0.0f}, 60000, false});
    EXPECT_EQ(mixer.Written("no-motors"), RumbleLevels{}) << "a device without motors is forgotten";
}

TEST(Haptics_Test, Service_WritesTheOwningBackendAndStopsOnExit)
{
    HostInputService service;
    auto owned = std::make_unique<FakeBackend>();
    FakeBackend* fake = owned.get();
    std::string reason;
    ASSERT_TRUE(service.AddBackend(std::move(owned), reason)) << reason;
    ASSERT_TRUE(fake->Connect({"fake:1", "Pad", "xbox", "xbox", ""}));
    EXPECT_TRUE(service.HasRumble("fake:1"));
    EXPECT_FALSE(service.HasRumble("fake:2"));

    service.PlayEffect("fake:1", {{0.25f, 0.75f, 0.0f, 0.0f}, 60000, false});
    EXPECT_EQ(fake->Rumble("fake:1"), (RumbleLevels{0.25f, 0.75f, 0.0f, 0.0f}));
    service.SetHapticsGate(false);
    EXPECT_EQ(fake->Rumble("fake:1"), RumbleLevels{});
    service.SetHapticsGate(true);

    service.Identify("fake:1");
    EXPECT_GT(fake->Rumble("fake:1").low, 0.0f) << "identify pulses both motors";
    service.Stop();
    EXPECT_EQ(service.Haptics().Written("fake:1"), RumbleLevels{}) << "H-5: every motor stopped on exit";
}
