/// @file surfacedial_test.cpp
/// @brief The Surface Dial (045E:091B) as a host device: its report descriptor, input report 1 through the shared HID
/// driver (knob steps, push and touch edges, the other report ids ignored), feature report 1, the vocabulary's
/// `knob` / `push` / `touch`, the device database family, and the device through the service and the devices verb
/// with the fake backend (testdata/controllers/hid-captures/surface-dial/macos-ble/).

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include "_helpers/testpathhelper.h"
#include "devicedatabase.h"
#include "fake/fakebackend.h"
#include "haptics/testeffects.h"
#include "hid/drivers/surfacedial.h"
#include "hid/hiddescriptor.h"
#include "hostcontrols.h"
#include "hostinputcontrol.h"
#include "hostinputservice.h"
#include "router/binding.h"

using namespace controllers;

namespace
{
constexpr const char* kCapture =
    "controllers/hid-captures/surface-dial/macos-ble/2026-10-18-descriptor-and-reports.txt";

struct Capture
{
    std::vector<uint8_t> descriptor;
    std::vector<std::vector<uint8_t>> reports;
};

std::vector<uint8_t> Bytes(const std::string& hex)
{
    std::vector<uint8_t> out;
    std::istringstream in(hex);
    std::string byte;
    while (in >> byte)
        out.push_back(static_cast<uint8_t>(std::stoul(byte, nullptr, 16)));
    return out;
}

/// hiddump's log: `descriptor dev0 len=N : <bytes>`, `<ms> dev0 id=XX len=N : <bytes>`
Capture Load()
{
    Capture capture;
    std::ifstream in(TestPathHelper::GetTestDataPath(kCapture));
    std::string line;
    while (std::getline(in, line))
    {
        const size_t colon = line.find(" : ");
        if (colon == std::string::npos)
            continue;
        if (line.rfind("descriptor", 0) == 0)
            capture.descriptor = Bytes(line.substr(colon + 3));
        else if (line.find(" id=") != std::string::npos)
            capture.reports.push_back(Bytes(line.substr(colon + 3)));
    }
    return capture;
}

using Events = std::vector<std::pair<std::string, float>>;

ControlSink Into(Events& events)
{
    return [&events](const std::string& control, float value) { events.emplace_back(control, value); };
}

Events Decode(SurfaceDial& dial, const std::string& hex)
{
    Events events;
    const std::vector<uint8_t> report = Bytes(hex);
    dial.Decode(report.data(), report.size(), Into(events));
    return events;
}

Events Decode(SurfaceDial& dial, const std::string& hex, uint64_t nowMs)
{
    Events events;
    const std::vector<uint8_t> report = Bytes(hex);
    dial.Decode(report.data(), report.size(), Into(events), nowMs);
    return events;
}

Events Expire(SurfaceDial& dial, uint64_t nowMs)
{
    Events events;
    dial.Expire(nowMs, Into(events));
    return events;
}

const HidField* FieldOf(const HidDescriptor& descriptor, uint16_t page, uint16_t usage)
{
    for (const HidField& field : descriptor.fields)
        if (field.usagePage == page && field.usage == usage)
            return &field;
    return nullptr;
}

/// The Qt client's HostControllers for the devices verb: only the context's devices matter
class DevicesOwner : public IHostInputOwner
{
public:
    explicit DevicesOwner(std::vector<HostDeviceInfo> devices)
    {
        _context.devices = std::move(devices);
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

private:
    ProfileStore _store;
    ProfileContext _context;
    ResolvedProfiles _resolved;
};

const HostDeviceInfo kDial = {"bt:045e:091b:1", "Surface Dial", "surface-dial", "generic", "fake"};
}  // namespace

TEST(SurfaceDial_Test, Descriptor_TheDialPatternAndItsFields)
{
    const Capture capture = Load();
    ASSERT_EQ(capture.descriptor.size(), 488u);
    HidDescriptor descriptor;
    std::string error;
    ASSERT_TRUE(ParseHidDescriptor(capture.descriptor.data(), capture.descriptor.size(), descriptor, error)) << error;
    EXPECT_TRUE(descriptor.hasReportIds);
    EXPECT_TRUE(SurfaceDial::Matches(descriptor)) << "a Dial usage in a System Multi-Axis Controller";
    EXPECT_TRUE(SurfaceDial::Handles(0x045E, 0x091B));
    EXPECT_FALSE(SurfaceDial::Handles(0x045E, 0x0B22)) << "an Xbox pad";

    const HidField* dial = FieldOf(descriptor, 0x01, 0x37);
    ASSERT_NE(dial, nullptr);
    EXPECT_EQ(dial->reportId, 1);
    EXPECT_EQ(dial->bitOffset, 8u) << "bytes 2-3 of the report";
    EXPECT_EQ(dial->bitSize, 16);
    EXPECT_EQ(dial->logicalMin, -32767);
    EXPECT_EQ(dial->application, 0x0E);
    const HidField* push = FieldOf(descriptor, 0x09, 0x01);
    ASSERT_NE(push, nullptr);
    EXPECT_EQ(push->bitOffset, 0u);
    const HidField* touch = FieldOf(descriptor, 0x0D, 0x33);
    ASSERT_NE(touch, nullptr);
    EXPECT_EQ(touch->bitOffset, 1u);

    // A pad's descriptor is not of the pattern
    HidDescriptor pad;
    pad.fields.push_back(*dial);
    pad.fields.back().application = 0x05;
    EXPECT_FALSE(SurfaceDial::Matches(pad));
}

/// The measured reports: hand on, a step right, a step left, pressed while touching, released
TEST(SurfaceDial_Test, Decode_TurnsPressesAndTouch)
{
    SurfaceDial dial;
    EXPECT_EQ(Decode(dial, "01 02 00 00 0a 0b 0c 0d 3a"), (Events{{"touch", 1.0f}}));
    EXPECT_EQ(Decode(dial, "01 02 00 00 0a 0b 0c 0d 3a"), Events{}) << "nothing changed";
    EXPECT_EQ(Decode(dial, "01 02 01 00 0a 0b 0c 0d 3a"), (Events{{"knob", 1.0f}}));
    EXPECT_EQ(Decode(dial, "01 02 ff ff 0a 0b 0c 0d 3a"), (Events{{"knob", -1.0f}}));
    EXPECT_EQ(Decode(dial, "01 03 00 00 0a 0b 0c 0d 3a"), (Events{{"push", 1.0f}}));
    EXPECT_EQ(Decode(dial, "01 03 00 00 0a 0b 0c 0d 3a"), Events{}) << "held";
    EXPECT_EQ(Decode(dial, "01 03 01 00 0a 0b 0c 0d 3a"), (Events{{"knob", 1.0f}})) << "a turn while pressed";
    EXPECT_EQ(Decode(dial, "01 00 00 00 0a 0b 0c 0d 3a"), (Events{{"push", 0.0f}, {"touch", 0.0f}}))
        << "the hand leaves after the release";

    SurfaceDial fresh;
    EXPECT_EQ(Decode(fresh, "01 03 ff ff 0a 0b 0c 0d 3a"), (Events{{"touch", 1.0f}, {"push", 1.0f}, {"knob", -1.0f}}))
        << "the first report gives what is on; the hand arrives first";
}

TEST(SurfaceDial_Test, Decode_LargeAndNegativeSteps)
{
    SurfaceDial dial;
    EXPECT_EQ(Decode(dial, "01 02 05 00 0a 0b 0c 0d 3a"), (Events{{"touch", 1.0f}, {"knob", 5.0f}}));
    EXPECT_EQ(Decode(dial, "01 02 fb ff 0a 0b 0c 0d 3a"), (Events{{"knob", -5.0f}}));
    EXPECT_EQ(Decode(dial, "01 02 ff 7f 0a 0b 0c 0d 3a"), (Events{{"knob", 32767.0f}}));
    EXPECT_EQ(Decode(dial, "01 02 01 80 0a 0b 0c 0d 3a"), (Events{{"knob", -32767.0f}}));
    EXPECT_EQ(Decode(dial, "01 02 00 80 0a 0b 0c 0d 3a"), (Events{{"knob", -32768.0f}})) << "int16 sign, no clamp";
    EXPECT_EQ(Decode(dial, "01 02 00 01 0a 0b 0c 0d 3a"), (Events{{"knob", 256.0f}})) << "the high byte";
}

TEST(SurfaceDial_Test, Decode_OtherReportsAreIgnored)
{
    SurfaceDial dial;
    EXPECT_EQ(Decode(dial, "32 02"), Events{}) << "System Control: sleep / wake";
    std::string vendor = "2a";
    for (int i = 0; i < 72; ++i)
        vendor += " ff";
    EXPECT_EQ(Decode(dial, vendor), Events{}) << "vendor page 0xFF07";
    EXPECT_EQ(Decode(dial, "01 03"), Events{}) << "too short for the dial";
    EXPECT_EQ(Decode(dial, ""), Events{});
    EXPECT_EQ(Decode(dial, "01 02 00 00 0a 0b 0c 0d 3a"), (Events{{"touch", 1.0f}})) << "the state was not touched";
}

/// The whole session: the measured layout and the one read from the descriptor agree, event for event
TEST(SurfaceDial_Test, Capture_MeasuredLayoutAndDescriptorAgree)
{
    const Capture capture = Load();
    ASSERT_EQ(capture.reports.size(), 44u);
    HidDescriptor descriptor;
    std::string error;
    ASSERT_TRUE(ParseHidDescriptor(capture.descriptor.data(), capture.descriptor.size(), descriptor, error)) << error;
    SurfaceDial measured;
    SurfaceDial described(descriptor);
    Events a;
    Events b;
    for (const std::vector<uint8_t>& report : capture.reports)
    {
        measured.Decode(report.data(), report.size(), Into(a));
        described.Decode(report.data(), report.size(), Into(b));
    }
    EXPECT_EQ(a, b);

    int presses = 0;
    int touches = 0;
    int turns = 0;
    float steps = 0;
    for (const auto& [control, value] : a)
    {
        presses += control == "push" && value > 0;
        touches += control == "touch" && value > 0;
        if (control == "knob")
        {
            ++turns;
            steps += value;
        }
    }
    EXPECT_EQ(presses, 3);
    EXPECT_EQ(touches, 4);
    EXPECT_EQ(turns, 28) << "one report per step";
    EXPECT_EQ(steps, -2.0f);
}

TEST(SurfaceDial_Test, Feature_StepsPerTurnAndTheHaptics)
{
    const std::vector<uint8_t> found = Bytes("01 24 00 00 01 05 50 00");
    SurfaceDialFeature feature;
    ASSERT_TRUE(SurfaceDial::ParseFeature(found.data(), found.size(), feature));
    EXPECT_EQ(feature.stepsPerTurn, 36);
    EXPECT_EQ(feature.repeatCount, 0);
    EXPECT_EQ(feature.autoTrigger, 1) << "no local clicks";
    EXPECT_EQ(feature.cutoff, 5);
    EXPECT_EQ(feature.retriggerMs, 80);

    const std::vector<uint8_t> clicks = Bytes("01 0c 00 00 03 05 50 00");
    ASSERT_TRUE(SurfaceDial::ParseFeature(clicks.data(), clicks.size(), feature));
    EXPECT_EQ(feature.stepsPerTurn, 12);
    EXPECT_EQ(feature.autoTrigger, 3);
    SurfaceDial dial;
    dial.SetFeature(feature);
    EXPECT_EQ(dial.Feature().stepsPerTurn, 12);

    const std::vector<uint8_t> other = Bytes("02 24 00 00 01 05 50 00");
    EXPECT_FALSE(SurfaceDial::ParseFeature(other.data(), other.size(), feature));
    EXPECT_FALSE(SurfaceDial::ParseFeature(found.data(), 7, feature));
}

TEST(SurfaceDial_Test, ParseControl_KnobPushTouch)
{
    ControlInfo info;
    ASSERT_TRUE(ParseControl("Knob", info));
    EXPECT_EQ(info.name, "knob");
    EXPECT_EQ(info.type, ControlType::Encoder) << "an encoder, not a mouse delta";
    ASSERT_TRUE(ParseControl("push", info));
    EXPECT_EQ(info.type, ControlType::Button);
    ASSERT_TRUE(ParseControl("touch", info));
    EXPECT_EQ(info.type, ControlType::Button);
    for (const char* name : {"knob", "push", "touch"})
        EXPECT_NE(std::find(KnownControls().begin(), KnownControls().end(), name), KnownControls().end()) << name;
    EXPECT_EQ(SurfaceDial::Controls(), (std::vector<std::string>{"knob", "push", "touch"}));

    // The knob drives no retro target yet; the push does
    Binding binding;
    binding.sources = {"knob"};
    std::string error;
    ASSERT_TRUE(ParseRetroTarget("kempston:fire", binding.target, error)) << error;
    EXPECT_FALSE(ValidateBinding(binding, error));
    EXPECT_NE(error.find("knob"), std::string::npos) << error;
    binding.sources = {"push"};
    EXPECT_TRUE(ValidateBinding(binding, error)) << error;
}

TEST(SurfaceDial_Test, DeviceDatabase_TheFamily)
{
    const DeviceDatabase& db = DeviceDatabase::Builtin();
    const DeviceMatch match = db.Lookup({"bt", 0x045E, 0x091B, 0x0001, "Surface Dial", "iohid"});
    EXPECT_EQ(match.family, "surface-dial");
    EXPECT_EQ(match.name, "Surface Dial");
    EXPECT_EQ(match.vendor, "Microsoft");
    EXPECT_EQ(match.step, "exact");
    EXPECT_FALSE(match.player) << "a control surface takes no player connector";
    EXPECT_EQ(match.controls, (std::vector<std::string>{"knob", "push", "touch"}));
    EXPECT_EQ(match.images, "surface-dial");
    EXPECT_EQ(db.Glyph(match.glyphs, "knob"), "Turn");
    EXPECT_EQ(db.Lookup({"", 0x045E, 0x091B, -1, "", "evdev"}).family, "surface-dial") << "a bus not known";
    EXPECT_TRUE(db.ForFamily("xbox-elite-2").player) << "a pad is a player";
}

/// A Dial through the service: listed with its family, its events reach the router and do nothing unbound; the live
/// view sums the knob's steps; the devices verb names its controls
TEST(SurfaceDial_Test, FakeBackend_ListedAndUnboundEventsDoNothing)
{
    HostInputService service;
    auto fake = std::make_unique<FakeBackend>();
    FakeBackend* devices = fake.get();
    std::string reason;
    ASSERT_TRUE(service.AddBackend(std::move(fake), reason)) << reason;
    ASSERT_TRUE(devices->Connect(kDial));

    const std::vector<HostDeviceInfo> listed = service.Router().Devices();
    ASSERT_EQ(listed.size(), 1u);
    EXPECT_EQ(listed[0].id, kDial.id);
    EXPECT_EQ(listed[0].family, "surface-dial");
    EXPECT_EQ(listed[0].name, "Surface Dial");
    EXPECT_EQ(service.Router().Role(kDial.id), DeviceRole::Player) << "the router's default, for now";

    Binding fire;
    fire.sources = {"south"};
    ASSERT_TRUE(ParseRetroTarget("kempston:fire", fire.target, reason)) << reason;
    ASSERT_TRUE(service.Router().SetBindings({}, {fire}, reason)) << reason;
    std::shared_ptr<IHostInputSource> source = service.SourceFor("emu-1");
    ASSERT_TRUE(devices->Set(kDial.id, "touch", 1.0f));
    ASSERT_TRUE(devices->Set(kDial.id, "knob", 1.0f));
    ASSERT_TRUE(devices->Set(kDial.id, "knob", 1.0f));
    ASSERT_TRUE(devices->Set(kDial.id, "knob", -3.0f));
    ASSERT_TRUE(devices->Press(kDial.id, "push"));
    HostSample out;
    source->Sample(1, out);
    for (const ConnectorSample& connector : out.connectors)
        EXPECT_EQ(connector.mask, 0) << connector.connector << ": nothing binds the Dial";
    EXPECT_TRUE(out.keys.empty());

    const std::map<std::string, float> live = service.Live(kDial.id);
    EXPECT_EQ(live.at("knob"), -1.0f) << "the sum of the steps";
    EXPECT_EQ(live.at("push"), 1.0f);
    EXPECT_EQ(live.at("touch"), 1.0f);

    DevicesOwner owner(listed);
    const HostReply reply = HostInputControl(owner).Execute({"devices", {}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    const JsonValue body = reply.ToValue();
    const JsonValue* list = body.Find("devices");
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->Items().size(), 1u);
    const JsonValue& item = list->Items()[0];
    ASSERT_NE(item.Find("family"), nullptr);
    EXPECT_EQ(item.Find("family")->AsString(), "surface-dial");
    EXPECT_EQ(item.Find("name")->AsString(), "Surface Dial");
    const JsonValue* controls = item.Find("controls");
    ASSERT_NE(controls, nullptr);
    std::vector<std::string> names;
    for (const JsonValue& control : controls->Items())
        names.push_back(control.AsString());
    EXPECT_EQ(names, (std::vector<std::string>{"knob", "push", "touch"}));

    service.Stop();
    EXPECT_TRUE(service.Router().Devices().empty());
}

TEST(SurfaceDial_Test, HapticReport_ClicksAndBuzz)
{
    // Output report 1 as measured on the owner's Dial: 01 03 03 00 00 played three clicks
    EXPECT_EQ(SurfaceDial::HapticReport({HapticClicks::Wave::Click, 3, 0}),
              (std::vector<uint8_t>{0x01, 0x03, 0x03, 0x00, 0x00}));
    EXPECT_EQ(SurfaceDial::HapticReport({HapticClicks::Wave::Buzz, 1, 300}),
              (std::vector<uint8_t>{0x01, 0x01, 0x04, 0x2C, 0x01}));
    EXPECT_EQ(SurfaceDial::HapticReport({HapticClicks::Wave::Click, 0, 0})[1], 1);  // zero plays once
}

TEST(SurfaceDial_Test, TestEffects_AClickerGetsClicksNotMotors)
{
    const std::vector<TestEffect> effects = TestEffects(kHapticClicker);
    std::vector<std::string> ids;
    for (const TestEffect& test : effects)
    {
        ids.push_back(test.id);
        EXPECT_TRUE(test.clicks.has_value()) << test.id;
    }
    EXPECT_EQ(ids, (std::vector<std::string>{"identify", "click", "clicks", "buzz"}));
    EXPECT_EQ(DurationMs(effects[2]), 120u);  // three clicks, 60 ms apart
    EXPECT_EQ(ActuatorNames(kHapticClicker), (std::vector<std::string>{"clicker"}));
}

/// The sensor is seconds late (measured 2026-10-19): a turn or a press shows the hand at once, the sensor's release
/// ends it, and a touch the sensor never confirmed ends DialTouch::kUnconfirmedMs after the last action
TEST(SurfaceDial_Test, Touch_InferredFromActionsBeforeTheSensor)
{
    constexpr uint64_t kWait = DialTouch::kUnconfirmedMs;
    SurfaceDial dial;
    EXPECT_EQ(Decode(dial, "01 00 01 00 0a 0b 0c 0d 3a", 1000), (Events{{"touch", 1.0f}, {"knob", 1.0f}}))
        << "a turn before the sensor saw the hand";
    EXPECT_EQ(Decode(dial, "01 00 01 00 0a 0b 0c 0d 3a", 1100), (Events{{"knob", 1.0f}}));
    EXPECT_EQ(Expire(dial, 1100 + kWait - 1), Events{});
    EXPECT_EQ(Decode(dial, "01 02 00 00 0a 0b 0c 0d 3a", 1500), Events{}) << "the sensor caught up";
    EXPECT_EQ(Expire(dial, 1100 + 10 * kWait), Events{}) << "the sensor's touch is never cut short";
    EXPECT_EQ(Decode(dial, "01 00 00 00 0a 0b 0c 0d 3a", 40000), (Events{{"touch", 0.0f}})) << "its release ends it";

    SurfaceDial quick;
    EXPECT_EQ(Decode(quick, "01 00 ff ff 0a 0b 0c 0d 3a", 0), (Events{{"touch", 1.0f}, {"knob", -1.0f}}));
    EXPECT_EQ(Expire(quick, kWait - 1), Events{});
    EXPECT_EQ(Expire(quick, kWait), (Events{{"touch", 0.0f}})) << "the hand left before the sensor saw it";
    EXPECT_EQ(Expire(quick, 2 * kWait), Events{});

    SurfaceDial pressed;
    EXPECT_EQ(Decode(pressed, "01 01 00 00 0a 0b 0c 0d 3a", 0), (Events{{"touch", 1.0f}, {"push", 1.0f}}));
    EXPECT_EQ(Expire(pressed, 10 * kWait), Events{}) << "held down: still a hand on it";
    EXPECT_EQ(Decode(pressed, "01 00 00 00 0a 0b 0c 0d 3a", 10 * kWait), (Events{{"push", 0.0f}}));
    EXPECT_EQ(Expire(pressed, 11 * kWait), (Events{{"touch", 0.0f}}));
}
