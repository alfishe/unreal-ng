/// @file hapticprofile_test.cpp
/// @brief H3 (haptics.md §5-§7): a game profile's `haptics` section parses and refuses what it cannot do yet, its
/// frame probes fire on what they watch (Haptics_ProbeWriteDecreased of §12), and its rules play the library effects.
/// A play profile's `events:` are its sources too, and they report the game's events without a device.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "haptics/hapticmixer.h"
#include "_helpers/fixedmixerclock.h"
#include "haptics/hapticprofile.h"
#include "haptics/profilehaptics.h"
#include "profiles/profile.h"

using namespace controllers;

namespace
{
const char* const kGame = R"({
  "schema": 1, "kind": "play", "id": "game/haptic-test",
  "match": { "file_sha256": ["00"] },
  "haptics": {
    "sources": {
      "boom":  { "filter": "ay-envelope", "source": "ay:0" },
      "died":  { "probe": "write", "address": "0x5f20", "when": "decreased" },
      "speed": { "probe": "value", "address": "page:7:0xc010", "scale": 1.0 }
    },
    "rules": [
      { "on": "boom", "play": "explosion" },
      { "on": "died", "play": "life-lost", "target": "trigger.right|handle.high" },
      { "on": "speed", "play": "engine" }
    ]
  }
})";

HapticProfile ParseGame()
{
    Profile profile;
    std::string error;
    EXPECT_TRUE(ParseProfile(kGame, profile, error)) << error;
    HapticProfile haptics;
    EXPECT_TRUE(ParseHapticProfile(profile.haptics, haptics, error)) << error;
    return haptics;
}

HapticFrame Frame(uint8_t lives, uint8_t speed)
{
    HapticFrame frame;
    frame.watchCount = 2;
    frame.watched[0] = lives;
    frame.watched[1] = speed;
    return frame;
}
}  // namespace

TEST(HapticProfile_Test, ParsesAndRefusesWhatItCannotDo)
{
    const HapticProfile haptics = ParseGame();
    ASSERT_EQ(haptics.sources.size(), 3u);
    EXPECT_EQ(haptics.sources[1].address, 0x5f20);
    EXPECT_EQ(haptics.sources[2].page, 7) << "a page-qualified probe";
    EXPECT_EQ(haptics.sources[2].address, 0x0010) << "the offset in the page";
    ASSERT_EQ(haptics.rules.size(), 3u);
    EXPECT_EQ(haptics.rules[1].target, kTriggerRight) << "the first of the fallbacks";

    struct Bad
    {
        const char* section;
        const char* reason;
    };
    const Bad bad[] = {
        {R"({"sources": {"s": {"probe": "peek", "address": 1}}})", "probe 'peek' unknown"},
        {R"({"sources": {"s": {"filter": "highband"}}})", "unknown"},
        {R"({"sources": {"s": {"probe": "write"}}})", "'address' is required"},
        {R"({"sources": {"s": {"filter": "ay-noise"}}, "rules": [{"on": "t", "play": "tick"}]})", "names no source"},
        {R"({"sources": {"s": {"filter": "ay-noise"}}, "rules": [{"on": "s", "play": "boing"}]})", "'play' 'boing'"},
        {R"({"sources": {"s": {"probe": "write", "address": 1, "word": true}}})", "on a value probe"},
        {R"({"sources": {"s": {"probe": "value", "address": "0xFFFF", "word": true}}})", "second byte"},
        {R"({"source": {}})", "unknown field"},
    };
    for (const Bad& entry : bad)
    {
        JsonValue section;
        std::string error;
        ASSERT_TRUE(ParseJson(entry.section, section, error)) << error;
        HapticProfile haptics;
        EXPECT_FALSE(ParseHapticProfile(section, haptics, error)) << entry.section;
        EXPECT_NE(error.find(entry.reason), std::string::npos) << error;
    }

    Profile profile;
    std::string error;
    std::string text = kGame;
    text.replace(text.find("\"decreased\""), 11, "\"sometimes\"");
    EXPECT_FALSE(ParseProfile(text, profile, error)) << "the profile refuses a bad haptics section";
    EXPECT_NE(error.find("haptics.sources.died"), std::string::npos) << error;
}

TEST(HapticProfile_Test, Haptics_ProbeWriteDecreased)
{
    std::vector<std::pair<HostDeviceId, RumbleLevels>> writes;
    HapticMixer mixer(
        [&writes](const HostDeviceId& device, const RumbleLevels& levels) {
            writes.emplace_back(device, levels);
            return true;
        },
        false, FixedMixerClock());
    ProfileHaptics feed(ParseGame(), "game/haptic-test", mixer, [] { return std::vector<HostDeviceId>{"pad"}; });
    const std::vector<HapticWatch> watches = feed.Watches();
    ASSERT_EQ(watches.size(), 2u) << "the probes ask the core for their bytes";
    EXPECT_EQ(watches[0].address, 0x5f20);
    EXPECT_EQ(watches[1].page, 7);

    feed.OnHapticFrame(Frame(3, 0));
    EXPECT_EQ(mixer.Written("pad"), RumbleLevels{}) << "the first frame only learns the byte";
    feed.OnHapticFrame(Frame(4, 0));
    EXPECT_EQ(mixer.Written("pad"), RumbleLevels{}) << "an increase is not a death";
    feed.OnHapticFrame(Frame(3, 0));
    EXPECT_EQ(mixer.Written("pad").rightTrigger, 1.0f) << "decreased: life-lost on the right trigger";
    EXPECT_EQ(mixer.Written("pad").low, 0.0f) << "only the target";
    mixer.StopAll();
    feed.OnHapticFrame(Frame(3, 0));
    EXPECT_EQ(mixer.Written("pad"), RumbleLevels{}) << "fires once per decrement";

    feed.OnHapticFrame(Frame(3, 255));
    EXPECT_FLOAT_EQ(mixer.Written("pad").low, 0.5f) << "a value probe scales the engine";
    mixer.StopAll();

    HapticFrame boom = Frame(3, 0);
    boom.ayCount = 1;
    boom.ay[0].envelopeRestarts = 1;
    boom.ay[0].envelopeUsed = true;
    boom.ay[0].envelopeShape = 0x09;
    feed.OnHapticFrame(boom);
    mixer.Tick(FixedMixerTime() + std::chrono::milliseconds(6));
    EXPECT_GT(mixer.Written("pad").low, 0.9f) << "an envelope boom plays the explosion, past its 5 ms attack";
    bool fired = false;
    for (const auto& [name, level] : feed.SourceLevels())
        fired = fired || (name == "boom" && level == 1.0f);
    EXPECT_TRUE(fired) << "the Haptics tab sees the source fire";
}

