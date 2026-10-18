/// @file inputrouter_test.cpp
/// @brief The controllers library's router (C2a, mapping-and-profiles.md §1-§3): bindings to retro targets,
/// OR-lists, sticks and the D-pad, turbo in emulated frames, roles, instance assignment, the `auto` connector.
/// No machine: the router is sampled directly, as a ControllerManager samples it at each frame boundary.

#include "router/inputrouter.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "fake/fakebackend.h"
#include "hostinputservice.h"

using namespace controllers;

namespace
{
const HostDeviceInfo kPad{"fake:1", "Pad 1", "xbox", {}};
const HostDeviceInfo kPad2{"fake:2", "Pad 2", "playstation", {}};

Binding Bind(std::vector<std::string> sources, const std::string& target, BindingOptions options = {})
{
    Binding binding;
    binding.sources = std::move(sources);
    std::string error;
    EXPECT_TRUE(ParseRetroTarget(target, binding.target, error)) << error;
    binding.options = options;
    return binding;
}

class InputRouter_Test : public ::testing::Test
{
protected:
    InputRouter _router;
    uint64_t _frame = 100;

    void SetBindings(const std::vector<Binding>& bindings, const std::string& scope = {})
    {
        std::string error;
        ASSERT_TRUE(_router.SetBindings(scope, bindings, error)) << error;
    }

    void Set(const HostDeviceId& device, const std::string& control, float value)
    {
        _router.OnControl({device, control, value, 0});
    }

    /// One frame boundary of @p instance: connector -> mask (absent = not sampled)
    std::map<std::string, uint16_t> Sample(const std::string& instance = "emu-1")
    {
        std::vector<ConnectorSample> out;
        _router.Sample(instance, _frame++, out);
        std::map<std::string, uint16_t> masks;
        for (const ConnectorSample& sample : out)
            masks[sample.connector] = sample.mask;
        return masks;
    }

    uint16_t Kempston(const std::string& instance = "emu-1")
    {
        const auto masks = Sample(instance);
        const auto it = masks.find("kempston");
        return it == masks.end() ? uint16_t{0xFFFF} : it->second;
    }
};
}  // namespace

TEST_F(InputRouter_Test, Router_OrListSticksAndDpad)
{
    _router.OnDeviceAdded(kPad);
    SetBindings({Bind({"south", "key.space"}, "kempston:fire"), Bind({"dpad.*", "lstick"}, "kempston:dpad"),
                 Bind({"lstick.left"}, "kempston:fire2")});

    EXPECT_TRUE(Sample().empty()) << "nothing held: nothing sampled";
    Set("fake:1", "key.space", 1);
    EXPECT_EQ(Kempston(), kFire) << "either member of the OR-list";
    Set("fake:1", "south", 1);
    Set("fake:1", "key.space", 0);
    EXPECT_EQ(Kempston(), kFire);
    Set("fake:1", "south", 0);
    EXPECT_EQ(Kempston(), 0) << "a release is sampled once";
    EXPECT_TRUE(Sample().empty()) << "then the connector is left alone";

    Set("fake:1", "dpad.up", 1);
    Set("fake:1", "lstick.x", 1);
    EXPECT_EQ(Kempston(), kUp | kRight) << "the D-pad and the stick keep their directions";
    Set("fake:1", "dpad.up", 0);
    Set("fake:1", "lstick.x", -1);
    EXPECT_EQ(Kempston(), kLeft | kFire2) << "lstick.left drives its own target too";
}

TEST_F(InputRouter_Test, Router_ShortPressReachesOneFrame)
{
    _router.OnDeviceAdded(kPad);
    SetBindings({Bind({"south"}, "kempston:fire")});
    Set("fake:1", "south", 1);
    Set("fake:1", "south", 0);
    EXPECT_EQ(Kempston(), kFire) << "pressed and released between two frames: the game still sees it";
    EXPECT_EQ(Kempston(), 0);
}

