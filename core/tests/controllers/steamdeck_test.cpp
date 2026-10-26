/// @file steamdeck_test.cpp
/// @brief The Steam Deck's controller (28DE:1205): the deck-state input report through the shared driver (buttons,
/// paddles, sticks, trackpads, motion), the feature reports that drive it (lizard mode, motors, voice coils), the
/// vocabulary's trackpads, the HD haptics channels (hd.left, hd.right) and the device database families, the gamepad
/// emulator's (steamdeck-bt-controller-emulator) among them.

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "devicedatabase.h"
#include "hid/drivers/steamdeck.h"
#include "hid/hidpad.h"
#include "hostcontrols.h"
#include "profiles/profilestore.h"
#include "router/binding.h"
#include "router/inputrouter.h"

using namespace controllers;

namespace
{
std::vector<uint8_t> Bytes(const std::string& hex)
{
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return out;
}

/// A report read from the owner's Deck on 2026-10-20, lying on the desk: no button, the sticks near the centre,
/// gravity on the accelerometer's z
const char* const kIdle = "01000940a85c1f000000000000000000000000000000000052fa2900d33f0000ffff00000071c4021a052ac40000"
                          "00002701a70285ff7f010000000000000000";

void Put16(std::vector<uint8_t>& report, size_t offset, int value)
{
    report[offset] = static_cast<uint8_t>(value & 0xFF);
    report[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void Put32(std::vector<uint8_t>& report, size_t offset, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        report[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

struct Recorder
{
    std::map<std::string, float> last;
    std::vector<std::string> order;

    ControlSink Sink()
    {
        return [this](const std::string& control, float value) {
            last[control] = value;
            order.push_back(control);
        };
    }
    bool Has(const std::string& control) const
    {
        return last.count(control) != 0;
    }
};
}  // namespace

TEST(SteamDeck_Test, IdleReport)
{
    const std::vector<uint8_t> idle = Bytes(kIdle);
    ASSERT_EQ(idle.size(), SteamDeck::kReportSize);
    SteamDeck deck;
    Recorder seen;
    deck.Decode(idle.data(), idle.size(), seen.Sink());
    EXPECT_FALSE(seen.Has("south")) << "no button is on";
    EXPECT_NEAR(seen.last["lstick.x"], 295 / 32767.0f, 1e-6f);
    EXPECT_NEAR(seen.last["lstick.y"], -679 / 32767.0f, 1e-6f) << "the Deck's y is + up";
    EXPECT_NEAR(seen.last["accel.y"], 16339 / 32768.0f, 1e-6f) << "gravity: the report's z is SDL's y";
    EXPECT_EQ(seen.last["ltouchpad.x"], 0.0f) << "no finger on the pad";
    EXPECT_EQ(seen.last["lt"], 0.0f);

    Recorder again;
    deck.Decode(idle.data(), idle.size(), again.Sink());
    EXPECT_TRUE(again.order.empty()) << "the same report changes nothing";
}

TEST(SteamDeck_Test, ButtonsPaddlesAndTrackpads)
{
    std::vector<uint8_t> report = Bytes(kIdle);
    SteamDeck deck;
    Recorder first;
    deck.Decode(report.data(), report.size(), first.Sink());

    // A, L5 (the lower left paddle), the right pad touched at its right edge, R4, the quick access button
    Put32(report, 8, 0x00000080 | 0x00008000 | 0x00100000);
    Put32(report, 12, 0x00000400 | 0x00040000);
    Put16(report, 20, 32767);
    Put16(report, 22, 32767);
    Put16(report, 46, 16384);
    Recorder pressed;
    deck.Decode(report.data(), report.size(), pressed.Sink());
    EXPECT_EQ(pressed.last["south"], 1.0f);
    EXPECT_EQ(pressed.last["paddle.p4"], 1.0f) << "L5";
    EXPECT_EQ(pressed.last["paddle.p1"], 1.0f) << "R4";
    EXPECT_EQ(pressed.last["misc.1"], 1.0f) << "the quick access button";
    EXPECT_EQ(pressed.last["rtouchpad.touch"], 1.0f);
    EXPECT_FLOAT_EQ(pressed.last["rtouchpad.x"], 1.0f);
    EXPECT_FLOAT_EQ(pressed.last["rtouchpad.y"], -1.0f) << "the top edge";
    EXPECT_NEAR(pressed.last["rt"], 0.5f, 1e-4f);

    // The finger leaves; the report keeps its last position
    Put32(report, 8, 0);
    Put32(report, 12, 0);
    Recorder released;
    deck.Decode(report.data(), report.size(), released.Sink());
    EXPECT_EQ(released.last["south"], 0.0f);
    EXPECT_EQ(released.last["rtouchpad.touch"], 0.0f);
    EXPECT_EQ(released.last["rtouchpad.x"], 0.0f) << "an untouched pad reads the centre";
    EXPECT_EQ(released.last["rtouchpad.y"], 0.0f);

    std::vector<uint8_t> other = report;
    other[2] = 1;
    Recorder ignored;
    deck.Decode(other.data(), other.size(), ignored.Sink());
    EXPECT_TRUE(ignored.order.empty()) << "only the deck-state report";
}

TEST(SteamDeck_Test, MotionDeadband)
{
    std::vector<uint8_t> report = Bytes(kIdle);
    SteamDeck deck;
    Recorder first;
    deck.Decode(report.data(), report.size(), first.Sink());
    Put16(report, 30, 2);  // gyro x: the sensor's noise
    Recorder noise;
    deck.Decode(report.data(), report.size(), noise.Sink());
    EXPECT_FALSE(noise.Has("gyro.x"));
    Put16(report, 30, 16384);
    Recorder turned;
    deck.Decode(report.data(), report.size(), turned.Sink());
    EXPECT_FLOAT_EQ(turned.last["gyro.x"], 0.5f) << "half of 2000 deg/s";
}

TEST(SteamDeck_Test, FeatureReports)
{
    const std::vector<uint8_t> rumble = SteamDeck::RumbleReport(1.0f, 0.5f);
    ASSERT_EQ(rumble.size(), SteamDeck::kFeatureSize);
    EXPECT_EQ(std::vector<uint8_t>(rumble.begin(), rumble.begin() + 12),
              (std::vector<uint8_t>{0, 0xEB, 9, 0, 0, 0, 0xFF, 0xFF, 0x00, 0x80, 2, 0}))
        << "hid-steam's layout: type, intensity, the speeds, the gains";

    const std::vector<uint8_t> pulse = SteamDeck::PulseReport(SteamDeck::Pad::Left, 2000, 500, 3, -6);
    EXPECT_EQ(std::vector<uint8_t>(pulse.begin(), pulse.begin() + 11),
              (std::vector<uint8_t>{0, 0x8F, 8, 1, 0xD0, 0x07, 0xF4, 0x01, 3, 0, 0xFA}))
        << "the left pad is 1 in this report";
    EXPECT_EQ(SteamDeck::PulseReport(SteamDeck::Pad::Right, 1, 1, 1, 0)[3], 0);
    EXPECT_EQ(SteamDeck::PulseReport(SteamDeck::Pad::Both, 1, 1, 1, 0)[3], 2);

    SteamDeck::PadEffect tone;
    tone.pad = SteamDeck::Pad::Right;
    tone.command = SteamDeck::PadCommand::Tone;
    tone.frequency = 200;
    tone.durationMs = -1;
    const std::vector<uint8_t> command = SteamDeck::PadEffectReport(tone);
    EXPECT_EQ(command[1], 0xEA);
    EXPECT_EQ(command[2], 19) << "MsgTriggerHaptic, packed";
    EXPECT_EQ(std::vector<uint8_t>(command.begin() + 3, command.begin() + 11),
              (std::vector<uint8_t>{1, 3, 0, 0, 200, 0, 0xFF, 0xFF}))
        << "the right pad is 1 in this report (measured)";
    tone.pad = SteamDeck::Pad::Both;
    EXPECT_EQ(SteamDeck::PadEffectReport(tone)[3], 2) << "both is 2: the firmware ignores SDL's 3";

    const auto lizard = SteamDeck::LizardOffReports();
    ASSERT_EQ(lizard.size(), 2u);
    EXPECT_EQ(lizard[0][1], 0x81);
    EXPECT_EQ(lizard[1][1], 0x87);
    EXPECT_EQ(lizard[1][2], 15) << "five settings of three bytes";
    EXPECT_EQ(std::vector<uint8_t>(lizard[1].begin() + 6, lizard[1].begin() + 9), (std::vector<uint8_t>{7, 7, 0}))
        << "the left trackpad: no mouse";

    EXPECT_EQ(SteamDeck::GainDb(1.0f), 0);
    EXPECT_EQ(SteamDeck::GainDb(0.5f), -6);
    EXPECT_EQ(SteamDeck::GainDb(0.0f), -24);
}

TEST(SteamDeck_Test, WatchdogEvery200Reports)
{
    const std::vector<uint8_t> idle = Bytes(kIdle);
    SteamDeck deck;
    int feeds = 0;
    for (unsigned i = 0; i < 2 * SteamDeck::kWatchdogReports; ++i)
    {
        deck.Decode(idle.data(), idle.size(), [](const std::string&, float) {});
        feeds += deck.WatchdogDue() ? 1 : 0;
    }
    EXPECT_EQ(feeds, 2);
}

TEST(SteamDeck_Test, Vocabulary)
{
    for (const std::string& control : SteamDeck::Controls())
    {
        ControlInfo info;
        EXPECT_TRUE(ParseControl(control, info)) << control;
    }
    ControlInfo info;
    ASSERT_TRUE(ParseControl("rtouchpad.left", info));
    EXPECT_EQ(info.base, "rtouchpad") << "a trackpad reads as a stick";
    EXPECT_EQ(StickOf("ltouchpad"), "ltouchpad");
    ASSERT_TRUE(ParseControl("ltouchpad.pressure", info));
    EXPECT_EQ(info.type, ControlType::Trigger);
}

TEST(SteamDeck_Test, DeviceDatabase)
{
    const DeviceDatabase& db = DeviceDatabase::Builtin();
    const DeviceMatch deck = db.Lookup({"usb", 0x28DE, 0x1205, 0x0100, "Valve Software Steam Deck Controller", "hidraw"});
    EXPECT_EQ(deck.family, "steam-deck");
    EXPECT_EQ(deck.images, "steam-deck");
    EXPECT_EQ(db.Glyph(deck.glyphs, "paddle.p4"), "L5");
    EXPECT_EQ(db.Glyph(deck.glyphs, "misc.1"), "···");
    EXPECT_TRUE(db.Lookup({"usb", 0x28DE, 0x1205, 0x0100, "Steam Deck", "evdev"}).ignore)
        << "hid-steam's evdev pad: the hidraw backend reads the controller";
    EXPECT_TRUE(db.Lookup({"usb", 0x28DE, 0x11FF, 0x0001, "Microsoft X-Box 360 pad 0", "evdev"}).ignore)
        << "Steam's virtual pad mirrors a real one";

    EXPECT_EQ(db.Lookup({"bt", 0x28DE, 0x0001, 0, "SteamDeckHoG", "evdev"}).family, "steam-deck-gamepad");
    EXPECT_EQ(db.Lookup({"usb", 0x1209, 0x0001, 0, "Steam Deck HID Controller", "evdev"}).family, "steam-deck-gamepad");
    EXPECT_NE(db.Lookup({"usb", 0x1209, 0x0001, 0, "Some pid.codes test device", "evdev"}).family,
              "steam-deck-gamepad")
        << "1209:0001 is shared: the name decides";
    EXPECT_EQ(HidLayoutFor(db.Lookup({"bt", 0x28DE, 0x0001, 0, "SteamDeckHoG", "evdev"})).buttons[2], "west")
        << "the emulator's Button 3 is X";
}

TEST(SteamDeck_Test, Profiles)
{
    const auto find = [](const ResolvedProfiles& resolved, const std::string& layer, const std::string& source) {
        for (const auto& [scope, bindings] : resolved.scopes)
            if (scope == "family:steam-deck")
                for (const Binding& binding : bindings)
                    if (binding.layer == layer && binding.sources.size() == 1 && binding.sources[0] == source)
                        return &binding;
        return static_cast<const Binding*>(nullptr);
    };
    ProfileStore store;
    ProfileContext context;
    context.machine = "pentagon";
    context.devices = {{"usb:28de:1205:0100:1", "Steam Deck", "steam-deck", "xbox", "hidraw"}};
    ResolvedProfiles resolved = store.Resolve(context);
    EXPECT_TRUE(resolved.warnings.empty()) << resolved.warnings.front();
    const Binding* mouse = find(resolved, "", "rtouchpad.x");
    ASSERT_NE(mouse, nullptr);
    EXPECT_EQ(mouse->target.kind, TargetKind::Mouse);
    EXPECT_EQ(mouse->target.mouse, MouseControl::Dx);
    const Binding* click = find(resolved, "", "rtouchpad.click");
    ASSERT_NE(click, nullptr);
    EXPECT_EQ(click->target.mouse, MouseControl::Left);
    ASSERT_NE(find(resolved, "", "paddle.p1"), nullptr);
    ASSERT_NE(find(resolved, "", "south"), nullptr) << "pad-default under the family profile";
    const Binding* l5 = find(resolved, "", "paddle.p4");
    ASSERT_NE(l5, nullptr);
    EXPECT_EQ(l5->target.kind, TargetKind::Layer) << "L5 is the debug layer key, no play binding";

    context.debugLayer = true;
    resolved = store.Resolve(context);
    EXPECT_NE(find(resolved, "debug", "paddle.p1"), nullptr);
}

TEST(SteamDeck_Test, PlayerConnectorTakesTheGamesJoystick)
{
    // A game that reads Sinclair (Dizzy): the auto choice makes sinclair-1 the Deck's player connector, and the D-pad,
    // the left trackpad and A reach the keys 6-0 instead of a Kempston byte nobody reads
    const HostDeviceInfo deck{"usb:28de:1205:0100:1", "Steam Deck", "steam-deck", "xbox", "hidraw"};
    ProfileStore store;
    ProfileContext context;
    context.machine = "pentagon";
    context.devices = {deck};
    const ResolvedProfiles resolved = store.Resolve(context);
    InputRouter router;
    router.OnDeviceAdded(deck);
    for (const auto& [scope, bindings] : resolved.scopes)
    {
        std::string error;
        ASSERT_TRUE(router.SetBindings(scope, bindings, error)) << scope << ": " << error;
    }
    uint64_t frame = 1;
    const auto sample = [&router, &frame] {
        HostSample out;
        router.Sample("emu-1", frame++, out);
        std::map<std::string, uint16_t> masks;
        for (const ConnectorSample& connector : out.connectors)
            masks[connector.connector] = connector.mask;
        return masks;
    };

    router.SetPlayerConnector(deck.id, "sinclair-1");
    router.OnControl({deck.id, "dpad.left", 1, 0});
    router.OnControl({deck.id, "south", 1, 0});
    auto masks = sample();
    EXPECT_EQ(masks["sinclair-1"], kLeft | kFire);
    EXPECT_EQ(masks.count("kempston"), 0u) << "nothing goes to Kempston";

    router.OnControl({deck.id, "dpad.left", 0, 0});
    router.OnControl({deck.id, "south", 0, 0});
    router.OnControl({deck.id, "ltouchpad.x", 1, 0});
    EXPECT_EQ(sample()["sinclair-1"], kRight) << "the left trackpad is a second D-pad";

    router.SetPlayerConnector(deck.id, "kempston");
    EXPECT_EQ(sample()["kempston"], kRight) << "a Kempston game: the same controls, the other connector";
}
