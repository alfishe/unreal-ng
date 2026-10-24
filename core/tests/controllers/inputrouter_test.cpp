/// @file inputrouter_test.cpp
/// @brief The controllers library's router (mapping-and-profiles.md §1-§3): bindings to retro targets, OR-lists,
/// sticks and the D-pad, turbo in emulated frames, roles, instance assignment, the `auto` connector (C2a); layers,
/// layer keys, chords, key / macro / mouse / action targets, consume (C2b).
/// No machine: the router is sampled directly, as a ControllerManager samples it at each frame boundary.

#include "router/inputrouter.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "fake/fakebackend.h"
#include "hostinputservice.h"
#include "router/zxkeys.h"

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

Binding To(std::vector<std::string> sources, const Target& target, const std::string& layer = kPlayLayer)
{
    Binding binding;
    binding.sources = std::move(sources);
    binding.target = target;
    binding.layer = layer;
    return binding;
}

Target Layer(const std::string& layer, const std::string& mode = "hold")
{
    Target target;
    std::string error;
    EXPECT_TRUE(ParseLayerTarget(layer, mode, target, error)) << error;
    return target;
}

Target Keys(const std::string& keys)
{
    Target target;
    std::string error;
    EXPECT_TRUE(ParseKeysTarget(keys, target, error)) << error;
    return target;
}

Target Mouse(const std::string& control, float scale = 8.0f)
{
    Target target;
    std::string error;
    EXPECT_TRUE(ParseMouseTarget(control, scale, target, error)) << error;
    return target;
}

Target Retro(const std::string& text)
{
    Target target;
    std::string error;
    EXPECT_TRUE(ParseRetroTarget(text, target, error)) << error;
    return target;
}

class InputRouter_Test : public ::testing::Test
{
protected:
    InputRouter _router;
    uint64_t _frame = 100;
    uint64_t _now = 1'000'000'000;  ///< the router's host clock, ns
    HostSample _last;               ///< the last Sample()
    std::vector<ActionEvent> _actions;

    void SetUp() override
    {
        _router.SetClock([this] { return _now; });
        _router.SetActionSink([this](const ActionEvent& action) { _actions.push_back(action); });
    }

