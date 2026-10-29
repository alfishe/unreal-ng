/// @file surfaces_test.cpp
/// @brief Control surfaces phase S3 (docs/inprogress/2026-10-18-control-surfaces): the profile parser's context layers
/// (`@ttd`, `@debug.*`), `curve` parameters and per-layer `haptics`; the router's context layers, encoder speed and
/// feel (steps / clicks per context); the family default role (Q-8); the service's surface outputs and marks; the
/// Dial's pattern and feature reports and its writer thread; the `output` and `devices` host verbs. No machine.

#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include "_helpers/testwaithelper.h"
#include "devicedatabase.h"
#include "fake/fakebackend.h"
#include "haptics/surfacehaptics.h"
#include "hid/drivers/surfacedial.h"
#include "hostinputcontrol.h"
#include "hostinputservice.h"
#include "profiles/profile.h"
#include "profiles/profilestore.h"
#include "router/inputrouter.h"

using namespace controllers;

namespace
{
const HostDeviceInfo kDial = {"bt:045e:091b:7", "Surface Dial", "surface-dial", "generic", "fake"};
const HostDeviceInfo kPad = {"usb:045e:0b00:7", "Elite", "xbox-elite-2", "xbox", "fake"};

std::string DebugProfile(const std::string& layers, const std::string& haptics = {})
{
    return "schema: 1\nkind: debug\nid: user/dial\napplies_to: { families: [surface-dial] }\nlayers:\n" + layers +
           (haptics.empty() ? "" : "haptics:\n" + haptics);
}

/// The shipped Dial profile applied to a router that has the Dial, as the Qt client applies it
struct DialRig
{
    ProfileStore store;
    InputRouter router;
    std::vector<ActionEvent> actions;
    std::vector<std::pair<HostDeviceId, StepsOut>> feel;
    uint64_t now = 1'000'000'000;

    DialRig()
    {
        router.SetClock([this] { return now; });
        router.SetActionSink([this](const ActionEvent& event) { actions.push_back(event); });
        router.SetOutputSink([this](const HostDeviceId& id, const StepsOut& steps) { feel.emplace_back(id, steps); });
        router.SetDefaultRole(kDial.id, DeviceRole::DebuggerRemote);
        router.OnDeviceAdded(kDial);
        router.SetFocus("emu-1");
        ProfileContext context;
        context.debugLayer = false;  // the Dial's profile needs no debug layer switch
        context.devices = {kDial};
        const ResolvedProfiles resolved = store.Apply(router, context);
        EXPECT_TRUE(resolved.warnings.empty()) << resolved.warnings.front();
    }
    void Turn(float steps, uint64_t advanceNs = 25'000'000)
    {
        now += advanceNs;
        router.OnControl({kDial.id, "knob", steps, now});
    }
};
}  // namespace

/// region <Parser>

TEST(Surfaces_Test, Parser_ContextLayersInADebugProfile)
{
    Profile profile;
    std::string error;
    ASSERT_TRUE(ParseProfile(DebugProfile("  debug: { push: { action: run.pause_toggle } }\n"
                                          "  \"@ttd\": { knob: { action: ttd.seek_by } }\n"
                                          "  \"@debug.*\": { knob: { action: run.step_by } }\n"),
                             profile, error))
        << error;
    ASSERT_EQ(profile.layers.size(), 3u);
    EXPECT_EQ(profile.layers[1].first, "@ttd");
    EXPECT_EQ(profile.layers[2].first, "@debug.*");

    for (const char* bad : {"\"@\"", "\"@Ttd\"", "\"@ttd*\"", "\"@1ttd\"", "\"@ttd.*.x\"", "shift"})
    {
        EXPECT_FALSE(ParseProfile(DebugProfile(std::string("  ") + bad + ": { knob: { action: ttd.seek_by } }\n"),
                                  profile, error))
            << bad;
    }
    EXPECT_FALSE(ParseProfile("schema: 1\nkind: play\nid: user/p\nlayers:\n  \"@ttd\": { south: { key: ENTER } }\n",
                              profile, error));
    EXPECT_NE(error.find("debug profiles"), std::string::npos) << error;
    // A layer key never raises a context layer: the context does
    EXPECT_FALSE(ParseProfile(DebugProfile("  debug: { push: { layer: \"@ttd\" } }\n"), profile, error));
    EXPECT_NE(error.find("context layer"), std::string::npos) << error;
}

