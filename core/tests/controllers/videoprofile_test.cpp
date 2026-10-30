/// @file videoprofile_test.cpp
/// @brief A game profile's `video:` section (mapping-and-profiles.md §7.7): parsed and checked with the reason, only on
/// a play profile, `picture_ready` naming one of the profile's events, the most specific profile's whole section
/// resolved, kept by a save.

#include <gtest/gtest.h>

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
    const std::vector<std::string> events = {"picture_done"};
    return ParseVideoProfile(value, &events, out, error);
}
}  // namespace

TEST(VideoProfile_Test, ParsesTheSection)
{
    VideoProfile video;
    std::string error;
    ASSERT_TRUE(Parse("picture_period: 5\nsmooth: cadence\nwindow: [32, 24, 256, 160]\n"
                      "exclude: [[32, 168, 256, 16]]\npicture_ready: { on: picture_done }\n",
                      video, error))
        << error;
    EXPECT_EQ(video.picturePeriod, 5u);
    EXPECT_EQ(video.smooth, "cadence");
    EXPECT_EQ(video.window, (VideoProfile::Rect{32, 24, 256, 160}));
    ASSERT_EQ(video.exclude.size(), 1u);
    EXPECT_EQ(video.exclude[0], (VideoProfile::Rect{32, 168, 256, 16}));
    EXPECT_EQ(video.pictureReady, "picture_done");

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
        {"picture_ready: { on: nothing }\n", "no event 'nothing'"},
        {"picture_ready: picture_done\n", "picture_ready is {on: <event>}"},
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
    // picture_ready names one of the profile's events
    EXPECT_FALSE(store.AddText(Game("user/no-event", "aa01", "video:\n  picture_ready: { on: picture_done }\n"),
                               ProfileScope::User, error));
    EXPECT_NE(error.find("no event 'picture_done'"), std::string::npos) << error;
    ASSERT_TRUE(store.AddText(Game("user/darkwing", "aa02",
                                   "events:\n  picture_done: { probe: exec, address: \"0x8A3C\" }\n"
                                   "video:\n  picture_period: 2\n  smooth: carry\n"
                                   "  picture_ready: { on: picture_done }\n"),
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
    ASSERT_TRUE(ParseVideoProfile(resolved.video, nullptr, video, error)) << error;
    EXPECT_EQ(video.picturePeriod, 2u);
    EXPECT_EQ(video.smooth, "carry");

    // A save keeps it
    const Profile* profile = store.Find("user/darkwing");
    ASSERT_NE(profile, nullptr);
    Profile again;
    ASSERT_TRUE(ParseProfile(ProfileStore::Dump(*profile), again, error)) << error;
    EXPECT_EQ(again.video.Dump(), profile->video.Dump());
}