TEST(HapticProfile_Test, EventsAreTheSourcesOfTheHaptics)
{
    // A play profile's `events:` (breakpoint taps README.md §7) are haptics sources: the rules name them, beside the
    // ones written under haptics.sources
    const char* const text = R"({
      "schema": 1, "kind": "play", "id": "game/events-test",
      "match": { "file_sha256": ["00"] },
      "events": { "jump": { "probe": "exec", "address": "0x795A" }, "lives": { "probe": "value", "address": "0x767F" } },
      "haptics": {
        "sources": { "boom": { "filter": "ay-envelope" } },
        "rules": [ { "on": "jump", "play": "tick" }, { "on": "boom", "play": "explosion" } ]
      }
    })";
    Profile profile;
    std::string error;
    ASSERT_TRUE(ParseProfile(text, profile, error)) << error;
    HapticProfile haptics;
    ASSERT_TRUE(ParseHapticProfile(profile.haptics, haptics, error)) << error;
    ASSERT_EQ(haptics.sources.size(), 3u);
    std::vector<std::string> names;
    for (const HapticSource& source : haptics.sources)
        names.push_back(source.name);
    EXPECT_NE(std::find(names.begin(), names.end(), "jump"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "lives"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "boom"), names.end());

    // Events alone: the haptics section is made, with no rules
    const char* const alone = R"({"schema": 1, "kind": "play", "id": "game/events-only", "match": {"file_sha256": ["00"]},
      "events": {"died": {"probe": "exec", "address": "0x8B80"}}})";
    ASSERT_TRUE(ParseProfile(alone, profile, error)) << error;
    ASSERT_TRUE(ParseHapticProfile(profile.haptics, haptics, error)) << error;
    ASSERT_EQ(haptics.sources.size(), 1u);
    EXPECT_TRUE(haptics.rules.empty());

    std::string twice = text;
    twice.replace(twice.find("\"boom\""), 6, "\"jump\"");
    EXPECT_FALSE(ParseProfile(twice, profile, error));
    EXPECT_NE(error.find("also a haptics source"), std::string::npos) << error;
    EXPECT_FALSE(ParseProfile(R"({"schema": 1, "kind": "debug", "id": "debug/x", "events": {}})", profile, error));
    EXPECT_NE(error.find("play profile"), std::string::npos) << error;
}

TEST(HapticProfile_Test, GameEventsWithoutADevice)
{
    // The game's events come from the profile's sources with no device to play on: each frame's levels and watched
    // values; a hold (a replay or seek) reports nothing
    HapticMixer mixer([](const HostDeviceId&, const RumbleLevels&) { return true; }, false, FixedMixerClock());
    ProfileHaptics feed(ParseGame(), "game/haptic-test", mixer, [] { return std::vector<HostDeviceId>{}; });
    const std::vector<GameEventInfo> events = feed.GameEvents();
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[1].name, "died");
    EXPECT_FALSE(events[1].level) << "a write is an event";
    EXPECT_TRUE(events[2].level) << "a value is a level";
    EXPECT_EQ(feed.GameEventsProfile(), "game/haptic-test");

    GameEventFrame frame;
    EXPECT_FALSE(feed.TakeGameEvents(frame)) << "no frame evaluated yet";
    feed.OnHapticFrame(Frame(3, 0x40));  // the first frame learns the lives
    ASSERT_TRUE(feed.TakeGameEvents(frame));
    EXPECT_EQ(frame.fired & 2u, 0u);
    EXPECT_EQ(frame.value[1], 3);
    EXPECT_EQ(frame.value[2], 0x40);
    EXPECT_EQ(frame.value[0], -1) << "a sound source reads no byte";
    EXPECT_FALSE(feed.TakeGameEvents(frame)) << "taken once";

    frame = GameEventFrame{};
    feed.OnHapticFrame(Frame(2, 0x40));
    ASSERT_TRUE(feed.TakeGameEvents(frame));
    EXPECT_NE(frame.fired & 2u, 0u) << "died: the lives went down";
    EXPECT_EQ(frame.level[1], 1.0f);
    EXPECT_NE(frame.fired & 4u, 0u) << "a level above 0 counts as fired";

    feed.OnHapticHold(true);
    feed.OnHapticFrame(Frame(1, 0x40));
    EXPECT_FALSE(feed.TakeGameEvents(frame)) << "held: nothing to report";
}
