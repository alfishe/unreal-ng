/// @file videoprofile_test.cpp
/// @brief A game profile's `video:` section (mapping-and-profiles.md §7.7): parsed and checked with the reason, only on
/// a play profile, the most specific profile's whole section resolved, kept by a save.

#include <gtest/gtest.h>

#include <iterator>

#include "profiles/profilestore.h"
#include "profiles/videoprofile.h"
#include "profiles/yaml.h"

using namespace controllers;

namespace
{
std::string Game(const std::string& id, const std::string& sha, const std::string& extra)
{
    return "schema: 1\nkind: play\nid: " + id + "\nmatch:\n  file_sha256: [\"" + sha + "\"]\n" + extra +
           "layers:\n  play:\n    west: { key: X }\n";
}

bool Parse(const std::string& video, VideoProfile& out, std::string& error)
{
    JsonValue value;
    if (!ParseYaml(video, value, error))
        return false;
    return ParseVideoProfile(value, out, error);
}
}  // namespace

TEST(VideoProfile_Test, ParsesTheSection)
{
    VideoProfile video;
    std::string error;
    ASSERT_TRUE(Parse("picture_period: 5\nsmooth: cadence\nwindow: [32, 24, 256, 160]\n"
                      "exclude: [[32, 168, 256, 16]]\n",
                      video, error))
        << error;
    EXPECT_EQ(video.picturePeriod, 5u);
    EXPECT_EQ(video.smooth, smoothmotion::GameMode::Cadence);
    EXPECT_EQ(video.window, (VideoProfile::Rect{32, 24, 256, 160}));
    ASSERT_EQ(video.exclude.size(), 1u);
    EXPECT_EQ(video.exclude[0], (VideoProfile::Rect{32, 168, 256, 16}));

    ASSERT_TRUE(Parse("smooth: carry\n", video, error)) << error;
    EXPECT_EQ(video.picturePeriod, 1u) << "the defaults";
    EXPECT_EQ(video.window, VideoProfile::Rect{}) << "the whole frame";
    EXPECT_TRUE(video.exclude.empty());
    EXPECT_TRUE(VideoProfileFits(video, 352, 288, error));
}

TEST(VideoProfile_Test, InvalidSectionsSayWhy)
{
    const std::pair<const char*, const char*> cases[] = {
        {"picture_period: 0\n", "picture_period is a whole number 1..16"},
        {"picture_period: 17\n", "picture_period is a whole number 1..16"},
        {"picture_period: 2.5\n", "picture_period is a whole number 1..16"},
        {"smooth: blend\n", "smooth is off, cadence or carry"},
        {"window: [0, 0, 256]\n", "window is [x, y, width, height]"},
        {"window: [0, 0, 0, 16]\n", "the width and the height above 0"},
        {"window: [32, 24, 256, 160]\nexclude: [[0, 0, 8, 8]]\n", "exclude[0] is outside video.window"},
        {"picture_ready: { on: picture_done }\n", "unknown field 'picture_ready'"},
        {"speed: 2\n", "unknown field 'speed'"},
    };
    for (const auto& [text, reason] : cases)
    {
        VideoProfile video;
        std::string error;
        EXPECT_FALSE(Parse(text, video, error)) << text;
        EXPECT_NE(error.find(reason), std::string::npos) << text << ": " << error;
    }
    VideoProfile video;
    std::string error;
    ASSERT_TRUE(Parse("window: [32, 24, 256, 160]\n", video, error)) << error;
    EXPECT_FALSE(VideoProfileFits(video, 256, 192, error));
    EXPECT_NE(error.find("outside the 256x192 frame"), std::string::npos) << error;
}

TEST(VideoProfile_Test, ProfilesCarryAndResolveIt)
{
    ProfileStore store;
    std::string error;
    // A bad section fails its profile
    EXPECT_FALSE(store.AddText(Game("user/bad", "aa01", "video:\n  smooth: blend\n"), ProfileScope::User, error));
    EXPECT_NE(error.find("smooth is off, cadence or carry"), std::string::npos) << error;
    ASSERT_TRUE(store.AddText(Game("user/darkwing", "aa02", "video:\n  picture_period: 2\n  smooth: carry\n"),
                              ProfileScope::User, error))
        << error;
    // A debug profile has no picture cadence
    EXPECT_FALSE(store.AddText("schema: 1\nkind: debug\nid: user/dbg\nvideo:\n  smooth: carry\n"
                               "layers:\n  debug:\n    south: { action: ttd.pause }\n",
                               ProfileScope::User, error));
    EXPECT_NE(error.find("'video' belongs to a play profile"), std::string::npos) << error;

    ProfileContext context;
    EXPECT_TRUE(store.Resolve(context).video.IsNull()) << "no game";
    context.fileSha256 = {"aa02"};
    const ResolvedProfiles resolved = store.Resolve(context);
    ASSERT_FALSE(resolved.video.IsNull());
    EXPECT_EQ(resolved.videoProfile, "user/darkwing");
    VideoProfile video;
    ASSERT_TRUE(ParseVideoProfile(resolved.video, video, error)) << error;
    EXPECT_EQ(video.picturePeriod, 2u);
    EXPECT_EQ(video.smooth, smoothmotion::GameMode::Carry);

    // A save keeps it
    const Profile* profile = store.Find("user/darkwing");
    ASSERT_NE(profile, nullptr);
    Profile again;
    ASSERT_TRUE(ParseProfile(ProfileStore::Dump(*profile), again, error)) << error;
    EXPECT_EQ(again.video.Dump(), profile->video.Dump());
}

/// The profile's words are smooth motion's own (smoothmotion::ParseGameMode, what the presenter runs): the library
/// keeps a list of them, held to the engine's here
TEST(VideoProfile_Test, SmoothWordsAreSmoothMotions)
{
    ASSERT_EQ(std::size(kVideoSmoothWords), static_cast<size_t>(smoothmotion::GameMode::Carry) + 1);
    for (size_t i = 0; i < std::size(kVideoSmoothWords); i++)
    {
        const auto mode = static_cast<smoothmotion::GameMode>(i);
        EXPECT_STREQ(kVideoSmoothWords[i], smoothmotion::GameModeName(mode));
        smoothmotion::GameMode parsed = smoothmotion::GameMode::Off;
        ASSERT_TRUE(smoothmotion::ParseGameMode(kVideoSmoothWords[i], parsed));
        EXPECT_EQ(parsed, mode);
        VideoProfile video;
        std::string error;
        ASSERT_TRUE(Parse(std::string("smooth: ") + kVideoSmoothWords[i] + "\n", video, error)) << error;
        EXPECT_EQ(video.smooth, mode);
    }
}