TEST(Surfaces_Test, Parser_CurveObjectIsTheActionsParameters)
{
    Profile profile;
    std::string error;
    ASSERT_TRUE(ParseProfile(DebugProfile("  \"@ttd\":\n    knob: { action: ttd.seek_by, curve: { unit: seconds, min: "
                                          "0.02, max: 2.0, rate_lo: 4, rate_hi: 40 } }\n"),
                             profile, error))
        << error;
    Binding binding;
    ASSERT_TRUE(CompileBinding("knob", profile.layers[0].second[0].second, JsonValue::Object(), "@ttd", binding, error))
        << error;
    ASSERT_TRUE(binding.options.params.IsObject());
    EXPECT_EQ(binding.options.params.Find("unit")->AsString(), "seconds");
    EXPECT_DOUBLE_EQ(binding.options.params.Find("max")->AsNumber(), 2.0);
    EXPECT_FALSE(binding.options.level) << "an object is not the analog scrub";

    for (const char* bad : {"{ unit: minutes }", "{ min: 0 }", "{ min: -1 }", "{ min: 5, max: 1 }",
                            "{ rate_lo: 40, rate_hi: 4 }", "{ speed: 2 }", "{ min: fast }"})
        EXPECT_FALSE(ParseProfile(DebugProfile(std::string("  \"@ttd\": { knob: { action: ttd.seek_by, curve: ") + bad +
                                               " } }\n"),
                                  profile, error))
            << bad;
    // Parameters belong to an action target
    EXPECT_FALSE(ParseProfile("schema: 1\nkind: play\nid: user/p\nlayers:\n  play: { south: { key: ENTER, curve: { "
                              "min: 1 } } }\n",
                              profile, error));
    // `curve: scrub` stays the analog scrub
    ASSERT_TRUE(ParseProfile(DebugProfile("  debug: { lt: { action: ttd.scrub_back, curve: scrub } }\n"), profile,
                             error))
        << error;
}

TEST(Surfaces_Test, Parser_PerLayerHapticsOfADebugProfile)
{
    Profile profile;
    std::string error;
    ASSERT_TRUE(ParseProfile(DebugProfile("  \"@ttd\": { knob: { action: ttd.seek_by } }\n",
                                          "  \"@ttd\": { steps: 72, clicks: false, marks: { second: tick, edge: buzz }, "
                                          "min_interval_ms: 25 }\n  debug: { steps: 12 }\n"),
                             profile, error))
        << error;
    SurfaceHaptics haptics;
    ASSERT_TRUE(ParseSurfaceHaptics(profile.haptics, haptics, error)) << error;
    const LayerHaptics* ttd = haptics.Find("@ttd");
    ASSERT_NE(ttd, nullptr);
    EXPECT_EQ(ttd->steps.perTurn, 72);
    EXPECT_FALSE(ttd->steps.clicks);
    EXPECT_EQ(ttd->minIntervalMs, 25u);
    EXPECT_EQ(ttd->marks.at("second"), HapticPattern::Tick);
    EXPECT_EQ(ttd->marks.at("edge"), HapticPattern::Buzz);
    ASSERT_NE(haptics.Find("debug"), nullptr);
    EXPECT_TRUE(haptics.Find("debug")->steps.clicks) << "clicks default on";
    EXPECT_EQ(haptics.Find("debug")->minIntervalMs, 40u) << "the default interval";

    for (const char* bad : {"  \"@ttd\": { steps: 0 }\n", "  \"@ttd\": { steps: 4000 }\n",
                            "  \"@ttd\": { clicks: maybe }\n", "  \"@ttd\": { marks: { minute: tick } }\n",
                            "  \"@ttd\": { marks: { second: thud } }\n", "  \"@ttd\": { feel: soft }\n",
                            "  play: { steps: 36 }\n", "  \"@TTD\": { steps: 36 }\n"})
        EXPECT_FALSE(ParseProfile(DebugProfile("  debug: { push: { action: run.pause_toggle } }\n", bad), profile,
                                  error))
            << bad;
    // A play profile's haptics stay the game's form (sources and rules)
    EXPECT_FALSE(ParseProfile("schema: 1\nkind: play\nid: user/p\nhaptics: { \"@ttd\": { steps: 36 } }\n", profile,
                              error));
}

TEST(Surfaces_Test, Parser_TheShippedDialProfile)
{
    ProfileStore store;
    const Profile* dial = store.Find("builtin/debug/surface-dial");
    ASSERT_NE(dial, nullptr) << "compiled in";
    std::vector<std::string> layers;
    for (const auto& [layer, bindings] : dial->layers)
        layers.push_back(layer);
    EXPECT_EQ(layers, (std::vector<std::string>{"debug", "@ttd", "@ttd.timecode", "@debug.*"}));
    SurfaceHaptics haptics;
    std::string error;
    ASSERT_TRUE(ParseSurfaceHaptics(dial->haptics, haptics, error)) << error;
    EXPECT_TRUE(haptics.Find("@ttd")->steps.clicks) << "Q-6: clicks at each step";
    EXPECT_FALSE(haptics.Find("@ttd.timecode")->steps.clicks) << "Q-6: a free wheel with the mark clicks";
    EXPECT_EQ(haptics.Find("@ttd")->marks.at("ten_seconds"), HapticPattern::Double);
    // It round-trips through the user file form
    Profile again;
    ASSERT_TRUE(ParseProfile(ProfileStore::Dump(*dial), again, error)) << error;
    EXPECT_EQ(again.layers.size(), dial->layers.size());
}

