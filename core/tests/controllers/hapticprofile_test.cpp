/// @file hapticprofile_test.cpp
/// @brief H3 (haptics.md §5-§7): a game profile's `haptics` section parses and refuses what it cannot do yet, its
/// frame probes fire on what they watch (Haptics_ProbeWriteDecreased of §12), and its rules play the library effects.

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "haptics/hapticmixer.h"
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
        false);
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
    mixer.Tick(HapticMixer::Clock::now() + std::chrono::milliseconds(6));
    EXPECT_GT(mixer.Written("pad").low, 0.9f) << "an envelope boom plays the explosion, past its 5 ms attack";
    bool fired = false;
    for (const auto& [name, level] : feed.SourceLevels())
        fired = fired || (name == "boom" && level == 1.0f);
    EXPECT_TRUE(fired) << "the Haptics tab sees the source fire";
}