/// testing.md Autofire_EmulatedFrames: on 2 / off 2 gives 1100 1100 in frame samples, whatever the host timing
TEST_F(InputRouter_Test, Autofire_EmulatedFrames)
{
    _router.OnDeviceAdded(kPad);
    BindingOptions turbo;
    turbo.turbo = Turbo::Classic;
    SetBindings({Bind({"rt.click"}, "kempston:fire", turbo)});
    Set("fake:1", "rt", 0.9f);
    std::string pattern;
    for (int i = 0; i < 8; i++)
        pattern += Kempston() == kFire ? '1' : '0';
    EXPECT_EQ(pattern, "11001100") << "an off frame after an on frame is a zero sample, then nothing";
    Set("fake:1", "rt", 0.0f);
    EXPECT_TRUE(Sample().empty()) << "released in an off phase: nothing left to release";
}

TEST_F(InputRouter_Test, Turbo_BurstToggleHoldAndTap)
{
    _router.OnDeviceAdded(kPad);
    BindingOptions burst;
    burst.turbo = Turbo::Burst;
    burst.count = 2;
    burst.on = burst.off = 1;
    BindingOptions toggle;
    toggle.turbo = Turbo::Toggle;
    toggle.on = toggle.off = 1;
    BindingOptions hold;
    hold.turbo = Turbo::Hold;
    BindingOptions tap;
    tap.press = PressMode::Tap;
    tap.frames = 3;
    SetBindings({Bind({"south"}, "kempston:fire", burst), Bind({"east"}, "kempston:fire2", toggle),
                 Bind({"west"}, "kempston:fire3", hold), Bind({"north"}, "kempston:fire4", tap)});

    auto run = [&](uint16_t bit, int frames) {
        std::string pattern;
        for (int i = 0; i < frames; i++)
        {
            const uint16_t mask = Kempston();
            pattern += mask != 0xFFFF && (mask & bit) ? '1' : '0';
        }
        return pattern;
    };

    Set("fake:1", "south", 1);
    Set("fake:1", "south", 0);
    EXPECT_EQ(run(kFire, 6), "101000") << "a burst of two, the button already released";

    Set("fake:1", "east", 1);
    Set("fake:1", "east", 0);
    EXPECT_EQ(run(kFire2, 4), "1010") << "toggle: pulsing after one press";
    Set("fake:1", "east", 1);
    EXPECT_EQ(run(kFire2, 3), "000") << "the next press stops it";
    Set("fake:1", "east", 0);

    Set("fake:1", "west", 1);
    Set("fake:1", "west", 0);
    EXPECT_EQ(run(kFire3, 3), "111") << "autohold";
    Set("fake:1", "west", 1);
    Set("fake:1", "west", 0);
    EXPECT_EQ(run(kFire3, 2), "00");

    Set("fake:1", "north", 1);
    EXPECT_EQ(run(kFire4, 5), "11100") << "a tap is its frames however long the button is held";
}

TEST_F(InputRouter_Test, Roles_InstancesAndTheAutoConnector)
{
    _router.OnDeviceAdded(kPad);
    _router.OnDeviceAdded(kPad2);
    SetBindings({Bind({"south"}, "auto:fire")});
    Set("fake:1", "south", 1);
    Set("fake:2", "south", 1);
    EXPECT_TRUE(Sample().empty()) << "auto: no player connector yet";

    _router.SetPlayerConnector("fake:1", "kempston");
    _router.SetPlayerConnector("fake:2", "sinclair-1");
    auto masks = Sample();
    EXPECT_EQ(masks["kempston"], kFire);
    EXPECT_EQ(masks["sinclair-1"], kFire);

    _router.SetRole("fake:2", DeviceRole::Disabled);
    masks = Sample();
    EXPECT_EQ(masks.count("kempston"), 1u);
    EXPECT_EQ(masks["sinclair-1"], 0) << "a disabled device reaches nothing";

    // Follow focus: only the focused instance; pinned: only its instance; all: every one
    _router.SetRole("fake:2", DeviceRole::Player);
    _router.SetFocus("emu-1");
    EXPECT_EQ(Kempston("emu-2"), 0xFFFF);
    _router.SetInstanceMode("fake:1", InstanceMode::Pinned, "emu-2");
    EXPECT_EQ(Kempston("emu-2"), kFire);
    EXPECT_EQ(Sample("emu-1").count("kempston"), 1u) << "emu-1 had it held: one release";
    EXPECT_EQ(Sample("emu-1").count("kempston"), 0u);
    _router.SetInstanceMode("fake:1", InstanceMode::All);
    EXPECT_EQ(Kempston("emu-1"), kFire);
    EXPECT_EQ(Kempston("emu-3"), kFire);
}