/// endregion </Parser>

/// region <Router>

TEST(Surfaces_Test, ContextLayerMatch_ExactWildcardLongest)
{
    EXPECT_EQ(ContextLayerMatch("@ttd", "ttd"), 4);
    EXPECT_EQ(ContextLayerMatch("@ttd", "ttd.timecode"), -1) << "exact";
    EXPECT_EQ(ContextLayerMatch("@ttd.*", "ttd.timecode"), 6);
    EXPECT_EQ(ContextLayerMatch("@ttd.timecode", "ttd.timecode"), 13) << "longer than @ttd.*: it wins";
    EXPECT_EQ(ContextLayerMatch("@debug.*", "debug"), -1) << "a prefix with its dot";
    EXPECT_EQ(ContextLayerMatch("@debug.*", "debug.disasm"), 8);
    EXPECT_EQ(ContextLayerMatch("debug", "debug"), -1) << "not a context layer";
    EXPECT_EQ(ContextLayerMatch("@ttd", ""), -1);
}

/// The shipped profile: the knob follows the context, the push pauses in every context, play binds nothing
TEST(Surfaces_Test, Router_TheDialFollowsTheContext)
{
    DialRig rig;
    rig.Turn(1);
    EXPECT_TRUE(rig.actions.empty()) << "no context yet: the knob is unbound";
    EXPECT_EQ(rig.router.Layers(kDial.id), (std::vector<std::string>{"debug"}));

    rig.router.SetContext("emu-1", "ttd");
    EXPECT_EQ(rig.router.Layers(kDial.id), (std::vector<std::string>{"debug", "@ttd"}));
    EXPECT_EQ(rig.router.Context(kDial.id), "ttd");
    rig.Turn(2);
    ASSERT_EQ(rig.actions.size(), 1u);
    EXPECT_EQ(rig.actions[0].action, "ttd.seek_by");
    EXPECT_TRUE(rig.actions[0].turn);
    EXPECT_EQ(rig.actions[0].value, 2.0f);
    EXPECT_GT(rig.actions[0].rate, 0.0);
    ASSERT_TRUE(rig.actions[0].params.IsObject());
    EXPECT_EQ(rig.actions[0].params.Find("unit")->AsString(), "frames");

    rig.actions.clear();
    rig.router.SetContext("emu-1", "ttd.timecode");
    rig.Turn(-1);
    ASSERT_EQ(rig.actions.size(), 1u);
    EXPECT_EQ(rig.actions[0].action, "ttd.seek_by");
    EXPECT_EQ(rig.actions[0].params.Find("unit")->AsString(), "seconds");

    rig.actions.clear();
    rig.router.SetContext("emu-1", "debug.disasm");
    EXPECT_EQ(rig.router.Layers(kDial.id), (std::vector<std::string>{"debug", "@debug.*"}));
    rig.Turn(3);
    ASSERT_EQ(rig.actions.size(), 1u);
    EXPECT_EQ(rig.actions[0].action, "run.step_by");
    EXPECT_TRUE(rig.actions[0].params.IsNull());

    // The push: from `debug` here, from `@ttd` (with the push+knob chord) there; on its release either way
    for (const char* context : {"debug.disasm", "ttd", "play"})
    {
        rig.router.SetContext("emu-1", context);
        rig.actions.clear();
        rig.now += 1'000'000;
        rig.router.OnControl({kDial.id, "push", 1, rig.now});
        rig.now += 1'000'000;
        rig.router.OnControl({kDial.id, "push", 0, rig.now});
        ASSERT_FALSE(rig.actions.empty()) << context;
        EXPECT_EQ(rig.actions[0].action, "run.pause_toggle") << context;
    }
    // Held down while turning in `ttd`: instructions, and the push does not pause
    rig.router.SetContext("emu-1", "ttd");
    rig.actions.clear();
    rig.router.OnControl({kDial.id, "push", 1, rig.now += 1'000'000});
    rig.Turn(1);
    rig.router.OnControl({kDial.id, "push", 0, rig.now += 1'000'000});
    ASSERT_EQ(rig.actions.size(), 1u);
    EXPECT_EQ(rig.actions[0].action, "ttd.jog_instructions");
}

/// Another instance in focus has its own context; a pinned device keeps its instance's
TEST(Surfaces_Test, Router_ContextIsPerInstance)
{
    DialRig rig;
    rig.router.SetContext("emu-1", "ttd");
    rig.router.SetContext("emu-2", "debug.memory");
    rig.router.SetFocus("emu-2");
    EXPECT_EQ(rig.router.Context(kDial.id), "debug.memory");
    EXPECT_EQ(rig.router.Layers(kDial.id), (std::vector<std::string>{"debug", "@debug.*"}));
    rig.router.SetInstanceMode(kDial.id, InstanceMode::Pinned, "emu-1");
    EXPECT_EQ(rig.router.Context(kDial.id), "ttd");
}