    void AdvanceMs(uint64_t ms)
    {
        _now += ms * 1'000'000;
    }

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
        _last.Clear();
        _router.Sample(instance, _frame++, _last);
        std::map<std::string, uint16_t> masks;
        for (const ConnectorSample& sample : _last.connectors)
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

TEST_F(InputRouter_Test, Connectors_ThePatchCordsOfADevice)
{
    // The dialog's cords (ui-ux.md §2.3): the connectors a player's bindings drive, `auto` as its player connector,
    // each once; the debug layer and the non-retro targets draw none
    _router.OnDeviceAdded(kPad);
    SetBindings({Bind({"south"}, "auto:fire"), Bind({"dpad.*"}, "auto:dpad"), Bind({"east"}, "sinclair-1:fire"),
                 To({"north"}, Keys("SPACE")), To({"west"}, Retro("keyjoy.1:fire"), kDebugLayer)});
    EXPECT_EQ(_router.Connectors("fake:1"), std::vector<std::string>{"sinclair-1"}) << "no player connector yet";
    _router.SetPlayerConnector("fake:1", "kempston");
    EXPECT_EQ(_router.PlayerConnector("fake:1"), "kempston");
    EXPECT_EQ(_router.Connectors("fake:1"), (std::vector<std::string>{"kempston", "sinclair-1"}));
    _router.SetRole("fake:1", DeviceRole::DebuggerRemote);
    EXPECT_TRUE(_router.Connectors("fake:1").empty()) << "a debugger remote reaches no connector";
    EXPECT_TRUE(_router.Connectors("fake:9").empty()) << "an unknown device";
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
    HostSample out;
    source->Sample(1, out);
    ASSERT_EQ(out.connectors.size(), 1u);
    EXPECT_EQ(out.connectors[0].connector, "kempston");
    EXPECT_EQ(out.connectors[0].mask, kFire);

    service.Stop();
    EXPECT_TRUE(service.Router().Devices().empty()) << "stopping the backends removes their devices";
    out.Clear();
    source->Sample(2, out);
    ASSERT_EQ(out.connectors.size(), 1u);
    EXPECT_EQ(out.connectors[0].mask, 0) << "and releases what they held";
}

/// region <C2b: layers, chords, targets>

/// testing.md Router_LayerReleasesHeld; DT-5 rules 2, 4, 5
TEST_F(InputRouter_Test, Router_LayerReleasesHeld)
{
    _router.OnDeviceAdded(kPad);
    SetBindings({Bind({"south"}, "kempston:fire"), To({"paddle.p4"}, Layer(kDebugLayer)),
                 To({"south"}, ActionTarget("ttd.scrub_back"), kDebugLayer), Bind({"paddle.p4"}, "kempston:fire2")});
    Set("fake:1", "south", 1);
    EXPECT_EQ(Kempston(), kFire);

    Set("fake:1", "paddle.p4", 1);
    EXPECT_EQ(_router.Layers("fake:1"), (std::vector<std::string>{kDebugLayer}));
    EXPECT_EQ(Kempston(), 0) << "the switch releases the held fire in the next sample";
    EXPECT_TRUE(_actions.empty()) << "south held across the switch is ignored until released";
    EXPECT_TRUE(_router.Consumes("fake:1", "paddle.p4")) << "the layer key is the layer's alone";

    Set("fake:1", "south", 0);
    Set("fake:1", "south", 1);
    ASSERT_EQ(_actions.size(), 1u);
    EXPECT_EQ(_actions[0].action, "ttd.scrub_back");
    EXPECT_TRUE(_actions[0].pressed);
    EXPECT_TRUE(Sample().empty()) << "debug is opaque: play's fire does nothing while it is up";

    Set("fake:1", "paddle.p4", 0);
    ASSERT_EQ(_actions.size(), 2u);
    EXPECT_FALSE(_actions[1].pressed) << "dropping the layer releases its held action";
    EXPECT_TRUE(Sample().empty()) << "south held across the drop: still ignored, fire2 never sent";
    Set("fake:1", "south", 0);
    Set("fake:1", "south", 1);
    EXPECT_EQ(Kempston(), kFire);
}

/// testing.md Router_OrListAndChord; DT-5 rule 4: the chord's members wait for the window
TEST_F(InputRouter_Test, Router_OrListAndChord)
{
    _router.OnDeviceAdded(kPad);
    Binding chord = To({"view", "menu"}, Layer(kDebugLayer, "toggle"));
    chord.chord = true;
    SetBindings({chord, Bind({"view"}, "kempston:fire2"), To({"menu"}, ActionTarget("osd.keyboard")),
                 To({"south"}, ActionTarget("ttd.pause"), kDebugLayer)});

    Set("fake:1", "view", 1);
    AdvanceMs(10);
    Set("fake:1", "menu", 1);
    EXPECT_EQ(_router.Layers("fake:1"), (std::vector<std::string>{kDebugLayer})) << "the chord entered debug";
    AdvanceMs(100);
    EXPECT_TRUE(Sample().empty()) << "view's own binding never saw the press";
    Set("fake:1", "view", 0);
    Set("fake:1", "menu", 0);
    EXPECT_TRUE(_actions.empty()) << "nor menu's";

    Set("fake:1", "view", 1);
    Set("fake:1", "menu", 1);
    EXPECT_EQ(_router.Layers("fake:1"), (std::vector<std::string>{kPlayLayer})) << "again: toggled off";
    Set("fake:1", "view", 0);
    Set("fake:1", "menu", 0);

    // A member alone: its binding acts once the window closes
    Set("fake:1", "view", 1);
    AdvanceMs(30);
    EXPECT_EQ(Kempston(), 0xFFFF) << "inside the window: held back";
    AdvanceMs(40);
    EXPECT_EQ(Kempston(), kFire2) << "the window closed at the sample";
    Set("fake:1", "view", 0);
    EXPECT_EQ(Kempston(), 0);

    // A tap inside the window: the press goes out at the release
    Set("fake:1", "menu", 1);
    AdvanceMs(20);
    Set("fake:1", "menu", 0);
    ASSERT_EQ(_actions.size(), 2u);
    EXPECT_EQ(_actions[0].action, "osd.keyboard");
    EXPECT_TRUE(_actions[0].pressed);
    EXPECT_FALSE(_actions[1].pressed);

    // A paused machine samples nothing: Poll closes the window
    Set("fake:1", "menu", 1);
    AdvanceMs(70);
    _router.Poll();
    ASSERT_EQ(_actions.size(), 3u);
    EXPECT_TRUE(_actions[2].pressed);
}

/// DT-5 rule 2: a shift layer overrides only what it names; layer modes
TEST_F(InputRouter_Test, Layers_ShiftOverridesWhatItNamesAndModes)
{
    _router.OnDeviceAdded(kPad);
    SetBindings({Bind({"south"}, "kempston:fire"), Bind({"east"}, "kempston:fire2"), To({"lb"}, Layer("keys")),
                 To({"rb"}, Layer("keys", "toggle")), To({"view"}, Layer("keys", "oneshot")),
                 To({"south"}, Keys("SPACE"), "keys"), To({"west"}, Target{}, "keys"),
                 Bind({"west"}, "kempston:fire3")});
    std::vector<uint8_t> space;
    ASSERT_TRUE(ZxKeysFromName("space", space));

    Set("fake:1", "lb", 1);
    Set("fake:1", "south", 1);
    Set("fake:1", "east", 1);
    Set("fake:1", "west", 1);
    EXPECT_EQ(Kempston(), kFire2) << "east is not named by the layer: play's binding acts";
    EXPECT_EQ(_last.keys, space) << "south is the layer's";
    Set("fake:1", "lb", 0);
    EXPECT_EQ(Kempston(), 0) << "the drop releases everything held; west was unbound by the layer";
    EXPECT_TRUE(_last.keys.empty());
    for (const char* control : {"south", "east", "west"})
        Set("fake:1", control, 0);

    Set("fake:1", "rb", 1);
    Set("fake:1", "rb", 0);
    EXPECT_EQ(_router.Layers("fake:1"), (std::vector<std::string>{kPlayLayer, "keys"})) << "toggle: up";
    Set("fake:1", "rb", 1);
    Set("fake:1", "rb", 0);
    EXPECT_EQ(_router.Layers("fake:1"), (std::vector<std::string>{kPlayLayer})) << "and down";

    Set("fake:1", "view", 1);
    Set("fake:1", "view", 0);
    Set("fake:1", "east", 1);
    Set("fake:1", "east", 0);
    EXPECT_EQ(_router.Layers("fake:1").size(), 2u) << "oneshot: a control the layer does not bind keeps it up";
    Set("fake:1", "south", 1);
    Sample();
    EXPECT_EQ(_last.keys, space);
    Set("fake:1", "south", 0);
    EXPECT_EQ(_router.Layers("fake:1"), (std::vector<std::string>{kPlayLayer})) << "dropped after the layer's press";
}

/// A layer key to a layer with no bindings is an ordinary button (DT-5 rule 6); a debugger remote has debug alone
TEST_F(InputRouter_Test, Layers_InertKeyAndTheDebuggerRemote)
{
    _router.OnDeviceAdded(kPad);
    SetBindings({To({"paddle.p4"}, Layer(kDebugLayer)), Bind({"paddle.p4"}, "kempston:fire"),
                 Bind({"south"}, "kempston:fire2")});
    Set("fake:1", "paddle.p4", 1);
    EXPECT_EQ(Kempston(), kFire) << "no debug profile: the layer key is a button";
    Set("fake:1", "paddle.p4", 0);
    Sample();

    SetBindings({Bind({"south"}, "kempston:fire2"), To({"south"}, ActionTarget("ttd.step_back"), kDebugLayer)});
    _router.SetRole("fake:1", DeviceRole::DebuggerRemote);
    EXPECT_EQ(_router.Layers("fake:1"), (std::vector<std::string>{kDebugLayer}));
    Set("fake:1", "south", 1);
    EXPECT_TRUE(Sample().empty()) << "a debugger remote drives no connector";
    ASSERT_EQ(_actions.size(), 1u);
    EXPECT_EQ(_actions[0].action, "ttd.step_back");
}

/// The debugger window has the focus: a player pad acts as a debugger remote with no layer key, a held play target
/// is released by the switch and the switch back
TEST_F(InputRouter_Test, DebugFocus_APlayerPadDrivesTheDebugger)
{
    _router.OnDeviceAdded(kPad);
    SetBindings({Bind({"south"}, "kempston:fire"), To({"south"}, ActionTarget("run.step_in"), kDebugLayer)});
    Set("fake:1", "south", 1);
    EXPECT_EQ(Kempston(), kFire);

    _router.SetDebugFocus(true);
    EXPECT_EQ(_router.Layers("fake:1"), (std::vector<std::string>{kDebugLayer}));
    EXPECT_EQ(Kempston(), 0) << "the switch releases the fire";
    EXPECT_TRUE(_actions.empty()) << "south held across the switch waits for its release";
    Set("fake:1", "south", 0);
    Set("fake:1", "south", 1);
    ASSERT_EQ(_actions.size(), 1u);
    EXPECT_EQ(_actions[0].action, "run.step_in");
    EXPECT_TRUE(Sample().empty()) << "the debug layer drives no connector";
    Set("fake:1", "south", 0);

    _router.SetDebugFocus(false);
    EXPECT_EQ(_router.Layers("fake:1"), (std::vector<std::string>{kPlayLayer}));
    Set("fake:1", "south", 1);
    EXPECT_EQ(Kempston(), kFire) << "the game has the pad back";
}

/// An encoder's steps run an action (debug-and-ttd.md §4.4): alone, or with buttons held while it turns; a button
/// held for a turn gives its own binding nothing, a press alone acts on its release
TEST_F(InputRouter_Test, Encoder_StepsAndButtonsHeldWhileItTurns)
{
    const HostDeviceInfo dial{"fake:9", "Dial", "surface-dial", {}};
    _router.OnDeviceAdded(dial);
    _router.SetRole("fake:9", DeviceRole::DebuggerRemote);
    Binding held = To({"push", "knob"}, ActionTarget("ttd.jog_instructions"), kDebugLayer);
    held.chord = true;
    SetBindings({To({"knob"}, ActionTarget("ttd.jog_frames"), kDebugLayer), held,
                 To({"push"}, ActionTarget("run.pause_toggle"), kDebugLayer)});

    Set("fake:9", "knob", 3);
    ASSERT_EQ(_actions.size(), 1u);
    EXPECT_EQ(_actions[0].action, "ttd.jog_frames");
    EXPECT_EQ(_actions[0].value, 3.0f) << "the steps of the event";

    _actions.clear();
    Set("fake:9", "push", 1);
    EXPECT_TRUE(_actions.empty()) << "a press waits: a turn may follow";
    Set("fake:9", "knob", -2);
    Set("fake:9", "push", 0);
    ASSERT_EQ(_actions.size(), 1u) << "the turn took the press";
    EXPECT_EQ(_actions[0].action, "ttd.jog_instructions");
    EXPECT_EQ(_actions[0].value, -2.0f);

    _actions.clear();
    Set("fake:9", "push", 1);
    Set("fake:9", "push", 0);
    ASSERT_EQ(_actions.size(), 2u) << "a press alone acts on its release";
    EXPECT_EQ(_actions[0].action, "run.pause_toggle");
    EXPECT_TRUE(_actions[0].pressed);
    EXPECT_FALSE(_actions[1].pressed);

    std::string error;
    EXPECT_FALSE(_router.SetBindings({}, {To({"knob"}, Retro("kempston:fire"))}, error));
    EXPECT_NE(error.find("drive an action only"), std::string::npos) << error;
}

/// `curve: scrub`: the action gets the trigger's level each time it changes, 0 on release
TEST_F(InputRouter_Test, Level_TheScrubActionGetsTheTriggerLevel)
{
    _router.OnDeviceAdded(kPad);
    _router.SetRole("fake:1", DeviceRole::DebuggerRemote);
    Binding scrub = To({"lt"}, ActionTarget("ttd.scrub_back"), kDebugLayer);
    scrub.options.level = true;
    SetBindings({scrub});

    Set("fake:1", "lt", 0.25f);
    Set("fake:1", "lt", 0.75f);
    Set("fake:1", "lt", 0);
    ASSERT_EQ(_actions.size(), 3u);
    EXPECT_TRUE(_actions[0].pressed);
    EXPECT_EQ(_actions[0].value, 0.25f) << "below the click too";
    EXPECT_EQ(_actions[1].value, 0.75f);
    EXPECT_FALSE(_actions[2].pressed);
    EXPECT_EQ(_actions[2].value, 0.0f);
}

TEST_F(InputRouter_Test, Targets_KeysMacroAndMouse)
{
    _router.OnDeviceAdded(kPad);
    Target macro;
    std::string error;
    ASSERT_TRUE(ParseMacroTarget({"J", "SYM+P"}, macro, error)) << error;
    SetBindings({To({"north"}, Keys("EDIT")), To({"west"}, macro), To({"rstick.x"}, Mouse("dx", 4)),
                 To({"dpad.up"}, Mouse("dy", -3)), To({"south"}, Mouse("left")), To({"rb"}, Mouse("wheel", 1))});

    std::vector<uint8_t> edit;
    ASSERT_TRUE(ZxKeysFromName("caps+1", edit));
    Set("fake:1", "north", 1);
    Sample();
    EXPECT_EQ(_last.keys.size(), 2u);
    EXPECT_TRUE(std::is_permutation(_last.keys.begin(), _last.keys.end(), edit.begin())) << "EDIT is CAPS and 1";
    Set("fake:1", "north", 0);

    // Each step one frame held, one released
    std::vector<uint8_t> j;
    std::vector<uint8_t> quote;
    ASSERT_TRUE(ZxKeysFromName("j", j));
    ASSERT_TRUE(ZxKeysFromName("\"", quote));
    Set("fake:1", "west", 1);
    Set("fake:1", "west", 0);
    std::vector<size_t> sizes;
    for (int f = 0; f < 5; f++)
    {
        Sample();
        sizes.push_back(_last.keys.size());
        if (f == 0)
            EXPECT_EQ(_last.keys, j);
        if (f == 2)
            EXPECT_TRUE(std::is_permutation(_last.keys.begin(), _last.keys.end(), quote.begin()));
    }
    EXPECT_EQ(sizes, (std::vector<size_t>{1, 0, 2, 0, 0}));

    // Full deflection: 4 counts a frame; the D-pad up at -3 (+ down in the profile) is 3 up for the machine
    Set("fake:1", "rstick.x", 1.0f);
    Set("fake:1", "dpad.up", 1);
    Set("fake:1", "south", 1);
    Set("fake:1", "rb", 1);
    Sample();
    EXPECT_EQ(_last.mouse.dx, 4);
    EXPECT_EQ(_last.mouse.dy, 3);
    EXPECT_EQ(_last.mouse.buttons, 0x01);
    EXPECT_EQ(_last.mouse.wheel, 1);
    Sample();
    EXPECT_EQ(_last.mouse.wheel, 0) << "one notch per press";
    Set("fake:1", "rstick.x", 0.1f);
    Sample();
    EXPECT_EQ(_last.mouse.dx, 0) << "inside the dead zone";
}

/// mapping-and-profiles.md §4.2
TEST_F(InputRouter_Test, Consume_KeyboardBindings)
{
    const HostDeviceInfo keyboard{"kbd:system", "Keyboard", "keyboard", {}};
    _router.OnDeviceAdded(keyboard);
    Binding pass = Bind({"key.kp5"}, "kempston:fire2");
    pass.options.consume = false;
    SetBindings({Bind({"key.kp8"}, "kempston:up"), pass}, "kbd:system");
    EXPECT_TRUE(_router.Consumes("kbd:system", "key.kp8"));
    EXPECT_FALSE(_router.Consumes("kbd:system", "key.kp5")) << "pass: the ZX keyboard sees it too";
    EXPECT_FALSE(_router.Consumes("kbd:system", "key.a"));
    _router.SetRole("kbd:system", DeviceRole::Disabled);
    EXPECT_FALSE(_router.Consumes("kbd:system", "key.kp8")) << "a disabled device takes nothing";
}

TEST_F(InputRouter_Test, InvalidTargetsAreRefusedWithTheReason)
{
    std::string error;
    Target target;
    EXPECT_FALSE(ParseKeysTarget("CAPS+HYPER", target, error));
    EXPECT_NE(error.find("edit"), std::string::npos) << error;
    EXPECT_FALSE(ParseMouseTarget("dz", 1, target, error));
    EXPECT_FALSE(ParseLayerTarget("keys", "sticky", target, error));
    EXPECT_FALSE(ParseMacroTarget({}, target, error));

    Binding chord = To({"south", "lstick"}, Retro("kempston:fire"));
    chord.chord = true;
    EXPECT_FALSE(_router.SetBindings({}, {chord}, error));
    EXPECT_NE(error.find("not a button"), std::string::npos) << error;
    EXPECT_FALSE(_router.SetBindings({}, {To({"lstick"}, Mouse("dx"))}, error));
    EXPECT_FALSE(_router.SetBindings({}, {To({"lb"}, Layer("keys"), "keys")}, error)) << "a layer cannot switch itself";
}

/// endregion </C2b>