TEST_F(InputRouter_Test, Scopes_DeviceBeatsFamilyBeatsGlobal)
{
    _router.OnDeviceAdded(kPad);
    _router.OnDeviceAdded(kPad2);
    SetBindings({Bind({"south"}, "kempston:fire")});
    SetBindings({Bind({"south"}, "kempston:fire2")}, "family:xbox");
    SetBindings({Bind({"south"}, "kempston:fire3")}, "fake:2");
    Set("fake:1", "south", 1);
    EXPECT_EQ(Kempston(), kFire2) << "the Xbox family's bindings";
    Set("fake:1", "south", 0);
    Kempston();
    Set("fake:2", "south", 1);
    EXPECT_EQ(Kempston(), kFire3) << "the device's own bindings";

    _router.ClearBindings("fake:2");
    EXPECT_EQ(Kempston(), kFire) << "the PlayStation pad falls back to the global bindings, still held";
}

TEST_F(InputRouter_Test, RemovingADeviceReleasesItsTargets)
{
    _router.OnDeviceAdded(kPad);
    SetBindings({Bind({"south"}, "kempston:fire")});
    _router.SetRole("fake:1", DeviceRole::Player);
    Set("fake:1", "south", 1);
    EXPECT_EQ(Kempston(), kFire);
    _router.OnDeviceRemoved("fake:1");
    EXPECT_EQ(Kempston(), 0);
    _router.OnDeviceAdded(kPad);
    EXPECT_EQ(_router.Role("fake:1"), DeviceRole::Player) << "settings are kept by id across a replug";
    EXPECT_TRUE(Sample().empty()) << "a replugged pad starts released";
}

TEST_F(InputRouter_Test, InvalidBindingsAreRefusedWithTheReason)
{
    std::string error;
    EXPECT_FALSE(_router.SetBindings({}, {Bind({"southpaw"}, "kempston:fire")}, error));
    EXPECT_NE(error.find("unknown control 'southpaw'"), std::string::npos) << error;
    EXPECT_FALSE(_router.SetBindings({}, {Bind({"mouse.dx"}, "kempston:fire")}, error));
    EXPECT_NE(error.find("not a button"), std::string::npos) << error;
    BindingOptions turbo;
    turbo.turbo = Turbo::Classic;
    turbo.on = 0;
    EXPECT_FALSE(_router.SetBindings({}, {Bind({"south"}, "kempston:fire", turbo)}, error));
    EXPECT_NE(error.find("binding 0"), std::string::npos) << error;
}

/// The service with the fake backend: devices and events reach the router, the source samples per instance
TEST(HostInputService_Test, FakeBackendThroughTheService)
{
    HostInputService service;
    auto fake = std::make_unique<FakeBackend>();
    FakeBackend* pads = fake.get();
    std::string reason;
    ASSERT_TRUE(service.AddBackend(std::move(fake), reason)) << reason;
    ASSERT_EQ(service.FindBackend("fake"), pads);

    ASSERT_TRUE(pads->Connect(kPad));
    EXPECT_FALSE(pads->Connect(kPad)) << "already connected";
    ASSERT_EQ(service.Router().Devices().size(), 1u);
    EXPECT_EQ(service.Router().Devices()[0].backend, "fake");
    const std::vector<BackendProbe> probe = service.Probe();
    ASSERT_EQ(probe.size(), 1u);
    EXPECT_TRUE(probe[0].running);
    EXPECT_EQ(probe[0].devices, 1u);

    Binding fire;
    fire.sources = {"south"};
    ASSERT_TRUE(ParseRetroTarget("kempston:fire", fire.target, reason)) << reason;
    ASSERT_TRUE(service.Router().SetBindings({}, {fire}, reason)) << reason;
    std::shared_ptr<IHostInputSource> source = service.SourceFor("emu-1");
    ASSERT_TRUE(pads->Press("fake:1", "south"));
    std::vector<ConnectorSample> out;
    source->Sample(1, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].connector, "kempston");
    EXPECT_EQ(out[0].mask, kFire);

    service.Stop();
    EXPECT_TRUE(service.Router().Devices().empty()) << "stopping the backends removes their devices";
    out.clear();
    source->Sample(2, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].mask, 0) << "and releases what they held";
}