/// The speed: the rest rule, the smoothing, the BLE bunch floor
TEST(Surfaces_Test, EncoderRate_RestSmoothingAndBunches)
{
    EncoderRate rate;
    EXPECT_NEAR(rate.Step(1, 1'000'000'000), 0.3 * 1e9 / (EncoderRate::kRestNs + 1), 1e-6) << "the first step rests";
    // Steps every 25 ms (40 steps/s) converge on 40
    uint64_t now = 1'000'000'000;
    for (int i = 0; i < 20; i++)
        rate.Step(1, now += 25'000'000);
    EXPECT_NEAR(rate.Rate(), 40.0, 0.5);
    // Two reports of one BLE bunch, 1 ms apart, count as 7.5 ms
    const double before = rate.Rate();
    rate.Step(1, now += 1'000'000);
    EXPECT_NEAR(rate.Rate(), 0.5 * before + 0.5 * (1e9 / EncoderRate::kMinNs), 1e-6);
    // After a rest the speed starts low again
    rate.Step(1, now += 500'000'000);
    EXPECT_LT(rate.Rate(), 1.0);
    // Several steps in one report count each
    EncoderRate many;
    const double first = many.Step(1, 0);
    EXPECT_NEAR(many.Step(-4, 100'000'000), 0.5 * first + 0.5 * 40.0, 1e-6) << "4 steps in 100 ms: 40 steps/s";
}

/// The feel: steps and clicks per context layer, clicks off in play, sent once per change
TEST(Surfaces_Test, Router_FeelFollowsTheContextLayer)
{
    DialRig rig;
    ASSERT_FALSE(rig.feel.empty()) << "the profile's haptics apply: play has no feel, clicks off";
    EXPECT_EQ(rig.feel.back().first, kDial.id);
    EXPECT_FALSE(rig.feel.back().second.clicks);
    EXPECT_EQ(rig.feel.back().second.perTurn, 36);
    LayerHaptics feel;
    EXPECT_FALSE(rig.router.Feel(kDial.id, feel));

    rig.feel.clear();
    rig.router.SetContext("emu-1", "ttd");
    ASSERT_EQ(rig.feel.size(), 1u);
    EXPECT_TRUE(rig.feel[0].second.clicks);
    ASSERT_TRUE(rig.router.Feel(kDial.id, feel));
    EXPECT_EQ(feel.marks.at("second"), HapticPattern::Tick);

    rig.feel.clear();
    rig.router.SetContext("emu-1", "ttd.timecode");
    ASSERT_EQ(rig.feel.size(), 1u);
    EXPECT_FALSE(rig.feel[0].second.clicks) << "the free wheel";
    rig.router.SetContext("emu-1", "ttd.timecode");
    rig.Turn(1);
    EXPECT_EQ(rig.feel.size(), 1u) << "no change, nothing sent";

    rig.feel.clear();
    rig.router.SetContext("emu-1", "debug.stack");
    ASSERT_EQ(rig.feel.size(), 1u);
    EXPECT_TRUE(rig.feel[0].second.clicks);
    rig.feel.clear();
    rig.router.SetContext("emu-1", "play");
    ASSERT_EQ(rig.feel.size(), 1u);
    EXPECT_FALSE(rig.feel[0].second.clicks) << "play: the knob is unbound and does not click";

    // A device whose profiles have no surface haptics is never asked
    rig.feel.clear();
    rig.router.OnDeviceAdded(kPad);
    rig.router.SetContext("emu-1", "ttd");
    for (const auto& [device, steps] : rig.feel)
        EXPECT_NE(device, kPad.id);
}

/// endregion </Router>

/// region <Roles (Q-8)>

TEST(Surfaces_Test, DefaultRole_TheDialIsADebuggerRemoteUntilSomebodyChooses)
{
    EXPECT_EQ(DeviceDatabase::Builtin().ForFamily("surface-dial").role, DeviceRole::DebuggerRemote);
    EXPECT_EQ(DeviceDatabase::Builtin().ForFamily("xbox-elite-2").role, DeviceRole::Player) << "pads unchanged";

    HostInputService service;
    auto fake = std::make_unique<FakeBackend>();
    FakeBackend* devices = fake.get();
    std::string reason;
    ASSERT_TRUE(service.AddBackend(std::move(fake), reason)) << reason;
    ASSERT_TRUE(devices->Connect(kDial));
    ASSERT_TRUE(devices->Connect(kPad));
    EXPECT_EQ(service.Router().Role(kDial.id), DeviceRole::DebuggerRemote);
    EXPECT_FALSE(service.Router().RoleSet(kDial.id));
    EXPECT_EQ(service.Router().Role(kPad.id), DeviceRole::Player);
    EXPECT_FALSE(service.Router().Drives(kDial.id, "emu-1")) << "a remote drives no connector";

    // A user's choice wins, also after a reconnect
    service.Router().SetRole(kDial.id, DeviceRole::Player);
    ASSERT_TRUE(devices->Disconnect(kDial.id));
    ASSERT_TRUE(devices->Connect(kDial));
    EXPECT_EQ(service.Router().Role(kDial.id), DeviceRole::Player);
    EXPECT_TRUE(service.Router().RoleSet(kDial.id));
    service.Stop();
}

/// endregion </Roles>

/// region <Service outputs and marks>

namespace
{
/// A service with a fake Dial that has both outputs and the shipped profile applied
struct ServiceRig
{
    HostInputService service;
    FakeBackend* devices = nullptr;
    ProfileStore store;

    ServiceRig()
    {
        auto fake = std::make_unique<FakeBackend>();
        devices = fake.get();
        devices->SetOutputs(kDial.id, kOutputHaptic | kOutputSteps);
        std::string reason;
        EXPECT_TRUE(service.AddBackend(std::move(fake), reason)) << reason;
        EXPECT_TRUE(devices->Connect(kDial));
        service.Router().SetFocus("emu-1");
        ProfileContext context;
        context.devices = service.Router().Devices();
        store.Apply(service.Router(), context);
    }
    ~ServiceRig()
    {
        service.Stop();
    }
};
}  // namespace

TEST(Surfaces_Test, Service_StepsReachTheDeviceOnAContextChange)
{
    ServiceRig rig;
    EXPECT_EQ(rig.service.Outputs(kDial.id), kOutputHaptic | kOutputSteps);
    rig.service.Router().SetContext("emu-1", "ttd");
    rig.service.Router().SetContext("emu-1", "ttd.timecode");
    const std::vector<StepsOut> steps = rig.devices->Steps(kDial.id);
    ASSERT_GE(steps.size(), 2u);
    EXPECT_TRUE(steps[steps.size() - 2].clicks);
    EXPECT_FALSE(steps.back().clicks);
    EXPECT_EQ(steps.back().perTurn, 36);
}

TEST(Surfaces_Test, Service_MarksPlayTheLayersPatternWithTheRateLimit)
{
    ServiceRig rig;
    HostInputService::MarksPlayed played = rig.service.PlayMarks(kDial.id, {"second"}, 1000);
    EXPECT_FALSE(played.played) << "no context layer: no feel";
    EXPECT_FALSE(played.reason.empty());

    rig.service.Router().SetContext("emu-1", "ttd");
    played = rig.service.PlayMarks(kDial.id, {"ten_seconds", "second"}, 1000);
    ASSERT_TRUE(played.played) << played.reason;
    EXPECT_EQ(played.mark, "ten_seconds") << "the strongest first";
    EXPECT_EQ(played.pattern, HapticPattern::Double);
    played = rig.service.PlayMarks(kDial.id, {"second"}, 1020);
    EXPECT_FALSE(played.played) << "20 ms later: merged into the last one";
    played = rig.service.PlayMarks(kDial.id, {"edge"}, 1030);
    EXPECT_TRUE(played.played) << "an edge is never dropped";
    EXPECT_EQ(played.pattern, HapticPattern::Buzz);
    played = rig.service.PlayMarks(kDial.id, {"second"}, 1100);
    EXPECT_TRUE(played.played);
    EXPECT_EQ(rig.devices->Patterns(kDial.id),
              (std::vector<HapticPattern>{HapticPattern::Double, HapticPattern::Buzz, HapticPattern::Tick}));

    rig.service.Router().SetContext("emu-1", "debug.disasm");
    played = rig.service.PlayMarks(kDial.id, {"second"}, 5000);
    EXPECT_FALSE(played.played) << "@debug.* names no marks";
    EXPECT_FALSE(rig.service.PlayMarks(kPad.id, {"edge"}, 6000).played) << "not connected / no outputs";
}

/// endregion </Service outputs and marks>

/// region <The Dial's reports and writer>

TEST(Surfaces_Test, Dial_PatternAndFeatureReports)
{
    EXPECT_EQ(SurfaceDial::PatternReport(HapticPattern::Tick), (std::vector<uint8_t>{0x01, 1, 3, 0x50, 0x00}));
    EXPECT_EQ(SurfaceDial::PatternReport(HapticPattern::Double), (std::vector<uint8_t>{0x01, 2, 3, 0x3C, 0x00}));
    EXPECT_EQ(SurfaceDial::PatternReport(HapticPattern::Buzz), (std::vector<uint8_t>{0x01, 1, 4, 0x50, 0x00}));
    // devices.md §1.2: clicks on = Auto Trigger 3, off = 1 (the steps kept)
    EXPECT_EQ(SurfaceDial::FeatureReport({36, true}), (std::vector<uint8_t>{0x01, 0x24, 0x00, 0, 3, 5, 0x50, 0x00}));
    EXPECT_EQ(SurfaceDial::FeatureReport({36, false}), (std::vector<uint8_t>{0x01, 0x24, 0x00, 0, 1, 5, 0x50, 0x00}));
    EXPECT_EQ(SurfaceDial::FeatureReport({360, true}), (std::vector<uint8_t>{0x01, 0x68, 0x01, 0, 3, 5, 0x50, 0x00}));
    SurfaceDialFeature found;
    found.cutoff = 7;
    found.retriggerMs = 120;
    EXPECT_EQ(SurfaceDial::FeatureReport({12, true}, found),
              (std::vector<uint8_t>{0x01, 12, 0, 0, 3, 7, 120, 0})) << "the device's own cutoff and retrigger";
    SurfaceDialFeature parsed;
    const std::vector<uint8_t> report = SurfaceDial::FeatureReport({72, false});
    ASSERT_TRUE(SurfaceDial::ParseFeature(report.data(), report.size(), parsed));
    EXPECT_EQ(parsed.stepsPerTurn, 72);
    EXPECT_EQ(parsed.autoTrigger, 1);
}

/// One play at a time: the Dial queues every output report it gets (ten buzzes in 300 ms buzzed ten times as long,
/// 2026-10-28), so the next waits until the last has played; requests meanwhile keep the strongest, and after such
/// an overflow a stop goes first. A buzz is ended by a stop after its length, or by the stop before the next play
TEST(Surfaces_Test, DialPlays_OneAtATimeTheStrongestWaits)
{
    SurfaceDialPlays plays({80, 250});
    const std::vector<uint8_t> tick = SurfaceDial::PatternReport(HapticPattern::Tick);
    const std::vector<uint8_t> twice = SurfaceDial::PatternReport(HapticPattern::Double);
    const std::vector<uint8_t> buzz = SurfaceDial::PatternReport(HapticPattern::Buzz);
    EXPECT_EQ(plays.PlayMs(tick), 80u);
    EXPECT_EQ(plays.PlayMs(twice), 160u) << "two clicks, each at least the click's length";
    EXPECT_EQ(plays.PlayMs(buzz), 250u);

    plays.Offer(tick);
    EXPECT_EQ(plays.Take(1000), (std::vector<std::vector<uint8_t>>{tick})) << "nothing plays: at once";
    plays.Offer(tick);
    EXPECT_TRUE(plays.Take(1010).empty()) << "the tick still plays";
    EXPECT_EQ(plays.DueMs(), 1080u);
    EXPECT_EQ(plays.Take(1080), (std::vector<std::vector<uint8_t>>{tick})) << "one waiting: no overflow, no stop";
    EXPECT_EQ(plays.Dropped(), 0u);

    // A fast turn at the end of the history: a buzz each step, a second tick among them
    plays.Offer(buzz);
    plays.Offer(tick);
    plays.Offer(buzz);
    plays.Offer(twice);
    EXPECT_EQ(plays.Dropped(), 3u);
    EXPECT_TRUE(plays.Take(1150).empty());
    EXPECT_EQ(plays.Take(1160), (std::vector<std::vector<uint8_t>>{SurfaceDial::StopReport(), buzz}))
        << "the strongest waited; a stop ends what the Dial may still hold";
    ASSERT_TRUE(plays.Waiting()) << "the stop that ends the buzz";
    EXPECT_EQ(plays.DueMs(), 1410u);
    EXPECT_TRUE(plays.Take(1409).empty());
    EXPECT_EQ(plays.Take(1410), (std::vector<std::vector<uint8_t>>{SurfaceDial::StopReport()}))
        << "the buzz lasts its length, not the device's cutoff";
    EXPECT_FALSE(plays.Waiting());
    plays.Offer(tick);
    EXPECT_EQ(plays.Take(1500), (std::vector<std::vector<uint8_t>>{tick})) << "the overflow is over: no stop";

    // A buzz at each step at the end of the history, slower than the buzz: each ends the last one
    plays.Offer(buzz);
    EXPECT_EQ(plays.Take(1600), (std::vector<std::vector<uint8_t>>{buzz}));
    plays.Offer(buzz);
    EXPECT_EQ(plays.Take(1850), (std::vector<std::vector<uint8_t>>{SurfaceDial::StopReport(), buzz}))
        << "never two buzzes in the Dial";
    EXPECT_EQ(plays.Take(2100), (std::vector<std::vector<uint8_t>>{SurfaceDial::StopReport()}));
    EXPECT_FALSE(plays.Waiting());
    EXPECT_EQ(SurfaceDial::StopReport(), (std::vector<uint8_t>{0x01, 1, 2, 0, 0})) << "Manual Trigger 2: stop";
}

/// The writer: requests while a write is in flight merge (the strongest pattern waits, a stop before it), the steps go
/// first, a buzz is stopped after its length
TEST(Surfaces_Test, DialWriter_StrongestWaitsStepsFirst)
{
    std::mutex mutex;
    std::condition_variable gate;
    bool open = false;
    std::vector<std::pair<SurfaceDialWriter::ReportType, std::vector<uint8_t>>> written;
    std::atomic<int> started{0};
    SurfaceDialWriter writer(
        [&](SurfaceDialWriter::ReportType type, const std::vector<uint8_t>& report) {
            std::unique_lock<std::mutex> lock(mutex);
            ++started;
            gate.wait(lock, [&] { return open; });  // the first write blocks as a BLE round trip would
            written.emplace_back(type, report);
            return true;
        },
        {}, {1, 1});
    writer.Pattern(HapticPattern::Tick);
    ASSERT_TRUE(TestWait::For([&] { return started.load() == 1; }));
    writer.Pattern(HapticPattern::Buzz);
    writer.Pattern(HapticPattern::Double);  // weaker than the buzz waiting: dropped
    writer.Steps({36, false});
    {
        std::lock_guard<std::mutex> lock(mutex);
        open = true;
    }
    gate.notify_all();
    ASSERT_TRUE(TestWait::For([&] { return writer.Written() == 5; }));
    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(written.size(), 5u);
    EXPECT_EQ(written[0].second, SurfaceDial::PatternReport(HapticPattern::Tick));
    EXPECT_EQ(written[1].first, SurfaceDialWriter::ReportType::Feature) << "the steps before the pattern";
    EXPECT_EQ(written[1].second, SurfaceDial::FeatureReport({36, false}));
    EXPECT_EQ(written[2].second, SurfaceDial::StopReport()) << "after an overflow";
    EXPECT_EQ(written[3].second, SurfaceDial::PatternReport(HapticPattern::Buzz)) << "the strongest";
    EXPECT_EQ(written[4].second, SurfaceDial::StopReport()) << "the buzz ends after its length";
    EXPECT_EQ(writer.Dropped(), 1u);
    EXPECT_EQ(writer.Failed(), 0u);
}

namespace
{
/// A Dial's feature report 1 as the device holds it: a write applies what is inside the descriptor's logical ranges
/// (POC 030: a cutoff or retrigger of 0 is dropped), a read returns it
struct FakeDialFeature
{
    std::mutex mutex;
    std::vector<uint8_t> feature;
    std::vector<std::vector<uint8_t>> writes;
    bool ignoreWrites = false;

    SurfaceDialWriter::Write Writer()
    {
        return [this](SurfaceDialWriter::ReportType type, const std::vector<uint8_t>& report) {
            std::lock_guard<std::mutex> lock(mutex);
            if (type != SurfaceDialWriter::ReportType::Feature)
                return true;
            writes.push_back(report);
            if (!ignoreWrites)
                feature = report;
            return true;
        };
    }
    SurfaceDialWriter::Read Reader()
    {
        return [this](std::vector<uint8_t>& report) {
            std::lock_guard<std::mutex> lock(mutex);
            report = feature;
            return true;
        };
    }
};
}  // namespace

/// With a Read: the found settings are read first, the steps keep their cutoff and retrigger, the write is read back,
/// and the found settings are written back when the writer ends
TEST(Surfaces_Test, DialWriter_ReadsTheFoundSettingsVerifiesAndRestores)
{
    FakeDialFeature dial;
    const std::vector<uint8_t> found = {0x01, 0x24, 0x00, 0x00, 0x01, 0x07, 0x64, 0x00};  // cutoff 7, retrigger 100
    dial.feature = found;
    {
        SurfaceDialWriter writer(dial.Writer(), dial.Reader());
        writer.Steps({72, true});
        ASSERT_TRUE(TestWait::For([&] { return writer.Written() == 1; }));
        ASSERT_TRUE(writer.Found().has_value());
        EXPECT_EQ(writer.Found()->stepsPerTurn, 36);
        EXPECT_EQ(writer.Found()->cutoff, 7);
        std::lock_guard<std::mutex> lock(dial.mutex);
        EXPECT_EQ(dial.feature, (std::vector<uint8_t>{0x01, 0x48, 0x00, 0x00, 0x03, 0x07, 0x64, 0x00}))
            << "72 steps, a click each, the device's own cutoff and retrigger";
        EXPECT_EQ(writer.Failed(), 0u);
    }
    std::lock_guard<std::mutex> lock(dial.mutex);
    EXPECT_EQ(dial.feature, found) << "restored as found when the writer ends";
    EXPECT_EQ(dial.writes.size(), 2u);
}

TEST(Surfaces_Test, DialWriter_AWriteTheDeviceDidNotApplyFails)
{
    FakeDialFeature dial;
    dial.feature = SurfaceDial::FeatureBytes({});
    dial.ignoreWrites = true;
    {
        SurfaceDialWriter writer(dial.Writer(), dial.Reader());
        writer.Steps({12, true});
        ASSERT_TRUE(TestWait::For([&] { return writer.Failed() == 1; }));
        EXPECT_EQ(writer.Written(), 0u);
    }
    // Nothing stuck at the end: the restore wrote the found bytes (the device ignores it, they are the same)
    std::lock_guard<std::mutex> lock(dial.mutex);
    ASSERT_EQ(dial.writes.size(), 2u);
    EXPECT_EQ(dial.writes.back(), SurfaceDial::FeatureBytes({}));
}

TEST(Surfaces_Test, DialWriter_NothingRestoredWithoutAStepsWriteOrARead)
{
    FakeDialFeature dial;
    dial.feature = SurfaceDial::FeatureBytes({});
    {
        SurfaceDialWriter writer(dial.Writer(), dial.Reader());
        writer.Pattern(HapticPattern::Tick);
        ASSERT_TRUE(TestWait::For([&] { return writer.Written() == 1; }));
    }
    {
        SurfaceDialWriter writer(dial.Writer());  // no Read: no found settings to restore
        writer.Steps({36, true});
        ASSERT_TRUE(TestWait::For([&] { return writer.Written() == 1; }));
        EXPECT_FALSE(writer.Found().has_value());
    }
    std::lock_guard<std::mutex> lock(dial.mutex);
    EXPECT_EQ(dial.writes.size(), 1u) << "only the steps of the writer without a Read";
}

/// endregion </The Dial's reports and writer>

/// region <Host verbs>

namespace
{
class ServiceOwner : public IHostInputOwner
{
public:
    explicit ServiceOwner(HostInputService& service) : _service(service)
    {
        _context.devices = service.Router().Devices();
    }
    ProfileStore& Profiles() override
    {
        return _store;
    }
    ProfileContext Context() const override
    {
        return _context;
    }
    const ResolvedProfiles& Resolved() const override
    {
        return _resolved;
    }
    std::string UserProfileDir() const override
    {
        return {};
    }
    std::string ManualProfile() const override
    {
        return {};
    }
    void SetManualProfile(const std::string&) override
    {
    }
    void Reapply() override
    {
    }
    HostInputService* Input() override
    {
        return &_service;
    }

private:
    HostInputService& _service;
    ProfileStore _store;
    ProfileContext _context;
    ResolvedProfiles _resolved;
};

const JsonValue* DeviceItem(const HostReply& reply, const std::string& id)
{
    for (const JsonValue& item : reply.body.Find("devices")->Items())
        if (item.Find("id")->AsString() == id)
            return &item;
    return nullptr;
}
}  // namespace

TEST(Surfaces_Test, HostVerbs_DevicesShowTheRoleLayersContextAndFeel)
{
    ServiceRig rig;
    rig.service.Router().SetContext("emu-1", "ttd");
    ServiceOwner owner(rig.service);
    const HostReply reply = HostInputControl(owner).Execute({"devices", {}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    const JsonValue* dial = DeviceItem(reply, kDial.id);
    ASSERT_NE(dial, nullptr);
    EXPECT_EQ(dial->Find("role")->AsString(), "debugger-remote");
    EXPECT_FALSE(dial->Find("role_set")->AsBool());
    ASSERT_EQ(dial->Find("layers")->Items().size(), 2u);
    EXPECT_EQ(dial->Find("layers")->Items()[1].AsString(), "@ttd");
    EXPECT_EQ(dial->Find("context")->AsString(), "ttd");
    ASSERT_EQ(dial->Find("outputs")->Items().size(), 2u);
    const JsonValue* feel = dial->Find("feel");
    ASSERT_NE(feel, nullptr);
    EXPECT_TRUE(feel->Find("clicks")->AsBool());
    EXPECT_EQ(feel->Find("marks")->Find("edge")->AsString(), "buzz");
}

TEST(Surfaces_Test, HostVerbs_OutputPatternStepsAndMarks)
{
    ServiceRig rig;
    rig.service.Router().SetContext("emu-1", "ttd");
    ServiceOwner owner(rig.service);
    HostInputControl control(owner);

    HostReply reply = control.Execute({"output", {{"device", kDial.id}, {"pattern", "buzz"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.Find("pattern")->AsString(), "buzz");
    EXPECT_EQ(rig.devices->Patterns(kDial.id).back(), HapticPattern::Buzz);

    reply = control.Execute({"output", {{"device", kDial.id}, {"steps", "72.0"}, {"clicks", "off"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(rig.devices->Steps(kDial.id).back(), (StepsOut{72, false}));

    reply = control.Execute({"output", {{"device", kDial.id}, {"marks", "second,edge"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_TRUE(reply.body.Find("played")->AsBool());
    EXPECT_EQ(reply.body.Find("mark")->AsString(), "second") << "the first named that the layer plays";

    EXPECT_EQ(control.Execute({"output", {{"device", kDial.id}}}).error, "bad-request") << "one form";
    EXPECT_EQ(control.Execute({"output", {{"device", kDial.id}, {"pattern", "thud"}}}).error, "bad-request");
    EXPECT_EQ(control.Execute({"output", {{"device", kDial.id}, {"steps", "0"}}}).error, "bad-request");
    EXPECT_EQ(control.Execute({"output", {{"device", kDial.id}, {"marks", "minute"}}}).error, "bad-request");
    EXPECT_EQ(control.Execute({"output", {{"device", "fake:none"}, {"pattern", "tick"}}}).error, "unknown-device");
    EXPECT_EQ(control.Execute({"output", {{"pattern", "tick"}}}).error, "bad-request");
}

/// endregion </Host verbs>
