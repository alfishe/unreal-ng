// ServiceFileFilter: host service files are skipped on every host, by rules
// kept as named collections (technical design §6.0)

#include <gtest/gtest.h>

#include <string>

#include "emulator/io/storage/hostfolder/servicefilefilter.h"

TEST(ServiceFileFilter_Test, WildcardMatchIsCaseInsensitive)
{
    EXPECT_TRUE(MatchesWildcard("*.lnk", "Game.LNK"));
    EXPECT_TRUE(MatchesWildcard("._*", "._boot.$B"));
    EXPECT_TRUE(MatchesWildcard(".Trash-*", ".Trash-1000"));
    EXPECT_TRUE(MatchesWildcard("a?c", "ABC"));
    EXPECT_TRUE(MatchesWildcard("*", ""));
    EXPECT_FALSE(MatchesWildcard("*.lnk", "lnk"));
    EXPECT_FALSE(MatchesWildcard("a?c", "ac"));
    EXPECT_TRUE(MatchesWildcard("*a*b*", "xxaYYbzz"));
}

TEST(ServiceFileFilter_Test, EveryCollectionAppliesOnEveryHost)
{
    ServiceFileFilter filter;
    std::string collection;

    EXPECT_TRUE(filter.IsService(".DS_Store", &collection));
    EXPECT_EQ(collection, "macos");
    EXPECT_TRUE(filter.IsService("._game.trd", &collection));
    EXPECT_EQ(collection, "macos");
    EXPECT_TRUE(filter.IsService("THUMBS.DB", &collection)) << "case-insensitive";
    EXPECT_EQ(collection, "windows");
    EXPECT_TRUE(filter.IsService("System Volume Information", &collection));
    EXPECT_TRUE(filter.IsService("notes.txt~", &collection));
    EXPECT_EQ(collection, "linux");
    EXPECT_TRUE(filter.IsService(".git", &collection));
    EXPECT_EQ(collection, "vcs");
    EXPECT_TRUE(filter.IsService(".unreal-media.yaml", &collection));
    EXPECT_EQ(collection, "unreal");

    for (const char* name : {"boot.$B", "game.trd", "SD_BOOT.$C", ".profile", "readme", "Длинное имя.txt"})
        EXPECT_FALSE(filter.IsService(name)) << name;
}

TEST(ServiceFileFilter_Test, OsNoiseCollectionsLeaveNoTrace)
{
    ServiceFileFilter filter;
    // Host-OS housekeeping appears in a folder at any moment: a snapshot drops
    // it without a skipped entry. Project metadata stays a reported decision
    EXPECT_TRUE(filter.IsOsNoiseCollection("macos"));
    EXPECT_TRUE(filter.IsOsNoiseCollection("windows"));
    EXPECT_TRUE(filter.IsOsNoiseCollection("linux"));
    EXPECT_FALSE(filter.IsOsNoiseCollection("vcs"));
    EXPECT_FALSE(filter.IsOsNoiseCollection("unreal"));
    EXPECT_FALSE(filter.IsOsNoiseCollection("no such collection"));
}

TEST(ServiceFileFilter_Test, CollectionsCanBeExtended)
{
    ServiceFileFilter filter;
    EXPECT_FALSE(filter.IsService("build.log"));
    filter.AddCollection({"project", {"*.log"}});
    std::string collection;
    EXPECT_TRUE(filter.IsService("build.log", &collection));
    EXPECT_EQ(collection, "project");
    EXPECT_GE(ServiceFileFilter::DefaultCollections().size(), 5u);
}
